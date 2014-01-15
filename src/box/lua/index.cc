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
static const char *iteratorlib_name = "box.index.iterator";

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

static struct iterator *
lbox_checkiterator(struct lua_State *L, int i)
{
	struct iterator **it = (struct iterator **)
			luaL_checkudata(L, i, iteratorlib_name);
	assert(it != NULL);
	return *it;
}

static void
lbox_pushiterator(struct lua_State *L, Index *index,
		  struct iterator *it, enum iterator_type type,
		  const char *key, size_t key_size, uint32_t part_count)
{
	struct lbox_iterator_udata {
		struct iterator *it;
		char key[];
	};

	struct lbox_iterator_udata *udata = (struct lbox_iterator_udata *)
		lua_newuserdata(L, sizeof(*udata) + key_size);
	luaL_getmetatable(L, iteratorlib_name);
	lua_setmetatable(L, -2);

	udata->it = it;
	if (key) {
		memcpy(udata->key, key, key_size);
		key = udata->key;
	}
	key_validate(index->key_def, type, key, part_count);
	index->initIterator(it, type, key, part_count);
}

static int
lbox_iterator_gc(struct lua_State *L)
{
	struct iterator *it = lbox_checkiterator(L, -1);
	it->free(it);
	return 0;
}

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
lbox_index_min(struct lua_State *L)
{
	Index *index = lua_checkindex(L, 1);
	lbox_pushtuple(L, index->min());
	return 1;
}

static int
lbox_index_max(struct lua_State *L)
{
	Index *index = lua_checkindex(L, 1);
	lbox_pushtuple(L, index->max());
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


/*
 * Lua iterator over a Taratnool/Box index.
 *
 *	(iteration_state, tuple) = index.next(index, [params])
 *
 * When [params] are absent or nil
 * returns a pointer to a new ALL iterator and
 * to the first tuple (or nil, if the index is
 * empty).
 *
 * When [params] is a userdata,
 * i.e. we're inside an iteration loop, retrieves
 * the next tuple from the iterator.
 *
 * Otherwise, [params] can be used to seed
 * a new iterator with iterator type and
 * type-specific arguments. For exaple,
 * for GE iterator, a list of Lua scalars
 * cann follow the box.index.GE: this will
 * start iteration from the offset specified by
 * the given (multipart) key.
 *
 * @return Returns an iterator object, either created
 *         or taken from Lua stack.
 */

static inline struct iterator *
lbox_create_iterator(struct lua_State *L)
{
	Index *index = lua_checkindex(L, 1);
	int argc = lua_gettop(L);

	/* Create a new iterator. */
	enum iterator_type type = ITER_ALL;
	uint32_t key_part_count = 0;
	const char *key = NULL;
	size_t key_size = 0;

	if (argc > 1 && lua_type(L, 2) != LUA_TNIL) {
		type = (enum iterator_type) luaL_checkint(L, 2);
		if (type < ITER_ALL || type >= iterator_type_MAX)
			luaL_error(L, "unknown iterator type: %d", type);
	}

	RegionGuard region_guard(&fiber_self()->gc);

	/* What else do we have on the stack? */
	if (argc > 2 && (lua_type(L, 3) != LUA_TNIL)) {
		/* Single or multi- part key. */
		struct tbuf *b = tbuf_new(&fiber_self()->gc);
		luamp_encodestack(L, b, 3, argc);
		key = b->data;
		assert(b->size > 0);
		if (unlikely(mp_typeof(*key) != MP_ARRAY))
			tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);
		key_part_count = mp_decode_array(&key);
		key_size = b->data + b->size - key;
	}

	struct iterator *it = index->allocIterator();
	lbox_pushiterator(L, index, it, type, key, key_size, key_part_count);
	return it;
}

/**
 * Lua-style next() function, for use in pairs().
 * @example:
 * for k, v in box.space[0].index[0].idx.next, box.space[0].index[0].idx, nil do
 *	print(v)
 * end
 */
static int
lbox_index_next(struct lua_State *L)
{
	int argc = lua_gettop(L);
	struct iterator *it = NULL;
	if (argc == 2 && lua_type(L, 2) == LUA_TUSERDATA) {
		/*
		 * Apart from the index itself, we have only one
		 * other argument, and it's a userdata: must be
		 * iteration state created before.
		 */
		it = lbox_checkiterator(L, 2);
	} else {
		it = lbox_create_iterator(L);
	}
	struct tuple *tuple = it->next(it);
	/* If tuple is NULL, pushes nil as end indicator. */
	lbox_pushtuple(L, tuple);
	return tuple ? 2 : 1;
}

/** iterator() closure function. */
static int
lbox_index_iterator_closure(struct lua_State *L)
{
	/* Extract closure arguments. */
	struct iterator *it = lbox_checkiterator(L, lua_upvalueindex(1));

	struct tuple *tuple = it->next(it);

	/* If tuple is NULL, push nil as end indicator. */
	lbox_pushtuple(L, tuple);
	return 1;
}

/**
 * @brief Create iterator closure over a Taratnool/Box index.
 * @example lua it = box.space[0].index[0]:iterator(box.index.GE, 1);
 *   print(it(), it()).
 * @param L lua stack
 * @see http://www.lua.org/pil/7.1.html
 * @return number of return values put on the stack
 */
static int
lbox_index_iterator(struct lua_State *L)
{
	/* Create iterator and push it onto the stack. */
	(void) lbox_create_iterator(L);
	lua_pushcclosure(L, &lbox_index_iterator_closure, 1);
	return 1;
}


/**
 * Lua index subtree count function.
 * Iterate over an index, count the number of tuples which equal the
 * provided search criteria. The argument can either point to a
 * tuple, a key, or one or more key parts. Returns the number of matched
 * tuples.
 */
static int
lbox_index_count(struct lua_State *L)
{
	Index *index = lua_checkindex(L, 1);
	int argc = lua_gettop(L);
	if (argc == 0)
		luaL_error(L, "index.count(): one or more arguments expected");

	/* preparing single or multi-part key */
	if (argc == 1 || (argc == 2 && lua_type(L, 2) == LUA_TNIL)) {
		/* Nothing */
		/* Return index size */
		lua_pushnumber(L, index->size());
		return 1;
	}

	RegionGuard region_guard(&fiber_self()->gc);
	struct tbuf *b = tbuf_new(&fiber_self()->gc);

	/* Single or multi- part key. */
	luamp_encodestack(L, b, 2, argc);

	const char *key = b->data;
	if (unlikely(mp_typeof(*key) != MP_ARRAY))
		tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);
	uint32_t part_count = mp_decode_array(&key);
	key_validate(index->key_def, ITER_EQ, key, part_count);

	/* Prepare index iterator */
	struct iterator *it = index->position();
	index->initIterator(it, ITER_EQ, key, part_count);
	/* Iterate over the index and count tuples. */
	struct tuple *tuple;
	uint32_t count = 0;
	while ((tuple = it->next(it)) != NULL)
		count++;

	/* Return subtree size */
	lua_pushnumber(L, count);
	return 1;
}

static const struct luaL_reg lbox_index_meta[] = {
	{"__tostring", lbox_index_tostring},
	{"__len", lbox_index_len},
	{"part_count", lbox_index_part_count},
	{"min", lbox_index_min},
	{"max", lbox_index_max},
	{"random", lbox_index_random},
	{"next", lbox_index_next},
	{"iterator", lbox_index_iterator},
	{"count", lbox_index_count},
	{NULL, NULL}
};

static const struct luaL_reg indexlib [] = {
	{"bind", lbox_index_bind},
	{NULL, NULL}
};

static const struct luaL_reg lbox_iterator_meta[] = {
	{"__gc", lbox_iterator_gc},
	{NULL, NULL}
};


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

void
box_lua_index_init(struct lua_State *L)
{
	/* box.index */
	luaL_register_type(L, indexlib_name, lbox_index_meta);
	luaL_register(L, "box.index", indexlib);
	box_index_init_iterator_types(L, -2);
	lua_pop(L, 1);
	luaL_register_type(L, iteratorlib_name, lbox_iterator_meta);
}
