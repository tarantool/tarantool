/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This file contains the C-language implementations for many of the SQL
 * functions of sql.  (Some function, and in particular the date and
 * time functions, are implemented separately.)
 */
#include "sqlInt.h"
#include "mem.h"
#include "vdbeInt.h"
#include "version.h"
#include "coll/coll.h"
#include "tarantoolInt.h"
#include <unicode/ustring.h>
#include <unicode/ucasemap.h>
#include <unicode/ucnv.h>
#include <unicode/uchar.h>
#include <unicode/ucol.h>
#include "box/coll_id_cache.h"
#include "box/schema.h"
#include "box/user.h"
#include "assoc.h"

static struct mh_strnptr_t *built_in_functions = NULL;
static struct func_sql_builtin **functions;

/** Implementation of the SUM() function. */
static void
step_sum(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	assert(mem_is_null(ctx->pOut) || mem_is_num(ctx->pOut));
	if (mem_is_null(&argv[0]))
		return;
	if (mem_is_null(ctx->pOut))
		return mem_copy_as_ephemeral(ctx->pOut, &argv[0]);
	if (mem_add(ctx->pOut, &argv[0], ctx->pOut) != 0)
		ctx->is_aborted = true;
}

/** Implementation of the TOTAL() function. */
static void
step_total(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	assert(mem_is_null(ctx->pOut) || mem_is_num(ctx->pOut));
	if (mem_is_null(&argv[0]))
		return;
	if (mem_is_null(ctx->pOut))
		mem_set_double(ctx->pOut, 0.0);
	if (mem_add(ctx->pOut, &argv[0], ctx->pOut) != 0)
		ctx->is_aborted = true;
}

/** Finalizer for the TOTAL() function. */
static int
fin_total(struct Mem *mem)
{
	assert(mem_is_null(mem) || mem_is_double(mem));
	if (mem_is_null(mem))
		mem_set_double(mem, 0.0);
	return 0;
}

/** Implementation of the AVG() function. */
static void
step_avg(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	assert(mem_is_null(ctx->pOut) || mem_is_bin(ctx->pOut));
	if (mem_is_null(&argv[0]))
		return;
	struct Mem *mem;
	uint32_t *count;
	if (mem_is_null(ctx->pOut)) {
		uint32_t size = sizeof(struct Mem) + sizeof(uint32_t);
		mem = sqlDbMallocRawNN(sql_get(), size);
		if (mem == NULL) {
			ctx->is_aborted = true;
			return;
		}
		count = (uint32_t *)(mem + 1);
		mem_create(mem);
		*count = 1;
		mem_copy_as_ephemeral(mem, &argv[0]);
		mem_set_bin_allocated(ctx->pOut, (char *)mem, size);
		return;
	}
	mem = (struct Mem *)ctx->pOut->z;
	count = (uint32_t *)(mem + 1);
	++*count;
	if (mem_add(mem, &argv[0], mem) != 0)
		ctx->is_aborted = true;
}

/** Finalizer for the AVG() function. */
static int
fin_avg(struct Mem *mem)
{
	assert(mem_is_null(mem) || mem_is_bin(mem));
	if (mem_is_null(mem))
		return 0;
	struct Mem *sum = (struct Mem *)mem->z;
	uint32_t *count_val = (uint32_t *)(sum + 1);
	assert(mem_is_trivial(sum));
	struct Mem count;
	mem_create(&count);
	mem_set_uint(&count, *count_val);
	return mem_div(sum, &count, mem);
}

/** Implementation of the COUNT() function. */
static void
step_count(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 0 || argc == 1);
	if (mem_is_null(ctx->pOut))
		mem_set_uint(ctx->pOut, 0);
	if (argc == 1 && mem_is_null(&argv[0]))
		return;
	assert(mem_is_uint(ctx->pOut));
	++ctx->pOut->u.u;
}

/** Finalizer for the COUNT() function. */
static int
fin_count(struct Mem *mem)
{
	assert(mem_is_null(mem) || mem_is_uint(mem));
	if (mem_is_null(mem))
		mem_set_uint(mem, 0);
	return 0;
}

/** Implementation of the MIN() and MAX() functions. */
static void
step_minmax(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	if (mem_is_null(&argv[0])) {
		if (!mem_is_null(ctx->pOut))
			ctx->skipFlag = 1;
		return;
	}
	if (mem_is_null(ctx->pOut)) {
		if (mem_copy(ctx->pOut, &argv[0]) != 0)
			ctx->is_aborted = true;
		return;
	}

	uint32_t flags = ((struct func_sql_builtin *)ctx->func)->flags;
	bool is_max = (flags & SQL_FUNC_MAX) != 0;
	/*
	 * This step function is used for both the min() and max() aggregates,
	 * the only difference between the two being that the sense of the
	 * comparison is inverted.
	 */
	int cmp = mem_cmp_scalar(ctx->pOut, &argv[0], ctx->coll);
	if ((is_max && cmp < 0) || (!is_max && cmp > 0)) {
		if (mem_copy(ctx->pOut, &argv[0]) != 0)
			ctx->is_aborted = true;
		return;
	}
	ctx->skipFlag = 1;
}

/** Implementation of the GROUP_CONCAT() function. */
static void
step_group_concat(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1 || argc == 2);
	(void)argc;
	if (mem_is_null(&argv[0]))
		return;
	assert(mem_is_str(&argv[0]) || mem_is_bin(&argv[0]));
	if (mem_is_null(ctx->pOut)) {
		if (mem_copy(ctx->pOut, &argv[0]) != 0)
			ctx->is_aborted = true;
		return;
	}
	const char *sep = NULL;
	int sep_len = 0;
	if (argc == 1) {
		sep = ",";
		sep_len = 1;
	} else if (mem_is_null(&argv[1])) {
		sep = "";
		sep_len = 0;
	} else {
		assert(mem_is_same_type(&argv[0], &argv[1]));
		sep = argv[1].z;
		sep_len = argv[1].n;
	}
	if (mem_append(ctx->pOut, sep, sep_len) != 0) {
		ctx->is_aborted = true;
		return;
	}
	if (mem_append(ctx->pOut, argv[0].z, argv[0].n) != 0)
		ctx->is_aborted = true;
}

/** Implementations of the ABS() function. */
static void
func_abs_int(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	struct Mem *arg = &argv[0];
	if (mem_is_null(arg))
		return;
	assert(mem_is_int(arg));
	uint64_t u = mem_is_uint(arg) ? arg->u.u : (uint64_t)-arg->u.i;
	mem_set_uint(ctx->pOut, u);
}

static void
func_abs_double(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	struct Mem *arg = &argv[0];
	if (mem_is_null(arg))
		return;
	assert(mem_is_double(arg));
	mem_set_double(ctx->pOut, arg->u.r < 0 ? -arg->u.r : arg->u.r);
}

/** Implementation of the CHAR_LENGTH() function. */
static void
func_char_length(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	struct Mem *arg = &argv[0];
	if (mem_is_null(arg))
		return;
	assert(mem_is_str(arg) && arg->n >= 0);
	uint32_t len = 0;
	int offset = 0;
	while (offset < arg->n) {
		UChar32 c;
		U8_NEXT((uint8_t *)arg->z, offset, arg->n, c);
		++len;
	}
	mem_set_uint(ctx->pOut, len);
}

/** Implementation of the UPPER() and LOWER() functions. */
static void
func_lower_upper(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	struct Mem *arg = &argv[0];
	if (mem_is_null(arg))
		return;
	assert(mem_is_str(arg) && arg->n >= 0);
	if (arg->n == 0)
		return mem_set_str0_static(ctx->pOut, "");
	const char *str = arg->z;
	int32_t len = arg->n;
	struct sql *db = sql_get();
	char *res = sqlDbMallocRawNN(db, len);
	if (res == NULL) {
		ctx->is_aborted = true;
		return;
	}
	int32_t size = sqlDbMallocSize(db, res);
	assert(size >= len);
	UErrorCode status = U_ZERO_ERROR;
	const char *locale = NULL;
	if (ctx->coll != NULL && ctx->coll->type == COLL_TYPE_ICU) {
		locale = ucol_getLocaleByType(ctx->coll->collator,
					      ULOC_VALID_LOCALE, &status);
	}
	UCaseMap *cm = ucasemap_open(locale, 0, &status);
	assert(cm != NULL);
	assert(ctx->func->def->name[0] == 'U' ||
	       ctx->func->def->name[0] == 'L');
	bool is_upper = ctx->func->def->name[0] == 'U';
	int32_t new_len =
		is_upper ?
		ucasemap_utf8ToUpper(cm, res, size, str, len, &status) :
		ucasemap_utf8ToLower(cm, res, size, str, len, &status);
	if (new_len > size) {
		res = sqlDbRealloc(db, res, new_len);
		if (db->mallocFailed != 0) {
			ctx->is_aborted = true;
			return;
		}
		status = U_ZERO_ERROR;
		if (is_upper)
			ucasemap_utf8ToUpper(cm, res, size, str, len, &status);
		else
			ucasemap_utf8ToLower(cm, res, size, str, len, &status);
	}
	ucasemap_close(cm);
	mem_set_str_allocated(ctx->pOut, res, new_len);
}

/** Implementation of the NULLIF() function. */
static void
func_nullif(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 2);
	(void)argc;
	if (mem_cmp_scalar(&argv[0], &argv[1], ctx->coll) == 0)
		return;
	if (mem_copy(ctx->pOut, &argv[0]) != 0)
		ctx->is_aborted = true;
}

/** Implementation of the TRIM() function. */
static inline int
trim_bin_end(const char *str, int end, const char *octets, int octets_size,
	     int flags)
{
	if ((flags & TRIM_TRAILING) == 0)
		return end;
	while (end > 0) {
		bool is_trimmed = false;
		char c = str[end - 1];
		for (int i = 0; i < octets_size && !is_trimmed; ++i)
			is_trimmed = c == octets[i];
		if (!is_trimmed)
			break;
		--end;
	}
	return end;
}

static inline int
trim_bin_start(const char *str, int end, const char *octets, int octets_size,
	       int flags)
{
	if ((flags & TRIM_LEADING) == 0)
		return 0;
	int start = 0;
	while (start < end) {
		bool is_trimmed = false;
		char c = str[start];
		for (int i = 0; i < octets_size && !is_trimmed; ++i)
			is_trimmed = c == octets[i];
		if (!is_trimmed)
			break;
		++start;
	}
	return start;
}

static void
func_trim_bin(struct sql_context *ctx, int argc, struct Mem *argv)
{
	if (mem_is_null(&argv[0]) || (argc == 3 && mem_is_null(&argv[2])))
		return;
	assert(argc == 2 || (argc == 3 && mem_is_bin(&argv[2])));
	assert(mem_is_bin(&argv[0]) && mem_is_uint(&argv[1]));
	const char *str = argv[0].z;
	int size = argv[0].n;
	const char *octets;
	int octets_size;
	if (argc == 3) {
		octets = argv[2].z;
		octets_size = argv[2].n;
	} else {
		octets = "\0";
		octets_size = 1;
	}

	int flags = argv[1].u.u;
	int end = trim_bin_end(str, size, octets, octets_size, flags);
	int start = trim_bin_start(str, end, octets, octets_size, flags);

	if (start >= end)
		return mem_set_bin_static(ctx->pOut, "", 0);
	if (mem_copy_bin(ctx->pOut, &str[start], end - start) != 0)
		ctx->is_aborted = true;
}

static inline int
trim_str_end(const char *str, int end, const char *chars, uint8_t *chars_len,
	     int chars_count, int flags)
{
	if ((flags & TRIM_TRAILING) == 0)
		return end;
	while (end > 0) {
		bool is_trimmed = false;
		const char *c = chars;
		int len;
		for (int i = 0; i < chars_count && !is_trimmed; ++i) {
			len = chars_len[i];
			const char *s = str + end - len;
			is_trimmed = len <= end && memcmp(c, s, len) == 0;
			c += len;
		}
		if (!is_trimmed)
			break;
		assert(len > 0);
		end -= len;
	}
	return end;
}

static inline int
trim_str_start(const char *str, int end, const char *chars, uint8_t *chars_len,
	       int chars_count, int flags)
{
	if ((flags & TRIM_LEADING) == 0)
		return 0;
	int start = 0;
	while (start < end) {
		bool is_trimmed = false;
		const char *c = chars;
		int len;
		for (int i = 0; i < chars_count && !is_trimmed; ++i) {
			len = chars_len[i];
			const char *s = str + start;
			is_trimmed = start + len <= end &&
				     memcmp(c, s, len) == 0;
			c += len;
		}
		if (!is_trimmed)
			break;
		assert(len > 0);
		start += len;
	}
	return start;
}

static void
func_trim_str(struct sql_context *ctx, int argc, struct Mem *argv)
{
	if (mem_is_null(&argv[0]) || (argc == 3 && mem_is_null(&argv[2])))
		return;
	assert(argc == 2 || (argc == 3 && mem_is_str(&argv[2])));
	assert(mem_is_str(&argv[0]) && mem_is_uint(&argv[1]));
	const char *str = argv[0].z;
	int size = argv[0].n;
	const char *chars;
	int chars_size;
	if (argc == 3) {
		chars = argv[2].z;
		chars_size = argv[2].n;
	} else {
		chars = " ";
		chars_size = 1;
	}

	struct region *region = &fiber()->gc;
	size_t svp = region_used(region);
	uint8_t *chars_len = region_alloc(region, chars_size);
	if (chars_len == NULL) {
		ctx->is_aborted = true;
		diag_set(OutOfMemory, chars_size, "region_alloc", "chars_len");
		return;
	}
	int chars_count = 0;

	int offset = 0;
	while (offset < chars_size) {
		UChar32 c;
		int prev = offset;
		U8_NEXT((uint8_t *)chars, offset, chars_size, c);
		chars_len[chars_count++] = offset - prev;
	}

	uint64_t flags = argv[1].u.u;
	int end = trim_str_end(str, size, chars, chars_len, chars_count, flags);
	int start = trim_str_start(str, end, chars, chars_len, chars_count,
				   flags);
	region_truncate(region, svp);

	if (start >= end)
		return mem_set_str0_static(ctx->pOut, "");
	if (mem_copy_str(ctx->pOut, &str[start], end - start) != 0)
		ctx->is_aborted = true;
}

/** Implementation of the POSITION() function. */
static void
func_position_octets(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 2);
	(void)argc;
	if (mem_is_any_null(&argv[0], &argv[1]))
		return;
	assert(mem_is_bytes(&argv[0]) && mem_is_bytes(&argv[1]));

	const char *key = argv[0].z;
	const char *str = argv[1].z;
	int key_size = argv[0].n;
	int str_size = argv[1].n;
	if (key_size <= 0)
		return mem_set_uint(ctx->pOut, 1);
	const char *pos = memmem(str, str_size, key, key_size);
	return mem_set_uint(ctx->pOut, pos == NULL ? 0 : pos - str + 1);
}

static void
func_position_characters(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 2);
	(void)argc;
	if (mem_is_any_null(&argv[0], &argv[1]))
		return;
	assert(mem_is_str(&argv[0]) && mem_is_str(&argv[1]));

	const char *key = argv[0].z;
	const char *str = argv[1].z;
	int key_size = argv[0].n;
	int str_size = argv[1].n;
	if (key_size <= 0)
		return mem_set_uint(ctx->pOut, 1);

	int key_end = 0;
	int str_end = 0;
	while (key_end < key_size && str_end < str_size) {
		UChar32 c;
		U8_NEXT((uint8_t *)key, key_end, key_size, c);
		U8_NEXT((uint8_t *)str, str_end, str_size, c);
	}
	if (key_end < key_size)
		return mem_set_uint(ctx->pOut, 0);

	struct coll *coll = ctx->coll;
	if (coll->cmp(key, key_size, str, str_end, coll) == 0)
		return mem_set_uint(ctx->pOut, 1);

	int i = 2;
	int str_pos = 0;
	while (str_end < str_size) {
		UChar32 c;
		U8_NEXT((uint8_t *)str, str_pos, str_size, c);
		U8_NEXT((uint8_t *)str, str_end, str_size, c);
		const char *s = str + str_pos;
		if (coll->cmp(key, key_size, s, str_end - str_pos, coll) == 0)
			return mem_set_uint(ctx->pOut, i);
		++i;
	}
	return mem_set_uint(ctx->pOut, 0);
}

/** Implementation of the SUBSTR() function. */
int
substr_normalize(int64_t base_start, bool is_start_neg, uint64_t base_length,
		 uint64_t *start, uint64_t *length)
{
	if (!is_start_neg && base_start > 0) {
		*start = (uint64_t)base_start - 1;
		*length = base_length;
		return 0;
	}
	*start = 0;
	if (base_length == 0) {
		*length = 0;
		return 0;
	}
	/*
	 * We are subtracting 1 from base_length instead of subtracting from
	 * base_start, since base_start can be INT64_MIN. At the same time,
	 * base_length is not less than 1.
	 */
	int64_t a = base_start;
	int64_t b = (int64_t)(base_length - 1);
	int64_t res;
	bool is_neg;
	/*
	 * Integer cannot overflow since non-positive value is added to positive
	 * value.
	 */
	if (sql_add_int(a, a != 0, b, false, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	*length = is_neg ? 0 : (uint64_t)res;
	return 0;
}

static void
func_substr_octets(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 2 || argc == 3);
	if (mem_is_any_null(&argv[0], &argv[1]))
		return;
	assert(mem_is_bytes(&argv[0]) && mem_is_int(&argv[1]));

	bool is_str = mem_is_str(&argv[0]);
	uint64_t size = argv[0].n;

	if (argc == 2) {
		uint64_t start = mem_is_uint(&argv[1]) && argv[1].u.u > 1 ?
				 argv[1].u.u - 1 : 0;
		if (start >= size) {
			if (is_str)
				return mem_set_str0_static(ctx->pOut, "");
			else
				return mem_set_bin_static(ctx->pOut, "", 0);
		}
		char *s = &argv[0].z[start];
		uint64_t n = size - start;
		ctx->is_aborted = is_str ? mem_copy_str(ctx->pOut, s, n) != 0 :
				  mem_copy_bin(ctx->pOut, s, n) != 0;
		return;
	}

	assert(argc == 3);
	if (mem_is_null(&argv[2]))
		return;
	assert(mem_is_int(&argv[2]));
	if (!mem_is_uint(&argv[2])) {
		diag_set(ClientError, ER_SQL_EXECUTE, "Length of the result "
			 "cannot be less than 0");
		ctx->is_aborted = true;
		return;
	}
	uint64_t start;
	uint64_t length;
	if (substr_normalize(argv[1].u.i, !mem_is_uint(&argv[1]), argv[2].u.u,
			     &start, &length) != 0) {
		ctx->is_aborted = true;
		return;
	}
	if (start >= size || length == 0) {
		if (is_str)
			return mem_set_str0_static(ctx->pOut, "");
		else
			return mem_set_bin_static(ctx->pOut, "", 0);
	}
	char *str = &argv[0].z[start];
	uint64_t len = MIN(size - start, length);
	ctx->is_aborted = is_str ? mem_copy_str(ctx->pOut, str, len) != 0 :
			  mem_copy_bin(ctx->pOut, str, len) != 0;
}

static void
func_substr_characters(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 2 || argc == 3);
	(void)argc;
	if (mem_is_any_null(&argv[0], &argv[1]))
		return;
	assert(mem_is_str(&argv[0]) && mem_is_int(&argv[1]));

	const char *str = argv[0].z;
	int pos = 0;
	int end = argv[0].n;
	if (argc == 2) {
		uint64_t start = mem_is_uint(&argv[1]) && argv[1].u.u > 1 ?
				 argv[1].u.u - 1 : 0;
		for (uint64_t i = 0; i < start && pos < end; ++i) {
			UChar32 c;
			U8_NEXT((uint8_t *)str, pos, end, c);
		}
		if (pos == end)
			return mem_set_str_static(ctx->pOut, "", 0);
		if (mem_copy_str(ctx->pOut, str + pos, end - pos) != 0)
			ctx->is_aborted = true;
		return;
	}

	assert(argc == 3);
	if (mem_is_null(&argv[2]))
		return;
	assert(mem_is_int(&argv[2]));
	if (!mem_is_uint(&argv[2])) {
		diag_set(ClientError, ER_SQL_EXECUTE, "Length of the result "
			 "cannot be less than 0");
		ctx->is_aborted = true;
		return;
	}
	uint64_t start;
	uint64_t length;
	if (substr_normalize(argv[1].u.i, !mem_is_uint(&argv[1]), argv[2].u.u,
			     &start, &length) != 0) {
		ctx->is_aborted = true;
		return;
	}
	if (length == 0)
		return mem_set_str_static(ctx->pOut, "", 0);

	for (uint64_t i = 0; i < start && pos < end; ++i) {
		UChar32 c;
		U8_NEXT((uint8_t *)str, pos, end, c);
	}
	if (pos == end)
		return mem_set_str_static(ctx->pOut, "", 0);

	int cur = pos;
	for (uint64_t i = 0; i < length && cur < end; ++i) {
		UChar32 c;
		U8_NEXT((uint8_t *)str, cur, end, c);
	}
	assert(cur > pos);
	if (mem_copy_str(ctx->pOut, str + pos, cur - pos) != 0)
		ctx->is_aborted = true;
}

/**
 * Implementation of the CHAR() function.
 *
 * This function takes zero or more arguments, each of which is an integer. It
 * constructs a string where each character of the string is the unicode
 * character for the corresponding integer argument.
 *
 * If an argument is negative or greater than 0x10ffff, the symbol "�" is used.
 * Symbol '\0' used instead of NULL argument.
 */
static void
func_char(struct sql_context *ctx, int argc, struct Mem *argv)
{
	if (argc == 0)
		return mem_set_str_static(ctx->pOut, "", 0);
	struct region *region = &fiber()->gc;
	size_t svp = region_used(region);
	uint32_t size;
	UChar32 *buf = region_alloc_array(region, typeof(*buf), argc, &size);
	if (buf == NULL) {
		ctx->is_aborted = true;
		diag_set(OutOfMemory, size, "region_alloc_array", "buf");
		return;
	}
	int len = 0;
	for (int i = 0; i < argc; ++i) {
		if (mem_is_null(&argv[i]))
			buf[i] = 0;
		else if (!mem_is_uint(&argv[i]) || argv[i].u.u > 0x10ffff)
			buf[i] = 0xfffd;
		else
			buf[i] = argv[i].u.u;
		len += U8_LENGTH(buf[i]);
	}

	char *str = sqlDbMallocRawNN(sql_get(), len);
	if (str == NULL) {
		region_truncate(region, svp);
		ctx->is_aborted = true;
		return;
	}
	int pos = 0;
	for (int i = 0; i < argc; ++i) {
		UBool is_error = false;
		U8_APPEND((uint8_t *)str, pos, len, buf[i], is_error);
		assert(!is_error);
		(void)is_error;
	}
	region_truncate(region, svp);
	assert(pos == len);
	(void)pos;
	mem_set_str_allocated(ctx->pOut, str, len);
}

/**
 * Implementation of the GREATEST() and LEAST() functions.
 *
 * The GREATEST() function returns the largest of the given arguments.
 * The LEAST() function returns the smallest of the given arguments.
 */
static void
func_greatest_least(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc > 1);
	int mask = ctx->func->def->name[0] == 'G' ? -1 : 0;
	assert(ctx->func->def->name[0] == 'G' ||
	       ctx->func->def->name[0] == 'L');

	if (mem_is_null(&argv[0]))
		return;
	int best = 0;
	for (int i = 1; i < argc; ++i) {
		if (mem_is_null(&argv[i]))
			return;
		int cmp = mem_cmp_scalar(&argv[best], &argv[i], ctx->coll);
		if ((cmp ^ mask) >= 0)
			best = i;
	}
	if (mem_copy(ctx->pOut, &argv[best]) != 0)
		ctx->is_aborted = true;
}

/**
 * Implementation of the HEX() function.
 *
 * The HEX() function returns the hexadecimal representation of the argument.
 */
static const char hexdigits[] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

static void
func_hex(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	struct Mem *arg = &argv[0];
	if (mem_is_null(arg))
		return;

	assert(mem_is_bin(arg) && arg->n >= 0);
	if (arg->n == 0)
		return mem_set_str0_static(ctx->pOut, "");

	uint32_t size = 2 * arg->n;
	char *str = sqlDbMallocRawNN(sql_get(), size);
	if (str == NULL) {
		ctx->is_aborted = true;
		return;
	}
	for (int i = 0; i < arg->n; ++i) {
		char c = arg->z[i];
		str[2 * i] = hexdigits[(c >> 4) & 0xf];
		str[2 * i + 1] = hexdigits[c & 0xf];
	}
	mem_set_str_allocated(ctx->pOut, str, size);
}

/** Implementation of the OCTET_LENGTH() function. */
static void
func_octet_length(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	struct Mem *arg = &argv[0];
	if (mem_is_null(arg))
		return;
	assert(mem_is_bytes(arg) && arg->n >= 0);
	mem_set_uint(ctx->pOut, arg->n);
}

/** Implementation of the PRINTF() function. */
static void
func_printf(struct sql_context *ctx, int argc, struct Mem *argv)
{
	if (argc < 1 || mem_is_null(&argv[0]))
		return;
	if (argc == 1 || !mem_is_str(&argv[0])) {
		struct Mem *mem = ctx->pOut;
		if (mem_copy(mem, &argv[0]) != 0 || mem_to_str(mem) != 0)
			ctx->is_aborted = true;
		return;
	}
	struct PrintfArguments pargs;
	struct StrAccum acc;
	char *format = argv[0].z;
	pargs.nArg = argc - 1;
	pargs.nUsed = 0;
	pargs.apArg = argv + 1;
	struct sql *db = sql_get();
	sqlStrAccumInit(&acc, db, 0, 0, db->aLimit[SQL_LIMIT_LENGTH]);
	acc.printfFlags = SQL_PRINTF_SQLFUNC;
	sqlXPrintf(&acc, format, &pargs);
	mem_set_str_allocated(ctx->pOut, sqlStrAccumFinish(&acc), acc.nChar);
}

/**
 * Implementation of the RANDOM() function.
 *
 * This function returns a random INT64 value.
 */
static void
func_random(struct sql_context *ctx, int argc, struct Mem *argv)
{
	(void)argc;
	(void)argv;
	int64_t r;
	sql_randomness(sizeof(r), &r);
	mem_set_int(ctx->pOut, r, r < 0);
}

/**
 * Implementation of the RANDOMBLOB() function.
 *
 * This function returns a random VARBINARY value. The size of this value is
 * specified as an argument of the function.
 */
static void
func_randomblob(struct sql_context *ctx, int argc, struct Mem *argv)
{
	assert(argc == 1);
	(void)argc;
	struct Mem *arg = &argv[0];
	assert(mem_is_null(arg) || mem_is_int(arg));
	if (mem_is_null(arg) || !mem_is_uint(arg))
		return;
	if (arg->u.u == 0)
		return mem_set_bin_static(ctx->pOut, "", 0);
	uint64_t len = arg->u.u;
	char *res = sqlDbMallocRawNN(sql_get(), len);
	if (res == NULL) {
		ctx->is_aborted = true;
		return;
	}
	sql_randomness(len, res);
	mem_set_bin_allocated(ctx->pOut, res, len);
}

static const unsigned char *
mem_as_ustr(struct Mem *mem)
{
	return (const unsigned char *)mem_as_str0(mem);
}

static const void *
mem_as_bin(struct Mem *mem)
{
	const char *s;
	if (mem_cast_explicit(mem, FIELD_TYPE_VARBINARY) != 0 &&
	    mem_to_str(mem) != 0)
		return NULL;
	if (mem_get_bin(mem, &s) != 0)
		return NULL;
	return s;
}

static void
sql_func_uuid(struct sql_context *ctx, int argc, struct Mem *argv)
{
	if (argc > 1) {
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT, "UUID",
			 "one or zero", argc);
		ctx->is_aborted = true;
		return;
	}
	if (argc == 1) {
		uint64_t version;
		if (mem_get_uint(&argv[0], &version) != 0) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
				 mem_str(&argv[0]), "integer");
			ctx->is_aborted = true;
			return;
		}
		if (version != 4) {
			diag_set(ClientError, ER_UNSUPPORTED, "Function UUID",
				 "versions other than 4");
			ctx->is_aborted = true;
			return;
		}
	}
	struct tt_uuid uuid;
	tt_uuid_create(&uuid);
	mem_set_uuid(ctx->pOut, &uuid);
}

/*
 * Return the type of the argument.
 */
static void
typeofFunc(struct sql_context *context, int argc, struct Mem *argv)
{
	(void)argc;
	const char *z = 0;
	if ((argv[0].flags & MEM_Number) != 0)
		return mem_set_str0_static(context->pOut, "number");
	if ((argv[0].flags & MEM_Scalar) != 0)
		return mem_set_str0_static(context->pOut, "scalar");
	switch (argv[0].type) {
	case MEM_TYPE_INT:
	case MEM_TYPE_UINT:
		z = "integer";
		break;
	case MEM_TYPE_DEC:
		z = "decimal";
		break;
	case MEM_TYPE_STR:
		z = "string";
		break;
	case MEM_TYPE_DOUBLE:
		z = "double";
		break;
	case MEM_TYPE_BIN:
	case MEM_TYPE_ARRAY:
	case MEM_TYPE_MAP:
		z = "varbinary";
		break;
	case MEM_TYPE_BOOL:
		z = "boolean";
		break;
	case MEM_TYPE_NULL:
		z = "NULL";
		break;
	case MEM_TYPE_UUID:
		z = "uuid";
		break;
	default:
		unreachable();
		break;
	}
	sql_result_text(context, z, -1, SQL_STATIC);
}

/*
 * Implementation of the round() function
 */
static void
roundFunc(struct sql_context *context, int argc, struct Mem *argv)
{
	int64_t n = 0;
	double r;
	if (argc != 1 && argc != 2) {
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT, "ROUND",
			 "1 or 2", argc);
		context->is_aborted = true;
		return;
	}
	if (argc == 2) {
		if (mem_is_null(&argv[1]))
			return;
		n = mem_get_int_unsafe(&argv[1]);
		if (n < 0)
			n = 0;
	}
	if (mem_is_null(&argv[0]))
		return;
	if (!mem_is_num(&argv[0]) && !mem_is_str(&argv[0])) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
			 mem_str(&argv[0]), "number");
		context->is_aborted = true;
		return;
	}
	r = mem_get_double_unsafe(&argv[0]);
	/* If Y==0 and X will fit in a 64-bit int,
	 * handle the rounding directly,
	 * otherwise use printf.
	 */
	if (n == 0 && r >= 0 && r < (double)(LARGEST_INT64 - 1)) {
		r = (double)((sql_int64) (r + 0.5));
	} else if (n == 0 && r < 0 && (-r) < (double)(LARGEST_INT64 - 1)) {
		r = -(double)((sql_int64) ((-r) + 0.5));
	} else {
		const char *rounded_value = tt_sprintf("%.*f", n, r);
		sqlAtoF(rounded_value, &r, sqlStrlen30(rounded_value));
	}
	sql_result_double(context, r);
}

/*
 * Allocate nByte bytes of space using sqlMalloc(). If the
 * allocation fails, return NULL. If nByte is larger than the
 * maximum string or blob length, then raise an error and return
 * NULL.
 */
static void *
contextMalloc(struct sql_context *context, i64 nByte)
{
	char *z;
	sql *db = sql_context_db_handle(context);
	assert(nByte > 0);
	testcase(nByte == db->aLimit[SQL_LIMIT_LENGTH]);
	testcase(nByte == db->aLimit[SQL_LIMIT_LENGTH] + 1);
	if (nByte > db->aLimit[SQL_LIMIT_LENGTH]) {
		diag_set(ClientError, ER_SQL_EXECUTE, "string or blob too big");
		context->is_aborted = true;
		z = 0;
	} else {
		z = sqlMalloc(nByte);
		if (z == NULL)
			context->is_aborted = true;
	}
	return z;
}

#define Utf8Read(s, e) \
	ucnv_getNextUChar(icu_utf8_conv, &(s), (e), &status)

#define SQL_END_OF_STRING        0xffff
#define SQL_INVALID_UTF8_SYMBOL  0xfffd

/**
 * Returns codes from sql_utf8_pattern_compare().
 */
enum pattern_match_status {
	MATCH = 0,
	NO_MATCH = 1,
	/** No match in spite of having * or % wildcards. */
	NO_WILDCARD_MATCH = 2,
	/** Pattern contains invalid UTF-8 symbol. */
	INVALID_PATTERN = 3
};

/**
 * Read an UTF-8 character from string, and move pointer to the
 * next character. Save current character and its length to output
 * params - they are served as arguments of coll->cmp() call.
 *
 * @param[out] str Ptr to string.
 * @param str_end Ptr the last symbol in @str.
 * @param[out] char_ptr Ptr to the UTF-8 character.
 * @param[out] char_len Ptr to length of the UTF-8 character in
 * bytes.
 *
 * @retval UTF-8 character.
 */
static UChar32
step_utf8_char(const char **str, const char *str_end, const char **char_ptr,
	       size_t *char_len)
{
	UErrorCode status = U_ZERO_ERROR;
	*char_ptr = *str;
	UChar32 next_utf8 = Utf8Read(*str, str_end);
	*char_len = *str - *char_ptr;
	return next_utf8;
}

/**
 * Compare two UTF-8 strings for equality where the first string
 * is a LIKE expression.
 *
 * Like matching rules:
 *
 *      '%'       Matches any sequence of zero or more
 *                characters.
 *
 *      '_'       Matches any one character.
 *
 *      Ec        Where E is the "esc" character and c is any
 *                other character, including '%', '_', and esc,
 *                match exactly c.
 *
 * This routine is usually quick, but can be N**2 in the worst
 * case.
 *
 * 'pattern_end' and 'string_end' params are used to determine
 * the end of strings, because '\0' is not supposed to be
 * end-of-string signal.
 *
 * @param pattern String containing comparison pattern.
 * @param string String being compared.
 * @param pattern_end Ptr to pattern last symbol.
 * @param string_end Ptr to string last symbol.
 * @param coll Pointer to collation.
 * @param match_other The escape char for LIKE.
 *
 * @retval One of pattern_match_status values.
 */
static int
sql_utf8_pattern_compare(const char *pattern,
			 const char *string,
			 const char *pattern_end,
			 const char *string_end,
			 struct coll *coll,
			 UChar32 match_other)
{
	/* Next pattern and input string chars. */
	UChar32 c, c2;
	/* One past the last escaped input char. */
	const char *zEscaped = 0;
	UErrorCode status = U_ZERO_ERROR;
	const char *pat_char_ptr = NULL;
	const char *str_char_ptr = NULL;
	size_t pat_char_len = 0;
	size_t str_char_len = 0;

	while (pattern < pattern_end) {
		c = step_utf8_char(&pattern, pattern_end, &pat_char_ptr,
				   &pat_char_len);
		if (c == SQL_INVALID_UTF8_SYMBOL)
			return INVALID_PATTERN;
		if (c == MATCH_ALL_WILDCARD) {
			/*
			 * Skip over multiple "%" characters in
			 * the pattern. If there are also "_"
			 * characters, skip those as well, but
			 * consume a single character of the
			 * input string for each "_" skipped.
			 */
			while ((c = step_utf8_char(&pattern, pattern_end,
						   &pat_char_ptr,
						   &pat_char_len)) !=
			       SQL_END_OF_STRING) {
				if (c == SQL_INVALID_UTF8_SYMBOL)
					return INVALID_PATTERN;
				if (c == MATCH_ONE_WILDCARD) {
					c2 = Utf8Read(string, string_end);
					if (c2 == SQL_INVALID_UTF8_SYMBOL)
						return NO_MATCH;
					if (c2 == SQL_END_OF_STRING)
						return NO_WILDCARD_MATCH;
				} else if (c != MATCH_ALL_WILDCARD) {
					break;
				}
			}
			/*
			 * "%" at the end of the pattern matches.
			 */
			if (c == SQL_END_OF_STRING) {
				return MATCH;
			}
			if (c == match_other) {
				c = step_utf8_char(&pattern, pattern_end,
						   &pat_char_ptr,
						   &pat_char_len);
				if (c == SQL_INVALID_UTF8_SYMBOL)
					return INVALID_PATTERN;
				if (c == SQL_END_OF_STRING)
					return NO_WILDCARD_MATCH;
			}

			/*
			 * At this point variable c contains the
			 * first character of the pattern string
			 * past the "%". Search in the input
			 * string for the first matching
			 * character and recursively continue the
			 * match from that point.
			 *
			 * For a case-insensitive search, set
			 * variable cx to be the same as c but in
			 * the other case and search the input
			 * string for either c or cx.
			 */

			int bMatch;
			while (string < string_end){
				/*
				 * This loop could have been
				 * implemented without if
				 * converting c2 to lower case
				 * by holding c_upper and
				 * c_lower,however it is
				 * implemented this way because
				 * lower works better with German
				 * and Turkish languages.
				 */
				c2 = step_utf8_char(&string, string_end,
						    &str_char_ptr,
						    &str_char_len);
				if (c2 == SQL_INVALID_UTF8_SYMBOL)
					return NO_MATCH;
				if (coll->cmp(pat_char_ptr, pat_char_len,
					      str_char_ptr, str_char_len, coll)
					!= 0)
					continue;
				bMatch = sql_utf8_pattern_compare(pattern,
								  string,
								  pattern_end,
								  string_end,
								  coll,
								  match_other);
				if (bMatch != NO_MATCH)
					return bMatch;
			}
			return NO_WILDCARD_MATCH;
		}
		if (c == match_other) {
			c = step_utf8_char(&pattern, pattern_end, &pat_char_ptr,
					   &pat_char_len);
			if (c == SQL_INVALID_UTF8_SYMBOL)
				return INVALID_PATTERN;
			if (c == SQL_END_OF_STRING)
				return NO_MATCH;
			zEscaped = pattern;
		}
		c2 = step_utf8_char(&string, string_end, &str_char_ptr,
				    &str_char_len);
		if (c2 == SQL_INVALID_UTF8_SYMBOL)
			return NO_MATCH;
		if (coll->cmp(pat_char_ptr, pat_char_len, str_char_ptr,
			      str_char_len, coll) == 0)
			continue;
		if (c == MATCH_ONE_WILDCARD && pattern != zEscaped &&
		    c2 != SQL_END_OF_STRING)
			continue;
		return NO_MATCH;
	}
	return string == string_end ? MATCH : NO_MATCH;
}

/**
 * Implementation of the like() SQL function. This function
 * implements the built-in LIKE operator. The first argument to
 * the function is the pattern and the second argument is the
 * string. So, the SQL statements of the following type:
 *
 *       A LIKE B
 *
 * are implemented as like(B,A).
 *
 * Both arguments (A and B) must be of type TEXT. If one arguments
 * is NULL then result is NULL as well.
 */
static void
likeFunc(sql_context *context, int argc, struct Mem *argv)
{
	u32 escape = SQL_END_OF_STRING;
	int nPat;
	assert(argc == 2 || argc == 3);
	if (mem_is_any_null(&argv[0], &argv[1]))
		return;
	assert(mem_is_str(&argv[0]) && mem_is_str(&argv[1]));
	const char *zB = argv[0].z;
	const char *zA = argv[1].z;
	const char *zB_end = zB + argv[0].n;
	const char *zA_end = zA + argv[1].n;

	/*
	 * Limit the length of the LIKE pattern to avoid problems
	 * of deep recursion and N*N behavior in
	 * sql_utf8_pattern_compare().
	 */
	nPat = argv[0].n;
	if (nPat > sql_get()->aLimit[SQL_LIMIT_LIKE_PATTERN_LENGTH]) {
		diag_set(ClientError, ER_SQL_EXECUTE, "LIKE pattern is too "\
			 "complex");
		context->is_aborted = true;
		return;
	}

	if (argc == 3) {
		if (mem_is_null(&argv[2]))
			return;
		/*
		 * The escape character string must consist of a
		 * single UTF-8 character. Otherwise, return an
		 * error.
		 */
		const char *str = argv[2].z;
		int pos = 0;
		int end = argv[2].n;
		U8_NEXT((uint8_t *)str, pos, end, escape);
		if (pos != end || end == 0) {
			diag_set(ClientError, ER_SQL_EXECUTE, "ESCAPE "\
				 "expression must be a single character");
			context->is_aborted = true;
			return;
		}
	}
	if (!zA || !zB)
		return;
	int res;
	struct coll *coll = context->coll;
	assert(coll != NULL);
	res = sql_utf8_pattern_compare(zB, zA, zB_end, zA_end, coll, escape);

	if (res == INVALID_PATTERN) {
		diag_set(ClientError, ER_SQL_EXECUTE, "LIKE pattern can only "\
			 "contain UTF-8 characters");
		context->is_aborted = true;
		return;
	}
	mem_set_bool(context->pOut, res == MATCH);
}

/**
 * Implementation of the version() function.  The result is the
 * version of the Tarantool that is running.
 *
 * @param context Context being used.
 * @param unused1 Unused.
 * @param unused2 Unused.
 */
static void
sql_func_version(struct sql_context *context, int argc, struct Mem *argv)
{
	(void)argc;
	(void)argv;
	sql_result_text(context, tarantool_version(), -1, SQL_STATIC);
}

/*
 * Implementation of the QUOTE() function.  This function takes a single
 * argument.  If the argument is numeric, the return value is the same as
 * the argument.  If the argument is NULL, the return value is the string
 * "NULL".  Otherwise, the argument is enclosed in single quotes with
 * single-quote escapes.
 */
static void
quoteFunc(struct sql_context *context, int argc, struct Mem *argv)
{
	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	switch (argv[0].type) {
	case MEM_TYPE_UUID: {
		char buf[UUID_STR_LEN + 1];
		tt_uuid_to_string(&argv[0].u.uuid, &buf[0]);
		sql_result_text(context, buf, UUID_STR_LEN, SQL_TRANSIENT);
		break;
	}
	case MEM_TYPE_DOUBLE:
	case MEM_TYPE_DEC:
	case MEM_TYPE_UINT:
	case MEM_TYPE_INT: {
			sql_result_value(context, &argv[0]);
			break;
		}
	case MEM_TYPE_BIN:
	case MEM_TYPE_ARRAY:
	case MEM_TYPE_MAP: {
			char *zText = 0;
			char const *zBlob = mem_as_bin(&argv[0]);
			int nBlob = mem_len_unsafe(&argv[0]);
			assert(zBlob == mem_as_bin(&argv[0]));	/* No encoding change */
			zText =
			    (char *)contextMalloc(context,
						  (2 * (i64) nBlob) + 4);
			if (zText) {
				int i;
				for (i = 0; i < nBlob; i++) {
					zText[(i * 2) + 2] =
					    hexdigits[(zBlob[i] >> 4) & 0x0F];
					zText[(i * 2) + 3] =
					    hexdigits[(zBlob[i]) & 0x0F];
				}
				zText[(nBlob * 2) + 2] = '\'';
				zText[(nBlob * 2) + 3] = '\0';
				zText[0] = 'X';
				zText[1] = '\'';
				sql_result_text(context, zText, -1,
						    SQL_TRANSIENT);
				sql_free(zText);
			}
			break;
		}
	case MEM_TYPE_STR: {
			int i, j;
			u64 n;
			const unsigned char *zArg = mem_as_ustr(&argv[0]);
			char *z;

			if (zArg == 0)
				return;
			for (i = 0, n = 0; zArg[i]; i++) {
				if (zArg[i] == '\'')
					n++;
			}
			z = contextMalloc(context, ((i64) i) + ((i64) n) + 3);
			if (z) {
				z[0] = '\'';
				for (i = 0, j = 1; zArg[i]; i++) {
					z[j++] = zArg[i];
					if (zArg[i] == '\'') {
						z[j++] = '\'';
					}
				}
				z[j++] = '\'';
				z[j] = 0;
				sql_result_text(context, z, j,
						    sql_free);
			}
			break;
		}
	case MEM_TYPE_BOOL: {
		sql_result_text(context,
				SQL_TOKEN_BOOLEAN(argv[0].u.b),
				-1, SQL_TRANSIENT);
		break;
	}
	default:{
			assert(mem_is_null(&argv[0]));
			sql_result_text(context, "NULL", 4, SQL_STATIC);
			break;
		}
	}
}

/*
 * The unicode() function.  Return the integer unicode code-point value
 * for the first character of the input string.
 */
static void
unicodeFunc(struct sql_context *context, int argc, struct Mem *argv)
{
	const unsigned char *z = mem_as_ustr(&argv[0]);
	(void)argc;
	if (z && z[0])
		sql_result_uint(context, sqlUtf8Read(&z));
}

/*
 * The zeroblob(N) function returns a zero-filled blob of size N bytes.
 */
static void
zeroblobFunc(struct sql_context *context, int argc, struct Mem *argv)
{
	int64_t n;
	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	n = mem_get_int_unsafe(&argv[0]);
	if (n < 0)
		n = 0;
	if (n > sql_get()->aLimit[SQL_LIMIT_LENGTH]) {
		diag_set(ClientError, ER_SQL_EXECUTE, "string or binary string"\
			 "is too big");
		context->is_aborted = true;
		return;
	}
	char *str = sqlDbMallocZero(sql_get(), n);
	if (str == NULL) {
		context->is_aborted = true;
		return;
	}
	mem_set_bin_allocated(context->pOut, str, n);
}

/*
 * The replace() function.  Three arguments are all strings: call
 * them A, B, and C. The result is also a string which is derived
 * from A by replacing every occurrence of B with C.  The match
 * must be exact.  Collating sequences are not used.
 */
static void
replaceFunc(struct sql_context *context, int argc, struct Mem *argv)
{
	const unsigned char *zStr;	/* The input string A */
	const unsigned char *zPattern;	/* The pattern string B */
	const unsigned char *zRep;	/* The replacement string C */
	unsigned char *zOut;	/* The output */
	int nStr;		/* Size of zStr */
	int nPattern;		/* Size of zPattern */
	int nRep;		/* Size of zRep */
	i64 nOut;		/* Maximum size of zOut */
	int loopLimit;		/* Last zStr[] that might match zPattern[] */
	int i, j;		/* Loop counters */

	assert(argc == 3);
	UNUSED_PARAMETER(argc);
	zStr = mem_as_ustr(&argv[0]);
	if (zStr == 0)
		return;
	nStr = mem_len_unsafe(&argv[0]);
	assert(zStr == mem_as_ustr(&argv[0]));	/* No encoding change */
	zPattern = mem_as_ustr(&argv[1]);
	if (zPattern == 0) {
		assert(mem_is_null(&argv[1])
		       || sql_context_db_handle(context)->mallocFailed);
		return;
	}
	nPattern = mem_len_unsafe(&argv[1]);
	if (nPattern == 0) {
		assert(!mem_is_null(&argv[1]));
		sql_result_value(context, &argv[0]);
		return;
	}
	assert(zPattern == mem_as_ustr(&argv[1]));	/* No encoding change */
	zRep = mem_as_ustr(&argv[2]);
	if (zRep == 0)
		return;
	nRep = mem_len_unsafe(&argv[2]);
	assert(zRep == mem_as_ustr(&argv[2]));
	nOut = nStr + 1;
	assert(nOut < SQL_MAX_LENGTH);
	zOut = contextMalloc(context, (i64) nOut);
	if (zOut == 0) {
		return;
	}
	loopLimit = nStr - nPattern;
	for (i = j = 0; i <= loopLimit; i++) {
		if (zStr[i] != zPattern[0]
		    || memcmp(&zStr[i], zPattern, nPattern)) {
			zOut[j++] = zStr[i];
		} else {
			u8 *zOld;
			sql *db = sql_context_db_handle(context);
			nOut += nRep - nPattern;
			testcase(nOut - 1 == db->aLimit[SQL_LIMIT_LENGTH]);
			testcase(nOut - 2 == db->aLimit[SQL_LIMIT_LENGTH]);
			if (nOut - 1 > db->aLimit[SQL_LIMIT_LENGTH]) {
				diag_set(ClientError, ER_SQL_EXECUTE, "string "\
					 "or binary string is too big");
				context->is_aborted = true;
				sql_free(zOut);
				return;
			}
			zOld = zOut;
			zOut = sql_realloc64(zOut, (int)nOut);
			if (zOut == 0) {
				context->is_aborted = true;
				sql_free(zOld);
				return;
			}
			memcpy(&zOut[j], zRep, nRep);
			j += nRep;
			i += nPattern - 1;
		}
	}
	assert(j + nStr - i + 1 == nOut);
	memcpy(&zOut[j], &zStr[i], nStr - i);
	j += nStr - i;
	assert(j <= nOut);
	zOut[j] = 0;
	if (context->func->def->returns == FIELD_TYPE_STRING)
		mem_set_str_dynamic(context->pOut, (char *)zOut, j);
	else
		mem_set_bin_dynamic(context->pOut, (char *)zOut, j);
}

/*
 * Compute the soundex encoding of a word.
 *
 * IMP: R-59782-00072 The soundex(X) function returns a string that is the
 * soundex encoding of the string X.
 */
static void
soundexFunc(struct sql_context *context, int argc, struct Mem *argv)
{
	(void) argc;
	char zResult[8];
	const u8 *zIn;
	int i, j;
	static const unsigned char iCode[] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 1, 2, 3, 0, 1, 2, 0, 0, 2, 2, 4, 5, 5, 0,
		1, 2, 6, 2, 3, 0, 1, 0, 2, 0, 2, 0, 0, 0, 0, 0,
		0, 0, 1, 2, 3, 0, 1, 2, 0, 0, 2, 2, 4, 5, 5, 0,
		1, 2, 6, 2, 3, 0, 1, 0, 2, 0, 2, 0, 0, 0, 0, 0,
	};
	assert(argc == 1);
	if (mem_is_bin(&argv[0]) || mem_is_map(&argv[0]) ||
	    mem_is_array(&argv[0])) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
			 mem_str(&argv[0]), "string");
		context->is_aborted = true;
		return;
	}
	zIn = (u8 *) mem_as_ustr(&argv[0]);
	if (zIn == 0)
		zIn = (u8 *) "";
	for (i = 0; zIn[i] && !sqlIsalpha(zIn[i]); i++) {
	}
	if (zIn[i]) {
		u8 prevcode = iCode[zIn[i] & 0x7f];
		zResult[0] = sqlToupper(zIn[i]);
		for (j = 1; j < 4 && zIn[i]; i++) {
			int code = iCode[zIn[i] & 0x7f];
			if (code > 0) {
				if (code != prevcode) {
					prevcode = code;
					zResult[j++] = code + '0';
				}
			} else {
				prevcode = 0;
			}
		}
		while (j < 4) {
			zResult[j++] = '0';
		}
		zResult[j] = 0;
		sql_result_text(context, zResult, 4, SQL_TRANSIENT);
	} else {
		/* IMP: R-64894-50321 The string "?000" is returned if the argument
		 * is NULL or contains no ASCII alphabetic characters.
		 */
		sql_result_text(context, "?000", 4, SQL_STATIC);
	}
}

int
sql_is_like_func(struct Expr *expr)
{
	if (expr->op != TK_FUNCTION || !expr->x.pList ||
	    expr->x.pList->nExpr != 2)
		return 0;
	assert(!ExprHasProperty(expr, EP_xIsSelect));
	struct func *func = sql_func_find(expr);
	if (func == NULL || !sql_func_flag_is_set(func, SQL_FUNC_LIKE))
		return 0;
	return 1;
}

static int
func_sql_builtin_call_stub(struct func *func, struct port *args,
			   struct port *ret)
{
	(void) func; (void) args; (void) ret;
	diag_set(ClientError, ER_UNSUPPORTED,
		 "sql builtin function", "Lua frontend");
	return -1;
}

static void
sql_builtin_stub(sql_context *ctx, int argc, struct Mem *argv)
{
	(void) argc; (void) argv;
	diag_set(ClientError, ER_SQL_EXECUTE,
		 tt_sprintf("function '%s' is not implemented",
			    ctx->func->def->name));
	ctx->is_aborted = true;
}

/**
 * A structure that defines the relationship between a function and its
 * implementations.
 */
 struct sql_func_dictionary {
	/** Name of the function. */
	const char *name;
	/** The minimum number of arguments for all implementations. */
	int32_t argc_min;
	/** The maximum number of arguments for all implementations. */
	int32_t argc_max;
	/** Additional informations about the function. */
	uint32_t flags;
	/**
	 * True if the function is deterministic (can give only one result with
	 * the given arguments).
	 */
	bool is_deterministic;
	/** Count of function's implementations. */
	uint32_t count;
	/** Array of function implementations. */
	struct func_sql_builtin **functions;
};

static struct sql_func_dictionary dictionaries[] = {
	{"ABS", 1, 1, 0, true, 0, NULL},
	{"AVG", 1, 1, SQL_FUNC_AGG, false, 0, NULL},
	{"CHAR", 0, SQL_MAX_FUNCTION_ARG, 0, true, 0, NULL},
	{"CHARACTER_LENGTH", 1, 1, 0, true, 0, NULL},
	{"CHAR_LENGTH", 1, 1, 0, true, 0, NULL},
	{"COALESCE", 2, SQL_MAX_FUNCTION_ARG, SQL_FUNC_COALESCE, true, 0, NULL},
	{"COUNT", 0, 1, SQL_FUNC_AGG, false, 0, NULL},
	{"GREATEST", 2, SQL_MAX_FUNCTION_ARG, SQL_FUNC_NEEDCOLL, true, 0, NULL},
	{"GROUP_CONCAT", 1, 2, SQL_FUNC_AGG, false, 0, NULL},
	{"HEX", 1, 1, 0, true, 0, NULL},
	{"IFNULL", 2, 2, SQL_FUNC_COALESCE, true, 0, NULL},
	{"LEAST", 2, SQL_MAX_FUNCTION_ARG, SQL_FUNC_NEEDCOLL, true, 0, NULL},
	{"LENGTH", 1, 1, SQL_FUNC_LENGTH, true, 0, NULL},
	{"LIKE", 2, 3, SQL_FUNC_LIKE | SQL_FUNC_NEEDCOLL, true, 0, NULL},
	{"LIKELIHOOD", 2, 2, SQL_FUNC_UNLIKELY, true, 0, NULL},
	{"LIKELY", 1, 1, SQL_FUNC_UNLIKELY, true, 0, NULL},
	{"LOWER", 1, 1, SQL_FUNC_DERIVEDCOLL | SQL_FUNC_NEEDCOLL, true, 0,
	 NULL},
	{"MAX", 1, 1, SQL_FUNC_MAX | SQL_FUNC_AGG | SQL_FUNC_NEEDCOLL, false, 0,
	 NULL},
	{"MIN", 1, 1, SQL_FUNC_MIN | SQL_FUNC_AGG | SQL_FUNC_NEEDCOLL, false, 0,
	 NULL},
	{"NULLIF", 2, 2, SQL_FUNC_NEEDCOLL, true, 0, NULL},
	{"POSITION", 2, 2, SQL_FUNC_NEEDCOLL, true, 0, NULL},
	{"PRINTF", 0, SQL_MAX_FUNCTION_ARG, 0, true, 0, NULL},
	{"QUOTE", 1, 1, 0, true, 0, NULL},
	{"RANDOM", 0, 0, 0, false, 0, NULL},
	{"RANDOMBLOB", 1, 1, 0, false, 0, NULL},
	{"REPLACE", 3, 3, SQL_FUNC_DERIVEDCOLL, true, 0, NULL},
	{"ROUND", 1, 2, 0, true, 0, NULL},
	{"ROW_COUNT", 0, 0, 0, true, 0, NULL},
	{"SOUNDEX", 1, 1, 0, true, 0, NULL},
	{"SUBSTR", 2, 3, SQL_FUNC_DERIVEDCOLL, true, 0, NULL},
	{"SUM", 1, 1, SQL_FUNC_AGG, false, 0, NULL},
	{"TOTAL", 1, 1, SQL_FUNC_AGG, false, 0, NULL},
	{"TRIM", 2, 3, SQL_FUNC_DERIVEDCOLL, true, 0, NULL},
	{"TYPEOF", 1, 1, SQL_FUNC_TYPEOF, true, 0, NULL},
	{"UNICODE", 1, 1, 0, true, 0, NULL},
	{"UNLIKELY", 1, 1, SQL_FUNC_UNLIKELY, true, 0, NULL},
	{"UPPER", 1, 1, SQL_FUNC_DERIVEDCOLL | SQL_FUNC_NEEDCOLL, true, 0,
	 NULL},
	{"UUID", 0, 1, 0, false, 0, NULL},
	{"VERSION", 0, 0, 0, true, 0, NULL},
	{"ZEROBLOB", 1, 1, 0, true, 0, NULL},
};

/**
 * The structure that defines the implementation of the function. These
 * definitions are used during initialization to create all descibed
 * implementations of all built-in SQL functions.
 */
struct sql_func_definition {
	/** Name of the function. */
	const char *name;
	/** The number of arguments of the implementation. */
	int32_t argc;
	/**
	 * Types of implementation arguments. Only the first three arguments are
	 * described, but this should be sufficient, since all built-in SQL
	 * functions either have up to three arguments, or the number of their
	 * arguments is not limited here (but limited globally). If a function
	 * has an unlimited number of arguments, all arguments are of the same
	 * type.
	 */
	enum field_type argt[3];
	/** Type of the result of the implementation. */
	enum field_type result;
	/** Call implementation with given arguments. */
	void (*call)(sql_context *ctx, int argc, struct Mem *argv);
	/** Call finalization function for this implementation. */
	int (*finalize)(struct Mem *mem);
};

/**
 * Array of function implementation definitions. All implementations of the same
 * function should be defined in succession.
 */
static struct sql_func_definition definitions[] = {
	{"ABS", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, func_abs_int,
	 NULL},
	{"ABS", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, func_abs_double,
	 NULL},
	{"AVG", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, step_avg, fin_avg},
	{"AVG", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, step_avg, fin_avg},
	{"CHAR", -1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_STRING, func_char, NULL},
	{"CHAR_LENGTH", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_INTEGER,
	 func_char_length, NULL},
	{"COALESCE", -1, {FIELD_TYPE_ANY}, FIELD_TYPE_SCALAR, sql_builtin_stub,
	 NULL},
	{"COUNT", 0, {}, FIELD_TYPE_INTEGER, step_count, fin_count},
	{"COUNT", 1, {FIELD_TYPE_ANY}, FIELD_TYPE_INTEGER, step_count,
	 fin_count},

	{"GREATEST", -1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER,
	 func_greatest_least, NULL},
	{"GREATEST", -1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE,
	 func_greatest_least, NULL},
	{"GREATEST", -1, {FIELD_TYPE_NUMBER}, FIELD_TYPE_NUMBER,
	 func_greatest_least, NULL},
	{"GREATEST", -1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_VARBINARY,
	 func_greatest_least, NULL},
	{"GREATEST", -1, {FIELD_TYPE_UUID}, FIELD_TYPE_UUID,
	 func_greatest_least, NULL},
	{"GREATEST", -1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING,
	 func_greatest_least, NULL},
	{"GREATEST", -1, {FIELD_TYPE_SCALAR}, FIELD_TYPE_SCALAR,
	 func_greatest_least, NULL},

	{"GROUP_CONCAT", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING,
	 step_group_concat, NULL},
	{"GROUP_CONCAT", 2, {FIELD_TYPE_STRING, FIELD_TYPE_STRING},
	 FIELD_TYPE_STRING, step_group_concat, NULL},
	{"GROUP_CONCAT", 1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_VARBINARY,
	 step_group_concat, NULL},
	{"GROUP_CONCAT", 2, {FIELD_TYPE_VARBINARY, FIELD_TYPE_VARBINARY},
	 FIELD_TYPE_VARBINARY, step_group_concat, NULL},

	{"HEX", 1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_STRING, func_hex, NULL},
	{"IFNULL", 2, {FIELD_TYPE_ANY, FIELD_TYPE_ANY}, FIELD_TYPE_SCALAR,
	 sql_builtin_stub, NULL},

	{"LEAST", -1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER,
	 func_greatest_least, NULL},
	{"LEAST", -1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE,
	 func_greatest_least, NULL},
	{"LEAST", -1, {FIELD_TYPE_NUMBER}, FIELD_TYPE_NUMBER,
	 func_greatest_least, NULL},
	{"LEAST", -1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_VARBINARY,
	 func_greatest_least, NULL},
	{"LEAST", -1, {FIELD_TYPE_UUID}, FIELD_TYPE_UUID,
	 func_greatest_least, NULL},
	{"LEAST", -1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING,
	 func_greatest_least, NULL},
	{"LEAST", -1, {FIELD_TYPE_SCALAR}, FIELD_TYPE_SCALAR,
	 func_greatest_least, NULL},

	{"LENGTH", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_INTEGER, func_char_length,
	 NULL},
	{"LENGTH", 1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_INTEGER,
	 func_octet_length, NULL},
	{"LIKE", 2, {FIELD_TYPE_STRING, FIELD_TYPE_STRING},
	 FIELD_TYPE_BOOLEAN, likeFunc, NULL},
	{"LIKE", 3, {FIELD_TYPE_STRING, FIELD_TYPE_STRING, FIELD_TYPE_STRING},
	 FIELD_TYPE_BOOLEAN, likeFunc, NULL},
	{"LIKELIHOOD", 2, {FIELD_TYPE_ANY, FIELD_TYPE_DOUBLE},
	 FIELD_TYPE_BOOLEAN, sql_builtin_stub, NULL},
	{"LIKELY", 1, {FIELD_TYPE_ANY}, FIELD_TYPE_BOOLEAN, sql_builtin_stub,
	 NULL},
	{"LOWER", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, func_lower_upper,
	 NULL},

	{"MAX", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, step_minmax, NULL},
	{"MAX", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, step_minmax, NULL},
	{"MAX", 1, {FIELD_TYPE_NUMBER}, FIELD_TYPE_NUMBER, step_minmax, NULL},
	{"MAX", 1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_VARBINARY, step_minmax,
	 NULL},
	{"MAX", 1, {FIELD_TYPE_UUID}, FIELD_TYPE_UUID, step_minmax, NULL},
	{"MAX", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, step_minmax, NULL},
	{"MAX", 1, {FIELD_TYPE_SCALAR}, FIELD_TYPE_SCALAR, step_minmax, NULL},

	{"MIN", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, step_minmax, NULL},
	{"MIN", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, step_minmax, NULL},
	{"MIN", 1, {FIELD_TYPE_NUMBER}, FIELD_TYPE_NUMBER, step_minmax, NULL},
	{"MIN", 1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_VARBINARY, step_minmax,
	 NULL},
	{"MIN", 1, {FIELD_TYPE_UUID}, FIELD_TYPE_UUID, step_minmax, NULL},
	{"MIN", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, step_minmax, NULL},
	{"MIN", 1, {FIELD_TYPE_SCALAR}, FIELD_TYPE_SCALAR, step_minmax, NULL},

	{"NULLIF", 2, {FIELD_TYPE_ANY, FIELD_TYPE_ANY}, FIELD_TYPE_SCALAR,
	 func_nullif, NULL},
	{"POSITION", 2, {FIELD_TYPE_STRING, FIELD_TYPE_STRING},
	 FIELD_TYPE_INTEGER, func_position_characters, NULL},
	{"POSITION", 2, {FIELD_TYPE_VARBINARY, FIELD_TYPE_VARBINARY},
	 FIELD_TYPE_INTEGER, func_position_octets, NULL},
	{"PRINTF", -1, {FIELD_TYPE_ANY}, FIELD_TYPE_STRING, func_printf, NULL},
	{"QUOTE", 1, {FIELD_TYPE_ANY}, FIELD_TYPE_STRING, quoteFunc, NULL},
	{"RANDOM", 0, {}, FIELD_TYPE_INTEGER, func_random, NULL},
	{"RANDOMBLOB", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_VARBINARY,
	 func_randomblob, NULL},
	{"REPLACE", 3,
	 {FIELD_TYPE_STRING, FIELD_TYPE_STRING, FIELD_TYPE_STRING},
	 FIELD_TYPE_STRING, replaceFunc, NULL},
	{"REPLACE", 3,
	 {FIELD_TYPE_VARBINARY, FIELD_TYPE_VARBINARY, FIELD_TYPE_VARBINARY},
	 FIELD_TYPE_VARBINARY, replaceFunc, NULL},
	{"ROUND", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, roundFunc, NULL},
	{"ROUND", 2, {FIELD_TYPE_DOUBLE, FIELD_TYPE_INTEGER}, FIELD_TYPE_DOUBLE,
	 roundFunc, NULL},
	{"ROW_COUNT", 0, {}, FIELD_TYPE_INTEGER, sql_row_count, NULL},
	{"SOUNDEX", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, soundexFunc,
	 NULL},
	{"SUBSTR", 2, {FIELD_TYPE_STRING, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_STRING, func_substr_characters, NULL},
	{"SUBSTR", 3,
	 {FIELD_TYPE_STRING, FIELD_TYPE_INTEGER, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_STRING, func_substr_characters, NULL},
	{"SUBSTR", 2, {FIELD_TYPE_VARBINARY, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_VARBINARY, func_substr_octets, NULL},
	{"SUBSTR", 3,
	 {FIELD_TYPE_VARBINARY, FIELD_TYPE_INTEGER, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_VARBINARY, func_substr_octets, NULL},
	{"SUM", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, step_sum, NULL},
	{"SUM", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, step_sum, NULL},
	{"TOTAL", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_DOUBLE, step_total,
	 fin_total},
	{"TOTAL", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, step_total,
	 fin_total},

	{"TRIM", 2, {FIELD_TYPE_STRING, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_STRING, func_trim_str, NULL},
	{"TRIM", 3, {FIELD_TYPE_STRING, FIELD_TYPE_INTEGER, FIELD_TYPE_STRING},
	 FIELD_TYPE_STRING, func_trim_str, NULL},
	{"TRIM", 2, {FIELD_TYPE_VARBINARY, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_VARBINARY, func_trim_bin, NULL},
	{"TRIM", 3,
	 {FIELD_TYPE_VARBINARY, FIELD_TYPE_INTEGER, FIELD_TYPE_VARBINARY},
	 FIELD_TYPE_VARBINARY, func_trim_bin, NULL},

	{"TYPEOF", 1, {FIELD_TYPE_ANY}, FIELD_TYPE_STRING, typeofFunc, NULL},
	{"UNICODE", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_INTEGER, unicodeFunc,
	 NULL},
	{"UNLIKELY", 1, {FIELD_TYPE_ANY}, FIELD_TYPE_BOOLEAN, sql_builtin_stub,
	 NULL},
	{"UPPER", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, func_lower_upper,
	 NULL},
	{"UUID", 0, {}, FIELD_TYPE_UUID, sql_func_uuid, NULL},
	{"UUID", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_UUID, sql_func_uuid, NULL},
	{"VERSION", 0, {}, FIELD_TYPE_STRING, sql_func_version, NULL},
	{"ZEROBLOB", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_VARBINARY,
	 zeroblobFunc, NULL},
};

static struct sql_func_dictionary *
built_in_func_get(const char *name)
{
	uint32_t len = strlen(name);
	mh_int_t k = mh_strnptr_find_inp(built_in_functions, name, len);
	if (k == mh_end(built_in_functions))
		return NULL;
	return mh_strnptr_node(built_in_functions, k)->val;
}

static void
built_in_func_put(struct sql_func_dictionary *dict)
{
	const char *name = dict->name;
	uint32_t len = strlen(name);
	assert(built_in_func_get(name) == NULL);

	uint32_t hash = mh_strn_hash(name, len);
	const struct mh_strnptr_node_t strnode = {name, len, hash, dict};
	mh_strnptr_put(built_in_functions, &strnode, NULL, NULL);
}

/**
 * Check if there is no need to cast argument to accepted type. Also, in some
 * cases operation 'op' may be important, for example when given argument is
 * NULL or is variable.
 *
 * Returns TRUE when:
 *  - when operation is NULL;
 *  - when accepted type and argument type are equal;
 *  - when accepted type is ANY;
 *  - when accepted type is INTEGER and argument type is UNSIGNED.
 */
static inline bool
is_exact(int op, enum field_type a, enum field_type b)
{
	return op == TK_NULL || a == b || a == FIELD_TYPE_ANY ||
	       (a == FIELD_TYPE_INTEGER && b == FIELD_TYPE_UNSIGNED);
}

/**
 * Check if the argument MEM type will not change during cast. It means that
 * either is_exact() returns TRUE or accepted type is metatype that includes
 * argument type.
 *
 * Returns TRUE when:
 *  - is_exact() returns TRUE;
 *  - when accepted type is NUMBER and argument type is numeric type;
 *  - when accepted type is SCALAR and argument type is not MAP or ARRAY.
 */
static inline bool
is_upcast(int op, enum field_type a, enum field_type b)
{
	return is_exact(op, a, b) ||
	       (a == FIELD_TYPE_NUMBER && sql_type_is_numeric(b)) ||
	       (a == FIELD_TYPE_SCALAR && b != FIELD_TYPE_MAP &&
		b != FIELD_TYPE_ARRAY);
}

/**
 * Check if there is a chance that the argument can be cast to accepted type
 * according to implicit cast rules.
 *
 * Returns TRUE when:
 *  - is_upcast() returns TRUE;
 *  - when accepted type and argument type are numeric types;
 *  - when argument is binded value;
 *  - when argument type is ANY, which means that is was not resolved.
 */
static inline bool
is_castable(int op, enum field_type a, enum field_type b)
{
	return is_upcast(op, a, b) || op == TK_VARIABLE ||
	       (sql_type_is_numeric(a) && sql_type_is_numeric(b)) ||
	       b == FIELD_TYPE_ANY;
}

enum check_type {
	CHECK_TYPE_EXACT,
	CHECK_TYPE_UPCAST,
	CHECK_TYPE_CASTABLE,
};

static struct func *
find_compatible(struct Expr *expr, struct sql_func_dictionary *dict,
		enum check_type check)
{
	int n = expr->x.pList != NULL ? expr->x.pList->nExpr : 0;
	for (uint32_t i = 0; i < dict->count; ++i) {
		struct func_sql_builtin *func = dict->functions[i];
		int argc = func->base.def->param_count;
		if (argc != n && argc != -1)
			continue;
		if (n == 0)
			return &func->base;

		enum field_type *types = func->param_list;
		bool is_match = true;
		for (int j = 0; j < n && is_match; ++j) {
			struct Expr *e = expr->x.pList->a[j].pExpr;
			enum field_type a = types[argc != -1 ? j : 0];
			enum field_type b = sql_expr_type(e);
			switch (check) {
			case CHECK_TYPE_EXACT:
				is_match = is_exact(e->op, a, b);
				break;
			case CHECK_TYPE_UPCAST:
				is_match = is_upcast(e->op, a, b);
				break;
			case CHECK_TYPE_CASTABLE:
				is_match = is_castable(e->op, a, b);
				break;
			default:
				unreachable();
			}
		}
		if (is_match)
			return &func->base;
	}
	return NULL;
}

static struct func *
find_built_in_func(struct Expr *expr, struct sql_func_dictionary *dict)
{
	const char *name = expr->u.zToken;
	int n = expr->x.pList != NULL ? expr->x.pList->nExpr : 0;
	int argc_min = dict->argc_min;
	int argc_max = dict->argc_max;
	if (n < argc_min || n > argc_max) {
		const char *str;
		if (argc_min == argc_max)
			str = tt_sprintf("%d", argc_min);
		else if (argc_max == SQL_MAX_FUNCTION_ARG && n < argc_min)
			str = tt_sprintf("at least %d", argc_min);
		else
			str = tt_sprintf("from %d to %d", argc_min, argc_max);
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT, name, str, n);
		return NULL;
	}
	struct func *func = find_compatible(expr, dict, CHECK_TYPE_EXACT);
	if (func != NULL)
		return func;
	func = find_compatible(expr, dict, CHECK_TYPE_UPCAST);
	if (func != NULL)
		return func;
	func = find_compatible(expr, dict, CHECK_TYPE_CASTABLE);
	if (func != NULL)
		return func;
	diag_set(ClientError, ER_SQL_EXECUTE,
		 tt_sprintf("wrong arguments for function %s()", name));
	return NULL;
}

struct func *
sql_func_find(struct Expr *expr)
{
	const char *name = expr->u.zToken;
	struct sql_func_dictionary *dict = built_in_func_get(name);
	if (dict != NULL)
		return find_built_in_func(expr, dict);
	struct func *func = func_by_name(name, strlen(name));
	if (func == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FUNCTION, name);
		return NULL;
	}
	if (!func->def->exports.sql) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 tt_sprintf("function %s() is not available in SQL",
				     name));
		return NULL;
	}
	int n = expr->x.pList != NULL ? expr->x.pList->nExpr : 0;
	if (func->def->param_count != n) {
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT, name,
			 tt_sprintf("%d", func->def->param_count), n);
		return NULL;
	}
	return func;
}

uint32_t
sql_func_flags(const char *name)
{
	struct sql_func_dictionary *dict = built_in_func_get(name);
	if (dict == NULL)
		return 0;
	return dict->flags;
}

static struct func_vtab func_sql_builtin_vtab;

void
sql_built_in_functions_cache_init(void)
{
	built_in_functions = mh_strnptr_new();
	for (uint32_t i = 0; i < nelem(dictionaries); ++i)
		built_in_func_put(&dictionaries[i]);

	functions = malloc(sizeof(*functions) * nelem(definitions));
	for (uint32_t i = 0; i < nelem(definitions); ++i) {
		struct sql_func_definition *desc = &definitions[i];
		const char *name = desc->name;
		struct sql_func_dictionary *dict = built_in_func_get(name);
		assert(dict != NULL);

		uint32_t len = strlen(name);
		uint32_t size = sizeof(struct func_def) + len + 1;
		struct func_def *def = malloc(size);
		if (def == NULL)
			panic("Out of memory on creating SQL built-in");
		def->fid = i;
		def->uid = 1;
		def->body = NULL;
		def->comment = NULL;
		def->setuid = true;
		def->is_deterministic = dict->is_deterministic;
		def->is_sandboxed = false;
		assert(desc->argc != -1 || dict->argc_min != dict->argc_max);
		def->param_count = desc->argc;
		def->returns = desc->result;
		def->aggregate = (dict->flags & SQL_FUNC_AGG) == 0 ?
				 FUNC_AGGREGATE_NONE : FUNC_AGGREGATE_GROUP;
		def->language = FUNC_LANGUAGE_SQL_BUILTIN;
		def->name_len = len;
		def->exports.sql = true;
		func_opts_create(&def->opts);
		memcpy(def->name, name, len + 1);

		struct func_sql_builtin *func = malloc(sizeof(*func));
		if (func == NULL)
			panic("Out of memory on creating SQL built-in");

		func->base.def = def;
		func->base.vtab = &func_sql_builtin_vtab;
		credentials_create_empty(&func->base.owner_credentials);
		memset(func->base.access, 0, sizeof(func->base.access));

		func->param_list = desc->argt;
		func->flags = dict->flags;
		func->call = desc->call;
		func->finalize = desc->finalize;
		functions[i] = func;
		assert(dict->count == 0 || dict->functions != NULL);
		if (dict->functions == NULL)
			dict->functions = &functions[i];
		++dict->count;
	}
	/*
	 * Initialization of CHARACTER_LENGTH() function, which is actually
	 * another name for CHAR_LENGTH().
	 */
	const char *name = "CHARACTER_LENGTH";
	struct sql_func_dictionary *dict = built_in_func_get(name);
	name = "CHAR_LENGTH";
	struct sql_func_dictionary *dict_original = built_in_func_get(name);
	dict->count = dict_original->count;
	dict->functions = dict_original->functions;
}

void
sql_built_in_functions_cache_free(void)
{
	if (built_in_functions == NULL)
		return;
	for (uint32_t i = 0; i < nelem(definitions); ++i)
		func_delete(&functions[i]->base);
	for (uint32_t i = 0; i < nelem(dictionaries); ++i) {
		const char *name = dictionaries[i].name;
		uint32_t len = strlen(name);
		mh_int_t k = mh_strnptr_find_inp(built_in_functions, name, len);
		if (k == mh_end(built_in_functions))
			continue;
		mh_strnptr_del(built_in_functions, k, NULL);
	}
	assert(mh_size(built_in_functions) == 0);
	mh_strnptr_delete(built_in_functions);
}

static void
func_sql_builtin_destroy(struct func *func)
{
	assert(func->vtab == &func_sql_builtin_vtab);
	assert(func->def->language == FUNC_LANGUAGE_SQL_BUILTIN);
	free(func);
}

static struct func_vtab func_sql_builtin_vtab = {
	.call = func_sql_builtin_call_stub,
	.destroy = func_sql_builtin_destroy,
};
