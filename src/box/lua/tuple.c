/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/lua/tuple.h"

#include "lua/utils.h" /* luaT_error() */
#include "lua/msgpack.h" /* luamp_encode_XXX() */
#include "diag.h" /* diag_set() */
#include <small/ibuf.h>
#include <small/region.h>
#include <fiber.h>

#include "box/tuple.h"
#include "box/tuple_convert.h"
#include "box/errcode.h"
#include "box/memtx_tuple.h"
#include "json/path.h"

/** {{{ box.tuple Lua library
 *
 * To avoid extra copying between Lua memory and garbage-collected
 * tuple memory, provide a Lua userdata object 'box.tuple'.  This
 * object refers to a tuple instance in the slab allocator, and
 * allows accessing it using Lua primitives (array subscription,
 * iteration, etc.). When Lua object is garbage-collected,
 * tuple reference counter in the slab allocator is decreased,
 * allowing the tuple to be eventually garbage collected in
 * the slab allocator.
 */

static const char *tuplelib_name = "box.tuple";
static const char *tuple_iteratorlib_name = "box.tuple.iterator";

extern char tuple_lua[]; /* Lua source */

uint32_t CTID_CONST_STRUCT_TUPLE_REF;

static inline box_tuple_t *
lua_checktuple(struct lua_State *L, int narg)
{
	struct tuple *tuple = luaT_istuple(L, narg);
	if (tuple == NULL)  {
		luaL_error(L, "Invalid argument #%d (box.tuple expected, got %s)",
		   narg, lua_typename(L, lua_type(L, narg)));
	}

	return tuple;
}

box_tuple_t *
luaT_istuple(struct lua_State *L, int narg)
{
	assert(CTID_CONST_STRUCT_TUPLE_REF != 0);
	uint32_t ctypeid;
	void *data;

	if (lua_type(L, narg) != LUA_TCDATA)
		return NULL;

	data = luaL_checkcdata(L, narg, &ctypeid);
	if (ctypeid != CTID_CONST_STRUCT_TUPLE_REF)
		return NULL;

	return *(struct tuple **) data;
}

int
lbox_tuple_new(lua_State *L)
{
	int argc = lua_gettop(L);
	if (argc < 1) {
		lua_newtable(L); /* create an empty tuple */
		++argc;
	}
	struct ibuf *buf = tarantool_lua_ibuf;

	ibuf_reset(buf);
	struct mpstream stream;
	mpstream_init(&stream, buf, ibuf_reserve_cb, ibuf_alloc_cb,
		      luamp_error, L);

	if (argc == 1 && (lua_istable(L, 1) || luaT_istuple(L, 1))) {
		/* New format: box.tuple.new({1, 2, 3}) */
		luamp_encode_tuple(L, luaL_msgpack_default, &stream, 1);
	} else {
		/* Backward-compatible format: box.tuple.new(1, 2, 3). */
		luamp_encode_array(luaL_msgpack_default, &stream, argc);
		for (int k = 1; k <= argc; ++k) {
			luamp_encode(L, luaL_msgpack_default, &stream, k);
		}
	}
	mpstream_flush(&stream);

	box_tuple_format_t *fmt = box_tuple_format_default();
	struct tuple *tuple = box_tuple_new(fmt, buf->buf,
					   buf->buf + ibuf_used(buf));
	if (tuple == NULL)
		return luaT_error(L);
	/* box_tuple_new() doesn't leak on exception, see public API doc */
	luaT_pushtuple(L, tuple);
	ibuf_reinit(tarantool_lua_ibuf);
	return 1;
}

static int
lbox_tuple_gc(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	box_tuple_unref(tuple);
	return 0;
}

static int
lbox_tuple_slice_wrapper(struct lua_State *L)
{
	box_tuple_iterator_t *it = (box_tuple_iterator_t *)
		lua_topointer(L, 1);
	uint32_t start = lua_tonumber(L, 2);
	uint32_t end = lua_tonumber(L, 3);
	assert(end >= start);
	const char *field;

	uint32_t field_no = start;
	field = box_tuple_seek(it, start);
	while (field && field_no < end) {
		luamp_decode(L, luaL_msgpack_default, &field);
		++field_no;
		field = box_tuple_next(it);
	}
	assert(field_no == end);
	return end - start;
}

static int
lbox_tuple_slice(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	int argc = lua_gettop(L) - 1;
	uint32_t start, end;
	int32_t offset;

	/*
	 * Prepare the range. The second argument is optional.
	 * If the end is beyond tuple size, adjust it.
	 * If no arguments, or start > end, return an error.
	 */
	if (argc == 0 || argc > 2)
		luaL_error(L, "tuple.slice(): bad arguments");

	int32_t field_count = box_tuple_field_count(tuple);
	offset = lua_tonumber(L, 2);
	if (offset >= 0 && offset < field_count) {
		start = offset;
	} else if (offset < 0 && -offset <= field_count) {
		start = offset + field_count;
	} else {
		return luaL_error(L, "tuple.slice(): start >= field count");
	}

	if (argc == 2) {
		offset = lua_tonumber(L, 3);
		if (offset > 0 && offset <= field_count) {
			end = offset;
		} else if (offset < 0 && -offset < field_count) {
			end = offset + field_count;
		} else {
			return luaL_error(L, "tuple.slice(): end > field count");
		}
	} else {
		end = field_count;
	}
	if (end <= start)
		return luaL_error(L, "tuple.slice(): start must be less than end");

	box_tuple_iterator_t *it = box_tuple_iterator(tuple);
	lua_pushcfunction(L, lbox_tuple_slice_wrapper);
	lua_pushlightuserdata(L, it);
	lua_pushinteger(L, start);
	lua_pushinteger(L, end);
	int rc = luaT_call(L, 3, end - start);
	box_tuple_iterator_free(it);
	if (rc != 0)
		return luaT_error(L);
	return end - start;
}

void
luamp_convert_key(struct lua_State *L, struct luaL_serializer *cfg,
		  struct mpstream *stream, int index)
{
	/* Performs keyfy() logic */

	struct tuple *tuple = luaT_istuple(L, index);
	if (tuple != NULL)
		return tuple_to_mpstream(tuple, stream);

	struct luaL_field field;
	luaL_tofield(L, cfg, index, &field);
	if (field.type == MP_ARRAY) {
		lua_pushvalue(L, index);
		luamp_encode_r(L, cfg, stream, &field, 0);
		lua_pop(L, 1);
	} else if (field.type == MP_NIL) {
		luamp_encode_array(cfg, stream, 0);
	} else {
		luamp_encode_array(cfg, stream, 1);
		lua_pushvalue(L, index);
		luamp_encode_r(L, cfg, stream, &field, 0);
		lua_pop(L, 1);
	}
}

void
luamp_encode_tuple(struct lua_State *L, struct luaL_serializer *cfg,
		   struct mpstream *stream, int index)
{
	struct tuple *tuple = luaT_istuple(L, index);
	if (tuple != NULL) {
		return tuple_to_mpstream(tuple, stream);
	} else if (luamp_encode(L, cfg, stream, index) != MP_ARRAY) {
		diag_set(ClientError, ER_TUPLE_NOT_ARRAY);
		luaT_error(L);
	}
}

void
tuple_to_mpstream(struct tuple *tuple, struct mpstream *stream)
{
	size_t bsize = box_tuple_bsize(tuple);
	char *ptr = mpstream_reserve(stream, bsize);
	box_tuple_to_buf(tuple, ptr, bsize);
	mpstream_advance(stream, bsize);
}

/* A MsgPack extensions handler that supports tuples */
static enum mp_type
luamp_encode_extension_box(struct lua_State *L, int idx,
			   struct mpstream *stream)
{
	struct tuple *tuple = luaT_istuple(L, idx);
	if (tuple != NULL) {
		tuple_to_mpstream(tuple, stream);
		return MP_ARRAY;
	}

	return MP_EXT;
}

/**
 * Convert a tuple into lua table. Named fields are stored as
 * {name = value} pairs. Not named fields are stored as
 * {1-based_index_in_tuple = value}.
 */
static int
lbox_tuple_to_map(struct lua_State *L)
{
	int argc = lua_gettop(L);
	if (argc < 1 || argc > 2)
		goto error;
	bool names_only = false;
	if (argc == 2) {
		if (!lua_istable(L, 2))
			goto error;
		lua_getfield(L, 2, "names_only");
		if (!lua_isboolean(L, -1) && !lua_isnil(L, -1))
			goto error;
		names_only = lua_toboolean(L, -1);
	}

	const struct tuple *tuple = lua_checktuple(L, 1);
	const struct tuple_format *format = tuple_format(tuple);
	const struct tuple_field *field = &format->fields[0];
	const char *pos = tuple_data(tuple);
	int field_count = (int)mp_decode_array(&pos);
	int n_named = format->dict->name_count;
	lua_createtable(L, field_count, n_named);
	for (int i = 0; i < n_named; ++i, ++field) {
		/* Access by name. */
		const char *name = format->dict->names[i];
		lua_pushstring(L, name);
		luamp_decode(L, luaL_msgpack_default, &pos);
		lua_rawset(L, -3);
		if (names_only)
			continue;
		/*
		 * Access the same field by an index. There is no
		 * copy for tables - lua optimizes it and uses
		 * references.
		 */
		lua_pushstring(L, name);
		lua_rawget(L, -2);
		lua_rawseti(L, -2, i + TUPLE_INDEX_BASE);
	}
	if (names_only)
		return 1;
	/* Access for not named fields by index. */
	for (int i = n_named; i < field_count; ++i) {
		luamp_decode(L, luaL_msgpack_default, &pos);
		lua_rawseti(L, -2, i + TUPLE_INDEX_BASE);
	}
	return 1;
error:
	luaL_error(L, "Usage: tuple:tomap(opts)");
	return 1;
}

/**
 * Tuple transforming function.
 *
 * Remove the fields designated by 'offset' and 'len' from an tuple,
 * and replace them with the elements of supplied data fields,
 * if any.
 *
 * Function returns newly allocated tuple.
 * It does not change any parent tuple data.
 */
static int
lbox_tuple_transform(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	int argc = lua_gettop(L);
	if (argc < 3)
		luaL_error(L, "tuple.transform(): bad arguments");
	lua_Integer offset = lua_tointeger(L, 2);  /* Can be negative and can be > INT_MAX */
	lua_Integer len = lua_tointeger(L, 3);

	lua_Integer field_count = box_tuple_field_count(tuple);
	/* validate offset and len */
	if (offset == 0) {
		luaL_error(L, "tuple.transform(): offset is out of bound");
	} else if (offset < 0) {
		if (-offset > field_count)
			luaL_error(L, "tuple.transform(): offset is out of bound");
		offset += field_count + 1;
	} else if (offset > field_count) {
		offset = field_count + 1;
	}
	if (len < 0)
		luaL_error(L, "tuple.transform(): len is negative");
	if (len > field_count + 1 - offset)
		len = field_count + 1 - offset;

	assert(offset + len <= field_count + 1);

	/*
	 * Calculate the number of operations and length of UPDATE expression
	 */
	uint32_t op_cnt = 0;
	if (offset < field_count + 1 && len > 0)
		op_cnt++;
	if (argc > 3)
		op_cnt += argc - 3;

	if (op_cnt == 0) {
		/* tuple_update() does not accept an empty operation list. */
		luaT_pushtuple(L, tuple);
		return 1;
	}

	struct ibuf *buf = tarantool_lua_ibuf;
	ibuf_reset(buf);
	struct mpstream stream;
	mpstream_init(&stream, buf, ibuf_reserve_cb, ibuf_alloc_cb,
		      luamp_error, L);

	/*
	 * Prepare UPDATE expression
	 */
	luamp_encode_array(luaL_msgpack_default, &stream, op_cnt);
	if (len > 0) {
		luamp_encode_array(luaL_msgpack_default, &stream, 3);
		luamp_encode_str(luaL_msgpack_default, &stream, "#", 1);
		luamp_encode_uint(luaL_msgpack_default, &stream, offset);
		luamp_encode_uint(luaL_msgpack_default, &stream, len);
	}

	for (int i = argc ; i > 3; i--) {
		luamp_encode_array(luaL_msgpack_default, &stream, 3);
		luamp_encode_str(luaL_msgpack_default, &stream, "!", 1);
		luamp_encode_uint(luaL_msgpack_default, &stream, offset);
		luamp_encode(L, luaL_msgpack_default, &stream, i);
	}
	mpstream_flush(&stream);

	/* Execute tuple_update */
	struct tuple *new_tuple =
		box_tuple_update(tuple, buf->buf, buf->buf + ibuf_used(buf));
	if (tuple == NULL)
		luaT_error(L);
	/* box_tuple_update() doesn't leak on exception, see public API doc */
	luaT_pushtuple(L, new_tuple);
	ibuf_reset(buf);
	return 1;
}

/**
 * Find a tuple field by JSON path. If a field was not found and a
 * path contains JSON syntax errors, then an exception is raised.
 * @param L Lua state.
 * @param tuple 1-th argument on a lua stack, tuple to get field
 *        from.
 * @param path 2-th argument on lua stack. Can be field name or a
 *        JSON path to a field.
 *
 * @retval not nil Found field value.
 * @retval     nil A field is NULL or does not exist.
 */
static int
lbox_tuple_field_by_path(struct lua_State *L)
{
	struct tuple *tuple = luaT_istuple(L, 1);
	/* Is checked in Lua wrapper. */
	assert(tuple != NULL);
	assert(lua_isstring(L, 2));
	size_t len;
	const char *field = NULL, *path = lua_tolstring(L, 2, &len);
	if (len == 0)
		return 0;
	if (tuple_field_by_path(tuple, path, (uint32_t) len,
				lua_hashstring(L, 2), &field) != 0) {
		return luaT_error(L);
	} else if (field == NULL) {
		return 0;
	}
	assert(field != NULL);
	luamp_decode(L, luaL_msgpack_default, &field);
	return 1;
}

static int
lbox_tuple_to_string(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	size_t used = region_used(&fiber()->gc);
	char *res = tuple_to_yaml(tuple);
	if (res == NULL) {
		region_truncate(&fiber()->gc, used);
		return luaT_error(L);
	}
	lua_pushstring(L, res);
	region_truncate(&fiber()->gc, used);
	return 1;
}

void
luaT_pushtuple(struct lua_State *L, box_tuple_t *tuple)
{
	assert(CTID_CONST_STRUCT_TUPLE_REF != 0);
	struct tuple **ptr = (struct tuple **)
		luaL_pushcdata(L, CTID_CONST_STRUCT_TUPLE_REF);
	*ptr = tuple;
	/* The order is important - first reference tuple, next set gc */
	if (box_tuple_ref(tuple) != 0) {
		luaT_error(L);
		return;
	}
	lua_pushcfunction(L, lbox_tuple_gc);
	luaL_setcdatagc(L, -2);
}

static const struct luaL_Reg lbox_tuple_meta[] = {
	{"__gc", lbox_tuple_gc},
	{"tostring", lbox_tuple_to_string},
	{"slice", lbox_tuple_slice},
	{"transform", lbox_tuple_transform},
	{"tuple_to_map", lbox_tuple_to_map},
	{"tuple_field_by_path", lbox_tuple_field_by_path},
	{NULL, NULL}
};

static const struct luaL_Reg lbox_tuplelib[] = {
	{"new", lbox_tuple_new},
	{NULL, NULL}
};

static const struct luaL_Reg lbox_tuple_iterator_meta[] = {
	{NULL, NULL}
};

/* }}} */

void
box_lua_tuple_init(struct lua_State *L)
{
	/* export C functions to Lua */
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.internal", 1);
	luaL_newmetatable(L, tuplelib_name);
	luaL_register(L, NULL, lbox_tuple_meta);
	lua_setfield(L, -2, "tuple");
	lua_pop(L, 1); /* box.internal */
	luaL_register_type(L, tuple_iteratorlib_name,
			   lbox_tuple_iterator_meta);
	luaL_register_module(L, tuplelib_name, lbox_tuplelib);
	lua_pop(L, 1);

	luamp_set_encode_extension(luamp_encode_extension_box);

	/* Get CTypeID for `struct tuple' */
	int rc = luaL_cdef(L, "struct tuple;");
	assert(rc == 0);
	(void) rc;
	CTID_CONST_STRUCT_TUPLE_REF = luaL_ctypeid(L, "const struct tuple &");
	assert(CTID_CONST_STRUCT_TUPLE_REF != 0);
}
