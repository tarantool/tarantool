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

static const unsigned char *
mem_as_ustr(struct Mem *mem)
{
	return (const unsigned char *)mem_as_str0(mem);
}

static const void *
mem_as_bin(struct Mem *mem)
{
	const char *s;
	if (!mem_is_bytes(mem) && mem_to_str(mem) != 0)
		return NULL;
	if (mem_get_bin(mem, &s) != 0)
		return NULL;
	return s;
}

/*
 * Return the collating function associated with a function.
 */
static struct coll *
sqlGetFuncCollSeq(sql_context * context)
{
	VdbeOp *pOp;
	assert(context->pVdbe != 0);
	pOp = &context->pVdbe->aOp[context->iOp - 1];
	assert(pOp->opcode == OP_CollSeq);
	assert(pOp->p4type == P4_COLLSEQ || pOp->p4.pColl == NULL);
	return pOp->p4.pColl;
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
	pColl = sqlGetFuncCollSeq(context);
	assert(mask == -1 || mask == 0);
	iBest = 0;
	if (mem_is_null(argv[0]))
		return;
	for (i = 1; i < argc; i++) {
		if (mem_is_null(argv[i]))
			return;
		if ((sqlMemCompare(argv[iBest], argv[i], pColl) ^ mask) >=
		    0) {
			testcase(mask == 0);
			iBest = i;
		}
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
	enum field_type f_t = argv[0]->field_type;
	/*
	 * SCALAR is not a basic type, but rather an aggregation of
	 * types. Thus, ignore SCALAR field type and return msgpack
	 * format type.
	 */
	if (f_t != field_type_MAX && f_t != FIELD_TYPE_SCALAR) {
		sql_result_text(context, field_type_strs[argv[0]->field_type],
				-1, SQL_STATIC);
		return;
	}
	switch (sql_value_type(argv[0])) {
	case MP_INT:
	case MP_UINT:
		z = "integer";
		break;
	case MP_STR:
		z = "string";
		break;
	case MP_DOUBLE:
		z = "double";
		break;
	case MP_BIN:
	case MP_ARRAY:
	case MP_MAP:
		z = "varbinary";
		break;
	case MP_BOOL:
	case MP_NIL:
		z = "boolean";
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
			struct coll *coll = sqlGetFuncCollSeq(context);
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
		sql_result_text64(context, (char *)z, z2 - z,
				      SQL_TRANSIENT);
	} else {
		if (p1 + p2 > len) {
			p2 = len - p1;
			if (p2 < 0)
				p2 = 0;
		}
		sql_result_blob64(context, (char *)&z[p1], (u64) p2,
				      SQL_TRANSIENT);
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
	struct coll *coll = sqlGetFuncCollSeq(context);                    \
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
	struct coll *coll = sqlGetFuncCollSeq(context);
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
	struct coll *pColl = sqlGetFuncCollSeq(context);
	UNUSED_PARAMETER(NotUsed);
	if (sqlMemCompare(argv[0], argv[1], pColl) != 0) {
		sql_result_value(context, argv[0]);
	}
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
	switch (sql_value_type(argv[0])) {
	case MP_DOUBLE:{
			double r1, r2;
			char zBuf[50];
			r1 = mem_get_double_unsafe(argv[0]);
			sql_snprintf(sizeof(zBuf), zBuf, "%!.15g", r1);
			sqlAtoF(zBuf, &r2, 20);
			if (r1 != r2) {
				sql_snprintf(sizeof(zBuf), zBuf, "%!.20e",
						 r1);
			}
			sql_result_text(context, zBuf, -1,
					    SQL_TRANSIENT);
			break;
		}
	case MP_UINT:
	case MP_INT:{
			sql_result_value(context, argv[0]);
			break;
		}
	case MP_BIN:
	case MP_ARRAY:
	case MP_MAP: {
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
	case MP_STR:{
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
	case MP_BOOL: {
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
	if (sql_result_zeroblob64(context, n) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "string or binary string"\
			 "is too big");
		context->is_aborted = true;
	}
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
	sql_result_text(context, (char *)zOut, j, sql_free);
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
	sql_result_text(context, (char *)input_str, input_str_sz,
			SQL_TRANSIENT);
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
 * Normalize args from @a argv input array when it has one arg
 * only.
 *
 * Case: TRIM(<str>)
 * Call trimming procedure with TRIM_BOTH as the flags and " " as
 * the trimming set.
 */
static void
trim_func_one_arg(struct sql_context *context, sql_value *arg)
{
	/* In case of VARBINARY type default trim octet is X'00'. */
	const unsigned char *default_trim;
	if (mem_is_null(arg))
		return;
	if (mem_is_bin(arg))
		default_trim = (const unsigned char *) "\0";
	else
		default_trim = (const unsigned char *) " ";
	int input_str_sz = mem_len_unsafe(arg);
	const unsigned char *input_str = mem_as_ustr(arg);
	uint8_t trim_char_len[1] = { 1 };
	trim_procedure(context, TRIM_BOTH, default_trim, trim_char_len, 1,
		       input_str, input_str_sz);
}

/**
 * Normalize args from @a argv input array when it has two args.
 *
 * Case: TRIM(<character_set> FROM <str>)
 * If user has specified <character_set> only, call trimming
 * procedure with TRIM_BOTH as the flags and that trimming set.
 *
 * Case: TRIM(LEADING/TRAILING/BOTH FROM <str>)
 * If user has specified side keyword only, then call trimming
 * procedure with the specified side and " " as the trimming set.
 */
static void
trim_func_two_args(struct sql_context *context, sql_value *arg1,
		   sql_value *arg2)
{
	const unsigned char *input_str, *trim_set;
	if ((input_str = mem_as_ustr(arg2)) == NULL)
		return;

	int input_str_sz = mem_len_unsafe(arg2);
	if (sql_value_type(arg1) == MP_INT || sql_value_type(arg1) == MP_UINT) {
		uint8_t len_one = 1;
		trim_procedure(context, mem_get_int_unsafe(arg1),
			       (const unsigned char *) " ", &len_one, 1,
			       input_str, input_str_sz);
	} else if ((trim_set = mem_as_ustr(arg1)) != NULL) {
		int trim_set_sz = mem_len_unsafe(arg1);
		uint8_t *char_len;
		int char_cnt = trim_prepare_char_len(context, trim_set,
						     trim_set_sz, &char_len);
		if (char_cnt == -1)
			return;
		trim_procedure(context, TRIM_BOTH, trim_set, char_len, char_cnt,
			       input_str, input_str_sz);
		sql_free(char_len);
	}
}

/**
 * Normalize args from @a argv input array when it has three args.
 *
 * Case: TRIM(LEADING/TRAILING/BOTH <character_set> FROM <str>)
 * If user has specified side keyword and <character_set>, then
 * call trimming procedure with that args.
 */
static void
trim_func_three_args(struct sql_context *context, sql_value *arg1,
		     sql_value *arg2, sql_value *arg3)
{
	assert(sql_value_type(arg1) == MP_INT || sql_value_type(arg1) == MP_UINT);
	const unsigned char *input_str, *trim_set;
	if ((input_str = mem_as_ustr(arg3)) == NULL ||
	    (trim_set = mem_as_ustr(arg2)) == NULL)
		return;

	int trim_set_sz = mem_len_unsafe(arg2);
	int input_str_sz = mem_len_unsafe(arg3);
	uint8_t *char_len;
	int char_cnt = trim_prepare_char_len(context, trim_set, trim_set_sz,
					     &char_len);
	if (char_cnt == -1)
		return;
	trim_procedure(context, mem_get_int_unsafe(arg1), trim_set, char_len,
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
	case 1:
		trim_func_one_arg(context, argv[0]);
		break;
	case 2:
		trim_func_two_args(context, argv[0], argv[1]);
		break;
	case 3:
		trim_func_three_args(context, argv[0], argv[1], argv[2]);
		break;
	default:
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT, "TRIM",
			 "1 or 2 or 3", argc);
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
 * An instance of the following structure holds the context of a
 * sum() or avg() aggregate computation.
 */
typedef struct SumCtx SumCtx;
struct SumCtx {
	double rSum;		/* Floating point sum */
	int64_t iSum;		/* Integer sum */
	/** True if iSum < 0. */
	bool is_neg;
	i64 cnt;		/* Number of elements summed */
	u8 overflow;		/* True if integer overflow seen */
	u8 approx;		/* True if non-integer value was input to the sum */
};

/*
 * Routines used to compute the sum, average, and total.
 *
 * The SUM() function follows the (broken) SQL standard which means
 * that it returns NULL if it sums over no inputs.  TOTAL returns
 * 0.0 in that case.  In addition, TOTAL always returns a float where
 * SUM might return an integer if it never encounters a floating point
 * value.  TOTAL never fails, but SUM might through an exception if
 * it overflows an integer.
 */
static void
sum_step(struct sql_context *context, int argc, sql_value **argv)
{
	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	struct SumCtx *p = sql_aggregate_context(context, sizeof(*p));
	int type = sql_value_type(argv[0]);
	if (type == MP_NIL || p == NULL)
		return;
	if (type != MP_DOUBLE && type != MP_INT && type != MP_UINT) {
		if (type != MP_STR || mem_to_number(argv[0]) != 0) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
				 mem_str(argv[0]), "number");
			context->is_aborted = true;
			return;
		}
		type = sql_value_type(argv[0]);
	}
	p->cnt++;
	if (type == MP_INT || type == MP_UINT) {
		int64_t v = mem_get_int_unsafe(argv[0]);
		if (type == MP_INT)
			p->rSum += v;
		else
			p->rSum += (uint64_t) v;
		if ((p->approx | p->overflow) == 0 &&
		    sql_add_int(p->iSum, p->is_neg, v, type == MP_INT, &p->iSum,
				&p->is_neg) != 0) {
			p->overflow = 1;
		}
	} else {
		p->rSum += mem_get_double_unsafe(argv[0]);
		p->approx = 1;
	}
}

static void
sumFinalize(sql_context * context)
{
	SumCtx *p;
	p = sql_aggregate_context(context, 0);
	if (p && p->cnt > 0) {
		if (p->overflow) {
			diag_set(ClientError, ER_SQL_EXECUTE, "integer "\
				 "overflow");
			context->is_aborted = true;
		} else if (p->approx) {
			sql_result_double(context, p->rSum);
		} else {
			mem_set_int(context->pOut, p->iSum, p->is_neg);
		}
	}
}

static void
avgFinalize(sql_context * context)
{
	SumCtx *p;
	p = sql_aggregate_context(context, 0);
	if (p && p->cnt > 0) {
		sql_result_double(context, p->rSum / (double)p->cnt);
	}
}

static void
totalFinalize(sql_context * context)
{
	SumCtx *p;
	p = sql_aggregate_context(context, 0);
	sql_result_double(context, p ? p->rSum : (double)0);
}

/*
 * The following structure keeps track of state information for the
 * count() aggregate function.
 */
typedef struct CountCtx CountCtx;
struct CountCtx {
	i64 n;
};

/*
 * Routines to implement the count() aggregate function.
 */
static void
countStep(sql_context * context, int argc, sql_value ** argv)
{
	CountCtx *p;
	if (argc != 0 && argc != 1) {
		diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT,
			 "COUNT", "0 or 1", argc);
		context->is_aborted = true;
		return;
	}
	p = sql_aggregate_context(context, sizeof(*p));
	if ((argc == 0 || !mem_is_null(argv[0])) && p != NULL)
		p->n++;
}

static void
countFinalize(sql_context * context)
{
	CountCtx *p;
	p = sql_aggregate_context(context, 0);
	sql_result_uint(context, p ? p->n : 0);
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
		int cmp;
		struct coll *pColl = sqlGetFuncCollSeq(context);
		/*
		 * This step function is used for both the min()
		 * and max() aggregates, the only difference
		 * between the two being that the sense of the
		 * comparison is inverted.
		 */
		bool is_max = (func->flags & SQL_FUNC_MAX) != 0;
		cmp = sqlMemCompare(pBest, pArg, pColl);
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
			sql_result_text(context,
					    sqlStrAccumFinish(pAccum),
					    pAccum->nChar, sql_free);
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
	struct func *func = sql_func_by_signature(expr->u.zToken, 2);
	if (func == NULL || !sql_func_flag_is_set(func, SQL_FUNC_LIKE))
		return 0;
	return 1;
}

struct func *
sql_func_by_signature(const char *name, int argc)
{
	struct func *base = func_by_name(name, strlen(name));
	if (base == NULL || !base->def->exports.sql)
		return NULL;

	if (base->def->param_count != -1 && base->def->param_count != argc)
		return NULL;
	return base;
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
 * A sequence of SQL builtins definitions in
 * lexicographic order.
 */
static struct {
	/**
	 * Name is used to find corresponding entry in array
	 * sql_builtins applying binary search.
	 */
	const char *name;
	/** Members below are related to struct func_sql_builtin. */
	uint16_t flags;
	void (*call)(sql_context *ctx, int argc, sql_value **argv);
	void (*finalize)(sql_context *ctx);
	/** Members below are related to struct func_def. */
	bool is_deterministic;
	int param_count;
	enum field_type returns;
	enum func_aggregate aggregate;
	bool export_to_sql;
} sql_builtins[] = {
	{.name = "ABS",
	 .param_count = 1,
	 .returns = FIELD_TYPE_NUMBER,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = absFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "AVG",
	 .param_count = 1,
	 .returns = FIELD_TYPE_NUMBER,
	 .is_deterministic = false,
	 .aggregate = FUNC_AGGREGATE_GROUP,
	 .flags = 0,
	 .call = sum_step,
	 .finalize = avgFinalize,
	 .export_to_sql = true,
	}, {
	 .name = "CEIL",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "CEILING",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "CHAR",
	 .param_count = -1,
	 .returns = FIELD_TYPE_STRING,
	 .is_deterministic = true,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .flags = 0,
	 .call = charFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	 }, {
	 .name = "CHARACTER_LENGTH",
	 .param_count = 1,
	 .returns = FIELD_TYPE_INTEGER,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = lengthFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "CHAR_LENGTH",
	 .param_count = 1,
	 .returns = FIELD_TYPE_INTEGER,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = lengthFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "COALESCE",
	 .param_count = -1,
	 .returns = FIELD_TYPE_SCALAR,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_COALESCE,
	 .call = sql_builtin_stub,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "COUNT",
	 .param_count = -1,
	 .returns = FIELD_TYPE_INTEGER,
	 .aggregate = FUNC_AGGREGATE_GROUP,
	 .is_deterministic = false,
	 .flags = 0,
	 .call = countStep,
	 .finalize = countFinalize,
	 .export_to_sql = true,
	}, {
	 .name = "CURRENT_DATE",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "CURRENT_TIME",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "CURRENT_TIMESTAMP",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "DATE",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "DATETIME",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "EVERY",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "EXISTS",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "EXP",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "EXTRACT",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "FLOOR",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "GREATER",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "GREATEST",
	 .param_count = -1,
	 .returns = FIELD_TYPE_SCALAR,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_NEEDCOLL | SQL_FUNC_MAX,
	 .call = minmaxFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "GROUP_CONCAT",
	 .param_count = -1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_GROUP,
	 .is_deterministic = false,
	 .flags = 0,
	 .call = groupConcatStep,
	 .finalize = groupConcatFinalize,
	 .export_to_sql = true,
	}, {
	 .name = "HEX",
	 .param_count = 1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = hexFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "IFNULL",
	 .param_count = 2,
	 .returns = FIELD_TYPE_SCALAR,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_COALESCE,
	 .call = sql_builtin_stub,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "JULIANDAY",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "LEAST",
	 .param_count = -1,
	 .returns = FIELD_TYPE_SCALAR,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_NEEDCOLL | SQL_FUNC_MIN,
	 .call = minmaxFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "LENGTH",
	 .param_count = 1,
	 .returns = FIELD_TYPE_INTEGER,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_LENGTH,
	 .call = lengthFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "LESSER",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "LIKE",
	 .param_count = -1,
	 .returns = FIELD_TYPE_INTEGER,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_NEEDCOLL | SQL_FUNC_LIKE,
	 .call = likeFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "LIKELIHOOD",
	 .param_count = 2,
	 .returns = FIELD_TYPE_BOOLEAN,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_UNLIKELY,
	 .call = sql_builtin_stub,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "LIKELY",
	 .param_count = 1,
	 .returns = FIELD_TYPE_BOOLEAN,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_UNLIKELY,
	 .call = sql_builtin_stub,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "LN",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "LOWER",
	 .param_count = 1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_DERIVEDCOLL | SQL_FUNC_NEEDCOLL,
	 .call = LowerICUFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "MAX",
	 .param_count = 1,
	 .returns = FIELD_TYPE_SCALAR,
	 .aggregate = FUNC_AGGREGATE_GROUP,
	 .is_deterministic = false,
	 .flags = SQL_FUNC_NEEDCOLL | SQL_FUNC_MAX,
	 .call = minmaxStep,
	 .finalize = minMaxFinalize,
	 .export_to_sql = true,
	}, {
	 .name = "MIN",
	 .param_count = 1,
	 .returns = FIELD_TYPE_SCALAR,
	 .aggregate = FUNC_AGGREGATE_GROUP,
	 .is_deterministic = false,
	 .flags = SQL_FUNC_NEEDCOLL | SQL_FUNC_MIN,
	 .call = minmaxStep,
	 .finalize = minMaxFinalize,
	 .export_to_sql = true,
	}, {
	 .name = "MOD",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "NULLIF",
	 .param_count = 2,
	 .returns = FIELD_TYPE_SCALAR,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_NEEDCOLL,
	 .call = nullifFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "OCTET_LENGTH",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "POSITION",
	 .param_count = 2,
	 .returns = FIELD_TYPE_INTEGER,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_NEEDCOLL,
	 .call = position_func,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "POWER",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "PRINTF",
	 .param_count = -1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = printfFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "QUOTE",
	 .param_count = 1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = quoteFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "RANDOM",
	 .param_count = 0,
	 .returns = FIELD_TYPE_INTEGER,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .call = randomFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "RANDOMBLOB",
	 .param_count = 1,
	 .returns = FIELD_TYPE_VARBINARY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .call = randomBlob,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "REPLACE",
	 .param_count = 3,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_DERIVEDCOLL,
	 .call = replaceFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "ROUND",
	 .param_count = -1,
	 .returns = FIELD_TYPE_INTEGER,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = roundFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "ROW_COUNT",
	 .param_count = 0,
	 .returns = FIELD_TYPE_INTEGER,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = sql_row_count,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "SOME",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "SOUNDEX",
	 .param_count = 1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = soundexFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "SQRT",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "STRFTIME",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "SUBSTR",
	 .param_count = -1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_DERIVEDCOLL,
	 .call = substrFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "SUM",
	 .param_count = 1,
	 .returns = FIELD_TYPE_NUMBER,
	 .aggregate = FUNC_AGGREGATE_GROUP,
	 .is_deterministic = false,
	 .flags = 0,
	 .call = sum_step,
	 .finalize = sumFinalize,
	 .export_to_sql = true,
	}, {
	 .name = "TIME",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "TOTAL",
	 .param_count = 1,
	 .returns = FIELD_TYPE_NUMBER,
	 .aggregate = FUNC_AGGREGATE_GROUP,
	 .is_deterministic = false,
	 .flags = 0,
	 .call = sum_step,
	 .finalize = totalFinalize,
	 .export_to_sql = true,
	}, {
	 .name = "TRIM",
	 .param_count = -1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_DERIVEDCOLL,
	 .call = trim_func,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "TYPEOF",
	 .param_count = 1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_TYPEOF,
	 .call = typeofFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "UNICODE",
	 .param_count = 1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = unicodeFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "UNLIKELY",
	 .param_count = 1,
	 .returns = FIELD_TYPE_BOOLEAN,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_UNLIKELY,
	 .call = sql_builtin_stub,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "UPPER",
	 .param_count = 1,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = SQL_FUNC_DERIVEDCOLL | SQL_FUNC_NEEDCOLL,
	 .call = UpperICUFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "VERSION",
	 .param_count = 0,
	 .returns = FIELD_TYPE_STRING,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = sql_func_version,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "ZEROBLOB",
	 .param_count = 1,
	 .returns = FIELD_TYPE_VARBINARY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = true,
	 .flags = 0,
	 .call = zeroblobFunc,
	 .finalize = NULL,
	 .export_to_sql = true,
	}, {
	 .name = "_sql_stat_get",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "_sql_stat_init",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	}, {
	 .name = "_sql_stat_push",
	 .call = sql_builtin_stub,
	 .export_to_sql = false,
	 .param_count = -1,
	 .returns = FIELD_TYPE_ANY,
	 .aggregate = FUNC_AGGREGATE_NONE,
	 .is_deterministic = false,
	 .flags = 0,
	 .finalize = NULL,
	},
};

static struct func_vtab func_sql_builtin_vtab;

struct func *
func_sql_builtin_new(struct func_def *def)
{
	assert(def->language == FUNC_LANGUAGE_SQL_BUILTIN);
	/** Binary search for corresponding builtin entry. */
	int idx = -1, left = 0, right = nelem(sql_builtins) - 1;
	while (left <= right) {
		uint32_t mid = (left + right) / 2;
		int rc = strcmp(def->name, sql_builtins[mid].name);
		if (rc == 0) {
			idx = mid;
			break;
		}
		if (rc < 0)
			right = mid - 1;
		else
			left = mid + 1;
	}
	/*
	 * All SQL built-in(s) (stubs) are defined in a snapshot.
	 * Implementation-specific metadata is defined in
	 * sql_builtins list. When a definition were not found
	 * above, the function name is invalid, i.e. it is
	 * not built-in function.
	 */
	if (idx == -1) {
		diag_set(ClientError, ER_CREATE_FUNCTION, def->name,
			 "given built-in is not predefined");
		return NULL;
	}
	struct func_sql_builtin *func =
		(struct func_sql_builtin *) calloc(1, sizeof(*func));
	if (func == NULL) {
		diag_set(OutOfMemory, sizeof(*func), "malloc", "func");
		return NULL;
	}
	func->base.def = def;
	func->base.vtab = &func_sql_builtin_vtab;
	func->flags = sql_builtins[idx].flags;
	func->call = sql_builtins[idx].call;
	func->finalize = sql_builtins[idx].finalize;
	def->param_count = sql_builtins[idx].param_count;
	def->is_deterministic = sql_builtins[idx].is_deterministic;
	def->returns = sql_builtins[idx].returns;
	def->aggregate = sql_builtins[idx].aggregate;
	def->exports.sql = sql_builtins[idx].export_to_sql;
	return &func->base;
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
