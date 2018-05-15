/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include <unicode/ucasemap.h>
#include <unicode/uchar.h>
#include <coll.h>
#include "lua/utils.h"
#include "lua/utf8.h"
#include "diag.h"
#include "small/ibuf.h"

extern struct ibuf *tarantool_lua_ibuf;

/** Default universal casemap for case transformations. */
static UCaseMap *root_map = NULL;

/** Collations for cmp/casecmp functions. */
static struct coll *unicode_coll = NULL;
static struct coll *unicode_ci_coll = NULL;

static int
utf8_str_to_case(struct lua_State *L, const char *src, int src_bsize,
		 bool is_to_upper)
{
	int i = 0;
	int dst_bsize = src_bsize;
	(void) i;
	do {
		UErrorCode err = U_ZERO_ERROR;
		ibuf_reset(tarantool_lua_ibuf);
		char *dst = ibuf_alloc(tarantool_lua_ibuf, dst_bsize);
		if (dst == NULL) {
			diag_set(OutOfMemory, dst_bsize, "ibuf_alloc", "dst");
			return luaT_error(L);
		}
		int real_bsize;
		if (is_to_upper) {
			real_bsize = ucasemap_utf8ToUpper(root_map, dst,
							  dst_bsize, src,
							  src_bsize, &err);
		} else {
			real_bsize = ucasemap_utf8ToLower(root_map, dst,
							  dst_bsize, src,
							  src_bsize, &err);
		}
		if (err == U_ZERO_ERROR ||
		    err == U_STRING_NOT_TERMINATED_WARNING) {
			lua_pushlstring(L, dst, real_bsize);
			return 1;
		} else if (err == U_BUFFER_OVERFLOW_ERROR) {
			assert(real_bsize > dst_bsize);
			dst_bsize = real_bsize;
		} else {
			lua_pushnil(L);
			lua_pushstring(L, tt_sprintf("error during ICU case "\
						     "transform: %s",
						     u_errorName(err)));
			return 2;
		}
		/*
		 * On a first run either all is ok, or
		 * toLower/Upper returned needed bsize, that is
		 * allocated on a second iteration. Third
		 * iteration is not possible.
		 */
		assert(++i < 2);
	} while (true);
	unreachable();
	return 0;
}

/**
 * Convert a UTF8 string into upper case.
 * @param String to convert.
 * @retval not nil String consisting of upper letters.
 * @retval nil, error Error.
 */
static int
utf8_upper(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isstring(L, 1))
		return luaL_error(L, "Usage: utf8.upper(<string>)");
	size_t len;
	const char *str = lua_tolstring(L, 1, &len);
	return utf8_str_to_case(L, str, len, true);
}

/**
 * Convert a UTF8 string into lower case.
 * @param String to convert.
 * @retval not nil String consisting of lower letters.
 * @retval nil, error Error.
 */
static int
utf8_lower(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isstring(L, 1))
		return luaL_error(L, "Usage: utf8.lower(<string>)");
	size_t len;
	const char *str = lua_tolstring(L, 1, &len);
	return utf8_str_to_case(L, str, len, false);
}

/**
 * Calculate a 1-based positive byte offset in a string by any
 * 1-based offset (possibly negative).
 * @param offset Original 1-based offset with any sign.
 * @param len A string byte length.
 * @retval 1-based positive offset.
 */
static inline int
utf8_convert_offset(int offset, size_t len)
{
	if (offset >= 0)
		return offset;
	else if ((size_t)-offset > len)
		return 0;
	return len + offset + 1;
}

/**
 * Calculate length of a UTF8 string. Length here is symbol count.
 * Works like utf8.len in Lua 5.3. Can take negative offsets. A
 * negative offset is an offset from the end of string.
 * Positive position must be inside .
 * @param String to get length.
 * @param Start byte offset in [1, #str + 1]. Must point to the
 *        start of symbol. On invalid symbol an error is returned.
 * @param End byte offset in [0, #str]. Can point to the middle of
 *        symbol. Partial symbol is counted too.
 * @retval not nil Symbol count.
 * @retval nil, number Error. Byte position of the error is
 *         returned in the second value.
 * @retval nil, string Error. Reason is returned in the second
 *         value.
 */
static int
utf8_len(struct lua_State *L)
{
	if (lua_gettop(L) > 3 || !lua_isstring(L, 1))
		return luaL_error(L, "Usage: utf8.len(<string>, [i, [j]])");
	size_t slen;
	const char *str = lua_tolstring(L, 1, &slen);
	int len = (int) slen;
	int start_pos = utf8_convert_offset(luaL_optinteger(L, 2, 1), len);
	int end_pos = utf8_convert_offset(luaL_optinteger(L, 3, -1), len);
	if (start_pos < 1 || --start_pos > len || end_pos > len) {
		lua_pushnil(L);
		lua_pushstring(L, "position is out of string");
		return 2;
	}
	int result = 0;
	if (end_pos > start_pos) {
		UChar32 c;
		while (start_pos < end_pos) {
			++result;
			U8_NEXT(str, start_pos, len, c);
			if (c == U_SENTINEL) {
				lua_pushnil(L);
				lua_pushinteger(L, start_pos);
				return 2;
			}
		}
	}
	lua_pushinteger(L, result);
	return 1;
}

/**
 * Get next symbol code by @an offset.
 * @param String to get symbol code.
 * @param Byte offset from which get.
 *
 * @retval - No more symbols.
 * @retval not nil, not nil Byte offset and symbol code.
 */
static int
utf8_next(struct lua_State *L)
{
	if (lua_gettop(L) > 2 || !lua_isstring(L, 1))
		return luaL_error(L, "Usage: utf8.next(<string>, "\
				     "[<byte offset>])");
	size_t slen;
	const char *str = lua_tolstring(L, 1, &slen);
	int len = (int) slen;
	int pos = utf8_convert_offset(luaL_optinteger(L, 2, 1), len);
	if (pos > 0)
		--pos;
	if (pos >= len)
		return 0;
	UChar32 c;
	U8_NEXT(str, pos, len, c);
	if (c == U_SENTINEL)
		return 0;
	lua_pushinteger(L, pos + 1);
	lua_pushinteger(L, c);
	return 2;
}

/**
 * Convert a UTF8 char code (or codes) into Lua string. When
 * multiple codes are provided, they are concatenated into a
 * monolite string.
 * @param Char codes.
 * @retval Result UTF8 string.
 */
static int
utf8_char(struct lua_State *L)
{
	int top = lua_gettop(L);
	if (top < 1)
		return luaL_error(L, "Usage: utf8.char(<char code>");
	int len = 0;
	UChar32 c;
	/* Fast way - convert one symbol. */
	if (top == 1) {
		char buf[U8_MAX_LENGTH];
		c = luaL_checkinteger(L, 1);
		U8_APPEND_UNSAFE(buf, len, c);
		assert(len <= (int)sizeof(buf));
		lua_pushlstring(L, buf, len);
		return 1;
	}
	/* Slow way - use dynamic buffer. */
	ibuf_reset(tarantool_lua_ibuf);
	char *str = ibuf_alloc(tarantool_lua_ibuf, top * U8_MAX_LENGTH);
	if (str == NULL) {
		diag_set(OutOfMemory, top * U8_MAX_LENGTH, "ibuf_alloc",
			 "str");
		return luaT_error(L);
	}
	for (int i = 1; i <= top; ++i) {
		c = luaL_checkinteger(L, i);
		U8_APPEND_UNSAFE(str, len, c);
	}
	lua_pushlstring(L, str, len);
	return 1;
}

/**
 * Get byte offsets by symbol positions in a string. Positions can
 * be negative.
 * @param s Original string.
 * @param len Length of @an s.
 * @param start_pos Start position (symbol offset).
 * @param end_pos End position (symbol offset).
 * @param[out] start_offset_ Start position (byte offset).
 * @param[out] end_offset_ End position (byte offset).
 */
static void
utf8_sub(const uint8_t *s, int len, int start_pos, int end_pos,
	 int *start_offset_, int *end_offset_)
{
	int start_offset = 0, end_offset = len;
	if (start_pos >= 0) {
		U8_FWD_N(s, start_offset, len, start_pos);
		if (end_pos >= 0) {
			/* --[-------]---- ...  */
			int n = end_pos - start_pos;
			end_offset = start_offset;
			U8_FWD_N(s, end_offset, len, n);
		} else {
			/* --[---- ... ----]--- */
			int n = -(end_pos + 1);
			U8_BACK_N(s, 0, end_offset, n);
		}
	} else {
		int n;
		if (end_pos < 0) {
			/* ... -----[-----]--- */
			n = -(end_pos + 1);
			U8_BACK_N(s, 0, end_offset, n);
			start_offset = end_offset;
			n = end_pos - start_pos + 1;
		} else {
			/* ---]-- ... --[---- */
			end_offset = 0;
			U8_FWD_N(s, end_offset, len, end_pos);
			n = -start_pos;
			start_offset = len;
		}
		U8_BACK_N(s, 0, start_offset, n);
	}
	*start_offset_ = start_offset;
	if (start_offset <= end_offset)
		*end_offset_ = end_offset;
	else
		*end_offset_ = start_offset;
}

/**
 * Get a substring from a UTF8 string.
 * @param String to get a substring.
 * @param Start position in symbol count. Optional, can be
 *        negative.
 * @param End position in symbol count. Optional, can be negative.
 *
 * @retval Substring.
 */
static int
utf8_lua_sub(struct lua_State *L)
{
	if (lua_gettop(L) < 2 || !lua_isstring(L, 1))
		return luaL_error(L, "Usage: utf8.sub(<string>, [i, [j]])");
	int start_pos = luaL_checkinteger(L, 2);
	if (start_pos > 0)
		--start_pos;
	int end_pos = luaL_optinteger(L, 3, -1);
	size_t slen;
	const char *str = lua_tolstring(L, 1, &slen);
	int len = (int) slen;
	int start_offset, end_offset;
	utf8_sub((const uint8_t *) str, len, start_pos, end_pos, &start_offset,
		 &end_offset);
	assert(end_offset >= start_offset);
	lua_pushlstring(L, str + start_offset, end_offset - start_offset);
	return 1;
}

/**
 * Macro to easy create lua wrappers for ICU symbol checkers.
 * @param One stmbol code or string.
 * @retval True, if the symbol has a requested property. Else
 *         false.
 */
#define UCHAR32_CHECKER(name) \
static int \
utf8_##name(struct lua_State *L) \
{ \
	if (lua_gettop(L) != 1) \
		return luaL_error(L, "Usage: utf8."#name"(<string> or "\
				     "<one symbol code>)"); \
	UChar32 c; \
	bool result = false; \
	if (lua_type(L, 1) == LUA_TSTRING) { \
		size_t slen; \
		const char *str = lua_tolstring(L, 1, &slen); \
		int len = (int) slen; \
		if (len > 0) { \
			int offset = 0; \
			U8_NEXT(str, offset, len, c); \
			result = c != U_SENTINEL && offset == len && \
				 u_##name(c); \
		} \
	} else { \
		result = u_##name(luaL_checkinteger(L, 1)); \
	} \
	lua_pushboolean(L, result); \
	return 1; \
}\

UCHAR32_CHECKER(islower)
UCHAR32_CHECKER(isupper)
UCHAR32_CHECKER(isdigit)
UCHAR32_CHECKER(isalpha)

static inline int
utf8_cmp_impl(struct lua_State *L, const char *usage, struct coll *coll)
{
	assert(coll != NULL);
	if (lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_isstring(L, 2))
		luaL_error(L, usage);
	size_t l1, l2;
	const char *s1 = lua_tolstring(L, 1, &l1);
	const char *s2 = lua_tolstring(L, 2, &l2);
	lua_pushinteger(L, coll->cmp(s1, l1, s2, l2, coll));
	return 1;
}

/**
 * Compare two UTF8 strings.
 * @param s1 First string.
 * @param s1 Second string.
 *
 * @retval <0 s1 < s2.
 * @retval >0 s1 > s2.
 * @retval =0 s1 = s2.
 */
static int
utf8_cmp(struct lua_State *L)
{
	return utf8_cmp_impl(L, "Usage: utf8.cmp(<string1>, <string2>)",
			     unicode_coll);
}

/**
 * Compare two UTF8 strings ignoring case.
 * @param s1 First string.
 * @param s1 Second string.
 *
 * @retval <0 s1 < s2.
 * @retval >0 s1 > s2.
 * @retval =0 s1 = s2.
 */
static int
utf8_casecmp(struct lua_State *L)
{
	return utf8_cmp_impl(L, "Usage: utf8.casecmp(<string1>, <string2>)",
			     unicode_ci_coll);
}

static const struct luaL_Reg utf8_lib[] = {
	{"upper", utf8_upper},
	{"lower", utf8_lower},
	{"len", utf8_len},
	{"next", utf8_next},
	{"char", utf8_char},
	{"sub", utf8_lua_sub},
	{"islower", utf8_islower},
	{"isupper", utf8_isupper},
	{"isdigit", utf8_isdigit},
	{"isalpha", utf8_isalpha},
	{"cmp", utf8_cmp},
	{"casecmp", utf8_casecmp},
	{NULL, NULL}
};

void
tarantool_lua_utf8_init(struct lua_State *L)
{
	UErrorCode err = U_ZERO_ERROR;
	root_map = ucasemap_open("", 0, &err);
	if (root_map == NULL) {
		luaL_error(L, tt_sprintf("error in ICU ucasemap_open: %s",
					 u_errorName(err)));
	}
	struct coll_def def;
	memset(&def, 0, sizeof(def));
	unicode_coll = coll_new(&def);
	if (unicode_coll == NULL)
		goto error_coll;
	def.icu.strength = COLL_ICU_STRENGTH_PRIMARY;
	unicode_ci_coll = coll_new(&def);
	if (unicode_ci_coll == NULL)
		goto error_coll;
	luaL_register(L, "utf8", utf8_lib);
	lua_pop(L, 1);
	return;
error_coll:
	tarantool_lua_utf8_free();
	luaT_error(L);
}

void
tarantool_lua_utf8_free()
{
	ucasemap_close(root_map);
	if (unicode_coll != NULL)
		coll_unref(unicode_coll);
	if (unicode_ci_coll != NULL)
		coll_unref(unicode_ci_coll);
}
