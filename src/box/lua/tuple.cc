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
#include "box/lua/tuple.h"
#include "box/lua/slab.h"
#include "box/tuple.h"
#include "box/tuple_update.h"
#include "fiber.h"
#include "lua/utils.h"
#include "lua/msgpack.h"
#include "third_party/lua-yaml/lyaml.h"
extern "C" {
#include <lj_obj.h>
}

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

static inline struct tuple *
lua_checktuple(struct lua_State *L, int narg)
{
	struct tuple *tuple = lua_istuple(L, narg);
	if (tuple == NULL)  {
		luaL_error(L, "Invalid argument #%d (box.tuple expected, got %s)",
		   narg, lua_typename(L, lua_type(L, narg)));
	}

	return tuple;
}

struct tuple *
lua_istuple(struct lua_State *L, int narg)
{
	assert(CTID_CONST_STRUCT_TUPLE_REF != 0);
	uint32_t ctypeid;
	void *data;

	if (lua_type(L, narg) != LUA_TCDATA)
		return NULL;

	data = luaL_checkcdata(L, narg, &ctypeid);
	if (ctypeid != CTID_CONST_STRUCT_TUPLE_REF)
		return NULL;

	struct tuple *t = *(struct tuple **) data;
	assert(t->refs);
	return t;
}

static int
lbox_tuple_new(lua_State *L)
{
	int argc = lua_gettop(L);
	if (unlikely(argc < 1)) {
		lua_newtable(L); /* create an empty tuple */
		++argc;
	}

	struct region *gc = &fiber()->gc;
	RegionGuard guard(gc);
	struct mpstream stream;
	mpstream_init(&stream, gc, region_reserve_cb, region_alloc_cb);

	if (argc == 1 && (lua_istable(L, 1) || lua_istuple(L, 1))) {
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

	size_t tuple_len = region_used(gc) - guard.used;
	const char *data = (char *) region_join(gc, tuple_len);
	struct tuple *tuple = tuple_new(tuple_format_ber, data, data + tuple_len);
	lbox_pushtuple(L, tuple);
	return 1;
}

static int
lbox_tuple_gc(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	tuple_unref(tuple);
	return 0;
}

static int
lbox_tuple_slice(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	int argc = lua_gettop(L) - 1;
	uint32_t start, end;
	int offset;

	/*
	 * Prepare the range. The second argument is optional.
	 * If the end is beyond tuple size, adjust it.
	 * If no arguments, or start > end, return an error.
	 */
	if (argc == 0 || argc > 2)
		luaL_error(L, "tuple.slice(): bad arguments");

	uint32_t field_count = tuple_field_count(tuple);
	offset = lua_tointeger(L, 2);
	if (offset >= 0 && offset < field_count) {
		start = offset;
	} else if (offset < 0 && -offset <= field_count) {
		start = offset + field_count;
	} else {
		return luaL_error(L, "tuple.slice(): start >= field count");
	}

	if (argc == 2) {
		offset = lua_tointeger(L, 3);
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

	struct tuple_iterator it;
	tuple_rewind(&it, tuple);
	const char *field;

	assert(start < field_count);
	uint32_t field_no = start;
	field = tuple_seek(&it, start);
	while (field && field_no < end) {
		luamp_decode(L, luaL_msgpack_default, &field);
		++field_no;
		field = tuple_next(&it);
	}
	assert(field_no == end);
	return end - start;
}

/* A MsgPack extensions handler that supports tuples */
static mp_type
luamp_encode_extension_box(struct lua_State *L, int idx,
			   struct mpstream *stream)
{
	struct tuple *tuple = lua_istuple(L, idx);
	if (tuple != NULL) {
		char *ptr = mpstream_reserve(stream, tuple->bsize);
		tuple_to_buf(tuple, ptr, tuple->bsize);
		mpstream_advance(stream, tuple->bsize);
		return MP_ARRAY;
	}

	return MP_EXT;
}

void
luamp_convert_tuple(struct lua_State *L, struct luaL_serializer *cfg,
		    struct mpstream *stream, int index)
{
	if (luaL_isarray(L, index) || lua_istuple(L, index)) {
		luamp_encode_tuple(L, cfg, stream, index);
	} else {
		luamp_encode_array(cfg, stream, 1);
		luamp_encode(L, cfg, stream, index);
	}
}

void
luamp_convert_key(struct lua_State *L, struct luaL_serializer *cfg,
		  struct mpstream *stream, int index)
{
	/* Performs keyfy() logic */
	if (lua_isnil(L, index)) {
		luamp_encode_array(cfg, stream, 0);
	} else {
		return luamp_convert_tuple(L, cfg, stream, index);
	}
}

void
luamp_encode_tuple(struct lua_State *L, struct luaL_serializer *cfg,
		   struct mpstream *stream, int index)
{
	if (luamp_encode(L, cfg, stream, index) != MP_ARRAY)
		tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);
}

char *
lbox_encode_tuple_on_gc(lua_State *L, int idx, size_t *p_len)
{
	struct region *gc = &fiber()->gc;
	size_t used = region_used(gc);
	struct mpstream stream;
	mpstream_init(&stream, gc, region_reserve_cb, region_alloc_cb);
	luamp_encode_tuple(L, luaL_msgpack_default, &stream, idx);
	mpstream_flush(&stream);
	*p_len = region_used(gc) - used;
	return (char *) region_join(gc, *p_len);
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

	uint32_t field_count = tuple_field_count(tuple);
	/* validate offset and len */
	if (offset == 0) {
		luaL_error(L, "tuple.transform(): offset is out of bound");
	} else if (offset < 0) {
		if (-offset > field_count)
			luaL_error(L, "tuple.transform(): offset is out of bound");
		offset += field_count;
	} else {
		--offset; /* offset is one-indexed */
		if (offset > field_count) {
			offset = field_count;
		}
	}
	if (len < 0)
		luaL_error(L, "tuple.transform(): len is negative");
	if (len > field_count - offset)
		len = field_count - offset;

	assert(offset + len <= field_count);

	/*
	 * Calculate the number of operations and length of UPDATE expression
	 */
	uint32_t op_cnt = 0;
	if (offset < field_count && len > 0)
		op_cnt++;
	if (argc > 3)
		op_cnt += argc - 3;

	if (op_cnt == 0) {
		/* tuple_update() does not accept an empty operation list. */
		lbox_pushtuple(L, tuple);
		return 1;
	}

	struct region *gc = &fiber()->gc;
	RegionGuard guard(gc);
	struct mpstream stream;
	mpstream_init(&stream, gc, region_reserve_cb, region_alloc_cb);
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
	size_t expr_len = region_used(gc) - guard.used;
	const char *expr = (char *) region_join(gc, expr_len);
	struct tuple *new_tuple = tuple_update(tuple_format_ber,
					       region_alloc_cb,
					       gc, tuple, expr,
					       expr + expr_len, 0);
	lbox_pushtuple(L, new_tuple);
	return 1;
}

void
lbox_pushtuple(struct lua_State *L, struct tuple *tuple)
{
	assert(CTID_CONST_STRUCT_TUPLE_REF != 0);
	struct tuple **ptr = (struct tuple **) luaL_pushcdata(L,
		CTID_CONST_STRUCT_TUPLE_REF, sizeof(struct tuple *));
	*ptr = tuple;
	/* The order is important - first reference tuple, next set gc */
	tuple_ref(tuple);
	lua_pushcfunction(L, lbox_tuple_gc);
	luaL_setcdatagc(L, -2);
}

static const struct luaL_reg lbox_tuple_meta[] = {
	{"__gc", lbox_tuple_gc},
	{"slice", lbox_tuple_slice},
	{"transform", lbox_tuple_transform},
	{NULL, NULL}
};

static const struct luaL_reg lbox_tuplelib[] = {
	{"new", lbox_tuple_new},
	{NULL, NULL}
};

static const struct luaL_reg lbox_tuple_iterator_meta[] = {
	{NULL, NULL}
};

/* }}} */

void
box_lua_tuple_init(struct lua_State *L)
{
	/* export C functions to Lua */
	luaL_newmetatable(L, tuplelib_name);
	luaL_register(L, NULL, lbox_tuple_meta);
	/* save Lua/C functions to the global variable (cleaned by tuple.lua) */
	lua_setglobal(L, "cfuncs");
	luaL_register_type(L, tuple_iteratorlib_name,
			   lbox_tuple_iterator_meta);
	luaL_register_module(L, tuplelib_name, lbox_tuplelib);
	lua_pop(L, 1);

	luamp_set_encode_extension(luamp_encode_extension_box);

	if (luaL_dostring(L, tuple_lua))
		panic("Error loading Lua source %.160s...: %s",
		      tuple_lua, lua_tostring(L, -1));
	assert(lua_gettop(L) == 0);

	/* Get CTypeIDs */
	CTID_CONST_STRUCT_TUPLE_REF = luaL_ctypeid(L, "const struct tuple &");

	box_lua_slab_init(L);
}

struct tuple *
boxffi_tuple_update(struct tuple *tuple, const char *expr, const char *expr_end)
{
	RegionGuard region_guard(&fiber()->gc);
	try {
		struct tuple *new_tuple = tuple_update(tuple_format_ber,
			region_alloc_cb, &fiber()->gc, tuple, expr, expr_end, 1);
		tuple_ref(new_tuple); /* must not throw in this case */
		return new_tuple;
	} catch (ClientError *e) {
		return NULL;
	}

}
struct tuple *
boxffi_tuple_upsert(struct tuple *tuple, const char *expr, const char *expr_end)
{
	RegionGuard region_guard(&fiber()->gc);
	try {
		struct tuple *new_tuple = tuple_upsert(tuple_format_ber,
			region_alloc_cb, &fiber()->gc, tuple, expr, expr_end, 1);
		tuple_ref(new_tuple); /* must not throw in this case */
		return new_tuple;
	} catch (ClientError *e) {
		return NULL;
	}
}
