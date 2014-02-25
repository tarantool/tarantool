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
#include "box/index.h"
#include "box/space.h"
#include "box/schema.h"
#include "box/lua/tuple.h"
#include "fiber.h"
#include "tbuf.h"

/** {{{ box.index Lua library: access to spaces and indexes
 */

static const char *indexlib_name = "box.index";

/* Index userdata. */
struct lbox_index
{
	Index *index;
	/* space id. */
	uint32_t id;
	/* index id. */
	uint32_t iid;
	/* space cache version at the time of push. */
	int sc_version;
};

static Index *
lua_checkindex(struct lua_State *L, int i)
{
	struct lbox_index *index =
		(struct lbox_index *) luaL_checkudata(L, i, indexlib_name);
	assert(index != NULL);
	if (index->sc_version != sc_version) {
		index->index = index_find(space_find(index->id), index->iid);
		index->sc_version = sc_version;
	}
	return index->index;
}

static int
lbox_index_bind(struct lua_State *L)
{
	uint32_t id = (uint32_t) luaL_checkint(L, 1); /* get space id */
	uint32_t iid = (uint32_t) luaL_checkint(L, 2); /* get index id in */
	/* locate the appropriate index */
	struct space *space = space_find(id);
	Index *i = index_find(space, iid);

	/* create a userdata object */
	struct lbox_index *index = (struct lbox_index *)
		lua_newuserdata(L, sizeof(struct lbox_index));
	index->id = id;
	index->iid = iid;
	index->sc_version = sc_version;
	index->index = i;
	/* set userdata object metatable to indexlib */
	luaL_getmetatable(L, indexlib_name);
	lua_setmetatable(L, -2);

	return 1;
}

static int
lbox_index_tostring(struct lua_State *L)
{
	Index *index = lua_checkindex(L, 1);
	lua_pushfstring(L, " index %d", (int) index_id(index));
	return 1;
}

static int
lbox_index_len(struct lua_State *L)
{
	Index *index = lua_checkindex(L, 1);
	lua_pushinteger(L, index->size());
	return 1;
}

static int
lbox_index_part_count(struct lua_State *L)
{
	Index *index = lua_checkindex(L, 1);
	lua_pushinteger(L, index->key_def->part_count);
	return 1;
}

static int
lbox_index_random(struct lua_State *L)
{
	if (lua_gettop(L) != 2 || lua_isnil(L, 2))
		luaL_error(L, "Usage: index:random((uint32) rnd)");

	Index *index = lua_checkindex(L, 1);
	uint32_t rnd = lua_tointeger(L, 2);
	lbox_pushtuple(L, index->random(rnd));
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
		struct space *space = space_find(space_id);
		Index *index = index_find(space, index_id);
		assert(mp_typeof(*key) == MP_ARRAY); /* checked by Lua */
		uint32_t part_count = mp_decode_array(&key);
		key_validate(index->key_def, itype, key, part_count);
		it = index->allocIterator();
		index->initIterator(it, itype, key, part_count);
		return it;
	} catch(Exception *) {
		if (it)
			it->free(it);
		/* will be hanled by box.raise() in Lua */
		return NULL;
	}
}

/* }}} */

void
box_lua_index_init(struct lua_State *L)
{
	static const struct luaL_reg lbox_index_meta[] = {
		{"__tostring", lbox_index_tostring},
		{"__len", lbox_index_len},
		{"part_count", lbox_index_part_count},
		{"random", lbox_index_random},
		{NULL, NULL}
	};

	static const struct luaL_reg indexlib [] = {
		{"bind", lbox_index_bind},
		{NULL, NULL}
	};


	/* box.index */
	luaL_register_type(L, indexlib_name, lbox_index_meta);
	luaL_register(L, "box.index", indexlib);
	box_index_init_iterator_types(L, -2);
	lua_pop(L, 1);
}
