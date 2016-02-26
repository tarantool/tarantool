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
		lbox_error(L);
	}
	(void) memcpy(to, ptr, size);
}

static int
lbox_pack(struct lua_State *L)
{
	const char *format = luaL_checkstring(L, 1);
	/* first arg comes second */
	int i = 2;
	int nargs = lua_gettop(L);
	size_t size;
	const char *str;

	struct region *buf = &fiber()->gc;
	/*
	 * XXX: this code leaks region memory in case of any
	 * Lua memory. In absence of external unwind, Lua C API
	 * makes it painfully difficult to clean up resources
	 * properly in case of error.
	 * Hope for the best, fiber_gc() will be called eventually.
	 */
	size_t used = region_used(buf);

	struct luaL_field field;
	double dbl;
	float flt;
	while (*format) {
		if (i > nargs)
			luaL_error(L, "pickle.pack: argument count does not match "
				   "the format");
		luaL_checkfield(L, luaL_msgpack_default, i, &field);
		switch (*format) {
		case 'B':
		case 'b':
			/* signed and unsigned 8-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "pickle.pack: expected 8-bit int");

			luaL_region_dup(L, buf, &field.ival, sizeof(uint8_t));
			break;
		case 'S':
		case 's':
			/* signed and unsigned 16-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "pickle.pack: expected 16-bit int");

			luaL_region_dup(L, buf, &field.ival, sizeof(uint16_t));
			break;
		case 'n':
			/* signed and unsigned 16-bit big endian integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "pickle.pack: expected 16-bit int");

			field.ival = (uint16_t) htons((uint16_t) field.ival);
			luaL_region_dup(L, buf, &field.ival, sizeof(uint16_t));
			break;
		case 'I':
		case 'i':
			/* signed and unsigned 32-bit integers */
			if (field.type != MP_UINT && field.ival != MP_INT)
				luaL_error(L, "pickle.pack: expected 32-bit int");

			luaL_region_dup(L, buf, &field.ival, sizeof(uint32_t));
			break;
		case 'N':
			/* signed and unsigned 32-bit big endian integers */
			if (field.type != MP_UINT && field.ival != MP_INT)
				luaL_error(L, "pickle.pack: expected 32-bit int");

			field.ival = htonl(field.ival);
			luaL_region_dup(L, buf, &field.ival, sizeof(uint32_t));
			break;
		case 'L':
		case 'l':
			/* signed and unsigned 64-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "pickle.pack: expected 64-bit int");

			luaL_region_dup(L, buf, &field.ival, sizeof(uint64_t));
			break;
		case 'Q':
		case 'q':
			/* signed and unsigned 64-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "pickle.pack: expected 64-bit int");

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
			/* A sequence of bytes */
			str = luaL_checklstring(L, i, &size);
			luaL_region_dup(L, buf, str, size);
			break;
		default:
			luaL_error(L, "pickle.pack: unsupported pack "
				   "format specifier '%c'", *format);
		}
		i++;
		format++;
	}

	size_t len = region_used(buf) - used;
	const char *res = (char *) region_join(buf, len);
	if (res == NULL) {
		region_truncate(buf, used);
		diag_set(OutOfMemory, size, "region", "region_join");
		lbox_error(L);
	}
	lua_pushlstring(L, res, len);
	region_truncate(buf, used);
	return 1;
}


static int
lbox_unpack(struct lua_State *L)
{
	size_t format_size = 0;
	const char *format = luaL_checklstring(L, 1, &format_size);
	const char *f = format;

	size_t str_size = 0;
	const char *str =  luaL_checklstring(L, 2, &str_size);
	const char *end = str + str_size;
	const char *s = str;

	int save_stacksize = lua_gettop(L);

#define CHECK_SIZE(cur) if (unlikely((cur) >= end)) {	                \
	luaL_error(L, "pickle.unpack('%c'): got %d bytes (expected: %d+)",	\
		   *f, (int) (end - str), (int) 1 + ((cur) - str));	\
}
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
			luaL_error(L, "pickle.unpack: unsupported "
				   "format specifier '%c'", *f);
		}
		f++;
	}

	assert(s <= end);

	if (s != end) {
		luaL_error(L, "pickle.unpack('%s'): too many bytes: "
			   "unpacked %d, total %d",
			   format, s - str, str_size);
	}

	return lua_gettop(L) - save_stacksize;

#undef CHECK_SIZE
}

void
tarantool_lua_pickle_init(struct lua_State *L)
{
	const luaL_reg picklelib[] = {
		{"pack", lbox_pack},
		{"unpack", lbox_unpack},
		{ NULL, NULL}
	};

	luaL_register_module(L, "pickle", picklelib);
	lua_pop(L, 1);
}
