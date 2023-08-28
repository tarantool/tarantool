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

#include "core/assoc.h"
#include "core/decimal.h" /* decimal_unpack() */
#include "core/tweaks.h"
#include "lua/decimal.h" /* luaT_newdecimal() */
#include "mp_extension_types.h"
#include "mp_uuid.h" /* mp_decode_uuid() */
#include "mp_datetime.h"
#include "mp_interval.h"
#include "tt_static.h"

#include "cord_buf.h"
#include <fiber.h>

/**
 * Lua object that stores raw msgpack data and implements methods for decoding
 * it in Lua. Allocated as Lua userdata.
 */
struct luamp_object {
	/** Pointer to the serializer used for decoding data. */
	struct luaL_serializer *cfg;
	/** Reference to the serializer. */
	int cfg_ref;
	/**
	 * If this object doesn't own data, but instead points to data of
	 * another object (i.e. it was created by an iterator), then this
	 * member stores a Lua reference to the original object. Otherwise,
	 * it's set to LUA_NOREF.
	 */
	int data_ref;
	/** Pointer to msgpack data. */
	const char *data;
	/** Pointer to the end of msgpack data. */
	const char *data_end;
	/**
	 * Upon first indexation the MsgPack data is completely decoded,
	 * pushed to Lua stack and referenced: the Lua stack reference is saved
	 * to this field.
	 * Initially set to `LUA_NOREF`.
	 */
	int decoded_ref;
	/**
	 * Translation table containing string key aliases. If present, used
	 * during indexation.
	 * Must use `lua_hash` as the hash function.
	 * Initially set to NULL.
	 */
	struct mh_strnu32_t *translation;
};

static const char luamp_object_typename[] = "msgpack.object";

/**
 * Iterator over a msgpack object. Allocated as Lua userdata.
 */
struct luamp_iterator {
	/** Pointer to the source object. */
	struct luamp_object *source;
	/** Lua reference to the source object. */
	int source_ref;
	/** Current iterator position in the source object data. */
	const char *pos;
};

static const char luamp_iterator_typename[] = "msgpack.iterator";

/**
 * If this flag is set, a binary data field will be decoded to a plain Lua
 * string, not a varbinary object.
 */
static bool msgpack_decode_binary_as_string = false;
TWEAK_BOOL(msgpack_decode_binary_as_string);

void
luamp_error(void *error_ctx)
{
	struct lua_State *L = (struct lua_State *) error_ctx;
	luaL_error(L, diag_last_error(diag_get())->errmsg);
}

struct luaL_serializer *luaL_msgpack_default = NULL;

const char *
luamp_get(struct lua_State *L, int idx, size_t *data_len)
{
	struct luamp_object *obj;
	obj = luaL_testudata(L, idx, luamp_object_typename);
	if (obj != NULL) {
		*data_len = obj->data_end - obj->data;
		return obj->data;
	}
	return NULL;
}

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

/**
 * Tries to translate MP_MAP key to an unsigned integer from the translation
 * table.
 */
static void
translate_map_key_field(struct luaL_field *field, uint32_t hash,
			struct mh_strnu32_t *translation)
{
	struct mh_strnu32_key_t key = {
		.str = field->sval.data,
		.len = field->sval.len,
		.hash = hash,
	};
	mh_int_t k = mh_strnu32_find(translation, &key, NULL);
	if (k != mh_end(translation)) {
		field->type = MP_UINT;
		field->ival = (int64_t)mh_strnu32_node(translation, k)->val;
	}
}

int
luamp_encode_with_translation_r(struct lua_State *L,
				struct luaL_serializer *cfg,
				struct mpstream *stream,
				struct luaL_field *field,
				int level,
				struct mh_strnu32_t *translation,
				enum mp_type *type_out)
{
	int top = lua_gettop(L);
	enum mp_type type;
	const char *data;
	size_t data_len;

restart: /* used by MP_EXT of unidentified subtype */
	switch (field->type) {
	case MP_UINT:
		mpstream_encode_uint(stream, field->ival);
		type = MP_UINT;
		break;
	case MP_STR:
		mpstream_encode_strn(stream, field->sval.data, field->sval.len);
		type = MP_STR;
		break;
	case MP_BIN:
		mpstream_encode_binl(stream, field->sval.len);
		mpstream_memcpy(stream, field->sval.data, field->sval.len);
		type = MP_BIN;
		break;
	case MP_INT:
		mpstream_encode_int(stream, field->ival);
		type = MP_INT;
		break;
	case MP_FLOAT:
		mpstream_encode_float(stream, field->fval);
		type = MP_FLOAT;
		break;
	case MP_DOUBLE:
		mpstream_encode_double(stream, field->dval);
		type = MP_DOUBLE;
		break;
	case MP_BOOL:
		mpstream_encode_bool(stream, field->bval);
		type = MP_BOOL;
		break;
	case MP_NIL:
		mpstream_encode_nil(stream);
		type = MP_NIL;
		break;
	case MP_MAP:
		/* Map */
		if (level >= cfg->encode_max_depth) {
			if (! cfg->encode_deep_as_nil) {
				diag_set(LuajitError,
					 tt_sprintf("Too high nest level - %d",
						    level + 1));
				return -1;
			}
			mpstream_encode_nil(stream); /* Limit nested maps */
			type = MP_NIL;
			break;
		}
		mpstream_encode_map(stream, field->size);
		lua_pushnil(L);  /* first key */
		while (lua_next(L, top) != 0) {
			lua_pushvalue(L, -2); /* push a copy of key to top */
			if (luaL_tofield(L, cfg, lua_gettop(L), field) < 0)
				goto error;
			if (translation != NULL && level == 0 &&
			    field->type == MP_STR)
				translate_map_key_field(field,
							lua_hashstring(L, -1),
							translation);
			if (luamp_encode_with_translation_r(
					L, cfg, stream, field, level + 1,
					translation, NULL) != 0)
				goto error;
			lua_pop(L, 1); /* pop a copy of key */
			if (luaL_tofield(L, cfg, lua_gettop(L), field) < 0)
				goto error;
			if (luamp_encode_with_translation_r(
					L, cfg, stream, field, level + 1,
					translation, NULL) != 0)
				goto error;
			lua_pop(L, 1); /* pop value */
		}
		assert(lua_gettop(L) == top);
		type = MP_MAP;
		break;
	case MP_ARRAY:
		/* Array */
		if (level >= cfg->encode_max_depth) {
			if (! cfg->encode_deep_as_nil) {
				diag_set(LuajitError,
					 tt_sprintf("Too high nest level - %d",
						    level + 1));
				return -1;
			}
			mpstream_encode_nil(stream); /* Limit nested arrays */
			type = MP_NIL;
			break;
		}
		uint32_t size = field->size;
		mpstream_encode_array(stream, size);
		for (uint32_t i = 0; i < size; i++) {
			lua_rawgeti(L, top, i + 1);
			if (luaL_tofield(L, cfg, top + 1, field) < 0)
				goto error;
			if (luamp_encode_with_translation_r(
					L, cfg, stream, field, level + 1,
					translation, NULL) != 0)
				goto error;
			lua_pop(L, 1);
		}
		assert(lua_gettop(L) == top);
		type = MP_ARRAY;
		break;
	case MP_EXT:
		type = MP_EXT;
		switch (field->ext_type) {
		case MP_DECIMAL:
			mpstream_encode_decimal(stream, field->decval);
			break;
		case MP_UUID:
			mpstream_encode_uuid(stream, field->uuidval);
			break;
		case MP_ERROR:
			if (!cfg->encode_error_as_ext) {
				field->ext_type = MP_UNKNOWN_EXTENSION;
				goto convert;
			}
			type = luamp_encode_extension(L, top, stream);
			break;
		case MP_DATETIME:
			mpstream_encode_datetime(stream, field->dateval);
			break;
		case MP_INTERVAL:
			mpstream_encode_interval(stream, field->interval);
			break;
		default:
			data = luamp_get(L, top, &data_len);
			if (data != NULL) {
				mpstream_memcpy(stream, data, data_len);
				type = mp_typeof(*data);
				break;
			}
			/* Run trigger if type can't be encoded */
			type = luamp_encode_extension(L, top, stream);
			if (type != MP_EXT) {
				/* Value has been packed by the trigger */
				break;
			}
convert:
			/* Try to convert value to serializable type */
			if (luaL_convertfield(L, cfg, top, field) != 0)
				goto error;
			/* handled by luaL_convertfield */
			assert(field->type != MP_EXT);
			assert(lua_gettop(L) == top);
			goto restart;
		}
	}
	if (type_out != NULL)
		*type_out = type;
	return 0;
error:
	lua_settop(L, top);
	return -1;
}

int
luamp_encode_with_translation(struct lua_State *L, struct luaL_serializer *cfg,
			      struct mpstream *stream, int index,
			      struct mh_strnu32_t *translation,
			      enum mp_type *type)
{
	int top = lua_gettop(L);
	if (index < 0)
		index = top + index + 1;

	bool on_top = (index == top);
	if (!on_top) {
		lua_pushvalue(L, index); /* copy a value to the stack top */
	}

	struct luaL_field field;
	int rc = -1;
	if (luaL_tofield(L, cfg, lua_gettop(L), &field) < 0)
		goto cleanup;
	if (luamp_encode_with_translation_r(L, cfg, stream, &field, 0,
					    translation, type) != 0)
		goto cleanup;
	rc = 0;
cleanup:
	if (!on_top) {
		lua_remove(L, top + 1); /* remove a value copy */
	}

	return rc;
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
		if (msgpack_decode_binary_as_string)
			lua_pushlstring(L, str, len);
		else
			luaT_pushvarbinary(L, str, len);
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
			decimal_t *dec = luaT_newdecimal(L);
			VERIFY(decimal_unpack(data, len, dec) != NULL);
			return;
		}
		case MP_UUID:
		{
			struct tt_uuid *uuid = luaT_newuuid(L);
			VERIFY(uuid_unpack(data, len, uuid) != NULL);
			return;
		}
		case MP_DATETIME:
		{
			struct datetime *date = luaT_newdatetime(L);
			VERIFY(datetime_unpack(data, len, date) != NULL);
			return;
		}
		case MP_INTERVAL:
		{
			struct interval *itv = luaT_newinterval(L);
			VERIFY(interval_unpack(data, len, itv) != NULL);
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

	if (luamp_encode(L, cfg, &stream, 1) != 0) {
		if (index > 1)
			ibuf_truncate(buf, used);
		else
			cord_ibuf_drop(buf);
		luaT_error(L);
	}
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
			return luaT_error(L);
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
			return luaT_error(L);
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

/**
 * Allocates a new msgpack object capable of storing msgpack data of the given
 * size and pushes it to Lua stack. Returns a pointer to the object.
 */
static struct luamp_object *
luamp_new_object(struct lua_State *L, size_t data_len)
{
	struct luamp_object *obj = lua_newuserdata(L, sizeof(*obj) + data_len);
	obj->cfg = luaL_msgpack_default;
	obj->cfg_ref = LUA_NOREF;
	obj->data_ref = LUA_NOREF;
	obj->data = (char *)obj + sizeof(*obj);
	obj->data_end = obj->data + data_len;
	obj->decoded_ref = LUA_NOREF;
	obj->translation = NULL;
	luaL_getmetatable(L, luamp_object_typename);
	lua_setmetatable(L, -2);
	return obj;
}

void
luamp_push_with_translation(struct lua_State *L, const char *data,
			    const char *data_end,
			    struct mh_strnu32_t *translation)
{
	size_t data_len = data_end - data;
	struct luamp_object *obj = luamp_new_object(L, data_len);
	memcpy((char *)obj->data, data, data_len);
	assert(mp_check_exact(&data, data_end) == 0);
	obj->translation = translation;
}

/**
 * Creates a new msgpack object and pushes it to Lua stack.
 * Takes a Lua object as the only argument.
 */
static int
lua_msgpack_object(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "msgpack.object: a Lua object expected");
	struct luaL_serializer *cfg = luaL_checkserializer(L);
	struct ibuf *buf = cord_ibuf_take();
	struct mpstream stream;
	mpstream_init(&stream, buf, ibuf_reserve_cb, ibuf_alloc_cb,
		      luamp_error, L);
	if (luamp_encode(L, cfg, &stream, 1) != 0) {
		cord_ibuf_put(buf);
		luaT_error(L);
	}
	mpstream_flush(&stream);
	struct luamp_object *obj = luamp_new_object(L, ibuf_used(buf));
	memcpy((char *)obj->data, buf->buf, obj->data_end - obj->data);
	cord_ibuf_put(buf);
	obj->cfg = cfg;
	luaL_pushserializer(L);
	obj->cfg_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 1;
}

/**
 * Creates a new msgpack object from raw data and pushes it to Lua stack.
 * The data is given either by a Lua string or by a char ptr and size.
 */
static int
lua_msgpack_object_from_raw(struct lua_State *L)
{
	const char *data;
	size_t data_len;
	uint32_t cdata_type;
	switch (lua_type(L, 1)) {
	case LUA_TCDATA:
		if (luaL_checkconstchar(L, 1, &data, &cdata_type) != 0)
			goto error;
		data_len = luaL_checkinteger(L, 2);
		break;
	case LUA_TSTRING:
		data = lua_tolstring(L, 1, &data_len);
		break;
	default:
		goto error;
	}
	const char *p = data;
	const char *data_end = data + data_len;
	if (mp_check_exact(&p, data_end) != 0)
		return luaT_error(L);
	struct luamp_object *obj = luamp_new_object(L, data_len);
	memcpy((char *)obj->data, data, data_len);
	obj->cfg = luaL_checkserializer(L);
	luaL_pushserializer(L);
	obj->cfg_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 1;
error:
	return luaL_error(L, "msgpack.object_from_raw: "
			  "a Lua string or 'char *' expected");
}

/**
 * Takes a Lua value. Returns true if it's a msgpack object, false otherwise.
 */
static int
lua_msgpack_is_object(struct lua_State *L)
{
	void *obj = luaL_testudata(L, 1, luamp_object_typename);
	lua_pushboolean(L, obj != NULL);
	return 1;
}

static inline struct luamp_object *
luamp_check_object(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, luamp_object_typename);
}

static int
luamp_object_gc(struct lua_State *L)
{
	struct luamp_object *obj = luamp_check_object(L, 1);
	luaL_unref(L, LUA_REGISTRYINDEX, obj->cfg_ref);
	luaL_unref(L, LUA_REGISTRYINDEX, obj->data_ref);
	luaL_unref(L, LUA_REGISTRYINDEX, obj->decoded_ref);
	return 0;
}

static int
luamp_object_tostring(struct lua_State *L)
{
	lua_pushstring(L, luamp_object_typename);
	return 1;
}

/**
 * Decodes the data stored in a msgpack object and pushes it to Lua stack.
 * Takes a msgpack object as the only argument.
 */
static int
luamp_object_decode(struct lua_State *L)
{
	struct luamp_object *obj = luamp_check_object(L, 1);
	const char *data = obj->data;
	luamp_decode(L, obj->cfg, &data);
	assert(data == obj->data_end);
	return 1;
}

/**
 * Creates an iterator over a msgpack object and pushes it to Lua stack.
 * Takes a msgpack object as the only argument.
 */
static int
luamp_object_iterator(struct lua_State *L)
{
	struct luamp_object *obj = luamp_check_object(L, 1);
	struct luamp_iterator *it = lua_newuserdata(L, sizeof(*it));
	it->source = obj;
	it->source_ref = LUA_NOREF;
	it->pos = obj->data;
	luaL_getmetatable(L, luamp_iterator_typename);
	lua_setmetatable(L, -2);
	lua_insert(L, 1);
	it->source_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 1;
}

/**
 * Takes a `msgpack.object` and an indexation key as the arguments, indexes
 * the MsgPack stored in the `msgpack.object` and pushes the result to Lua stack
 * or, if the MsgPack data type is not indexable, pushes nil.
 */
static int
luamp_object_get(struct lua_State *L)
{
	struct luamp_object *obj = luamp_check_object(L, 1);
	enum mp_type type = mp_typeof(*obj->data);
	if (type != MP_MAP && type != MP_ARRAY)
		return luaL_error(L, "not an array or map");
	if (obj->decoded_ref == LUA_NOREF) {
		const char *data = obj->data;
		luamp_decode(L, obj->cfg, &data);
		assert(data == obj->data_end);
		obj->decoded_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	/* Pushes the decoded MsgPack data on top of the stack. */
	lua_rawgeti(L, LUA_REGISTRYINDEX, obj->decoded_ref);
	/* Pushes the indexing key on top of the stack. */
	lua_pushvalue(L, -2);
	/* Indexes the decoded MsgPack data and pops the key. */
	lua_rawget(L, -2);
	if (!lua_isnil(L, -1) || obj->translation == NULL ||
	    lua_type(L, -3) != LUA_TSTRING)
		return 1;

	size_t len;
	const char *alias = lua_tolstring(L, -3, &len);
	struct mh_strnu32_key_t key = {
		.str = alias,
		.len = len,
		.hash = lua_hashstring(L, -3),
	};
	mh_int_t k = mh_strnu32_find(obj->translation, &key, NULL);
	if (k != mh_end(obj->translation)) {
		lua_pop(L, 1);
		struct mh_strnu32_node_t *node =
			mh_strnu32_node(obj->translation, k);
		luaL_pushuint64(L, node->val);
		lua_rawget(L, -2);
	}
	return 1;
}

/**
 * Takes a `msgpack.object` and an indexation key as the arguments: if the key
 * is of string type, first tries to match it with `msgpack.object` methods,
 * and, in case the match occurs, pushes the matched method to Lua stack —
 * otherwise, delegates indexation to `msgpack.object:get`.
 */
static int
luamp_object_index(struct lua_State *L)
{
	luamp_check_object(L, 1);
	if (lua_type(L, 2) != LUA_TSTRING)
		return luamp_object_get(L);
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);
	if (lua_isnil(L, -1)) {
		/* Pop the nil and the metatable. */
		lua_pop(L, 2);
		return luamp_object_get(L);
	}
	return 1;
}

/**
 * Push a table of `msgpack.object` methods for console autocompletion.
 */
static int
luamp_object_autocomplete(struct lua_State *L)
{
	luamp_check_object(L, 1);
	lua_getmetatable(L, 1);
	return 1;
}

static inline struct luamp_iterator *
luamp_check_iterator(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, luamp_iterator_typename);
}

static int
luamp_iterator_gc(struct lua_State *L)
{
	struct luamp_iterator *it = luamp_check_iterator(L, 1);
	luaL_unref(L, LUA_REGISTRYINDEX, it->source_ref);
	return 0;
}

static int
luamp_iterator_tostring(struct lua_State *L)
{
	lua_pushstring(L, luamp_iterator_typename);
	return 1;
}

/**
 * Raises a Lua error if there's no data to decode.
 */
static inline void
luamp_iterator_check_data_end(struct lua_State *L, struct luamp_iterator *it)
{
	assert(it->pos >= it->source->data);
	assert(it->pos <= it->source->data_end);
	if (it->pos == it->source->data_end)
		luaL_error(L, "iteration ended");
}

/**
 * Raises a Lua error if the type of the msgpack value under the iterator
 * cursor doesn't match the expected type.
 */
static inline void
luamp_iterator_check_data_type(struct lua_State *L, struct luamp_iterator *it,
			       enum mp_type type)
{
	luamp_iterator_check_data_end(L, it);
	if (mp_typeof(*it->pos) != type)
		luaL_error(L, "unexpected msgpack type");
}

/**
 * Decodes a msgpack array header and returns the number of elements in the
 * array. After calling this function the iterator points to the first element
 * of the array or to the value following the array if the array is empty.
 * Raises a Lua error if the type of the value under the iterator cursor is not
 * MP_ARRAY.
 */
static int
luamp_iterator_decode_array_header(struct lua_State *L)
{
	struct luamp_iterator *it = luamp_check_iterator(L, 1);
	luamp_iterator_check_data_type(L, it, MP_ARRAY);
	uint32_t len = mp_decode_array(&it->pos);
	lua_pushinteger(L, len);
	return 1;
}

/**
 * Decodes a msgpack map header and returns the number of key value paris in
 * the map. After calling this function the iterator points to the first
 * key stored in the map or to the value following the map if the map is empty.
 * Raises a Lua error if the type of the value under the iterator cursor is not
 * MP_MAP.
 */
static int
luamp_iterator_decode_map_header(struct lua_State *L)
{
	struct luamp_iterator *it = luamp_check_iterator(L, 1);
	luamp_iterator_check_data_type(L, it, MP_MAP);
	uint32_t len = mp_decode_map(&it->pos);
	lua_pushinteger(L, len);
	return 1;
}

/**
 * Decodes a msgpack value under the iterator cursor and advances the cursor.
 * Returns a Lua object corresponding to the msgpack value. Raises a Lua error
 * if there's no data to decode.
 */
static int
luamp_iterator_decode(struct lua_State *L)
{
	struct luamp_iterator *it = luamp_check_iterator(L, 1);
	luamp_iterator_check_data_end(L, it);
	luamp_decode(L, it->source->cfg, &it->pos);
	return 1;
}

/**
 * Returns a msgpack value under the iterator cursor as a msgpack object,
 * (without decoding) and advances the cursor. The new msgpack object
 * points to the data of the source object (references it). Raises a Lua error
 * if there's no data to decode.
 */
static int
luamp_iterator_take(struct lua_State *L)
{
	struct luamp_iterator *it = luamp_check_iterator(L, 1);
	luamp_iterator_check_data_end(L, it);
	struct luamp_object *obj = luamp_new_object(L, 0);
	obj->data = it->pos;
	mp_next(&it->pos);
	obj->data_end = it->pos;
	lua_rawgeti(L, LUA_REGISTRYINDEX, it->source_ref);
	obj->data_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	/* No need to take ref to cfg, because it's pinned via data_ref. */
	obj->cfg = it->source->cfg;
	return 1;
}

/**
 * Copies the given number of msgpack values starting from the iterator cursor
 * position to a new msgpack array object. On success returns the new msgpack
 * object and advances the iterator cursor. If there isn't enough values to
 * decode, raises a Lua error and leaves the iterator cursor unchanged.
 *
 * This function could be implemented in Lua like this:
 *
 *     function take_array(iter, count)
 *         local array = {}
 *         for _ = 1, count do
 *             table.insert(array, iter:take())
 *         end
 *         return msgpack.object(array)
 *     end
 *
 * Note, in contrast to iter.take(), this function actually copies the original
 * object data (not just references it), because it has to prepend a msgpack
 * array header to the copied data.
 */
static int
luamp_iterator_take_array(struct lua_State *L)
{
	struct luamp_iterator *it = luamp_check_iterator(L, 1);
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Usage: iter:take_array(count)");
	int count = luaL_checkinteger(L, 2);
	if (count < 0)
		return luaL_error(L, "count must be >= 0");
	const char *start = it->pos;
	const char *end = start;
	for (int i = 0; i < count; i++) {
		if (end == it->source->data_end)
			return luaL_error(L, "iteration ended");
		mp_next(&end);
	}
	size_t size = end - start;
	struct luamp_object *obj = luamp_new_object(
		L, mp_sizeof_array(count) + size);
	char *data = (char *)obj->data;
	data = mp_encode_array(data, count);
	if (size > 0)
		memcpy(data, start, size);
	it->pos = end;
	return 1;
}

/**
 * Advances the iterator cursor by skipping one msgpack value under the cursor.
 * Raises a Lua error if there's no data to skip.
 */
static int
luamp_iterator_skip(struct lua_State *L)
{
	struct luamp_iterator *it = luamp_check_iterator(L, 1);
	luamp_iterator_check_data_end(L, it);
	mp_next(&it->pos);
	return 0;
}

static int
lua_msgpack_new(struct lua_State *L);

static const luaL_Reg msgpacklib[] = {
	{ "encode", lua_msgpack_encode },
	{ "decode", lua_msgpack_decode },
	{ "decode_unchecked", lua_msgpack_decode_unchecked },
	{ "ibuf_decode", lua_ibuf_msgpack_decode },
	{ "decode_array_header", lua_decode_array_header },
	{ "decode_map_header", lua_decode_map_header },
	{ "object", lua_msgpack_object },
	{ "object_from_raw", lua_msgpack_object_from_raw },
	{ "is_object", lua_msgpack_is_object },
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
	static const struct luaL_Reg luamp_object_meta[] = {
		{ "__gc", luamp_object_gc },
		{ "__tostring", luamp_object_tostring },
		{ "__index", luamp_object_index },
		{ "__autocomplete", luamp_object_autocomplete },
		{ "decode", luamp_object_decode },
		{ "iterator", luamp_object_iterator },
		{ "get", luamp_object_get },
		{ NULL, NULL }
	};
	luaL_register_type(L, luamp_object_typename, luamp_object_meta);

	static const struct luaL_Reg luamp_iterator_meta[] = {
		{ "__gc", luamp_iterator_gc },
		{ "__tostring", luamp_iterator_tostring },
		{ "decode_array_header", luamp_iterator_decode_array_header },
		{ "decode_map_header", luamp_iterator_decode_map_header },
		{ "decode", luamp_iterator_decode },
		{ "take", luamp_iterator_take },
		{ "take_array", luamp_iterator_take_array },
		{ "skip", luamp_iterator_skip },
		{ NULL, NULL }
	};
	luaL_register_type(L, luamp_iterator_typename, luamp_iterator_meta);

	luaL_msgpack_default = luaL_newserializer(L, "msgpack", msgpacklib);
	return 1;
}
