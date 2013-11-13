/*
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

#include "lua/msgpack.h"

#include "lua/utils.h"
#if defined(LUAJIT)
#include <lj_ctype.h>
#endif /* defined(LUAJIT) */

static void
luamp_encode_extension_default(struct lua_State *L, int idx, luaL_Buffer *b);

static void
luamp_decode_extension_default(struct lua_State *L, const char **data);

static luamp_encode_extension_f luamp_encode_extension =
		luamp_encode_extension_default;
static luamp_decode_extension_f luamp_decode_extension =
		luamp_decode_extension_default;

void
luamp_encode_array(luaL_Buffer *buf, uint32_t size)
{
	char tmp[5];
	assert(mp_sizeof_array(size) <= sizeof(tmp));
	char *end = mp_encode_array(tmp, size);
	luaL_addlstring(buf, tmp, end - tmp);
}

void
luamp_encode_map(luaL_Buffer *buf, uint32_t size)
{
	char tmp[5];
	assert(mp_sizeof_map(size) <= sizeof(tmp));
	char *end = mp_encode_map(tmp, size);
	luaL_addlstring(buf, tmp, end - tmp);
}

void
luamp_encode_uint(luaL_Buffer *buf, uint64_t num)
{
	char tmp[9];
	assert(mp_sizeof_uint(num) <= sizeof(tmp));
	char *end = mp_encode_uint(tmp, num);
	luaL_addlstring(buf, tmp, end - tmp);
}

void
luamp_encode_int(luaL_Buffer *buf, int64_t num)
{
	char tmp[9];
	assert(mp_sizeof_int(num) <= sizeof(tmp));
	char *end = mp_encode_int(tmp, num);
	luaL_addlstring(buf, tmp, end - tmp);
}

void
luamp_encode_float(luaL_Buffer *buf, float num)
{
	char tmp[5];
	assert(mp_sizeof_float(num) <= sizeof(tmp));
	char *end = mp_encode_float(tmp, num);
	luaL_addlstring(buf, tmp, end - tmp);
}

void
luamp_encode_double(luaL_Buffer *buf, double num)
{
	char tmp[9];
	assert(mp_sizeof_double(num) <= sizeof(tmp));
	char *end = mp_encode_double(tmp, num);
	luaL_addlstring(buf, tmp, end - tmp);
}

void
luamp_encode_str(luaL_Buffer *buf, const char *str, uint32_t len)
{
	char tmp[5];
	assert(mp_sizeof_str(len) <= sizeof(tmp) + len);
	char *end = mp_encode_strl(tmp, len);
	luaL_addlstring(buf, tmp, end - tmp);
	luaL_addlstring(buf, str, len);
}

void
luamp_encode_nil(luaL_Buffer *buf)
{
	char tmp;
	mp_encode_nil(&tmp);
	luaL_addchar(buf, tmp);
}

void
luamp_encode_bool(luaL_Buffer *buf, bool val)
{
	char tmp;
	mp_encode_bool(&tmp, val);
	luaL_addchar(buf, tmp);
}

static void
luamp_encode_extension_default(struct lua_State *L, int idx, luaL_Buffer *b)
{
	(void) b;
	luaL_error(L, "msgpack.encode: can not encode Lua type '%s'",
		   lua_typename(L, lua_type(L, idx)));
}


void
luamp_set_encode_extension(luamp_encode_extension_f handler)
{
	if (handler == NULL) {
		luamp_encode_extension = luamp_encode_extension_default;
	} else {
		luamp_encode_extension = handler;
	}
}

static void
luamp_decode_extension_default(struct lua_State *L, const char **data)
{
	luaL_error(L, "msgpack.decode: unsupported extension: %u",
		   (unsigned char) **data);
}


void
luamp_set_decode_extension(luamp_decode_extension_f handler)
{
	if (handler == NULL) {
		luamp_decode_extension = luamp_decode_extension_default;
	} else {
		luamp_decode_extension = handler;
	}
}

static void
luamp_encode_r(struct lua_State *L, luaL_Buffer *b, int level)
{
	int index = lua_gettop(L);

	struct luaL_field field;
	luaL_tofield(L, index, &field);
	switch (field.type) {
	case MP_UINT:
		return luamp_encode_uint(b, field.ival);
	case MP_STR:
	case MP_BIN:
		return luamp_encode_str(b, field.sval.data, field.sval.len);
	case MP_INT:
		return luamp_encode_int(b, field.ival);
	case MP_FLOAT:
		return luamp_encode_float(b, field.fval);
	case MP_DOUBLE:
		return luamp_encode_double(b, field.dval);
	case MP_BOOL:
		return luamp_encode_bool(b, field.bval);
	case MP_NIL:
		return luamp_encode_nil(b);
	case MP_MAP:
		/* Map */
		if (level >= LUA_MP_MAXNESTING)
			return luamp_encode_nil(b); /* Limit nested maps */
		luamp_encode_map(b, field.size);
		lua_pushnil(L);  /* first key */
		while (lua_next(L, index) != 0) {
			lua_pushvalue(L, -2);
			luamp_encode_r(L, b, level + 1);
			lua_pop(L, 1);
			luamp_encode_r(L, b, level + 1);
			lua_pop(L, 1);
		}
		return;
	case MP_ARRAY:
		/* Array */
		if (level >= LUA_MP_MAXNESTING)
			return luamp_encode_nil(b); /* Limit nested arrays */
		luamp_encode_array(b, field.max);
		for (uint32_t i = 0; i < field.max; i++) {
			lua_rawgeti(L, index, i + 1);
			luamp_encode_r(L, b, level + 1);
			lua_pop(L, 1);
		}
		return;
	case MP_EXT:
		luamp_encode_extension(L, index, b);
		return;
	}
}

void
luamp_encode(struct lua_State *L, luaL_Buffer *b, int index)
{
	int top = lua_gettop(L);
	if (index < 0)
		index = top + index + 1;

	bool on_top = (index == top);
	if (!on_top) {
		lua_pushvalue(L, index); /* copy a value to the stack top */
	}

	luamp_encode_r(L, b, 0);

	if (!on_top) {
		lua_remove(L, top + 1); /* remove a value copy */
	}
}

void
luamp_decode(struct lua_State *L, const char **data)
{
	switch (mp_typeof(**data)) {
	case MP_UINT:
	{
		uint64_t val = mp_decode_uint(data);
#if defined(LUAJIT)
		if (val <= UINT32_MAX) {
			lua_pushinteger(L, val);
		} else {
			*(uint64_t *) luaL_pushcdata(L, CTID_UINT64,
						     sizeof(uint64_t)) = val;
		}
#else /* !defined(LUAJIT) */
		lua_pushinteger(L, val);
#endif /* defined(LUAJIT) */
		return;
	}
	case MP_INT:
	{
		int64_t val = mp_decode_int(data);
#if defined(LUAJIT)
		if (val >= INT32_MIN && val <= INT32_MAX) {
			lua_pushinteger(L, val);
		} else {
			*(int64_t *) luaL_pushcdata(L, CTID_INT64,
						     sizeof(int64_t)) = val;
		}
#else /* !defined(LUAJIT) */
		lua_pushinteger(L, val);
#endif /* defined(LUAJIT) */
		return;
	}
	case MP_FLOAT:
		lua_pushnumber(L, mp_decode_float(data));
		return;
	case MP_DOUBLE:
		lua_pushnumber(L, mp_decode_double(data));
		return;
	case MP_STR:
	{
		uint32_t len = 0;
		const char *str = mp_decode_str(data, &len);
		lua_pushlstring(L, str, len);
		return;
	}
	case MP_BIN:
	{
		uint32_t len = 0;
		const char *str = mp_decode_bin(data, &len);
		lua_pushlstring(L, str, len);
		return;
	}
	case MP_BOOL:
		lua_pushboolean(L, mp_decode_bool(data));
		return;
	case MP_NIL:
		mp_decode_nil(data);
		lua_pushnil(L);
		return;
	case MP_ARRAY:
	{
		bool maybe_map = false;
		uint32_t size = mp_decode_array(data);
		lua_createtable(L, size, 0);
		for (uint32_t i = 0; i < size; i++) {
			luamp_decode(L, data);
			if (lua_type(L, -1) == LUA_TNIL)
				maybe_map = true;
			lua_rawseti(L, -2, i + 1);
		}
		if (luaL_getn(L, -1) != size) {
			/*
			 * Lua does not support NIL as table values.
			 * Add a NULL userdata to emulate NIL.
			 */
			lua_pushinteger(L, size);
			lua_pushlightuserdata(L, NULL);
			lua_rawset(L, -3);
		}

		if (maybe_map) {
			/* Add a serializer hint to disambiguate */
			lua_newtable(L);
			lua_pushstring(L, "_serializer_type");
			lua_pushstring(L, "array");
			lua_settable(L, -3);
			lua_setmetatable(L, -2);
		}
		return;
	}
	case MP_MAP:
	{
		bool maybe_array = true;
		uint32_t size = mp_decode_map(data);
		lua_createtable(L, 0, size);
		for (uint32_t i = 0; i < size; i++) {
			luamp_decode(L, data);
			if (lua_type(L, -1) != LUA_TNUMBER)
				maybe_array = false;
			luamp_decode(L, data);
			if (lua_type(L, -1) == LUA_TNIL) {
				/*
				 * Lua does not support NIL as table values.
				 * Add a NULL userdata to emulate NIL.
				 */
				lua_pop(L, 1);
				lua_pushlightuserdata(L, NULL);
			}
			lua_settable(L, -3);
		}

		if (maybe_array) {
			/* Add a serializer hint to disambiguate  */
			lua_newtable(L);
			lua_pushstring(L, "_serializer_type");
			lua_pushstring(L, "map");
			lua_settable(L, -3);
			lua_setmetatable(L, -2);
		}
		return;
	}
	case MP_EXT:
		luamp_decode_extension(L, data);
		break;
	}
}


static int
lua_msgpack_encode(lua_State *L)
{
	int index = lua_gettop(L);
	if (index != 1)
		luaL_error(L, "msgpack.encode: a Lua object expected");

	luaL_Buffer b;
	luaL_buffinit(L, &b);
	luamp_encode_r(L, &b, 0);
	luaL_pushresult(&b);
	return 1;
}

static int
lua_msgpack_decode(lua_State *L)
{
	int index = lua_gettop(L);
	if (index != 1 || lua_type(L, index) != LUA_TSTRING)
		return luaL_error(L, "msgpack.decode: a Lua string expected");

	size_t data_len;
	const char *data = lua_tolstring(L, index, &data_len);
	const char *end = data + data_len;

	const char *b = data;
	if (!mp_check(&b, end) || b != end)
		return luaL_error(L, "msgpack.decode: invalid MsgPack");

	b = data;
	luamp_decode(L, &b);
	return 1;
}

LUALIB_API int
luaopen_msgpack(lua_State *L)
{
	const luaL_reg msgpacklib[] = {
		{ "encode", lua_msgpack_encode },
		{ "loads",  lua_msgpack_encode },
		{ "decode", lua_msgpack_decode },
		{ "dumps",  lua_msgpack_decode },
		{ NULL, NULL}
	};

	luaL_openlib(L, "msgpack", msgpacklib, 0);
	return 1;
}
