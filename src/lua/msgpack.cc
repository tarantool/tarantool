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

extern "C" {
#if defined(LUAJIT)
#include <lj_ctype.h>
#endif /* defined(LUAJIT) */
#include <lauxlib.h> /* struct luaL_error */
} /* extern "C" */

#include <msgpuck/msgpuck.h>
#include <tbuf.h>
#include <fiber.h>
#include "small/region.h"

struct luaL_serializer *luaL_msgpack_default = NULL;

static int
luamp_encode_extension_default(struct lua_State *L, int idx, struct tbuf *buf);

static void
luamp_decode_extension_default(struct lua_State *L, const char **data);

static luamp_encode_extension_f luamp_encode_extension =
		luamp_encode_extension_default;
static luamp_decode_extension_f luamp_decode_extension =
		luamp_decode_extension_default;

void
luamp_encode_array(struct luaL_serializer *cfg, struct tbuf *buf, uint32_t size)
{
	(void) cfg;
	assert(mp_sizeof_array(size) <= 5);
	tbuf_ensure(buf, 5 + size);
	char *data = mp_encode_array(buf->data + buf->size, size);
	assert(data <= buf->data + buf->capacity);
	buf->size = data - buf->data;
}

void
luamp_encode_map(struct luaL_serializer *cfg, struct tbuf *buf, uint32_t size)
{
	(void) cfg;
	assert(mp_sizeof_map(size) <= 5);
	tbuf_ensure(buf, 5 + size);

	char *data = mp_encode_map(buf->data + buf->size, size);
	assert(data <= buf->data + buf->capacity);
	buf->size = data - buf->data;
}

void
luamp_encode_uint(struct luaL_serializer *cfg, struct tbuf *buf, uint64_t num)
{
	(void) cfg;
	assert(mp_sizeof_uint(num) <= 9);
	tbuf_ensure(buf, 9);

	char *data = mp_encode_uint(buf->data + buf->size, num);
	assert(data <= buf->data + buf->capacity);
	buf->size = data - buf->data;
}

void
luamp_encode_int(struct luaL_serializer *cfg, struct tbuf *buf, int64_t num)
{
	(void) cfg;
	assert(mp_sizeof_int(num) <= 9);
	tbuf_ensure(buf, 9);

	char *data = mp_encode_int(buf->data + buf->size, num);
	assert(data <= buf->data + buf->capacity);
	buf->size = data - buf->data;
}

void
luamp_encode_float(struct luaL_serializer *cfg, struct tbuf *buf, float num)
{
	(void) cfg;
	assert(mp_sizeof_float(num) <= 5);
	tbuf_ensure(buf, 5);

	char *data = mp_encode_float(buf->data + buf->size, num);
	assert(data <= buf->data + buf->capacity);
	buf->size = data - buf->data;
}

void
luamp_encode_double(struct luaL_serializer *cfg, struct tbuf *buf, double num)
{
	(void) cfg;
	assert(mp_sizeof_double(num) <= 9);
	tbuf_ensure(buf, 9);

	char *data = mp_encode_double(buf->data + buf->size, num);
	assert(data <= buf->data + buf->capacity);
	buf->size = data - buf->data;
}

void
luamp_encode_str(struct luaL_serializer *cfg, struct tbuf *buf,
		 const char *str, uint32_t len)
{
	(void) cfg;
	assert(mp_sizeof_str(len) <= 5 + len);
	tbuf_ensure(buf, 5 + len);

	char *data = mp_encode_str(buf->data + buf->size, str, len);
	assert(data <= buf->data + buf->capacity);
	buf->size = data - buf->data;
}

void
luamp_encode_nil(struct luaL_serializer *cfg, struct tbuf *buf)
{
	(void) cfg;
	assert(mp_sizeof_nil() <= 1);
	tbuf_ensure(buf, 1);

	char *data = mp_encode_nil(buf->data + buf->size);
	assert(data <= buf->data + buf->capacity);
	buf->size = data - buf->data;
}

void
luamp_encode_bool(struct luaL_serializer *cfg, struct tbuf *buf, bool val)
{
	(void) cfg;
	assert(mp_sizeof_bool(val) <= 1);
	tbuf_ensure(buf, 1);

	char *data = mp_encode_bool(buf->data + buf->size, val);
	assert(data <= buf->data + buf->capacity);
	buf->size = data - buf->data;
}

static int
luamp_encode_extension_default(struct lua_State *L, int idx, struct tbuf *b)
{
	(void) L;
	(void) idx;
	(void) b;
	return -1;
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
luamp_encode_r(struct lua_State *L, struct luaL_serializer *cfg, struct tbuf *b,
	       int level)
{
	int index = lua_gettop(L);

	struct luaL_field field;
	/* Detect field type */
	luaL_tofield(L, cfg, index, &field);
	if (field.type == MP_EXT) {
		/* Run trigger if type can't be encoded */
		if (luamp_encode_extension(L, index, b) == 0)
			return; /* Value has been packed by the trigger */
		/* Try to convert value to serializable type */
		luaL_convertfield(L, cfg, index, &field);
	}
	switch (field.type) {
	case MP_UINT:
		return luamp_encode_uint(cfg, b, field.ival);
	case MP_STR:
	case MP_BIN:
		return luamp_encode_str(cfg, b, field.sval.data, field.sval.len);
	case MP_INT:
		return luamp_encode_int(cfg, b, field.ival);
	case MP_FLOAT:
		return luamp_encode_float(cfg, b, field.fval);
	case MP_DOUBLE:
		return luamp_encode_double(cfg, b, field.dval);
	case MP_BOOL:
		return luamp_encode_bool(cfg, b, field.bval);
	case MP_NIL:
		return luamp_encode_nil(cfg, b);
	case MP_MAP:
		/* Map */
		if (level >= cfg->encode_max_depth)
			return luamp_encode_nil(cfg, b); /* Limit nested maps */
		luamp_encode_map(cfg, b, field.size);
		lua_pushnil(L);  /* first key */
		while (lua_next(L, index) != 0) {
			lua_pushvalue(L, -2);
			luamp_encode_r(L, cfg, b, level + 1);
			lua_pop(L, 1);
			luamp_encode_r(L, cfg, b, level + 1);
			lua_pop(L, 1);
		}
		return;
	case MP_ARRAY:
		/* Array */
		if (level >= cfg->encode_max_depth)
			return luamp_encode_nil(cfg, b); /* Limit nested arrays */
		luamp_encode_array(cfg, b, field.size);
		for (uint32_t i = 0; i < field.size; i++) {
			lua_rawgeti(L, index, i + 1);
			luamp_encode_r(L, cfg, b, level + 1);
			lua_pop(L, 1);
		}
		return;
	case MP_EXT:
		/* handled by luaL_convertfield */
		assert(false);
		return;
	}
}

void
luamp_encode(struct lua_State *L, struct luaL_serializer *cfg, struct tbuf *b,
	     int index)
{
	int top = lua_gettop(L);
	if (index < 0)
		index = top + index + 1;

	bool on_top = (index == top);
	if (!on_top) {
		lua_pushvalue(L, index); /* copy a value to the stack top */
	}

	luamp_encode_r(L, cfg, b, 0);

	if (!on_top) {
		lua_remove(L, top + 1); /* remove a value copy */
	}
}

void
luamp_decode(struct lua_State *L, struct luaL_serializer *cfg,
	     const char **data)
{
	double d;
	switch (mp_typeof(**data)) {
	case MP_UINT:
		luaL_pushnumber64(L, mp_decode_uint(data));
		break;
	case MP_INT:
		luaL_pushinumber64(L, mp_decode_int(data));
		break;
	case MP_FLOAT:
		d = mp_decode_float(data);
		luaL_checkfinite(L, cfg, d);
		lua_pushnumber(L, d);
		return;
	case MP_DOUBLE:
		d = mp_decode_double(data);
		luaL_checkfinite(L, cfg, d);
		lua_pushnumber(L, d);
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
		luaL_pushnull(L);
		return;
	case MP_ARRAY:
	{
		uint32_t size = mp_decode_array(data);
		lua_createtable(L, size, 0);
		if (cfg->decode_save_metatables)
			luaL_setarrayhint(L, -1);
		for (uint32_t i = 0; i < size; i++) {
			luamp_decode(L, cfg, data);
			lua_rawseti(L, -2, i + 1);
		}
		return;
	}
	case MP_MAP:
	{
		uint32_t size = mp_decode_map(data);
		lua_createtable(L, 0, size);
		if (cfg->decode_save_metatables)
			luaL_setmaphint(L, -1);
		for (uint32_t i = 0; i < size; i++) {
			luamp_decode(L, cfg, data);
			luamp_decode(L, cfg, data);
			lua_settable(L, -3);
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

	struct luaL_serializer *cfg = luaL_checkserializer(L);

	RegionGuard region_guard(&fiber()->gc);
	struct tbuf *buf = tbuf_new(&fiber()->gc);
	luamp_encode_r(L, cfg, buf, 0);
	lua_pushlstring(L, buf->data, buf->size);
	return 1;
}

static int
lua_msgpack_decode(lua_State *L)
{
	int index = lua_gettop(L);
	if (index != 2 && index != 1 && lua_type(L, 1) != LUA_TSTRING)
		return luaL_error(L, "msgpack.decode: a Lua string expected");

	size_t data_len;
	uint32_t offset = index > 1 ? lua_tointeger(L, 2) - 1 : 0;
	const char *data = lua_tolstring(L, 1, &data_len);
	if (offset >= data_len)
		luaL_error(L, "msgpack.decode: offset is out of bounds");
	const char *end = data + data_len;

	const char *b = data + offset;
	if (mp_check(&b, end))
		return luaL_error(L, "msgpack.decode: invalid MsgPack");

	struct luaL_serializer *cfg = luaL_checkserializer(L);

	b = data + offset;
	luamp_decode(L, cfg, &b);
	lua_pushinteger(L, b - data + 1);
	return 2;
}

static int
lua_msgpack_new(lua_State *L);

const luaL_reg msgpacklib[] = {
	{ "encode", lua_msgpack_encode },
	{ "decode", lua_msgpack_decode },
	{ "new",    lua_msgpack_new },
	{ NULL, NULL}
};

static int
lua_msgpack_new(lua_State *L)
{
	struct luaL_serializer *parent = luaL_checkserializer(L);
	luaL_newserializer(L, msgpacklib, parent);
	return 1;
}

LUALIB_API int
luaopen_msgpack(lua_State *L)
{
	luaL_msgpack_default = luaL_newserializer(L, msgpacklib, NULL);
	luaL_register_module(L, "msgpack", NULL);
	return 1;
}
