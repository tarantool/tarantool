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
#include "lua/msgpack.h"
#include "mpstream/mpstream.h"
#include "lua/utils.h"
#include "lua/serializer.h"

#if defined(LUAJIT)
#include <lj_ctype.h>
#endif /* defined(LUAJIT) */
#include <lauxlib.h> /* struct luaL_error */

#include <msgpuck.h>
#include <small/region.h>
#include <small/ibuf.h>

#include "lua/decimal.h" /* lua_pushdecimal() */
#include "lib/core/decimal.h" /* decimal_unpack() */
#include "lib/uuid/mp_uuid.h" /* mp_decode_uuid() */
#include "lib/core/mp_extension_types.h"

#include "cord_buf.h"
#include <fiber.h>

void
luamp_error(void *error_ctx)
{
	struct lua_State *L = (struct lua_State *) error_ctx;
	luaL_error(L, diag_last_error(diag_get())->errmsg);
}

struct luaL_serializer *luaL_msgpack_default = NULL;

static enum mp_type
luamp_encode_extension_default(struct lua_State *L, int idx,
			       struct mpstream *stream);

static void
luamp_decode_extension_default(struct lua_State *L, const char **data);

static luamp_encode_extension_f luamp_encode_extension =
		luamp_encode_extension_default;
static luamp_decode_extension_f luamp_decode_extension =
		luamp_decode_extension_default;

static enum mp_type
luamp_encode_extension_default(struct lua_State *L, int idx,
			       struct mpstream *stream)
{
	(void) L;
	(void) idx;
	(void) stream;
	return MP_EXT;
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
	int8_t ext_type;
	mp_decode_extl(data, &ext_type);
	luaL_error(L, "msgpack.decode: unsupported extension: %d", ext_type);
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

enum mp_type
luamp_encode_r(struct lua_State *L, struct luaL_serializer *cfg,
	       const struct serializer_opts *opts, struct mpstream *stream,
	       struct luaL_field *field, int level)
{
	int top = lua_gettop(L);
	enum mp_type type;

restart: /* used by MP_EXT of unidentified subtype */
	switch (field->type) {
	case MP_UINT:
		mpstream_encode_uint(stream, field->ival);
		return MP_UINT;
	case MP_STR:
		mpstream_encode_strn(stream, field->sval.data, field->sval.len);
		return MP_STR;
	case MP_BIN:
		mpstream_encode_strn(stream, field->sval.data, field->sval.len);
		return MP_BIN;
	case MP_INT:
		mpstream_encode_int(stream, field->ival);
		return MP_INT;
	case MP_FLOAT:
		mpstream_encode_float(stream, field->fval);
		return MP_FLOAT;
	case MP_DOUBLE:
		mpstream_encode_double(stream, field->dval);
		return MP_DOUBLE;
	case MP_BOOL:
		mpstream_encode_bool(stream, field->bval);
		return MP_BOOL;
	case MP_NIL:
		mpstream_encode_nil(stream);
		return MP_NIL;
	case MP_MAP:
		/* Map */
		if (level >= cfg->encode_max_depth) {
			if (! cfg->encode_deep_as_nil) {
				return luaL_error(L, "Too high nest level - %d",
						  level + 1);
			}
			mpstream_encode_nil(stream); /* Limit nested maps */
			return MP_NIL;
		}
		mpstream_encode_map(stream, field->size);
		lua_pushnil(L);  /* first key */
		while (lua_next(L, top) != 0) {
			lua_pushvalue(L, -2); /* push a copy of key to top */
			if (luaL_tofield(L, cfg, opts, lua_gettop(L), field) < 0)
				return luaT_error(L);
			luamp_encode_r(L, cfg, opts, stream, field, level + 1);
			lua_pop(L, 1); /* pop a copy of key */
			if (luaL_tofield(L, cfg, opts, lua_gettop(L), field) < 0)
				return luaT_error(L);
			luamp_encode_r(L, cfg, opts, stream, field, level + 1);
			lua_pop(L, 1); /* pop value */
		}
		assert(lua_gettop(L) == top);
		return MP_MAP;
	case MP_ARRAY:
		/* Array */
		if (level >= cfg->encode_max_depth) {
			if (! cfg->encode_deep_as_nil) {
				return luaL_error(L, "Too high nest level - %d",
						  level + 1);
			}
			mpstream_encode_nil(stream); /* Limit nested arrays */
			return MP_NIL;
		}
		uint32_t size = field->size;
		mpstream_encode_array(stream, size);
		for (uint32_t i = 0; i < size; i++) {
			lua_rawgeti(L, top, i + 1);
			if (luaL_tofield(L, cfg, opts, top + 1, field) < 0)
				return luaT_error(L);
			luamp_encode_r(L, cfg, opts, stream, field, level + 1);
			lua_pop(L, 1);
		}
		assert(lua_gettop(L) == top);
		return MP_ARRAY;
	case MP_EXT:
		switch (field->ext_type) {
		case MP_DECIMAL:
			mpstream_encode_decimal(stream, field->decval);
			break;
		case MP_UUID:
			mpstream_encode_uuid(stream, field->uuidval);
			break;
		case MP_ERROR:
			return luamp_encode_extension(L, top, stream);
		default:
			/* Run trigger if type can't be encoded */
			type = luamp_encode_extension(L, top, stream);
			if (type != MP_EXT)
				return type; /* Value has been packed by the trigger */
			/* Try to convert value to serializable type */
			luaL_convertfield(L, cfg, top, field);
			/* handled by luaL_convertfield */
			assert(field->type != MP_EXT);
			assert(lua_gettop(L) == top);
			goto restart;
		}
	}
	return MP_EXT;
}

enum mp_type
luamp_encode(struct lua_State *L, struct luaL_serializer *cfg,
	     const struct serializer_opts *opts, struct mpstream *stream,
	     int index)
{
	int top = lua_gettop(L);
	if (index < 0)
		index = top + index + 1;

	bool on_top = (index == top);
	if (!on_top) {
		lua_pushvalue(L, index); /* copy a value to the stack top */
	}

	struct luaL_field field;
	if (luaL_tofield(L, cfg, opts, lua_gettop(L), &field) < 0)
		return luaT_error(L);
	enum mp_type top_type = luamp_encode_r(L, cfg, opts, stream, &field, 0);

	if (!on_top) {
		lua_remove(L, top + 1); /* remove a value copy */
	}

	return top_type;
}

void
luamp_decode(struct lua_State *L, struct luaL_serializer *cfg,
	     const char **data)
{
	double d;
	switch (mp_typeof(**data)) {
	case MP_UINT:
		luaL_pushuint64(L, mp_decode_uint(data));
		break;
	case MP_INT:
		luaL_pushint64(L, mp_decode_int(data));
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
		for (uint32_t i = 0; i < size; i++) {
			luamp_decode(L, cfg, data);
			lua_rawseti(L, -2, i + 1);
		}
		if (cfg->decode_save_metatables)
			luaL_setarrayhint(L, -1);
		return;
	}
	case MP_MAP:
	{
		uint32_t size = mp_decode_map(data);
		lua_createtable(L, 0, size);
		for (uint32_t i = 0; i < size; i++) {
			luamp_decode(L, cfg, data);
			luamp_decode(L, cfg, data);
			lua_settable(L, -3);
		}
		if (cfg->decode_save_metatables)
			luaL_setmaphint(L, -1);
		return;
	}
	case MP_EXT:
	{
		int8_t ext_type;
		const char *svp = *data;
		uint32_t len = mp_decode_extl(data, &ext_type);
		switch (ext_type) {
		case MP_DECIMAL:
		{
			decimal_t *dec = lua_pushdecimal(L);
			dec = decimal_unpack(data, len, dec);
			if (dec == NULL)
				goto ext_decode_err;
			return;
		}
		case MP_UUID:
		{
			struct tt_uuid *uuid = luaL_pushuuid(L);
			*data = svp;
			uuid = mp_decode_uuid(data, uuid);
			if (uuid == NULL)
				goto ext_decode_err;
			return;
		}
		default:
			/* reset data to the extension header */
			*data = svp;
			luamp_decode_extension(L, data);
			break;
		}
		break;
	}
	}
	return;
ext_decode_err:
	lua_pop(L, -1);
	luaL_error(L, "msgpack.decode: invalid MsgPack");
}


static int
lua_msgpack_encode(lua_State *L)
{
	int index = lua_gettop(L);
	if (index < 1)
		return luaL_error(L, "msgpack.encode: a Lua object expected");

	struct ibuf *buf;
	if (index > 1) {
		buf = luaT_toibuf(L, 2);
		if (buf == NULL) {
			return luaL_error(L, "msgpack.encode: argument 2 "
					  "must be of type 'struct ibuf'");
		}
	} else {
		buf = cord_ibuf_take();
	}
	size_t used = ibuf_used(buf);

	struct luaL_serializer *cfg = luaL_checkserializer(L);

	struct mpstream stream;
	mpstream_init(&stream, buf, ibuf_reserve_cb, ibuf_alloc_cb,
		      luamp_error, L);

	luamp_encode(L, cfg, NULL, &stream, 1);
	mpstream_flush(&stream);

	if (index > 1) {
		lua_pushinteger(L, ibuf_used(buf) - used);
	} else {
		lua_pushlstring(L, buf->buf, ibuf_used(buf));
		cord_ibuf_drop(buf);
	}
	return 1;
}

static int
lua_msgpack_decode_cdata(lua_State *L, bool check)
{
	const char *data;
	uint32_t cdata_type;
	if (luaL_checkconstchar(L, 1, &data, &cdata_type) != 0) {
		return luaL_error(L, "msgpack.decode: "
				  "a Lua string or 'char *' expected");
	}
	if (check) {
		ptrdiff_t data_len = luaL_checkinteger(L, 2);
		if (data_len < 0) {
			return luaL_error(L, "msgpack.decode: size can't be "\
					  "negative");
		}
		const char *p = data;
		if (mp_check(&p, data + data_len) != 0)
			return luaL_error(L, "msgpack.decode: invalid MsgPack");
	}
	struct luaL_serializer *cfg = luaL_checkserializer(L);
	luamp_decode(L, cfg, &data);
	*(const char **)luaL_pushcdata(L, cdata_type) = data;
	return 2;
}

static int
lua_msgpack_decode_string(lua_State *L, bool check)
{
	ptrdiff_t offset = 0;
	size_t data_len;
	const char *data = lua_tolstring(L, 1, &data_len);
	if (lua_gettop(L) > 1) {
		offset = luaL_checkinteger(L, 2) - 1;
		if (offset < 0 || (size_t)offset >= data_len)
			return luaL_error(L, "msgpack.decode: "
					  "offset is out of bounds");
	}
	if (check) {
		const char *p = data + offset;
		if (mp_check(&p, data + data_len) != 0)
			return luaL_error(L, "msgpack.decode: invalid MsgPack");
	}
	struct luaL_serializer *cfg = luaL_checkserializer(L);
	const char *p = data + offset;
	luamp_decode(L, cfg, &p);
	lua_pushinteger(L, p - data + 1);
	return 2;
}

static int
lua_msgpack_decode(lua_State *L)
{
	int index = lua_gettop(L);
	int type = index >= 1 ? lua_type(L, 1) : LUA_TNONE;
	switch (type) {
	case LUA_TCDATA:
		return lua_msgpack_decode_cdata(L, true);
	case LUA_TSTRING:
		return lua_msgpack_decode_string(L, true);
	default:
		return luaL_error(L, "msgpack.decode: "
				  "a Lua string or 'char *' expected");
	}
}

static int
lua_msgpack_decode_unchecked(lua_State *L)
{
	int index = lua_gettop(L);
	int type = index >= 1 ? lua_type(L, 1) : LUA_TNONE;
	switch (type) {
	case LUA_TCDATA:
		return lua_msgpack_decode_cdata(L, false);
	case LUA_TSTRING:
		return lua_msgpack_decode_string(L, false);
	default:
		return luaL_error(L, "msgpack.decode: "
				  "a Lua string or 'char *' expected");
	}
}

static int
lua_ibuf_msgpack_decode(lua_State *L)
{
	uint32_t ctypeid = 0;
	const char *rpos = *(const char **)luaL_checkcdata(L, 1, &ctypeid);
	if (rpos == NULL) {
		luaL_error(L, "msgpack.ibuf_decode: rpos is null");
	}
	struct luaL_serializer *cfg = luaL_checkserializer(L);
	luamp_decode(L, cfg, &rpos);
	*(const char **)luaL_pushcdata(L, ctypeid) = rpos;
	lua_pushvalue(L, -2);
	return 2;
}

/**
 * Verify and set arguments: data and size.
 *
 * Always return 0. In case of any fail raise a Lua error.
 */
static int
verify_decode_header_args(lua_State *L, const char *func_name,
			  const char **data_p, uint32_t *cdata_type_p,
			  ptrdiff_t *size_p)
{
	/* Verify arguments count. */
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Usage: %s(ptr, size)", func_name);

	/* Verify ptr type. */
	const char *data;
	uint32_t cdata_type;
	if (luaL_checkconstchar(L, 1, &data, &cdata_type) != 0)
		return luaL_error(L, "%s: 'char *' expected", func_name);

	/* Verify size type and value. */
	ptrdiff_t size = (ptrdiff_t) luaL_checkinteger(L, 2);
	if (size <= 0)
		return luaL_error(L, "%s: non-positive size", func_name);

	*data_p = data;
	*size_p = size;
	*cdata_type_p = cdata_type;

	return 0;
}

/**
 * msgpack.decode_array_header(buf.rpos, buf:size())
 * -> arr_len, new_rpos
 */
static int
lua_decode_array_header(lua_State *L)
{
	const char *func_name = "msgpack.decode_array_header";
	const char *data;
	uint32_t cdata_type;
	ptrdiff_t size;
	verify_decode_header_args(L, func_name, &data, &cdata_type, &size);

	if (mp_typeof(*data) != MP_ARRAY)
		return luaL_error(L, "%s: unexpected msgpack type", func_name);

	if (mp_check_array(data, data + size) > 0)
		return luaL_error(L, "%s: unexpected end of buffer", func_name);

	uint32_t len = mp_decode_array(&data);

	lua_pushinteger(L, len);
	*(const char **) luaL_pushcdata(L, cdata_type) = data;
	return 2;
}

/**
 * msgpack.decode_map_header(buf.rpos, buf:size())
 * -> map_len, new_rpos
 */
static int
lua_decode_map_header(lua_State *L)
{
	const char *func_name = "msgpack.decode_map_header";
	const char *data;
	uint32_t cdata_type;
	ptrdiff_t size;
	verify_decode_header_args(L, func_name, &data, &cdata_type, &size);

	if (mp_typeof(*data) != MP_MAP)
		return luaL_error(L, "%s: unexpected msgpack type", func_name);

	if (mp_check_map(data, data + size) > 0)
		return luaL_error(L, "%s: unexpected end of buffer", func_name);

	uint32_t len = mp_decode_map(&data);

	lua_pushinteger(L, len);
	*(const char **) luaL_pushcdata(L, cdata_type) = data;
	return 2;
}

static int
lua_msgpack_new(lua_State *L);

static const luaL_Reg msgpacklib[] = {
	{ "encode", lua_msgpack_encode },
	{ "decode", lua_msgpack_decode },
	{ "decode_unchecked", lua_msgpack_decode_unchecked },
	{ "ibuf_decode", lua_ibuf_msgpack_decode },
	{ "decode_array_header", lua_decode_array_header },
	{ "decode_map_header", lua_decode_map_header },
	{ "new", lua_msgpack_new },
	{ NULL, NULL }
};

static int
lua_msgpack_new(lua_State *L)
{
	luaL_newserializer(L, NULL, msgpacklib);
	return 1;
}

LUALIB_API int
luaopen_msgpack(lua_State *L)
{
	luaL_msgpack_default = luaL_newserializer(L, "msgpack", msgpacklib);
	return 1;
}
