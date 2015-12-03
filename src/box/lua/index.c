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
#include "box/lua/index.h"

#include "lua/utils.h"
#include "box/box.h"
#include "box/index.h"
#include "box/lua/tuple.h"
#include "box/lua/misc.h" /* lbox_encode_tuple_on_gc() */

/** {{{ box.index Lua library: access to spaces and indexes
 */

static int CTID_STRUCT_ITERATOR_REF = 0;

static int
lbox_insert(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isnumber(L, 1))
		return luaL_error(L, "Usage space:insert(tuple)");

	uint32_t space_id = lua_tointeger(L, 1);
	size_t tuple_len;
	const char *tuple = lbox_encode_tuple_on_gc(L, 2, &tuple_len);

	struct tuple *result;
	if (box_insert(space_id, tuple, tuple + tuple_len, &result) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, result);
}

static int
lbox_replace(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isnumber(L, 1))
		return luaL_error(L, "Usage space:replace(tuple)");

	uint32_t space_id = lua_tointeger(L, 1);
	size_t tuple_len;
	const char *tuple = lbox_encode_tuple_on_gc(L, 2, &tuple_len);

	struct tuple *result;
	if (box_replace(space_id, tuple, tuple + tuple_len, &result) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, result);
}

static int
lbox_index_update(lua_State *L)
{
	if (lua_gettop(L) != 4 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    lua_type(L, 3) != LUA_TTABLE || lua_type(L, 4) != LUA_TTABLE)
		return luaL_error(L, "Usage index:update(key, ops)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);
	size_t ops_len;
	const char *ops = lbox_encode_tuple_on_gc(L, 4, &ops_len);

	struct tuple *result;
	if (box_update(space_id, index_id, key, key + key_len,
		       ops, ops + ops_len, 1, &result) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, result);
}

static int
lbox_index_upsert(lua_State *L)
{
	if (lua_gettop(L) != 4 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    lua_type(L, 3) != LUA_TTABLE || lua_type(L, 4) != LUA_TTABLE)
		return luaL_error(L, "Usage index:upsert(tuple_key, ops)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	size_t tuple_len;
	const char *tuple = lbox_encode_tuple_on_gc(L, 3, &tuple_len);
	size_t ops_len;
	const char *ops = lbox_encode_tuple_on_gc(L, 4, &ops_len);

	struct tuple *result;
	if (box_upsert(space_id, index_id, tuple, tuple + tuple_len,
		       ops, ops + ops_len, 1, &result) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, result);
}

static int
lbox_index_delete(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    lua_type(L, 3) != LUA_TTABLE)
		return luaL_error(L, "Usage space:delete(key)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);

	struct tuple *result;
	if (box_delete(space_id, index_id, key, key + key_len, &result) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, result);
}

static int
lbox_index_random(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    !lua_isnumber(L, 3))
		return luaL_error(L, "Usage index.random(space_id, index_id, rnd)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	uint32_t rnd = lua_tointeger(L, 3);

	struct tuple *tuple;
	if (box_index_random(space_id, index_id, rnd, &tuple) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, tuple);
}

static int
lbox_index_get(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		return luaL_error(L, "Usage index.get(space_id, index_id, key)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);

	struct tuple *tuple;
	if (box_index_get(space_id, index_id, key, key + key_len, &tuple) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, tuple);
}

static int
lbox_index_min(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		return luaL_error(L, "usage index.min(space_id, index_id, key)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);

	struct tuple *tuple;
	if (box_index_min(space_id, index_id, key, key + key_len, &tuple) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, tuple);
}

static int
lbox_index_max(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		return luaL_error(L, "usage index.max(space_id, index_id, key)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);

	struct tuple *tuple;
	if (box_index_max(space_id, index_id, key, key + key_len, &tuple) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, tuple);
}

static int
lbox_index_count(lua_State *L)
{
	if (lua_gettop(L) != 4 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    !lua_isnumber(L, 3)) {
		return luaL_error(L, "usage index.count(space_id, index_id, "
		       "iterator, key)");
	}

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	uint32_t iterator = lua_tointeger(L, 3);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 4, &key_len);

	ssize_t count = box_index_count(space_id, index_id, iterator, key,
					key + key_len);
	if (count == -1)
		return lbox_error(L);
	lua_pushinteger(L, count);
	return 1;
}

static void
box_index_init_iterator_types(struct lua_State *L, int idx)
{
	for (int i = 0; i < iterator_type_MAX; i++) {
		lua_pushnumber(L, i);
		lua_setfield(L, idx, iterator_type_strs[i]);
	}
}

/* }}} */

/* {{{ box.index.iterator Lua library: index iterators */

static int
lbox_index_iterator(lua_State *L)
{
	if (lua_gettop(L) != 4 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    !lua_isnumber(L, 3))
		return luaL_error(L, "usage index.iterator(space_id, index_id, type, key)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	uint32_t iterator = lua_tointeger(L, 3);
	size_t mpkey_len;
	const char *mpkey = lua_tolstring(L, 4, &mpkey_len); /* Key encoded by Lua */
	/* const char *key = lbox_encode_tuple_on_gc(L, 4, key_len); */
	struct iterator *it = box_index_iterator(space_id, index_id, iterator,
						 mpkey, mpkey + mpkey_len);
	if (it == NULL)
		return lbox_error(L);

	assert(CTID_STRUCT_ITERATOR_REF != 0);
	struct iterator **ptr = (struct iterator **) luaL_pushcdata(L,
		CTID_STRUCT_ITERATOR_REF);
	*ptr = it; /* NULL handled by Lua, gc also set by Lua */
	return 1;
}

static int
lbox_iterator_next(lua_State *L)
{
	/* first argument is key buffer */
	if (lua_gettop(L) < 1 || lua_type(L, 1) != LUA_TCDATA)
		return luaL_error(L, "usage: next(state)");

	assert(CTID_STRUCT_ITERATOR_REF != 0);
	uint32_t ctypeid;
	void *data = luaL_checkcdata(L, 1, &ctypeid);
	if (ctypeid != CTID_STRUCT_ITERATOR_REF)
		return luaL_error(L, "usage: next(state)");

	struct iterator *itr = *(struct iterator **) data;
	struct tuple *tuple;
	if (box_iterator_next(itr, &tuple) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, tuple);
}

/* }}} */

void
box_lua_index_init(struct lua_State *L)
{
	/* Get CTypeIDs */
	int rc = luaL_cdef(L, "struct iterator;");
	assert(rc == 0);
	(void) rc;
	CTID_STRUCT_ITERATOR_REF = luaL_ctypeid(L, "struct iterator&");
	assert(CTID_STRUCT_ITERATOR_REF != 0);

	static const struct luaL_reg indexlib [] = {
		{NULL, NULL}
	};

	/* box.index */
	luaL_register_module(L, "box.index", indexlib);
	box_index_init_iterator_types(L, -2);
	lua_pop(L, 1);

	static const struct luaL_reg boxlib_internal[] = {
		{"insert", lbox_insert},
		{"replace",  lbox_replace},
		{"update", lbox_index_update},
		{"upsert",  lbox_index_upsert},
		{"delete",  lbox_index_delete},
		{"random", lbox_index_random},
		{"get",  lbox_index_get},
		{"min", lbox_index_min},
		{"max", lbox_index_max},
		{"count", lbox_index_count},
		{"iterator", lbox_index_iterator},
		{"iterator_next", lbox_iterator_next},
		{NULL, NULL}
	};

	luaL_register(L, "box.internal", boxlib_internal);
	lua_pop(L, 1);
}
