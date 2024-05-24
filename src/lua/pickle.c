/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#include "lua/pickle.h"

#include <arpa/inet.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua/utils.h"
#include "lua/serializer.h"
#include "lua/msgpack.h" /* luaL_msgpack_default */
#include <fiber.h>
#include "bit/bit.h"

static inline void
luaL_region_dup(struct lua_State *L, struct region *region,
		const void *ptr, size_t size)
{
	void *to = region_alloc(region, size);
	if (to == NULL) {
		diag_set(OutOfMemory, size, "region", "luaL_region_dup");
		luaT_error(L);
	}
	(void) memcpy(to, ptr, size);
}

static int
lbox_pack(struct lua_State *L)
{
	const char *format = luaT_checkstring(L, 1);
	/* first arg comes second */
	int i = 2;
	int nargs = lua_gettop(L);
	const char *str;

	struct region *buf = &fiber()->gc;
	/*
	 * XXX: this code leaks region memory in case of any
	 * Lua memory. In absence of external unwind, Lua C API
	 * makes it painfully difficult to clean up resources
	 * properly in case of error.
	 */
	size_t used = region_used(buf);

	struct luaL_field field;
	double dbl;
	float flt;
	while (*format) {
		if (i > nargs) {
			diag_set(IllegalParams,
				 "pickle.pack: argument count does not match "
				 "the format");
			luaT_error(L);
		}
		luaL_checkfield(L, luaL_msgpack_default, i, &field);
		switch (*format) {
		case 'B':
		case 'b':
			/* signed and unsigned 8-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT) {
				diag_set(IllegalParams,
					 "pickle.pack: expected 8-bit int");
				luaT_error(L);
			}

			luaL_region_dup(L, buf, &field.ival, sizeof(uint8_t));
			break;
		case 'S':
		case 's':
			/* signed and unsigned 16-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT) {
				diag_set(IllegalParams,
					 "pickle.pack: expected 16-bit int");
				luaT_error(L);
			}

			luaL_region_dup(L, buf, &field.ival, sizeof(uint16_t));
			break;
		case 'n':
			/* signed and unsigned 16-bit big endian integers */
			if (field.type != MP_UINT && field.type != MP_INT) {
				diag_set(IllegalParams,
					 "pickle.pack: expected 16-bit int");
				luaT_error(L);
			}

			field.ival = (uint16_t) htons((uint16_t) field.ival);
			luaL_region_dup(L, buf, &field.ival, sizeof(uint16_t));
			break;
		case 'I':
		case 'i':
			/* signed and unsigned 32-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT) {
				diag_set(IllegalParams,
					 "pickle.pack: expected 32-bit int");
				luaT_error(L);
			}

			luaL_region_dup(L, buf, &field.ival, sizeof(uint32_t));
			break;
		case 'N':
			/* signed and unsigned 32-bit big endian integers */
			if (field.type != MP_UINT && field.type != MP_INT) {
				diag_set(IllegalParams,
					 "pickle.pack: expected 32-bit int");
				luaT_error(L);
			}

			field.ival = htonl(field.ival);
			luaL_region_dup(L, buf, &field.ival, sizeof(uint32_t));
			break;
		case 'L':
		case 'l':
			/* signed and unsigned 64-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT) {
				diag_set(IllegalParams,
					 "pickle.pack: expected 64-bit int");
				luaT_error(L);
			}

			luaL_region_dup(L, buf, &field.ival, sizeof(uint64_t));
			break;
		case 'Q':
		case 'q':
			/* signed and unsigned 64-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT) {
				diag_set(IllegalParams,
					 "pickle.pack: expected 64-bit int");
				luaT_error(L);
			}

			field.ival = bswap_u64(field.ival);
			luaL_region_dup(L, buf,  &field.ival, sizeof(uint64_t));
			break;
		case 'd':
			dbl = (double) lua_tonumber(L, i);
			luaL_region_dup(L, buf, &dbl, sizeof(double));
			break;
		case 'f':
			flt = (float) lua_tonumber(L, i);
			luaL_region_dup(L, buf, &flt, sizeof(float));
			break;
		case 'A':
		case 'a':
		{
			/* A sequence of bytes */
			size_t len;
			str = luaT_checklstring(L, i, &len);
			luaL_region_dup(L, buf, str, len);
			break;
		}
		default:
			diag_set(IllegalParams,
				 "pickle.pack: unsupported pack "
				 "format specifier '%c'", *format);
			luaT_error(L);
		}
		i++;
		format++;
	}

	size_t total_len = region_used(buf) - used;
	const char *res = (char *) region_join(buf, total_len);
	if (res == NULL) {
		region_truncate(buf, used);
		diag_set(OutOfMemory, total_len, "region", "region_join");
		luaT_error(L);
	}
	lua_pushlstring(L, res, total_len);
	region_truncate(buf, used);
	return 1;
}


static int
lbox_unpack(struct lua_State *L)
{
	size_t format_size = 0;
	const char *format = luaT_checklstring(L, 1, &format_size);
	const char *f = format;

	size_t str_size = 0;
	const char *str = luaT_checklstring(L, 2, &str_size);
	const char *end = str + str_size;
	const char *s = str;

	int save_stacksize = lua_gettop(L);

#define CHECK_SIZE(cur) \
	do { \
		if (unlikely((cur) >= end)) { \
			diag_set(IllegalParams, \
				 "pickle.unpack('%c'): got %td bytes " \
				 "(expected: %td+)", \
				 *f, end - str, cur - str + 1); \
			luaT_error(L); \
		} \
	} while (0)

	while (*f) {
		switch (*f) {
		case 'b':
			CHECK_SIZE(s);
			lua_pushnumber(L, load_u8(s));
			s++;
			break;
		case 's':
			CHECK_SIZE(s + 1);
			lua_pushnumber(L, load_u16(s));
			s += 2;
			break;
		case 'n':
			CHECK_SIZE(s + 1);
			lua_pushnumber(L, ntohs(load_u16(s)));
			s += 2;
			break;
		case 'i':
			CHECK_SIZE(s + 3);
			lua_pushnumber(L, load_u32(s));
			s += 4;
			break;
		case 'N':
			CHECK_SIZE(s + 3);
			lua_pushnumber(L, ntohl(load_u32(s)));
			s += 4;
			break;
		case 'l':
			CHECK_SIZE(s + 7);
			luaL_pushuint64(L, load_u64(s));
			s += 8;
			break;
		case 'q':
			CHECK_SIZE(s + 7);
			luaL_pushuint64(L, bswap_u64(load_u64(s)));
			s += 8;
			break;
		case 'd':
			CHECK_SIZE(s + 7);
			lua_pushnumber(L, load_double(s));
			s += 8;
			break;
		case 'f':
			CHECK_SIZE(s + 3);
			lua_pushnumber(L, load_float(s));
			s += 4;
			break;
		case 'a':
		case 'A': /* The rest of the data is a Lua string. */
			lua_pushlstring(L, s, end - s);
			s = end;
			break;
		default:
			diag_set(IllegalParams,
				 "pickle.unpack: unsupported "
				 "format specifier '%c'", *f);
			luaT_error(L);
		}
		f++;
	}

	assert(s <= end);

	if (s != end) {
		diag_set(IllegalParams,
			 "pickle.unpack('%s'): too many bytes: "
			 "unpacked %td, total %zd", format, s - str,
			 str_size);
		luaT_error(L);
	}

	return lua_gettop(L) - save_stacksize;

#undef CHECK_SIZE
}

void
tarantool_lua_pickle_init(struct lua_State *L)
{
	static const luaL_Reg picklelib[] = {
		{"pack", lbox_pack},
		{"unpack", lbox_unpack},
		{ NULL, NULL}
	};

	luaT_newmodule(L, "pickle", picklelib);
	lua_pop(L, 1);
}
