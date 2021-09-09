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
step_sum(struct sql_context *ctx, int argc, struct Mem **argv)
{
	assert(argc == 1);
	(void)argc;
	assert(mem_is_null(ctx->pMem) || mem_is_num(ctx->pMem));
	if (mem_is_null(argv[0]))
		return;
	if (mem_is_null(ctx->pMem))
		return mem_copy_as_ephemeral(ctx->pMem, argv[0]);
	if (mem_add(ctx->pMem, argv[0], ctx->pMem) != 0)
		ctx->is_aborted = true;
}

/** Finalizer for the SUM() function. */
static void
fin_sum(struct sql_context *ctx)
{
	assert(mem_is_null(ctx->pMem) || mem_is_num(ctx->pMem));
	mem_copy_as_ephemeral(ctx->pOut, ctx->pMem);
}

/** Implementation of the TOTAL() function. */
static void
step_total(struct sql_context *ctx, int argc, struct Mem **argv)
{
	assert(argc == 1);
	(void)argc;
	assert(mem_is_null(ctx->pMem) || mem_is_num(ctx->pMem));
	if (mem_is_null(argv[0]))
		return;
	if (mem_is_null(ctx->pMem))
		mem_set_double(ctx->pMem, 0.0);
	if (mem_add(ctx->pMem, argv[0], ctx->pMem) != 0)
		ctx->is_aborted = true;
}

/** Finalizer for the TOTAL() function. */
static void
fin_total(struct sql_context *ctx)
{
	assert(mem_is_null(ctx->pMem) || mem_is_double(ctx->pMem));
	if (mem_is_null(ctx->pMem))
		mem_set_double(ctx->pOut, 0.0);
	else
		mem_copy_as_ephemeral(ctx->pOut, ctx->pMem);
}

/** Implementation of the AVG() function. */
static void
step_avg(struct sql_context *ctx, int argc, struct Mem **argv)
{
	assert(argc == 1);
	(void)argc;
	assert(mem_is_null(ctx->pMem) || mem_is_bin(ctx->pMem));
	if (mem_is_null(argv[0]))
		return;
	struct Mem *mem;
	uint32_t *count;
	if (mem_is_null(ctx->pMem)) {
		uint32_t size = sizeof(struct Mem) + sizeof(uint32_t);
		mem = sqlDbMallocRawNN(sql_get(), size);
		if (mem == NULL) {
			ctx->is_aborted = true;
			return;
		}
		count = (uint32_t *)(mem + 1);
		mem_create(mem);
		*count = 1;
		mem_copy_as_ephemeral(mem, argv[0]);
		mem_set_bin_allocated(ctx->pMem, (char *)mem, size);
		return;
	}
	mem = (struct Mem *)ctx->pMem->z;
	count = (uint32_t *)(mem + 1);
	++*count;
	if (mem_add(mem, argv[0], mem) != 0)
		ctx->is_aborted = true;
}

/** Finalizer for the AVG() function. */
static void
fin_avg(struct sql_context *ctx)
{
	assert(mem_is_null(ctx->pMem) || mem_is_bin(ctx->pMem));
	if (mem_is_null(ctx->pMem))
		return mem_set_null(ctx->pOut);
	struct Mem *sum = (struct Mem *)ctx->pMem->z;
	uint32_t *count_val = (uint32_t *)(sum + 1);
	assert(mem_is_trivial(sum));
	struct Mem count;
	mem_create(&count);
	mem_set_uint(&count, *count_val);
	if (mem_div(sum, &count, ctx->pOut) != 0)
		ctx->is_aborted = true;
}

/** Implementation of the COUNT() function. */
static void
step_count(struct sql_context *ctx, int argc, struct Mem **argv)
{
	assert(argc == 0 || argc == 1);
	if (mem_is_null(ctx->pMem))
		mem_set_uint(ctx->pMem, 0);
	if (argc == 1 && mem_is_null(argv[0]))
		return;
	assert(mem_is_uint(ctx->pMem));
	++ctx->pMem->u.u;
}

/** Finalizer for the COUNT() function. */
static void
fin_count(struct sql_context *ctx)
{
	assert(mem_is_null(ctx->pMem) || mem_is_uint(ctx->pMem));
	if (mem_is_null(ctx->pMem))
		return mem_set_uint(ctx->pOut, 0);
	mem_copy_as_ephemeral(ctx->pOut, ctx->pMem);
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
sql_func_uuid(struct sql_context *ctx, int argc, struct Mem **argv)
{
	if (argc > 1) {
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT, "UUID",
			 "one or zero", argc);
		ctx->is_aborted = true;
		return;
	}
	if (argc == 1) {
		uint64_t version;
		if (mem_get_uint(argv[0], &version) != 0) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
				 mem_str(argv[0]), "integer");
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
 * Indicate that the accumulator load should be skipped on this
 * iteration of the aggregate loop.
 */
static void
sqlSkipAccumulatorLoad(sql_context * context)
{
	context->skipFlag = 1;
}

/*
 * Implementation of the non-aggregate min() and max() functions
 */
static void
minmaxFunc(sql_context * context, int argc, sql_value ** argv)
{
	int i;
	int iBest;
	struct coll *pColl;
	struct func *func = context->func;
	int mask = sql_func_flag_is_set(func, SQL_FUNC_MAX) ? -1 : 0;
	if (argc < 2) {
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT,
		mask ? "GREATEST" : "LEAST", "at least two", argc);
		context->is_aborted = true;
		return;
	}
	pColl = context->coll;
	assert(mask == -1 || mask == 0);
	iBest = 0;
	if (mem_is_null(argv[0]))
		return;
	for (i = 1; i < argc; i++) {
		if (mem_is_null(argv[i]))
			return;
		if ((mem_cmp_scalar(argv[iBest], argv[i], pColl) ^ mask) >= 0)
			iBest = i;
	}
	sql_result_value(context, argv[iBest]);
}

/*
 * Return the type of the argument.
 */
static void
typeofFunc(sql_context * context, int NotUsed, sql_value ** argv)
{
	const char *z = 0;
	UNUSED_PARAMETER(NotUsed);
	if ((argv[0]->flags & MEM_Number) != 0)
		return mem_set_str0_static(context->pOut, "number");
	if ((argv[0]->flags & MEM_Scalar) != 0)
		return mem_set_str0_static(context->pOut, "scalar");
	switch (argv[0]->type) {
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
 * Implementation of the length() function
 */
static void
lengthFunc(sql_context * context, int argc, sql_value ** argv)
{
	int len;

	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	switch (sql_value_type(argv[0])) {
	case MP_BIN:
	case MP_ARRAY:
	case MP_MAP:
	case MP_INT:
	case MP_UINT:
	case MP_BOOL:
	case MP_DOUBLE:{
			mem_as_bin(argv[0]);
			sql_result_uint(context, mem_len_unsafe(argv[0]));
			break;
		}
	case MP_EXT:
	case MP_STR:{
			const unsigned char *z = mem_as_ustr(argv[0]);
			if (z == 0)
				return;
			len = sql_utf8_char_count(z, mem_len_unsafe(argv[0]));
			sql_result_uint(context, len);
			break;
		}
	default:{
			sql_result_null(context);
			break;
		}
	}
}

/*
 * Implementation of the abs() function.
 *
 * IMP: R-23979-26855 The abs(X) function returns the absolute value of
 * the numeric argument X.
 */
static void
absFunc(sql_context * context, int argc, sql_value ** argv)
{
	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	switch (sql_value_type(argv[0])) {
	case MP_UINT: {
		sql_result_uint(context, mem_get_uint_unsafe(argv[0]));
		break;
	}
	case MP_INT: {
		int64_t value = mem_get_int_unsafe(argv[0]);
		assert(value < 0);
		sql_result_uint(context, -value);
		break;
	}
	case MP_NIL:{
			/* IMP: R-37434-19929 Abs(X) returns NULL if X is NULL. */
			sql_result_null(context);
			break;
		}
	case MP_BOOL:
	case MP_BIN:
	case MP_EXT:
	case MP_ARRAY:
	case MP_MAP: {
		diag_set(ClientError, ER_INCONSISTENT_TYPES, "number",
			 mem_str(argv[0]));
		context->is_aborted = true;
		return;
	}
	default:{
			/*
			 * Abs(X) returns 0.0 if X is a string or blob
			 * that cannot be converted to a numeric value.
			 */
			double rVal = mem_get_double_unsafe(argv[0]);
			if (rVal < 0)
				rVal = -rVal;
			sql_result_double(context, rVal);
			break;
		}
	}
}

/**
 * Implementation of the position() function.
 *
 * position(needle, haystack) finds the first occurrence of needle
 * in haystack and returns the number of previous characters
 * plus 1, or 0 if needle does not occur within haystack.
 *
 * If both haystack and needle are BLOBs, then the result is one
 * more than the number of bytes in haystack prior to the first
 * occurrence of needle, or 0 if needle never occurs in haystack.
 */
static void
position_func(struct sql_context *context, int argc, struct Mem **argv)
{
	UNUSED_PARAMETER(argc);
	struct Mem *needle = argv[0];
	struct Mem *haystack = argv[1];
	enum mp_type needle_type = sql_value_type(needle);
	enum mp_type haystack_type = sql_value_type(haystack);

	if (haystack_type == MP_NIL || needle_type == MP_NIL)
		return;
	/*
	 * Position function can be called only with string
	 * or blob params.
	 */
	struct Mem *inconsistent_type_arg = NULL;
	if (needle_type != MP_STR && needle_type != MP_BIN)
		inconsistent_type_arg = needle;
	if (haystack_type != MP_STR && haystack_type != MP_BIN)
		inconsistent_type_arg = haystack;
	if (inconsistent_type_arg != NULL) {
		diag_set(ClientError, ER_INCONSISTENT_TYPES,
			 "string or varbinary", mem_str(inconsistent_type_arg));
		context->is_aborted = true;
		return;
	}
	/*
	 * Both params of Position function must be of the same
	 * type.
	 */
	if (haystack_type != needle_type) {
		diag_set(ClientError, ER_INCONSISTENT_TYPES,
			 mem_type_to_str(needle), mem_str(haystack));
		context->is_aborted = true;
		return;
	}

	int n_needle_bytes = mem_len_unsafe(needle);
	int n_haystack_bytes = mem_len_unsafe(haystack);
	int position = 1;
	if (n_needle_bytes > 0) {
		const unsigned char *haystack_str;
		const unsigned char *needle_str;
		if (haystack_type == MP_BIN) {
			needle_str = mem_as_bin(needle);
			haystack_str = mem_as_bin(haystack);
			assert(needle_str != NULL);
			assert(haystack_str != NULL || n_haystack_bytes == 0);
			/*
			 * Naive implementation of substring
			 * searching: matching time O(n * m).
			 * Can be improved.
			 */
			while (n_needle_bytes <= n_haystack_bytes &&
			       memcmp(haystack_str, needle_str, n_needle_bytes) != 0) {
				position++;
				n_haystack_bytes--;
				haystack_str++;
			}
			if (n_needle_bytes > n_haystack_bytes)
				position = 0;
		} else {
			/*
			 * Code below handles not only simple
			 * cases like position('a', 'bca'), but
			 * also more complex ones:
			 * position('a', 'bcá' COLLATE "unicode_ci")
			 * To do so, we need to use comparison
			 * window, which has constant character
			 * size, but variable byte size.
			 * Character size is equal to
			 * needle char size.
			 */
			haystack_str = mem_as_ustr(haystack);
			needle_str = mem_as_ustr(needle);

			int n_needle_chars =
				sql_utf8_char_count(needle_str, n_needle_bytes);
			int n_haystack_chars =
				sql_utf8_char_count(haystack_str,
						    n_haystack_bytes);

			if (n_haystack_chars < n_needle_chars) {
				position = 0;
				goto finish;
			}
			/*
			 * Comparison window is determined by
			 * beg_offset and end_offset. beg_offset
			 * is offset in bytes from haystack
			 * beginning to window beginning.
			 * end_offset is offset in bytes from
			 * haystack beginning to window end.
			 */
			int end_offset = 0;
			for (int c = 0; c < n_needle_chars; c++) {
				SQL_UTF8_FWD_1(haystack_str, end_offset,
					       n_haystack_bytes);
			}
			int beg_offset = 0;
			struct coll *coll = context->coll;
			int c;
			for (c = 0; c + n_needle_chars <= n_haystack_chars; c++) {
				if (coll->cmp((const char *) haystack_str + beg_offset,
					      end_offset - beg_offset,
					      (const char *) needle_str,
					      n_needle_bytes, coll) == 0)
					goto finish;
				position++;
				/* Update offsets. */
				SQL_UTF8_FWD_1(haystack_str, beg_offset,
					       n_haystack_bytes);
				SQL_UTF8_FWD_1(haystack_str, end_offset,
					       n_haystack_bytes);
			}
			/* Needle was not found in the haystack. */
			position = 0;
		}
	}
finish:
	assert(position >= 0);
	sql_result_uint(context, position);
}

/*
 * Implementation of the printf() function.
 */
static void
printfFunc(sql_context * context, int argc, sql_value ** argv)
{
	PrintfArguments x;
	StrAccum str;
	const char *zFormat;
	int n;
	sql *db = sql_context_db_handle(context);

	if (argc >= 1 && (zFormat = mem_as_str0(argv[0])) != NULL) {
		x.nArg = argc - 1;
		x.nUsed = 0;
		x.apArg = argv + 1;
		sqlStrAccumInit(&str, db, 0, 0,
				    db->aLimit[SQL_LIMIT_LENGTH]);
		str.printfFlags = SQL_PRINTF_SQLFUNC;
		sqlXPrintf(&str, zFormat, &x);
		n = str.nChar;
		sql_result_text(context, sqlStrAccumFinish(&str), n,
				    SQL_DYNAMIC);
	}
}

/*
 * Implementation of the substr() function.
 *
 * substr(x,p1,p2)  returns p2 characters of x[] beginning with p1.
 * p1 is 1-indexed.  So substr(x,1,1) returns the first character
 * of x.  If x is text, then we actually count UTF-8 characters.
 * If x is a blob, then we count bytes.
 *
 * If p1 is negative, then we begin abs(p1) from the end of x[].
 *
 * If p2 is negative, return the p2 characters preceding p1.
 */
static void
substrFunc(sql_context * context, int argc, sql_value ** argv)
{
	const unsigned char *z;
	const unsigned char *z2;
	int len;
	int p0type;
	int64_t p1, p2;
	int negP2 = 0;

	if (argc != 2 && argc != 3) {
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT, "SUBSTR",
			 "1 or 2", argc);
		context->is_aborted = true;
		return;
	}
	if (mem_is_null(argv[1]) || (argc == 3 && mem_is_null(argv[2])))
		return;
	p0type = sql_value_type(argv[0]);
	p1 = mem_get_int_unsafe(argv[1]);
	if (p0type == MP_BIN) {
		z = mem_as_bin(argv[0]);
		len = mem_len_unsafe(argv[0]);
		if (z == 0)
			return;
		assert(len == mem_len_unsafe(argv[0]));
	} else {
		z = mem_as_ustr(argv[0]);
		if (z == 0)
			return;
		len = 0;
		if (p1 < 0)
			len = sql_utf8_char_count(z, mem_len_unsafe(argv[0]));
	}
	if (argc == 3) {
		p2 = mem_get_int_unsafe(argv[2]);
		if (p2 < 0) {
			p2 = -p2;
			negP2 = 1;
		}
	} else {
		p2 = sql_context_db_handle(context)->
		    aLimit[SQL_LIMIT_LENGTH];
	}
	if (p1 < 0) {
		p1 += len;
		if (p1 < 0) {
			p2 += p1;
			if (p2 < 0)
				p2 = 0;
			p1 = 0;
		}
	} else if (p1 > 0) {
		p1--;
	} else if (p2 > 0) {
		p2--;
	}
	if (negP2) {
		p1 -= p2;
		if (p1 < 0) {
			p2 += p1;
			p1 = 0;
		}
	}
	assert(p1 >= 0 && p2 >= 0);
	if (p0type != MP_BIN) {
		/*
		 * In the code below 'cnt' and 'n_chars' is
		 * used because '\0' is not supposed to be
		 * end-of-string symbol.
		 */
		int byte_size = mem_len_unsafe(argv[0]);
		int n_chars = sql_utf8_char_count(z, byte_size);
		int cnt = 0;
		int i = 0;
		while (cnt < n_chars && p1) {
			SQL_UTF8_FWD_1(z, i, byte_size);
			cnt++;
			p1--;
		}
		z += i;
		i = 0;
		for (z2 = z; cnt < n_chars && p2; p2--) {
			SQL_UTF8_FWD_1(z2, i, byte_size);
			cnt++;
		}
		z2 += i;
		mem_copy_str(context->pOut, (char *)z, z2 - z);
	} else {
		if (p1 + p2 > len) {
			p2 = len - p1;
			if (p2 < 0)
				p2 = 0;
		}
		mem_copy_bin(context->pOut, (char *)&z[p1], p2);
	}
}

/*
 * Implementation of the round() function
 */
static void
roundFunc(sql_context * context, int argc, sql_value ** argv)
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
		if (mem_is_null(argv[1]))
			return;
		n = mem_get_int_unsafe(argv[1]);
		if (n < 0)
			n = 0;
	}
	if (mem_is_null(argv[0]))
		return;
	if (!mem_is_num(argv[0]) && !mem_is_str(argv[0])) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
			 mem_str(argv[0]), "number");
		context->is_aborted = true;
		return;
	}
	r = mem_get_double_unsafe(argv[0]);
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
contextMalloc(sql_context * context, i64 nByte)
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

/*
 * Implementation of the upper() and lower() SQL functions.
 */

#define ICU_CASE_CONVERT(case_type)                                            \
static void                                                                    \
case_type##ICUFunc(sql_context *context, int argc, sql_value **argv)   \
{                                                                              \
	char *z1;                                                              \
	const char *z2;                                                        \
	int n;                                                                 \
	UNUSED_PARAMETER(argc);                                                \
	if (mem_is_bin(argv[0]) || mem_is_map(argv[0]) ||                      \
	    mem_is_array(argv[0])) {                                           \
		diag_set(ClientError, ER_INCONSISTENT_TYPES, "string",         \
			 mem_str(argv[0]));                                    \
		context->is_aborted = true;                                    \
		return;                                                        \
	}                                                                      \
	z2 = mem_as_str0(argv[0]);                                             \
	n = mem_len_unsafe(argv[0]);                                           \
	/*                                                                     \
	 * Verify that the call to _bytes()                                    \
	 * does not invalidate the _text() pointer.                            \
	 */                                                                    \
	assert(z2 == mem_as_str0(argv[0]));                                    \
	if (!z2)                                                               \
		return;                                                        \
	z1 = contextMalloc(context, ((i64) n) + 1);                            \
	if (z1 == NULL) {                                                      \
		context->is_aborted = true;                                    \
		return;                                                        \
	}                                                                      \
	UErrorCode status = U_ZERO_ERROR;                                      \
	struct coll *coll = context->coll;                                     \
	const char *locale = NULL;                                             \
	if (coll != NULL && coll->type == COLL_TYPE_ICU) {                     \
		locale = ucol_getLocaleByType(coll->collator,                  \
					      ULOC_VALID_LOCALE, &status);     \
	}                                                                      \
	UCaseMap *case_map = ucasemap_open(locale, 0, &status);                \
	assert(case_map != NULL);                                              \
	int len = ucasemap_utf8To##case_type(case_map, z1, n, z2, n, &status); \
	if (len > n) {                                                         \
		status = U_ZERO_ERROR;                                         \
		sql_free(z1);                                              \
		z1 = contextMalloc(context, ((i64) len) + 1);                  \
		if (z1 == NULL) {                                              \
			context->is_aborted = true;                            \
			return;                                                \
		}                                                              \
		ucasemap_utf8To##case_type(case_map, z1, len, z2, n, &status); \
	}                                                                      \
	ucasemap_close(case_map);                                              \
	sql_result_text(context, z1, len, sql_free);                   \
}                                                                              \

ICU_CASE_CONVERT(Lower);
ICU_CASE_CONVERT(Upper);


/*
 * Some functions like COALESCE() and IFNULL() and UNLIKELY() are implemented
 * as VDBE code so that unused argument values do not have to be computed.
 * However, we still need some kind of function implementation for this
 * routines in the function table.  The noopFunc macro provides this.
 * noopFunc will never be called so it doesn't matter what the implementation
 * is.  We might as well use the "version()" function as a substitute.
 */
#define noopFunc sql_func_version /* Substitute function - never called */

/*
 * Implementation of random().  Return a random integer.
 */
static void
randomFunc(sql_context * context, int NotUsed, sql_value ** NotUsed2)
{
	int64_t r;
	UNUSED_PARAMETER2(NotUsed, NotUsed2);
	sql_randomness(sizeof(r), &r);
	sql_result_int(context, r);
}

/*
 * Implementation of randomblob(N).  Return a random blob
 * that is N bytes long.
 */
static void
randomBlob(sql_context * context, int argc, sql_value ** argv)
{
	int64_t n;
	unsigned char *p;
	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	if (mem_is_bin(argv[0]) || mem_is_map(argv[0]) ||
	    mem_is_array(argv[0])) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
			 mem_str(argv[0]), "number");
		context->is_aborted = true;
		return;
	}
	n = mem_get_int_unsafe(argv[0]);
	if (n < 1)
		return;
	p = contextMalloc(context, n);
	if (p) {
		sql_randomness(n, p);
		sql_result_blob(context, (char *)p, n, sql_free);
	}
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
likeFunc(sql_context *context, int argc, sql_value **argv)
{
	u32 escape = SQL_END_OF_STRING;
	int nPat;
	if (argc != 2 && argc != 3) {
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT,
			 "LIKE", "2 or 3", argc);
		context->is_aborted = true;
		return;
	}
	sql *db = sql_context_db_handle(context);
	int rhs_type = sql_value_type(argv[0]);
	int lhs_type = sql_value_type(argv[1]);

	if (lhs_type != MP_STR || rhs_type != MP_STR) {
		if (lhs_type == MP_NIL || rhs_type == MP_NIL)
			return;
		const char *str = rhs_type != MP_STR ?
				  mem_str(argv[0]) : mem_str(argv[1]);
		diag_set(ClientError, ER_INCONSISTENT_TYPES, "string", str);
		context->is_aborted = true;
		return;
	}
	const char *zB = mem_as_str0(argv[0]);
	const char *zA = mem_as_str0(argv[1]);
	const char *zB_end = zB + mem_len_unsafe(argv[0]);
	const char *zA_end = zA + mem_len_unsafe(argv[1]);

	/*
	 * Limit the length of the LIKE pattern to avoid problems
	 * of deep recursion and N*N behavior in
	 * sql_utf8_pattern_compare().
	 */
	nPat = mem_len_unsafe(argv[0]);
	testcase(nPat == db->aLimit[SQL_LIMIT_LIKE_PATTERN_LENGTH]);
	testcase(nPat == db->aLimit[SQL_LIMIT_LIKE_PATTERN_LENGTH] + 1);
	if (nPat > db->aLimit[SQL_LIMIT_LIKE_PATTERN_LENGTH]) {
		diag_set(ClientError, ER_SQL_EXECUTE, "LIKE pattern is too "\
			 "complex");
		context->is_aborted = true;
		return;
	}
	/* Encoding did not change */
	assert(zB == mem_as_str0(argv[0]));

	if (argc == 3) {
		/*
		 * The escape character string must consist of a
		 * single UTF-8 character. Otherwise, return an
		 * error.
		 */
		const unsigned char *zEsc = mem_as_ustr(argv[2]);
		if (zEsc == 0)
			return;
		if (sql_utf8_char_count(zEsc, mem_len_unsafe(argv[2])) != 1) {
			diag_set(ClientError, ER_SQL_EXECUTE, "ESCAPE "\
				 "expression must be a single character");
			context->is_aborted = true;
			return;
		}
		escape = sqlUtf8Read(&zEsc);
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
	sql_result_bool(context, res == MATCH);
}

/*
 * Implementation of the NULLIF(x,y) function.  The result is the first
 * argument if the arguments are different.  The result is NULL if the
 * arguments are equal to each other.
 */
static void
nullifFunc(sql_context * context, int NotUsed, sql_value ** argv)
{
	struct coll *pColl = context->coll;
	UNUSED_PARAMETER(NotUsed);
	if (mem_cmp_scalar(argv[0], argv[1], pColl) != 0)
		sql_result_value(context, argv[0]);
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
sql_func_version(struct sql_context *context,
		 MAYBE_UNUSED int unused1,
		 MAYBE_UNUSED sql_value **unused2)
{
	sql_result_text(context, tarantool_version(), -1, SQL_STATIC);
}

/* Array for converting from half-bytes (nybbles) into ASCII hex
 * digits.
 */
static const char hexdigits[] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

/*
 * Implementation of the QUOTE() function.  This function takes a single
 * argument.  If the argument is numeric, the return value is the same as
 * the argument.  If the argument is NULL, the return value is the string
 * "NULL".  Otherwise, the argument is enclosed in single quotes with
 * single-quote escapes.
 */
static void
quoteFunc(sql_context * context, int argc, sql_value ** argv)
{
	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	switch (argv[0]->type) {
	case MEM_TYPE_UUID: {
		char buf[UUID_STR_LEN + 1];
		tt_uuid_to_string(&argv[0]->u.uuid, &buf[0]);
		sql_result_text(context, buf, UUID_STR_LEN, SQL_TRANSIENT);
		break;
	}
	case MEM_TYPE_DOUBLE:
	case MEM_TYPE_DEC:
	case MEM_TYPE_UINT:
	case MEM_TYPE_INT: {
			sql_result_value(context, argv[0]);
			break;
		}
	case MEM_TYPE_BIN:
	case MEM_TYPE_ARRAY:
	case MEM_TYPE_MAP: {
			char *zText = 0;
			char const *zBlob = mem_as_bin(argv[0]);
			int nBlob = mem_len_unsafe(argv[0]);
			assert(zBlob == mem_as_bin(argv[0]));	/* No encoding change */
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
			const unsigned char *zArg = mem_as_ustr(argv[0]);
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
				SQL_TOKEN_BOOLEAN(mem_get_bool_unsafe(argv[0])),
				-1, SQL_TRANSIENT);
		break;
	}
	default:{
			assert(mem_is_null(argv[0]));
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
unicodeFunc(sql_context * context, int argc, sql_value ** argv)
{
	const unsigned char *z = mem_as_ustr(argv[0]);
	(void)argc;
	if (z && z[0])
		sql_result_uint(context, sqlUtf8Read(&z));
}

/*
 * The char() function takes zero or more arguments, each of which is
 * an integer.  It constructs a string where each character of the string
 * is the unicode character for the corresponding integer argument.
 */
static void
charFunc(sql_context * context, int argc, sql_value ** argv)
{
	unsigned char *z, *zOut;
	int i;
	zOut = z = sql_malloc64(argc * 4 + 1);
	if (z == NULL) {
		context->is_aborted = true;
		return;
	}
	for (i = 0; i < argc; i++) {
		uint64_t x;
		unsigned c;
		if (sql_value_type(argv[i]) == MP_INT)
			x = 0xfffd;
		else
			x = mem_get_uint_unsafe(argv[i]);
		if (x > 0x10ffff)
			x = 0xfffd;
		c = (unsigned)(x & 0x1fffff);
		if (c < 0x00080) {
			*zOut++ = (u8) (c & 0xFF);
		} else if (c < 0x00800) {
			*zOut++ = 0xC0 + (u8) ((c >> 6) & 0x1F);
			*zOut++ = 0x80 + (u8) (c & 0x3F);
		} else if (c < 0x10000) {
			*zOut++ = 0xE0 + (u8) ((c >> 12) & 0x0F);
			*zOut++ = 0x80 + (u8) ((c >> 6) & 0x3F);
			*zOut++ = 0x80 + (u8) (c & 0x3F);
		} else {
			*zOut++ = 0xF0 + (u8) ((c >> 18) & 0x07);
			*zOut++ = 0x80 + (u8) ((c >> 12) & 0x3F);
			*zOut++ = 0x80 + (u8) ((c >> 6) & 0x3F);
			*zOut++ = 0x80 + (u8) (c & 0x3F);
		}
	}
	sql_result_text64(context, (char *)z, zOut - z, sql_free);
}

/*
 * The hex() function.  Interpret the argument as a blob.  Return
 * a hexadecimal rendering as text.
 */
static void
hexFunc(sql_context * context, int argc, sql_value ** argv)
{
	int i, n;
	const unsigned char *pBlob;
	char *zHex, *z;
	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	pBlob = mem_as_bin(argv[0]);
	n = mem_len_unsafe(argv[0]);
	assert(pBlob == mem_as_bin(argv[0]));	/* No encoding change */
	z = zHex = contextMalloc(context, ((i64) n) * 2 + 1);
	if (zHex) {
		for (i = 0; i < n; i++, pBlob++) {
			unsigned char c = *pBlob;
			*(z++) = hexdigits[(c >> 4) & 0xf];
			*(z++) = hexdigits[c & 0xf];
		}
		*z = 0;
		sql_result_text(context, zHex, n * 2, sql_free);
	}
}

/*
 * The zeroblob(N) function returns a zero-filled blob of size N bytes.
 */
static void
zeroblobFunc(sql_context * context, int argc, sql_value ** argv)
{
	int64_t n;
	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	n = mem_get_int_unsafe(argv[0]);
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
replaceFunc(sql_context * context, int argc, sql_value ** argv)
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
	zStr = mem_as_ustr(argv[0]);
	if (zStr == 0)
		return;
	nStr = mem_len_unsafe(argv[0]);
	assert(zStr == mem_as_ustr(argv[0]));	/* No encoding change */
	zPattern = mem_as_ustr(argv[1]);
	if (zPattern == 0) {
		assert(mem_is_null(argv[1])
		       || sql_context_db_handle(context)->mallocFailed);
		return;
	}
	nPattern = mem_len_unsafe(argv[1]);
	if (nPattern == 0) {
		assert(!mem_is_null(argv[1]));
		sql_result_value(context, argv[0]);
		return;
	}
	assert(zPattern == mem_as_ustr(argv[1]));	/* No encoding change */
	zRep = mem_as_ustr(argv[2]);
	if (zRep == 0)
		return;
	nRep = mem_len_unsafe(argv[2]);
	assert(zRep == mem_as_ustr(argv[2]));
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

/**
 * Remove characters included in @a trim_set from @a input_str
 * until encounter a character that doesn't belong to @a trim_set.
 * Remove from the side specified by @a flags.
 * @param context SQL context.
 * @param flags Trim specification: left, right or both.
 * @param trim_set The set of characters for trimming.
 * @param char_len Lengths of each UTF-8 character in @a trim_set.
 * @param char_cnt A number of UTF-8 characters in @a trim_set.
 * @param input_str Input string for trimming.
 * @param input_str_sz Input string size in bytes.
 */
static void
trim_procedure(struct sql_context *context, enum trim_side_mask flags,
	       const unsigned char *trim_set, const uint8_t *char_len,
	       int char_cnt, const unsigned char *input_str, int input_str_sz)
{
	if (char_cnt == 0)
		goto finish;
	int i, len;
	const unsigned char *z;
	if ((flags & TRIM_LEADING) != 0) {
		while (input_str_sz > 0) {
			z = trim_set;
			for (i = 0; i < char_cnt; ++i, z += len) {
				len = char_len[i];
				if (len <= input_str_sz
				    && memcmp(input_str, z, len) == 0)
					break;
			}
			if (i >= char_cnt)
				break;
			input_str += len;
			input_str_sz -= len;
		}
	}
	if ((flags & TRIM_TRAILING) != 0) {
		while (input_str_sz > 0) {
			z = trim_set;
			for (i = 0; i < char_cnt; ++i, z += len) {
				len = char_len[i];
				if (len <= input_str_sz
				    && memcmp(&input_str[input_str_sz - len],
					      z, len) == 0)
					break;
			}
			if (i >= char_cnt)
				break;
			input_str_sz -= len;
		}
	}
finish:
	if (context->func->def->returns == FIELD_TYPE_STRING)
		mem_copy_str(context->pOut, (char *)input_str, input_str_sz);
	else
		mem_copy_bin(context->pOut, (char *)input_str, input_str_sz);
}

/**
 * Prepare arguments for trimming procedure. Allocate memory for
 * @a char_len (array of lengths each character in @a trim_set)
 * and fill it.
 *
 * @param context SQL context.
 * @param trim_set The set of characters for trimming.
 * @param[out] char_len Lengths of each character in @ trim_set.
 * @retval >=0 A number of UTF-8 characters in @a trim_set.
 * @retval -1 Memory allocation error.
 */
static int
trim_prepare_char_len(struct sql_context *context,
		      const unsigned char *trim_set, int trim_set_sz,
		      uint8_t **char_len)
{
	/*
	 * Count the number of UTF-8 characters passing through
	 * the entire char set, but not up to the '\0' or X'00'
	 * character. This allows to handle trimming set
	 * containing such characters.
	 */
	int char_cnt = sql_utf8_char_count(trim_set, trim_set_sz);
	if (char_cnt == 0) {
		*char_len = NULL;
		return 0;
	}

	if ((*char_len = (uint8_t *)contextMalloc(context, char_cnt)) == NULL)
		return -1;

	int i = 0, j = 0;
	while(j < char_cnt) {
		int old_i = i;
		SQL_UTF8_FWD_1(trim_set, i, trim_set_sz);
		(*char_len)[j++] = i - old_i;
	}

	return char_cnt;
}

/**
 * Normalize args from @a argv input array when it has two args.
 *
 * Case: TRIM(<str>)
 * Call trimming procedure with TRIM_BOTH as the flags and " " as
 * the trimming set.
 *
 * Case: TRIM(LEADING/TRAILING/BOTH FROM <str>)
 * If user has specified side keyword only, then call trimming
 * procedure with the specified side and " " as the trimming set.
 */
static void
trim_func_two_args(struct sql_context *context, sql_value *arg1,
		   sql_value *arg2)
{
	const unsigned char *trim_set;
	if (mem_is_bin(arg1))
		trim_set = (const unsigned char *)"\0";
	else
		trim_set = (const unsigned char *)" ";
	const unsigned char *input_str;
	if ((input_str = mem_as_ustr(arg1)) == NULL)
		return;

	int input_str_sz = mem_len_unsafe(arg1);
	assert(arg2->type == MEM_TYPE_UINT);
	uint8_t len_one = 1;
	trim_procedure(context, arg2->u.u, trim_set,
		       &len_one, 1, input_str, input_str_sz);
}

/**
 * Normalize args from @a argv input array when it has three args.
 *
 * Case: TRIM(<character_set> FROM <str>)
 * If user has specified <character_set> only, call trimming procedure with
 * TRIM_BOTH as the flags and that trimming set.
 *
 * Case: TRIM(LEADING/TRAILING/BOTH <character_set> FROM <str>)
 * If user has specified side keyword and <character_set>, then
 * call trimming procedure with that args.
 */
static void
trim_func_three_args(struct sql_context *context, sql_value *arg1,
		     sql_value *arg2, sql_value *arg3)
{
	assert(arg2->type == MEM_TYPE_UINT);
	const unsigned char *input_str, *trim_set;
	if ((input_str = mem_as_ustr(arg1)) == NULL ||
	    (trim_set = mem_as_ustr(arg3)) == NULL)
		return;

	int trim_set_sz = mem_len_unsafe(arg3);
	int input_str_sz = mem_len_unsafe(arg1);
	uint8_t *char_len;
	int char_cnt = trim_prepare_char_len(context, trim_set, trim_set_sz,
					     &char_len);
	if (char_cnt == -1)
		return;
	trim_procedure(context, arg2->u.u, trim_set, char_len,
		       char_cnt, input_str, input_str_sz);
	sql_free(char_len);
}

/**
 * Normalize args from @a argv input array when it has one,
 * two or three args.
 *
 * This is a dispatcher function that calls corresponding
 * implementation depending on the number of arguments.
*/
static void
trim_func(struct sql_context *context, int argc, sql_value **argv)
{
	switch (argc) {
	case 2:
		trim_func_two_args(context, argv[0], argv[1]);
		break;
	case 3:
		trim_func_three_args(context, argv[0], argv[1], argv[2]);
		break;
	default:
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT, "TRIM",
			"2 or 3", argc);
		context->is_aborted = true;
	}
}

/*
 * Compute the soundex encoding of a word.
 *
 * IMP: R-59782-00072 The soundex(X) function returns a string that is the
 * soundex encoding of the string X.
 */
static void
soundexFunc(sql_context * context, int argc, sql_value ** argv)
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
	if (mem_is_bin(argv[0]) || mem_is_map(argv[0]) ||
	    mem_is_array(argv[0])) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
			 mem_str(argv[0]), "string");
		context->is_aborted = true;
		return;
	}
	zIn = (u8 *) mem_as_ustr(argv[0]);
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

/*
 * Routines to implement min() and max() aggregate functions.
 */
static void
minmaxStep(sql_context * context, int NotUsed, sql_value ** argv)
{
	Mem *pArg = (Mem *) argv[0];
	Mem *pBest;
	UNUSED_PARAMETER(NotUsed);

	struct func_sql_builtin *func =
		(struct func_sql_builtin *)context->func;
	pBest = sql_context_agg_mem(context);
	if (!pBest)
		return;

	if (mem_is_null(argv[0])) {
		if (!mem_is_null(pBest))
			sqlSkipAccumulatorLoad(context);
	} else if (!mem_is_null(pBest)) {
		struct coll *pColl = context->coll;
		/*
		 * This step function is used for both the min()
		 * and max() aggregates, the only difference
		 * between the two being that the sense of the
		 * comparison is inverted.
		 */
		bool is_max = (func->flags & SQL_FUNC_MAX) != 0;
		int cmp = mem_cmp_scalar(pBest, pArg, pColl);
		if ((is_max && cmp < 0) || (!is_max && cmp > 0)) {
			if (mem_copy(pBest, pArg) != 0)
				context->is_aborted = true;
		} else {
			sqlSkipAccumulatorLoad(context);
		}
	} else {
		pBest->db = sql_context_db_handle(context);
		if (mem_copy(pBest, pArg) != 0)
			context->is_aborted = true;
	}
}

static void
minMaxFinalize(sql_context * context)
{
	struct Mem *mem = context->pMem;
	struct Mem *res;
	if (mem_get_agg(mem, (void **)&res) != 0)
		return;
	if (!mem_is_null(res))
		sql_result_value(context, res);
	mem_destroy(res);
}

/*
 * group_concat(EXPR, ?SEPARATOR?)
 */
static void
groupConcatStep(sql_context * context, int argc, sql_value ** argv)
{
	const char *zVal;
	StrAccum *pAccum;
	const char *zSep;
	int nVal, nSep;
	if (argc != 1 && argc != 2) {
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT,
			 "GROUP_CONCAT", "1 or 2", argc);
		context->is_aborted = true;
		return;
	}
	if (mem_is_null(argv[0]))
		return;
	pAccum =
	    (StrAccum *) sql_aggregate_context(context, sizeof(*pAccum));

	if (pAccum) {
		sql *db = sql_context_db_handle(context);
		int firstTerm = pAccum->mxAlloc == 0;
		pAccum->mxAlloc = db->aLimit[SQL_LIMIT_LENGTH];
		if (!firstTerm) {
			if (argc == 2) {
				zSep = mem_as_str0(argv[1]);
				nSep = mem_len_unsafe(argv[1]);
			} else {
				zSep = ",";
				nSep = 1;
			}
			if (zSep)
				sqlStrAccumAppend(pAccum, zSep, nSep);
		}
		zVal = mem_as_str0(argv[0]);
		nVal = mem_len_unsafe(argv[0]);
		if (zVal)
			sqlStrAccumAppend(pAccum, zVal, nVal);
	}
}

static void
groupConcatFinalize(sql_context * context)
{
	StrAccum *pAccum;
	pAccum = sql_aggregate_context(context, 0);
	if (pAccum) {
		if (pAccum->accError == STRACCUM_TOOBIG) {
			diag_set(ClientError, ER_SQL_EXECUTE, "string or binary"\
				 "string is too big");
			context->is_aborted = true;
		} else if (pAccum->accError == STRACCUM_NOMEM) {
			context->is_aborted = true;
		} else {
			char *str = sqlStrAccumFinish(pAccum);
			int len = pAccum->nChar;
			assert(len >= 0);
			if (context->func->def->returns == FIELD_TYPE_STRING)
				mem_set_str_dynamic(context->pOut, str, len);
			else
				mem_set_bin_dynamic(context->pOut, str, len);
		}
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
sql_builtin_stub(sql_context *ctx, int argc, sql_value **argv)
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
	{"GREATEST", 2, SQL_MAX_FUNCTION_ARG, SQL_FUNC_MAX | SQL_FUNC_NEEDCOLL,
	 true, 0, NULL},
	{"GROUP_CONCAT", 1, 2, SQL_FUNC_AGG, false, 0, NULL},
	{"HEX", 1, 1, 0, true, 0, NULL},
	{"IFNULL", 2, 2, SQL_FUNC_COALESCE, true, 0, NULL},
	{"LEAST", 2, SQL_MAX_FUNCTION_ARG, SQL_FUNC_MIN | SQL_FUNC_NEEDCOLL,
	 true, 0, NULL},
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
	void (*call)(sql_context *ctx, int argc, sql_value **argv);
	/** Call finalization function for this implementation. */
	void (*finalize)(sql_context *ctx);
};

/**
 * Array of function implementation definitions. All implementations of the same
 * function should be defined in succession.
 */
static struct sql_func_definition definitions[] = {
	{"ABS", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, absFunc, NULL},
	{"ABS", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, absFunc, NULL},
	{"AVG", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, step_avg, fin_avg},
	{"AVG", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, step_avg, fin_avg},
	{"CHAR", -1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_STRING, charFunc, NULL},
	{"CHAR_LENGTH", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_INTEGER, lengthFunc,
	 NULL},
	{"COALESCE", -1, {FIELD_TYPE_ANY}, FIELD_TYPE_SCALAR, sql_builtin_stub,
	 NULL},
	{"COUNT", 0, {}, FIELD_TYPE_INTEGER, step_count, fin_count},
	{"COUNT", 1, {FIELD_TYPE_ANY}, FIELD_TYPE_INTEGER, step_count,
	 fin_count},

	{"GREATEST", -1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, minmaxFunc,
	 NULL},
	{"GREATEST", -1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, minmaxFunc,
	 NULL},
	{"GREATEST", -1, {FIELD_TYPE_NUMBER}, FIELD_TYPE_NUMBER, minmaxFunc,
	 NULL},
	{"GREATEST", -1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_VARBINARY,
	 minmaxFunc, NULL},
	{"GREATEST", -1, {FIELD_TYPE_UUID}, FIELD_TYPE_UUID, minmaxFunc, NULL},
	{"GREATEST", -1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, minmaxFunc,
	 NULL},
	{"GREATEST", -1, {FIELD_TYPE_SCALAR}, FIELD_TYPE_SCALAR, minmaxFunc,
	 NULL},

	{"GROUP_CONCAT", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING,
	 groupConcatStep, groupConcatFinalize},
	{"GROUP_CONCAT", 2, {FIELD_TYPE_STRING, FIELD_TYPE_STRING},
	 FIELD_TYPE_STRING, groupConcatStep, groupConcatFinalize},
	{"GROUP_CONCAT", 1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_VARBINARY,
	 groupConcatStep, groupConcatFinalize},
	{"GROUP_CONCAT", 2, {FIELD_TYPE_VARBINARY, FIELD_TYPE_VARBINARY},
	 FIELD_TYPE_VARBINARY, groupConcatStep, groupConcatFinalize},

	{"HEX", 1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_STRING, hexFunc, NULL},
	{"IFNULL", 2, {FIELD_TYPE_ANY, FIELD_TYPE_ANY}, FIELD_TYPE_SCALAR,
	 sql_builtin_stub, NULL},

	{"LEAST", -1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, minmaxFunc,
	 NULL},
	{"LEAST", -1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, minmaxFunc, NULL},
	{"LEAST", -1, {FIELD_TYPE_NUMBER}, FIELD_TYPE_NUMBER, minmaxFunc, NULL},
	{"LEAST", -1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_VARBINARY, minmaxFunc,
	 NULL},
	{"LEAST", -1, {FIELD_TYPE_UUID}, FIELD_TYPE_UUID, minmaxFunc, NULL},
	{"LEAST", -1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, minmaxFunc, NULL},
	{"LEAST", -1, {FIELD_TYPE_SCALAR}, FIELD_TYPE_SCALAR, minmaxFunc, NULL},

	{"LENGTH", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_INTEGER, lengthFunc,
	 NULL},
	{"LENGTH", 1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_INTEGER, lengthFunc,
	 NULL},
	{"LIKE", 2, {FIELD_TYPE_STRING, FIELD_TYPE_STRING},
	 FIELD_TYPE_BOOLEAN, likeFunc, NULL},
	{"LIKE", 3, {FIELD_TYPE_STRING, FIELD_TYPE_STRING, FIELD_TYPE_STRING},
	 FIELD_TYPE_BOOLEAN, likeFunc, NULL},
	{"LIKELIHOOD", 2, {FIELD_TYPE_ANY, FIELD_TYPE_DOUBLE},
	 FIELD_TYPE_BOOLEAN, sql_builtin_stub, NULL},
	{"LIKELY", 1, {FIELD_TYPE_ANY}, FIELD_TYPE_BOOLEAN, sql_builtin_stub,
	 NULL},
	{"LOWER", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, LowerICUFunc,
	 NULL},

	{"MAX", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, minmaxStep,
	 minMaxFinalize},
	{"MAX", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, minmaxStep,
	 minMaxFinalize},
	{"MAX", 1, {FIELD_TYPE_NUMBER}, FIELD_TYPE_NUMBER, minmaxStep,
	 minMaxFinalize},
	{"MAX", 1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_VARBINARY, minmaxStep,
	 minMaxFinalize},
	{"MAX", 1, {FIELD_TYPE_UUID}, FIELD_TYPE_UUID, minmaxStep,
	 minMaxFinalize},
	{"MAX", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, minmaxStep,
	 minMaxFinalize},
	{"MAX", 1, {FIELD_TYPE_SCALAR}, FIELD_TYPE_SCALAR, minmaxStep,
	 minMaxFinalize},

	{"MIN", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, minmaxStep,
	 minMaxFinalize},
	{"MIN", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, minmaxStep,
	 minMaxFinalize},
	{"MIN", 1, {FIELD_TYPE_NUMBER}, FIELD_TYPE_NUMBER, minmaxStep,
	 minMaxFinalize},
	{"MIN", 1, {FIELD_TYPE_VARBINARY}, FIELD_TYPE_VARBINARY, minmaxStep,
	 minMaxFinalize},
	{"MIN", 1, {FIELD_TYPE_UUID}, FIELD_TYPE_UUID, minmaxStep,
	 minMaxFinalize},
	{"MIN", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, minmaxStep,
	 minMaxFinalize},
	{"MIN", 1, {FIELD_TYPE_SCALAR}, FIELD_TYPE_SCALAR, minmaxStep,
	 minMaxFinalize},

	{"NULLIF", 2, {FIELD_TYPE_ANY, FIELD_TYPE_ANY}, FIELD_TYPE_SCALAR,
	 nullifFunc, NULL},
	{"POSITION", 2, {FIELD_TYPE_STRING, FIELD_TYPE_STRING},
	 FIELD_TYPE_INTEGER, position_func, NULL},
	{"PRINTF", -1, {FIELD_TYPE_ANY}, FIELD_TYPE_STRING, printfFunc, 
	 NULL},
	{"QUOTE", 1, {FIELD_TYPE_ANY}, FIELD_TYPE_STRING, quoteFunc, NULL},
	{"RANDOM", 0, {}, FIELD_TYPE_INTEGER, randomFunc, NULL},
	{"RANDOMBLOB", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_VARBINARY,
	 randomBlob, NULL},
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
	 FIELD_TYPE_STRING, substrFunc, NULL},
	{"SUBSTR", 3,
	 {FIELD_TYPE_STRING, FIELD_TYPE_INTEGER, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_STRING, substrFunc, NULL},
	{"SUBSTR", 2, {FIELD_TYPE_VARBINARY, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_VARBINARY, substrFunc, NULL},
	{"SUBSTR", 3,
	 {FIELD_TYPE_VARBINARY, FIELD_TYPE_INTEGER, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_VARBINARY, substrFunc, NULL},
	{"SUM", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_INTEGER, step_sum, fin_sum},
	{"SUM", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, step_sum, fin_sum},
	{"TOTAL", 1, {FIELD_TYPE_INTEGER}, FIELD_TYPE_DOUBLE, step_total,
	 fin_total},
	{"TOTAL", 1, {FIELD_TYPE_DOUBLE}, FIELD_TYPE_DOUBLE, step_total,
	 fin_total},

	{"TRIM", 2, {FIELD_TYPE_STRING, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_STRING, trim_func, NULL},
	{"TRIM", 3, {FIELD_TYPE_STRING, FIELD_TYPE_INTEGER, FIELD_TYPE_STRING},
	 FIELD_TYPE_STRING, trim_func, NULL},
	{"TRIM", 2, {FIELD_TYPE_VARBINARY, FIELD_TYPE_INTEGER},
	 FIELD_TYPE_VARBINARY, trim_func, NULL},
	{"TRIM", 3,
	 {FIELD_TYPE_VARBINARY, FIELD_TYPE_INTEGER, FIELD_TYPE_VARBINARY},
	 FIELD_TYPE_VARBINARY, trim_func, NULL},

	{"TYPEOF", 1, {FIELD_TYPE_ANY}, FIELD_TYPE_STRING, typeofFunc, NULL},
	{"UNICODE", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_INTEGER, unicodeFunc,
	 NULL},
	{"UNLIKELY", 1, {FIELD_TYPE_ANY}, FIELD_TYPE_BOOLEAN, sql_builtin_stub,
	 NULL},
	{"UPPER", 1, {FIELD_TYPE_STRING}, FIELD_TYPE_STRING, UpperICUFunc,
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
		def->aggregate = desc->finalize == NULL ?
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
