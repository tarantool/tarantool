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
#include "box/lua/index.h"
#include "lua/utils.h"
#include "lua/msgpack.h"
#include "box/index.h"
#include "box/space.h"
#include "box/schema.h"
#include "box/user_def.h"
#include "box/tuple.h"
#include "box/lua/error.h"
#include "box/lua/tuple.h"
#include "fiber.h"
#include "iobuf.h"

/** {{{ box.index Lua library: access to spaces and indexes
 */

static int CTID_STRUCT_ITERATOR_REF = 0;

static inline Index *
check_index(uint32_t space_id, uint32_t index_id)
{
	struct space *space = space_cache_find(space_id);
	access_check_space(space, PRIV_R);
	return index_find(space, index_id);
}

size_t
boxffi_index_bsize(uint32_t space_id, uint32_t index_id)
{
       try {
               return check_index(space_id, index_id)->bsize();
       } catch (Exception *) {
               return (size_t) -1; /* handled by box.error() in Lua */
       }
}

size_t
boxffi_index_len(uint32_t space_id, uint32_t index_id)
{
	try {
		return check_index(space_id, index_id)->size();
	} catch (Exception *) {
		return (size_t) -1; /* handled by box.error() in Lua */
	}
}

static inline int
lbox_returntuple(lua_State *L, struct tuple *tuple)
{
	if (tuple == (struct tuple *) -1) {
		return lbox_error(L);
	} else if (tuple == NULL) {
		lua_pushnil(L);
		return 1;
	} else {
		lbox_pushtuple_noref(L, tuple);
		return 1;
	}
}

static inline struct tuple *
boxffi_returntuple(struct tuple *tuple)
{
	if (tuple == NULL)
		return NULL;
	tuple_ref(tuple);
	return tuple;
}

static inline char *
lbox_tokey(lua_State *L, int idx)
{
	struct obuf key_buf;
	obuf_create(&key_buf, &fiber()->gc, LUAMP_ALLOC_FACTOR);
	luamp_encode_tuple(L, luaL_msgpack_default, &key_buf, idx);
	return obuf_join(&key_buf);
}

struct tuple *
boxffi_index_random(uint32_t space_id, uint32_t index_id, uint32_t rnd)
{
	try {
		Index *index = check_index(space_id, index_id);
		struct tuple *tuple = index->random(rnd);
		return boxffi_returntuple(tuple);
	}  catch (Exception *) {
		return (struct tuple *) -1; /* handled by box.error() in Lua */
	}
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

	struct tuple *tuple = boxffi_index_random(space_id, index_id, rnd);
	return lbox_returntuple(L, tuple);
}

struct tuple *
boxffi_index_get(uint32_t space_id, uint32_t index_id, const char *key)
{
	try {
		Index *index = check_index(space_id, index_id);
		if (!index->key_def->is_unique)
			tnt_raise(ClientError, ER_MORE_THAN_ONE_TUPLE);
		uint32_t part_count = key ? mp_decode_array(&key) : 0;
		primary_key_validate(index->key_def, key, part_count);
		struct tuple *tuple = index->findByKey(key, part_count);
		return boxffi_returntuple(tuple);
	}  catch (Exception *) {
		return (struct tuple *) -1; /* handled by box.error() in Lua */
	}
}

static int
lbox_index_get(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		return luaL_error(L, "Usage index.get(space_id, index_id, key)");

	RegionGuard region_guard(&fiber()->gc);
	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	const char *key = lbox_tokey(L, 3);

	struct tuple *tuple = boxffi_index_get(space_id, index_id, key);
	return lbox_returntuple(L, tuple);
}

struct tuple *
boxffi_index_min(uint32_t space_id, uint32_t index_id, const char *key)
{
	try {
		Index *index = check_index(space_id, index_id);
		if (index->key_def->type != TREE) {
			/* Show nice error messages in Lua */
			tnt_raise(ClientError, ER_UNSUPPORTED,
				  index_type_strs[index->key_def->type],
				  "min()");
		}
		uint32_t part_count = key ? mp_decode_array(&key) : 0;
		key_validate(index->key_def, ITER_GE, key, part_count);
		struct tuple *tuple = index->min(key, part_count);
		return boxffi_returntuple(tuple);
	}  catch (Exception *) {
		return (struct tuple *) -1; /* handled by box.error() in Lua */
	}
}

static int
lbox_index_min(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		return luaL_error(L, "usage index.min(space_id, index_id, key)");

	RegionGuard region_guard(&fiber()->gc);
	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	const char *key = lbox_tokey(L, 3);

	struct tuple *tuple = boxffi_index_min(space_id, index_id, key);
	return lbox_returntuple(L, tuple);
}

struct tuple *
boxffi_index_max(uint32_t space_id, uint32_t index_id, const char *key)
{
	try {
		Index *index = check_index(space_id, index_id);
		if (index->key_def->type != TREE) {
			/* Show nice error messages in Lua */
			tnt_raise(ClientError, ER_UNSUPPORTED,
				  index_type_strs[index->key_def->type],
				  "max()");
		}
		uint32_t part_count = key ? mp_decode_array(&key) : 0;
		key_validate(index->key_def, ITER_LE, key, part_count);
		struct tuple *tuple = index->max(key, part_count);
		return boxffi_returntuple(tuple);
	}  catch (Exception *) {
		return (struct tuple *) -1; /* handled by box.error() in Lua */
	}
}

static int
lbox_index_max(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		return luaL_error(L, "usage index.max(space_id, index_id, key)");

	RegionGuard region_guard(&fiber()->gc);
	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	const char *key = lbox_tokey(L, 3);

	struct tuple *tuple = boxffi_index_max(space_id, index_id, key);
	return lbox_returntuple(L, tuple);
}

ssize_t
boxffi_index_count(uint32_t space_id, uint32_t index_id, int type, const char *key)
{
	enum iterator_type itype = (enum iterator_type) type;
	try {
		Index *index = check_index(space_id, index_id);
		uint32_t part_count = key ? mp_decode_array(&key) : 0;
		key_validate(index->key_def, itype, key, part_count);
		return index->count(itype, key, part_count);
	} catch (Exception *) {
		return -1; /* handled by box.error() in Lua */
	}
}
static int
lbox_index_count(lua_State *L)
{
	if (lua_gettop(L) != 4 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    !lua_isnumber(L, 3)) {
		return luaL_error(L, "usage index.count(space_id, index_id, "
		       "iterator, key)");
	}

	RegionGuard region_guard(&fiber()->gc);
	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	uint32_t iterator = lua_tointeger(L, 3);
	const char *key = lbox_tokey(L, 4);

	ssize_t count = boxffi_index_count(space_id, index_id,
		iterator, key);
	if (count == -1)
		return lbox_error(L);
	lua_pushinteger(L, count);
	return 1;
}

static void
box_index_init_iterator_types(struct lua_State *L, int idx)
{
	for (int i = 0; i < iterator_type_MAX; i++) {
		assert(strncmp(iterator_type_strs[i], "ITER_", 5) == 0);
		lua_pushnumber(L, i);
		/* cut ITER_ prefix from enum name */
		lua_setfield(L, idx, iterator_type_strs[i] + 5);
	}
}

/* }}} */

/* {{{ box.index.iterator Lua library: index iterators */

struct iterator *
boxffi_index_iterator(uint32_t space_id, uint32_t index_id, int type,
		      const char *key)
{
	struct iterator *it = NULL;
	enum iterator_type itype = (enum iterator_type) type;
	try {
		Index *index = check_index(space_id, index_id);
		assert(mp_typeof(*key) == MP_ARRAY); /* checked by Lua */
		uint32_t part_count = mp_decode_array(&key);
		key_validate(index->key_def, itype, key, part_count);
		it = index->allocIterator();
		index->initIterator(it, itype, key, part_count);
		it->sc_version = sc_version;
		it->space_id = space_id;
		it->index_id = index_id;
		it->index = index;
		return it;
	} catch (Exception *) {
		if (it)
			it->free(it);
		/* will be handled by box.error() in Lua */
		return NULL;
	}
}

static int
lbox_index_iterator(lua_State *L)
{
	if (lua_gettop(L) != 4 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    !lua_isnumber(L, 3))
		return luaL_error(L, "usage index.iterator(space_id, index_id, type, key)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	uint32_t iterator = lua_tointeger(L, 3);
	/* const char *key = lbox_tokey(L, 4); */
	const char *mpkey = lua_tolstring(L, 4, NULL); /* Key encoded by Lua */
	struct iterator *it = boxffi_index_iterator(space_id, index_id,
		iterator, mpkey);
	if (it == NULL)
		return lbox_error(L);

	assert(CTID_STRUCT_ITERATOR_REF != 0);
	struct iterator **ptr = (struct iterator **) luaL_pushcdata(L,
		CTID_STRUCT_ITERATOR_REF, sizeof(struct iterator *));
	*ptr = it; /* NULL handled by Lua, gc also set by Lua */
	return 1;
}

struct tuple*
boxffi_iterator_next(struct iterator *itr)
{
	try {
		if (itr->sc_version != sc_version) {
			Index *index = check_index(itr->space_id, itr->index_id);
			if (index != itr->index)
				return NULL;
			if (index->sc_version > itr->sc_version)
				return NULL;
			itr->sc_version = sc_version;
		}
	} catch (Exception *) {
		return NULL; /* invalidate iterator */
	}
	try {
		struct tuple *tuple = itr->next(itr);
		if (tuple == NULL)
			return NULL;
		tuple_ref(tuple); /* must not throw in this case */
		return tuple;
	} catch (Exception *) {
		return (struct tuple *) -1; /* handled by box.error() in Lua */
	}
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
	struct tuple *tuple = boxffi_iterator_next(itr);
	return lbox_returntuple(L, tuple);
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
