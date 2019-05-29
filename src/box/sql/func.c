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
#include "vdbeInt.h"
#include "version.h"
#include "coll/coll.h"
#include "tarantoolInt.h"
#include <unicode/ustring.h>
#include <unicode/ucasemap.h>
#include <unicode/ucnv.h>
#include <unicode/uchar.h>
#include <unicode/ucol.h>

static UConverter* pUtf8conv;

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
	int mask;		/* 0 for min() or 0xffffffff for max() */
	int iBest;
	struct coll *pColl;

	assert(argc > 1);
	mask = sql_user_data(context) == 0 ? 0 : -1;
	pColl = sqlGetFuncCollSeq(context);
	assert(mask == -1 || mask == 0);
	iBest = 0;
	if (sql_value_is_null(argv[0]))
		return;
	for (i = 1; i < argc; i++) {
		if (sql_value_is_null(argv[i]))
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
	switch (sql_value_type(argv[0])) {
	case MP_INT:
		z = "integer";
		break;
	case MP_STR:
		z = "string";
		break;
	case MP_DOUBLE:
		z = "number";
		break;
	case MP_BIN:
		z = "scalar";
		break;
	case MP_BOOL:
		z = "boolean";
		break;
	default:
		z = "null";
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
	case MP_INT:
	case MP_DOUBLE:{
			sql_result_int(context,
					   sql_value_bytes(argv[0]));
			break;
		}
	case MP_STR:{
			const unsigned char *z = sql_value_text(argv[0]);
			if (z == 0)
				return;
			len = sql_utf8_char_count(z, sql_value_bytes(argv[0]));
			sql_result_int(context, len);
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
	case MP_INT:{
			i64 iVal = sql_value_int64(argv[0]);
			if (iVal < 0) {
				if (iVal == SMALLEST_INT64) {
					/* IMP: R-31676-45509 If X is the integer -9223372036854775808
					 * then abs(X) throws an integer overflow error since there is no
					 * equivalent positive 64-bit two complement value.
					 */
					sql_result_error(context,
							     "integer overflow",
							     -1);
					return;
				}
				iVal = -iVal;
			}
			sql_result_int64(context, iVal);
			break;
		}
	case MP_NIL:{
			/* IMP: R-37434-19929 Abs(X) returns NULL if X is NULL. */
			sql_result_null(context);
			break;
		}
	case MP_BOOL: {
		diag_set(ClientError, ER_INCONSISTENT_TYPES, "number",
			 "boolean");
		context->isError = SQL_TARANTOOL_ERROR;
		context->fErrorOrAux = 1;
		return;
	}
	default:{
			/* Because sql_value_double() returns 0.0 if the argument is not
			 * something that can be converted into a number, we have:
			 * IMP: R-01992-00519 Abs(X) returns 0.0 if X is a string or blob
			 * that cannot be converted to a numeric value.
			 */
			double rVal = sql_value_double(argv[0]);
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
		diag_set(ClientError, ER_INCONSISTENT_TYPES, "TEXT or BLOB",
			 mem_type_to_str(inconsistent_type_arg));
		context->isError = SQL_TARANTOOL_ERROR;
		context->fErrorOrAux = 1;
		return;
	}
	/*
	 * Both params of Position function must be of the same
	 * type.
	 */
	if (haystack_type != needle_type) {
		diag_set(ClientError, ER_INCONSISTENT_TYPES,
			 mem_type_to_str(needle), mem_type_to_str(haystack));
		context->isError = SQL_TARANTOOL_ERROR;
		context->fErrorOrAux = 1;
		return;
	}

	int n_needle_bytes = sql_value_bytes(needle);
	int n_haystack_bytes = sql_value_bytes(haystack);
	int position = 1;
	if (n_needle_bytes > 0) {
		const unsigned char *haystack_str;
		const unsigned char *needle_str;
		if (haystack_type == MP_BIN) {
			needle_str = sql_value_blob(needle);
			haystack_str = sql_value_blob(haystack);
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
			haystack_str = sql_value_text(haystack);
			needle_str = sql_value_text(needle);

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
	sql_result_int(context, position);
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

	if (argc >= 1
	    && (zFormat = (const char *)sql_value_text(argv[0])) != 0) {
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
	i64 p1, p2;
	int negP2 = 0;

	assert(argc == 3 || argc == 2);
	if (sql_value_is_null(argv[1])
	    || (argc == 3 && sql_value_is_null(argv[2]))
	    ) {
		return;
	}
	p0type = sql_value_type(argv[0]);
	p1 = sql_value_int(argv[1]);
	if (p0type == MP_BIN) {
		len = sql_value_bytes(argv[0]);
		z = sql_value_blob(argv[0]);
		if (z == 0)
			return;
		assert(len == sql_value_bytes(argv[0]));
	} else {
		z = sql_value_text(argv[0]);
		if (z == 0)
			return;
		len = 0;
		if (p1 < 0)
			len = sql_utf8_char_count(z, sql_value_bytes(argv[0]));
	}
	if (argc == 3) {
		p2 = sql_value_int(argv[2]);
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
		int byte_size = sql_value_bytes(argv[0]);
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
	int n = 0;
	double r;
	char *zBuf;
	assert(argc == 1 || argc == 2);
	if (argc == 2) {
		if (sql_value_is_null(argv[1]))
			return;
		n = sql_value_int(argv[1]);
		if (n < 0)
			n = 0;
	}
	if (sql_value_is_null(argv[0]))
		return;
	r = sql_value_double(argv[0]);
	/* If Y==0 and X will fit in a 64-bit int,
	 * handle the rounding directly,
	 * otherwise use printf.
	 */
	if (n == 0 && r >= 0 && r < LARGEST_INT64 - 1) {
		r = (double)((sql_int64) (r + 0.5));
	} else if (n == 0 && r < 0 && (-r) < LARGEST_INT64 - 1) {
		r = -(double)((sql_int64) ((-r) + 0.5));
	} else {
		zBuf = sql_mprintf("%.*f", n, r);
		if (zBuf == 0) {
			sql_result_error_nomem(context);
			return;
		}
		sqlAtoF(zBuf, &r, sqlStrlen30(zBuf));
		sql_free(zBuf);
	}
	sql_result_double(context, r);
}

/*
 * Allocate nByte bytes of space using sqlMalloc(). If the
 * allocation fails, call sql_result_error_nomem() to notify
 * the database handle that malloc() has failed and return NULL.
 * If nByte is larger than the maximum string or blob length, then
 * raise an SQL_TOOBIG exception and return NULL.
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
		sql_result_error_toobig(context);
		z = 0;
	} else {
		z = sqlMalloc(nByte);
		if (!z) {
			sql_result_error_nomem(context);
		}
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
	z2 = (char *)sql_value_text(argv[0]);                              \
	n = sql_value_bytes(argv[0]);                                      \
	/*                                                                     \
	 * Verify that the call to _bytes()                                    \
	 * does not invalidate the _text() pointer.                            \
	 */                                                                    \
	assert(z2 == (char *)sql_value_text(argv[0]));                     \
	if (!z2)                                                               \
		return;                                                        \
	z1 = contextMalloc(context, ((i64) n) + 1);                            \
	if (!z1) {                                                             \
		sql_result_error_nomem(context);                           \
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
		if (!z1) {                                                     \
			sql_result_error_nomem(context);                   \
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
	sql_int64 r;
	UNUSED_PARAMETER2(NotUsed, NotUsed2);
	sql_randomness(sizeof(r), &r);
	if (r < 0) {
		/* We need to prevent a random number of 0x8000000000000000
		 * (or -9223372036854775808) since when you do abs() of that
		 * number of you get the same value back again.  To do this
		 * in a way that is testable, mask the sign bit off of negative
		 * values, resulting in a positive value.  Then take the
		 * 2s complement of that positive value.  The end result can
		 * therefore be no less than -9223372036854775807.
		 */
		r = -(r & LARGEST_INT64);
	}
	sql_result_int64(context, r);
}

/*
 * Implementation of randomblob(N).  Return a random blob
 * that is N bytes long.
 */
static void
randomBlob(sql_context * context, int argc, sql_value ** argv)
{
	int n;
	unsigned char *p;
	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	n = sql_value_int(argv[0]);
	if (n < 1)
		return;
	p = contextMalloc(context, n);
	if (p) {
		sql_randomness(n, p);
		sql_result_blob(context, (char *)p, n, sql_free);
	}
}

#define Utf8Read(s, e) ucnv_getNextUChar(pUtf8conv, &(s), (e), &status)

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
 * @param is_like_ci true if LIKE is case insensitive.
 * @param match_other The escape char for LIKE.
 *
 * @retval One of pattern_match_status values.
 */
static int
sql_utf8_pattern_compare(const char *pattern,
			 const char *string,
			 const char *pattern_end,
			 const char *string_end,
			 const int is_like_ci,
			 UChar32 match_other)
{
	/* Next pattern and input string chars. */
	UChar32 c, c2;
	/* One past the last escaped input char. */
	const char *zEscaped = 0;
	UErrorCode status = U_ZERO_ERROR;

	while (pattern < pattern_end) {
		c = Utf8Read(pattern, pattern_end);
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
			while ((c = Utf8Read(pattern, pattern_end)) !=
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
				c = Utf8Read(pattern, pattern_end);
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
			if (is_like_ci)
				c = u_tolower(c);
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
				c2 = Utf8Read(string, string_end);
				if (c2 == SQL_INVALID_UTF8_SYMBOL)
					return NO_MATCH;
				if (!is_like_ci) {
					if (c2 != c)
						continue;
				} else {
					if (c2 != c && u_tolower(c2) != c)
						continue;
				}
				bMatch = sql_utf8_pattern_compare(pattern,
								  string,
								  pattern_end,
								  string_end,
								  is_like_ci,
								  match_other);
				if (bMatch != NO_MATCH)
					return bMatch;
			}
			return NO_WILDCARD_MATCH;
		}
		if (c == match_other) {
			c = Utf8Read(pattern, pattern_end);
			if (c == SQL_INVALID_UTF8_SYMBOL)
				return INVALID_PATTERN;
			if (c == SQL_END_OF_STRING)
				return NO_MATCH;
			zEscaped = pattern;
		}
		c2 = Utf8Read(string, string_end);
		if (c2 == SQL_INVALID_UTF8_SYMBOL)
			return NO_MATCH;
		if (c == c2)
			continue;
		if (is_like_ci) {
			/*
			 * Small optimization. Reduce number of
			 * calls to u_tolower function. SQL
			 * standards suggest use to_upper for
			 * symbol normalisation. However, using
			 * to_lower allows to respect Turkish 'İ'
			 * in default locale.
			 */
			if (u_tolower(c) == c2 || c == u_tolower(c2))
				continue;
		}
		if (c == MATCH_ONE_WILDCARD && pattern != zEscaped &&
		    c2 != SQL_END_OF_STRING)
			continue;
		return NO_MATCH;
	}
	return string == string_end ? MATCH : NO_MATCH;
}

/**
 * Compare two UTF-8 strings for equality using case sensitive
 * sql_utf8_pattern_compare.
 */
int
sql_strlike_cs(const char *zPattern, const char *zStr, unsigned int esc)
{
	return sql_utf8_pattern_compare(zPattern, zStr,
		                        zPattern + strlen(zPattern),
		                        zStr + strlen(zStr), 0, esc);
}

/**
 * Compare two UTF-8 strings for equality using case insensitive
 * sql_utf8_pattern_compare.
 */
int
sql_strlike_ci(const char *zPattern, const char *zStr, unsigned int esc)
{
	return sql_utf8_pattern_compare(zPattern, zStr,
		                        zPattern + strlen(zPattern),
		                        zStr + strlen(zStr), 1, esc);
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
	sql *db = sql_context_db_handle(context);
	int is_like_ci = SQL_PTR_TO_INT(sql_user_data(context));
	int rhs_type = sql_value_type(argv[0]);
	int lhs_type = sql_value_type(argv[1]);

	if (lhs_type != MP_STR || rhs_type != MP_STR) {
		if (lhs_type == MP_NIL || rhs_type == MP_NIL)
			return;
		char *inconsistent_type = rhs_type != MP_STR ?
					  mem_type_to_str(argv[0]) :
					  mem_type_to_str(argv[1]);
		diag_set(ClientError, ER_INCONSISTENT_TYPES, "TEXT",
			 inconsistent_type);
		context->fErrorOrAux = 1;
		context->isError = SQL_TARANTOOL_ERROR;
		return;
	}
	const char *zB = (const char *) sql_value_text(argv[0]);
	const char *zA = (const char *) sql_value_text(argv[1]);
	const char *zB_end = zB + sql_value_bytes(argv[0]);
	const char *zA_end = zA + sql_value_bytes(argv[1]);

	/*
	 * Limit the length of the LIKE pattern to avoid problems
	 * of deep recursion and N*N behavior in
	 * sql_utf8_pattern_compare().
	 */
	nPat = sql_value_bytes(argv[0]);
	testcase(nPat == db->aLimit[SQL_LIMIT_LIKE_PATTERN_LENGTH]);
	testcase(nPat == db->aLimit[SQL_LIMIT_LIKE_PATTERN_LENGTH] + 1);
	if (nPat > db->aLimit[SQL_LIMIT_LIKE_PATTERN_LENGTH]) {
		sql_result_error(context,
				     "LIKE pattern is too complex", -1);
		return;
	}
	/* Encoding did not change */
	assert(zB == (const char *) sql_value_text(argv[0]));

	if (argc == 3) {
		/*
		 * The escape character string must consist of a
		 * single UTF-8 character. Otherwise, return an
		 * error.
		 */
		const unsigned char *zEsc = sql_value_text(argv[2]);
		if (zEsc == 0)
			return;
		const char *const err_msg =
			"ESCAPE expression must be a single character";
		if (sql_utf8_char_count(zEsc, sql_value_bytes(argv[2])) != 1) {
			sql_result_error(context, err_msg, -1);
			return;
		}
		escape = sqlUtf8Read(&zEsc);
	}
	if (!zA || !zB)
		return;
	int res;
	res = sql_utf8_pattern_compare(zB, zA, zB_end, zA_end,
				       is_like_ci, escape);
	if (res == INVALID_PATTERN) {
		const char *const err_msg =
			"LIKE pattern can only contain UTF-8 characters";
		sql_result_error(context, err_msg, -1);
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
			r1 = sql_value_double(argv[0]);
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
	case MP_INT:{
			sql_result_value(context, argv[0]);
			break;
		}
	case MP_BIN:{
			char *zText = 0;
			char const *zBlob = sql_value_blob(argv[0]);
			int nBlob = sql_value_bytes(argv[0]);
			assert(zBlob == sql_value_blob(argv[0]));	/* No encoding change */
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
			const unsigned char *zArg = sql_value_text(argv[0]);
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
		sql_result_text(context, sql_value_boolean(argv[0]) ?
				"true" : "false", -1, SQL_TRANSIENT);
		break;
	}
	default:{
			assert(sql_value_is_null(argv[0]));
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
	const unsigned char *z = sql_value_text(argv[0]);
	(void)argc;
	if (z && z[0])
		sql_result_int(context, sqlUtf8Read(&z));
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
	if (z == 0) {
		sql_result_error_nomem(context);
		return;
	}
	for (i = 0; i < argc; i++) {
		sql_int64 x;
		unsigned c;
		x = sql_value_int64(argv[i]);
		if (x < 0 || x > 0x10ffff)
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
	pBlob = sql_value_blob(argv[0]);
	n = sql_value_bytes(argv[0]);
	assert(pBlob == sql_value_blob(argv[0]));	/* No encoding change */
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
	i64 n;
	int rc;
	assert(argc == 1);
	UNUSED_PARAMETER(argc);
	n = sql_value_int64(argv[0]);
	if (n < 0)
		n = 0;
	rc = sql_result_zeroblob64(context, n);	/* IMP: R-00293-64994 */
	if (rc) {
		sql_result_error_code(context, rc);
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
	zStr = sql_value_text(argv[0]);
	if (zStr == 0)
		return;
	nStr = sql_value_bytes(argv[0]);
	assert(zStr == sql_value_text(argv[0]));	/* No encoding change */
	zPattern = sql_value_text(argv[1]);
	if (zPattern == 0) {
		assert(sql_value_is_null(argv[1])
		       || sql_context_db_handle(context)->mallocFailed);
		return;
	}
	nPattern = sql_value_bytes(argv[1]);
	if (nPattern == 0) {
		assert(! sql_value_is_null(argv[1]));
		sql_result_value(context, argv[0]);
		return;
	}
	assert(zPattern == sql_value_text(argv[1]));	/* No encoding change */
	zRep = sql_value_text(argv[2]);
	if (zRep == 0)
		return;
	nRep = sql_value_bytes(argv[2]);
	assert(zRep == sql_value_text(argv[2]));
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
				sql_result_error_toobig(context);
				sql_free(zOut);
				return;
			}
			zOld = zOut;
			zOut = sql_realloc64(zOut, (int)nOut);
			if (zOut == 0) {
				sql_result_error_nomem(context);
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
trim_func_one_arg(struct sql_context *context, int argc, sql_value **argv)
{
	assert(argc == 1);
	(void) argc;

	const unsigned char *input_str;
	if ((input_str = sql_value_text(argv[0])) == NULL)
		return;

	int input_str_sz = sql_value_bytes(argv[0]);
	uint8_t len_one = 1;
	trim_procedure(context, TRIM_BOTH, (const unsigned char *) " ",
		       &len_one, 1, input_str, input_str_sz);
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
trim_func_two_args(struct sql_context *context, int argc, sql_value **argv)
{
	assert(argc == 2);
	(void) argc;

	const unsigned char *input_str, *trim_set;
	if ((input_str = sql_value_text(argv[1])) == NULL)
		return;

	int input_str_sz = sql_value_bytes(argv[1]);
	if (sql_value_type(argv[0]) == MP_INT) {
		uint8_t len_one = 1;
		trim_procedure(context, sql_value_int(argv[0]),
			       (const unsigned char *) " ", &len_one, 1,
			       input_str, input_str_sz);
	} else if ((trim_set = sql_value_text(argv[0])) != NULL) {
		int trim_set_sz = sql_value_bytes(argv[0]);
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
trim_func_three_args(struct sql_context *context, int argc, sql_value **argv)
{
	assert(argc == 3);
	(void) argc;

	assert(sql_value_type(argv[0]) == MP_INT);
	const unsigned char *input_str, *trim_set;
	if ((input_str = sql_value_text(argv[2])) == NULL ||
	    (trim_set = sql_value_text(argv[1])) == NULL)
		return;

	int trim_set_sz = sql_value_bytes(argv[1]);
	int input_str_sz = sql_value_bytes(argv[2]);
	uint8_t *char_len;
	int char_cnt = trim_prepare_char_len(context, trim_set, trim_set_sz,
					     &char_len);
	if (char_cnt == -1)
		return;
	trim_procedure(context, sql_value_int(argv[0]), trim_set, char_len,
		       char_cnt, input_str, input_str_sz);
	sql_free(char_len);
}

/* IMP: R-25361-16150 This function is omitted from sql by default. It
 * is only available if the SQL_SOUNDEX compile-time option is used
 * when sql is built.
 */
#ifdef SQL_SOUNDEX
/*
 * Compute the soundex encoding of a word.
 *
 * IMP: R-59782-00072 The soundex(X) function returns a string that is the
 * soundex encoding of the string X.
 */
static void
soundexFunc(sql_context * context, int argc, sql_value ** argv)
{
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
	zIn = (u8 *) sql_value_text(argv[0]);
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
#endif				/* SQL_SOUNDEX */

/*
 * An instance of the following structure holds the context of a
 * sum() or avg() aggregate computation.
 */
typedef struct SumCtx SumCtx;
struct SumCtx {
	double rSum;		/* Floating point sum */
	i64 iSum;		/* Integer sum */
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
	if (type != MP_DOUBLE && type != MP_INT) {
		if (mem_apply_numeric_type(argv[0]) != 0) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
				 sql_value_text(argv[0]), "number");
			context->fErrorOrAux = 1;
			context->isError = SQL_TARANTOOL_ERROR;
			return;
		}
		type = sql_value_type(argv[0]);
	}
	p->cnt++;
	if (type == MP_INT) {
		int64_t v = sql_value_int64(argv[0]);
		p->rSum += v;
		if ((p->approx | p->overflow) == 0 &&
		    sqlAddInt64(&p->iSum, v) != 0) {
			p->overflow = 1;
		}
	} else {
		p->rSum += sql_value_double(argv[0]);
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
			sql_result_error(context, "integer overflow", -1);
		} else if (p->approx) {
			sql_result_double(context, p->rSum);
		} else {
			sql_result_int64(context, p->iSum);
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
	p = sql_aggregate_context(context, sizeof(*p));
	if ((argc == 0 || ! sql_value_is_null(argv[0])) && p) {
		p->n++;
	}
}

static void
countFinalize(sql_context * context)
{
	CountCtx *p;
	p = sql_aggregate_context(context, 0);
	sql_result_int64(context, p ? p->n : 0);
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

	pBest = (Mem *) sql_aggregate_context(context, sizeof(*pBest));
	if (!pBest)
		return;

	if (sql_value_is_null(argv[0])) {
		if (pBest->flags)
			sqlSkipAccumulatorLoad(context);
	} else if (pBest->flags) {
		int max;
		int cmp;
		struct coll *pColl = sqlGetFuncCollSeq(context);
		/* This step function is used for both the min() and max() aggregates,
		 * the only difference between the two being that the sense of the
		 * comparison is inverted. For the max() aggregate, the
		 * sql_user_data() function returns (void *)-1. For min() it
		 * returns (void *)db, where db is the sql* database pointer.
		 * Therefore the next statement sets variable 'max' to 1 for the max()
		 * aggregate, or 0 for min().
		 */
		max = sql_user_data(context) != 0;
		cmp = sqlMemCompare(pBest, pArg, pColl);
		if ((max && cmp < 0) || (!max && cmp > 0)) {
			sqlVdbeMemCopy(pBest, pArg);
		} else {
			sqlSkipAccumulatorLoad(context);
		}
	} else {
		pBest->db = sql_context_db_handle(context);
		sqlVdbeMemCopy(pBest, pArg);
	}
}

static void
minMaxFinalize(sql_context * context)
{
	sql_value *pRes;
	pRes = (sql_value *) sql_aggregate_context(context, 0);
	if (pRes) {
		if (pRes->flags) {
			sql_result_value(context, pRes);
		}
		sqlVdbeMemRelease(pRes);
	}
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
	assert(argc == 1 || argc == 2);
	if (sql_value_is_null(argv[0]))
		return;
	pAccum =
	    (StrAccum *) sql_aggregate_context(context, sizeof(*pAccum));

	if (pAccum) {
		sql *db = sql_context_db_handle(context);
		int firstTerm = pAccum->mxAlloc == 0;
		pAccum->mxAlloc = db->aLimit[SQL_LIMIT_LENGTH];
		if (!firstTerm) {
			if (argc == 2) {
				zSep = (char *)sql_value_text(argv[1]);
				nSep = sql_value_bytes(argv[1]);
			} else {
				zSep = ",";
				nSep = 1;
			}
			if (zSep)
				sqlStrAccumAppend(pAccum, zSep, nSep);
		}
		zVal = (char *)sql_value_text(argv[0]);
		nVal = sql_value_bytes(argv[0]);
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
			sql_result_error_toobig(context);
		} else if (pAccum->accError == STRACCUM_NOMEM) {
			sql_result_error_nomem(context);
		} else {
			sql_result_text(context,
					    sqlStrAccumFinish(pAccum),
					    pAccum->nChar, sql_free);
		}
	}
}

/*
 * If the function already exists as a regular global function, then
 * this routine is a no-op.  If the function does not exist, then create
 * a new one that always throws a run-time error.
 */
static inline int
sql_overload_function(sql * db, const char *zName,
			  enum field_type type, int nArg)
{
	int rc = SQL_OK;

	if (sqlFindFunction(db, zName, nArg, 0) == 0) {
		rc = sqlCreateFunc(db, zName, type, nArg, 0, 0,
				       sqlInvalidFunction, 0, 0, 0);
	}
	rc = sqlApiExit(db, rc);
	return rc;
}

/*
 * This routine does per-connection function registration.  Most
 * of the built-in functions above are part of the global function set.
 * This routine only deals with those that are not global.
 */
void
sqlRegisterPerConnectionBuiltinFunctions(sql * db)
{
	int rc = sql_overload_function(db, "MATCH", FIELD_TYPE_SCALAR, 2);
	assert(rc == SQL_NOMEM || rc == SQL_OK);
	if (rc == SQL_NOMEM) {
		sqlOomFault(db);
	}
}

/*
 * Set the LIKEOPT flag on the 2-argument function with the given name.
 */
static void
setLikeOptFlag(sql * db, const char *zName, u8 flagVal)
{
	FuncDef *pDef;
	pDef = sqlFindFunction(db, zName, 2, 0);
	if (ALWAYS(pDef)) {
		pDef->funcFlags |= flagVal;
	}
}

/**
 * Register the built-in LIKE function.
 */
void
sqlRegisterLikeFunctions(sql *db, int is_case_insensitive)
{
	/*
	 * FIXME: after introducing type <BOOLEAN> LIKE must
	 * return that type: TRUE if the string matches the
	 * supplied pattern and FALSE otherwise.
	 */
	int *is_like_ci = SQL_INT_TO_PTR(is_case_insensitive);
	sqlCreateFunc(db, "LIKE", FIELD_TYPE_BOOLEAN, 2, 0,
			  is_like_ci, likeFunc, 0, 0, 0);
	sqlCreateFunc(db, "LIKE", FIELD_TYPE_BOOLEAN, 3, 0,
			  is_like_ci, likeFunc, 0, 0, 0);
	setLikeOptFlag(db, "LIKE",
		       !(is_case_insensitive) ? (SQL_FUNC_LIKE |
		       SQL_FUNC_CASE) : SQL_FUNC_LIKE);
}

int
sql_is_like_func(struct sql *db, struct Expr *expr, int *is_like_ci)
{
	if (expr->op != TK_FUNCTION || !expr->x.pList ||
	    expr->x.pList->nExpr != 2)
		return 0;
	assert(!ExprHasProperty(expr, EP_xIsSelect));
	struct FuncDef *func = sqlFindFunction(db, expr->u.zToken, 2, 0);
	assert(func != NULL);
	if ((func->funcFlags & SQL_FUNC_LIKE) == 0)
		return 0;
	*is_like_ci = (func->funcFlags & SQL_FUNC_CASE) == 0;
	return 1;
}

/*
 * All of the FuncDef structures in the aBuiltinFunc[] array above
 * to the global function hash table.  This occurs at start-time (as
 * a consequence of calling sql_initialize()).
 *
 * After this routine runs
 */
void
sqlRegisterBuiltinFunctions(void)
{
	/*
	 * Initialize default case map for UPPER/LOWER functions
	 * This structure is not freed at db exit, but that is ok.
	 */
	UErrorCode status = U_ZERO_ERROR;

	pUtf8conv = ucnv_open("utf8", &status);
	assert(pUtf8conv);
	/*
	 * The following array holds FuncDef structures for all of the functions
	 * defined in this file.
	 *
	 * The array cannot be constant since changes are made to the
	 * FuncDef.pHash elements at start-time.  The elements of this array
	 * are read-only after initialization is complete.
	 *
	 * For peak efficiency, put the most frequently used function last.
	 */
	static FuncDef aBuiltinFunc[] = {
#ifdef SQL_SOUNDEX
		FUNCTION(soundex, 1, 0, 0, soundexFunc),
#endif
		FUNCTION2(unlikely, 1, 0, 0, noopFunc, SQL_FUNC_UNLIKELY,
			  FIELD_TYPE_BOOLEAN),
		FUNCTION2(likelihood, 2, 0, 0, noopFunc, SQL_FUNC_UNLIKELY,
			  FIELD_TYPE_BOOLEAN),
		FUNCTION2(likely, 1, 0, 0, noopFunc, SQL_FUNC_UNLIKELY,
			  FIELD_TYPE_BOOLEAN),
		FUNCTION_COLL(trim, 1, 3, 0, trim_func_one_arg),
		FUNCTION_COLL(trim, 2, 3, 0, trim_func_two_args),
		FUNCTION_COLL(trim, 3, 3, 0, trim_func_three_args),
		FUNCTION(min, -1, 0, 1, minmaxFunc, FIELD_TYPE_SCALAR),
		FUNCTION(min, 0, 0, 1, 0, FIELD_TYPE_SCALAR),
		AGGREGATE2(min, 1, 0, 1, minmaxStep, minMaxFinalize,
			   SQL_FUNC_MINMAX, FIELD_TYPE_SCALAR),
		FUNCTION(max, -1, 1, 1, minmaxFunc, FIELD_TYPE_SCALAR),
		FUNCTION(max, 0, 1, 1, 0, FIELD_TYPE_SCALAR),
		AGGREGATE2(max, 1, 1, 1, minmaxStep, minMaxFinalize,
			   SQL_FUNC_MINMAX, FIELD_TYPE_SCALAR),
		FUNCTION2(typeof, 1, 0, 0, typeofFunc, SQL_FUNC_TYPEOF,
			  FIELD_TYPE_STRING),
		FUNCTION2(length, 1, 0, 0, lengthFunc, SQL_FUNC_LENGTH,
			  FIELD_TYPE_INTEGER),
		FUNCTION(position, 2, 0, 1, position_func, FIELD_TYPE_INTEGER),
		FUNCTION(printf, -1, 0, 0, printfFunc, FIELD_TYPE_STRING),
		FUNCTION(unicode, 1, 0, 0, unicodeFunc, FIELD_TYPE_STRING),
		FUNCTION(char, -1, 0, 0, charFunc, FIELD_TYPE_STRING),
		FUNCTION(abs, 1, 0, 0, absFunc, FIELD_TYPE_NUMBER),
		FUNCTION(round, 1, 0, 0, roundFunc, FIELD_TYPE_INTEGER),
		FUNCTION(round, 2, 0, 0, roundFunc, FIELD_TYPE_INTEGER),
		FUNCTION_COLL(upper, 1, 0, 1, UpperICUFunc),
		FUNCTION_COLL(lower, 1, 0, 1, LowerICUFunc),
		FUNCTION(hex, 1, 0, 0, hexFunc, FIELD_TYPE_STRING),
		FUNCTION2(ifnull, 2, 0, 0, noopFunc, SQL_FUNC_COALESCE,
			  FIELD_TYPE_INTEGER),
		VFUNCTION(random, 0, 0, 0, randomFunc, FIELD_TYPE_INTEGER),
		VFUNCTION(randomblob, 1, 0, 0, randomBlob, FIELD_TYPE_SCALAR),
		FUNCTION(nullif, 2, 0, 1, nullifFunc, FIELD_TYPE_SCALAR),
		FUNCTION(version, 0, 0, 0, sql_func_version, FIELD_TYPE_STRING),
		FUNCTION(quote, 1, 0, 0, quoteFunc, FIELD_TYPE_STRING),
		VFUNCTION(row_count, 0, 0, 0, sql_row_count, FIELD_TYPE_INTEGER),
		FUNCTION_COLL(replace, 3, 0, 0, replaceFunc),
		FUNCTION(zeroblob, 1, 0, 0, zeroblobFunc, FIELD_TYPE_SCALAR),
		FUNCTION_COLL(substr, 2, 0, 0, substrFunc),
		FUNCTION_COLL(substr, 3, 0, 0, substrFunc),
		AGGREGATE(sum, 1, 0, 0, sum_step, sumFinalize,
			  FIELD_TYPE_NUMBER),
		AGGREGATE(total, 1, 0, 0, sum_step, totalFinalize,
			  FIELD_TYPE_NUMBER),
		AGGREGATE(avg, 1, 0, 0, sum_step, avgFinalize,
			  FIELD_TYPE_NUMBER),
		AGGREGATE2(count, 0, 0, 0, countStep, countFinalize,
			   SQL_FUNC_COUNT, FIELD_TYPE_INTEGER),
		AGGREGATE(count, 1, 0, 0, countStep, countFinalize,
			  FIELD_TYPE_INTEGER),
		AGGREGATE(group_concat, 1, 0, 0, groupConcatStep,
			  groupConcatFinalize, FIELD_TYPE_STRING),
		AGGREGATE(group_concat, 2, 0, 0, groupConcatStep,
			  groupConcatFinalize, FIELD_TYPE_STRING),

		LIKEFUNC(like, 2, 1, SQL_FUNC_LIKE,
			 FIELD_TYPE_INTEGER),
		LIKEFUNC(like, 3, 1, SQL_FUNC_LIKE,
			 FIELD_TYPE_INTEGER),
		FUNCTION(coalesce, 1, 0, 0, 0, FIELD_TYPE_SCALAR),
		FUNCTION(coalesce, 0, 0, 0, 0, FIELD_TYPE_SCALAR),
		FUNCTION2(coalesce, -1, 0, 0, noopFunc, SQL_FUNC_COALESCE,
			  FIELD_TYPE_SCALAR),
	};
	sqlAnalyzeFunctions();
	sqlRegisterDateTimeFunctions();
	sqlInsertBuiltinFuncs(aBuiltinFunc, ArraySize(aBuiltinFunc));

#if 0				/* Enable to print out how the built-in functions are hashed */
	{
		int i;
		FuncDef *p;
		for (i = 0; i < SQL_FUNC_HASH_SZ; i++) {
			printf("FUNC-HASH %02d:", i);
			for (p = sqlBuiltinFunctions.a[i]; p;
			     p = p->u.pHash) {
				int n = sqlStrlen30(p->zName);
				int h = p->zName[0] + n;
				printf(" %s(%d)", p->zName, h);
			}
			printf("\n");
		}
	}
#endif
}
