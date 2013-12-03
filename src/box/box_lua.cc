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
#include "box_lua.h"

#include "lua/utils.h"
#include "lua/msgpack.h"
#include <fiber.h>
#include "box/box.h"
#include "request.h"
#include "txn.h"
#include "tuple_update.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lj_obj.h>
#include <lj_ctype.h>
#include <lj_cdata.h>
#include <lj_cconv.h>
#include <arpa/inet.h>
#include "bit/bit.h"
} /* extern "C" */

#include "pickle.h"
#include "tuple.h"
#include "schema.h"
#include "space.h"
#include "port.h"
#include "tbuf.h"
#include "scoped_guard.h"

#include "third_party/lua-yaml/lyaml.h"

/* contents of box.lua, misc.lua, box.net.lua respectively */
extern char schema_lua[], box_lua[], box_net_lua[], misc_lua[], sql_lua[];
static const char *lua_sources[] = { schema_lua, box_lua, box_net_lua, misc_lua, sql_lua, NULL };

/*
 * Functions, exported in box_lua.h should have prefix
 * "box_lua_"; functions, available in Lua "box"
 * should start with "lbox_".
 */

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
static int tuple_totable_mt_ref = 0; /* a precreated metable for totable() */

static void
lbox_pushtuple(struct lua_State *L, struct tuple *tuple);

static struct tuple *
lua_totuple(struct lua_State *L, int first, int last);

static inline struct tuple *
lua_checktuple(struct lua_State *L, int narg)
{
	struct tuple *t = *(struct tuple **) luaL_checkudata(L, narg, tuplelib_name);
	assert(t->refs);
	return t;
}

struct tuple *
lua_istuple(struct lua_State *L, int narg)
{
	if (lua_getmetatable(L, narg) == 0)
		return NULL;
	luaL_getmetatable(L, tuplelib_name);
	struct tuple *tuple = 0;
	if (lua_equal(L, -1, -2))
		tuple = *(struct tuple **) lua_touserdata(L, narg);
	lua_pop(L, 2);
	return tuple;
}

static int
lbox_tuple_new(lua_State *L)
{
	int argc = lua_gettop(L);
	if (unlikely(argc < 1)) {
		lua_newtable(L); /* create an empty tuple */
		++argc;
	}
	struct tuple *tuple = lua_totuple(L, 1, argc);
	lbox_pushtuple(L, tuple);
	return 1;
}

static int
lbox_tuple_gc(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	tuple_ref(tuple, -1);
	return 0;
}

static int
lbox_tuple_len(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	lua_pushnumber(L, tuple_arity(tuple));
	return 1;
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

	uint32_t arity = tuple_arity(tuple);
	offset = lua_tointeger(L, 2);
	if (offset >= 0 && offset < arity) {
		start = offset;
	} else if (offset < 0 && -offset <= arity) {
		start = offset + arity;
	} else {
		return luaL_error(L, "tuple.slice(): start >= field count");
	}

	if (argc == 2) {
		offset = lua_tointeger(L, 3);
		if (offset > 0 && offset <= arity) {
			end = offset;
		} else if (offset < 0 && -offset < arity) {
			end = offset + arity;
		} else {
			return luaL_error(L, "tuple.slice(): end > field count");
		}
	} else {
		end = arity;
	}
	if (end <= start)
		return luaL_error(L, "tuple.slice(): start must be less than end");

	struct tuple_iterator it;
	tuple_rewind(&it, tuple);
	const char *field;

	assert(start < arity);
	uint32_t field_no = start;
	field = tuple_seek(&it, start);
	while (field && field_no < end) {
		luamp_decode(L, &field);
		++field_no;
		field = tuple_next(&it);
	}
	assert(field_no == end);
	return end - start;
}

/* A MsgPack extensions handler that supports tuples */
static void
luamp_encode_extension_box(struct lua_State *L, int idx, struct tbuf *b)
{
	if (lua_type(L, idx) == LUA_TUSERDATA &&
			lua_istuple(L, idx)) {
		struct tuple *tuple = lua_checktuple(L, idx);
		tuple_to_tbuf(tuple, b);
		return;
	}

	tnt_raise(ClientError, ER_PROC_RET,
		  lua_typename(L, lua_type(L, idx)));
}

/*
 * A luamp_encode wrapper to support old Tarantool 1.5 API.
 * Will be removed after API change.
 */
static int
luamp_encodestack(struct lua_State *L, struct tbuf *b, int first, int last)
{
	if (first == last && (lua_istable(L, first) ||
	    (lua_isuserdata(L, first) && lua_istuple(L, first)))) {
		/* New format */
		luamp_encode(L, b, first);
		return 1;
	} else {
		/* Backward-compatible format */
		/* sic: if arg_count is 0, first > last */
		luamp_encode_array(b, last + 1 - first);
		for (int k = first; k <= last; ++k) {
			luamp_encode(L, b, k);
		}
		return last + 1 - first;
	}
}

static void *
tuple_update_region_alloc(void *alloc_ctx, size_t size)
{
	return region_alloc((struct region *) alloc_ctx, size);
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
	lua_Integer field_count = lua_tointeger(L, 3);

	uint32_t arity = tuple_arity(tuple);
	/* validate offset and len */
	if (offset < 0) {
		if (-offset > arity)
			luaL_error(L, "tuple.transform(): offset is out of bound");
		offset += arity;
	} else if (offset > arity) {
		offset = arity;
	}
	if (field_count < 0)
		luaL_error(L, "tuple.transform(): len is negative");
	if (field_count > arity - offset)
		field_count = arity - offset;

	assert(offset + field_count <= arity);

	/*
	 * Calculate the number of operations and length of UPDATE expression
	 */
	uint32_t op_cnt = 0;
	if (offset < arity && field_count > 0)
		op_cnt++;
	if (argc > 3)
		op_cnt += argc - 3;

	if (op_cnt == 0) {
		/* tuple_update() does not accept an empty operation list. */
		lbox_pushtuple(L, tuple);
		return 1;
	}

	RegionGuard region_guard(&fiber->gc);

	/*
	 * Prepare UPDATE expression
	 */
	struct tbuf *b = tbuf_new(&fiber->gc);
	tbuf_append(b, (char *) &op_cnt, sizeof(op_cnt));
	if (field_count > 0) {
		tbuf_ensure(b, sizeof(uint32_t) + 1 + 9);

		/* offset */
		char *data = pack_u32(b->data + b->size, offset);

		/* operation */
		*data++ = UPDATE_OP_DELETE;

		assert(data <= b->data + b->capacity);
		b->size = data - b->data;

		/* field: count */
		luamp_encode_uint(b, field_count);
	}

	for (int i = argc ; i > 3; i--) {
		tbuf_ensure(b, sizeof(uint32_t) + 1 + 10);

		/* offset */
		char *data = pack_u32(b->data + b->size, offset);

		/* operation */
		*data++ = UPDATE_OP_INSERT;

		assert(data <= b->data + b->capacity);
		b->size = data - b->data;

		/* field */
		luamp_encode(L, b, i);
	}

	/* Execute tuple_update */
	struct tuple *new_tuple = tuple_update(tuple_format_ber,
					       tuple_update_region_alloc,
					       &fiber->gc,
					       tuple, tbuf_str(b), tbuf_end(b));
	lbox_pushtuple(L, new_tuple);
	return 1;
}

/*
 * Tuple find function.
 *
 * Find each or one tuple field according to the specified key.
 *
 * Function returns indexes of the tuple fields that match
 * key criteria.
 *
 */
static int
lbox_tuple_find_do(struct lua_State *L, bool all)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	int argc = lua_gettop(L);
	size_t offset = 0;
	switch (argc - 1) {
	case 1: break;
	case 2:
		offset = lua_tointeger(L, 2);
		break;
	default:
		luaL_error(L, "tuple.find(): bad arguments");
	}

	int top = lua_gettop(L);
	int idx = offset;

	struct luaL_field arg;
	luaL_checkfield(L, 2, &arg);
	struct tuple_iterator it;
	tuple_rewind(&it, tuple);
	const char *field = tuple_seek(&it, idx);
	for (; field; field = tuple_next(&it), idx++) {
		bool found = false;
		const char *f = field;
		if (arg.type != mp_typeof(*field))
			continue;

		switch (arg.type) {
		case MP_UINT:
			found = (arg.ival == mp_decode_uint(&f));
			break;
		case MP_INT:
			found = (arg.ival == mp_decode_int(&f));
			break;
		case MP_BOOL:
			found = (arg.bval == mp_decode_bool(&f));
			break;
		case MP_DOUBLE:
			found = (arg.bval == mp_decode_double(&f));
			break;
		case MP_STR:
		{
			uint32_t len1 = 0;
			const char *s1 = mp_decode_str(&f, &len1);
			size_t len2 = arg.sval.len;
			const char *s2 = arg.sval.data;
			found = (len1 == len2) && (memcmp(s1, s2, len1) == 0);
			break;
		}
		default:
			break;
		}
		if (found) {
			lua_pushinteger(L, idx);
			if (!all)
				break;
		}
	}
	return lua_gettop(L) - top;
}

static int
lbox_tuple_find(struct lua_State *L)
{
	return lbox_tuple_find_do(L, false);
}

static int
lbox_tuple_findall(struct lua_State *L)
{
	return lbox_tuple_find_do(L, true);
}

static int
lbox_tuple_unpack(struct lua_State *L)
{
	int argc = lua_gettop(L);
	struct tuple *tuple = lua_checktuple(L, 1);

	struct tuple_iterator it;
	tuple_rewind(&it, tuple);
	const char *field;
	while ((field = tuple_next(&it)))
		luamp_decode(L, &field);

	assert(lua_gettop(L) == argc + tuple_arity(tuple));
	(void) argc;
	return tuple_arity(tuple);
}

static int
lbox_tuple_totable(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	lua_newtable(L);
	int index = 1;

	struct tuple_iterator it;
	tuple_rewind(&it, tuple);
	const char *field;
	while ((field = tuple_next(&it))) {
		lua_pushnumber(L, index++);
		luamp_decode(L, &field);
		lua_rawset(L, -3);
	}

	/* Hint serializer */
	assert(tuple_totable_mt_ref != 0);
	lua_rawgeti(L, LUA_REGISTRYINDEX, tuple_totable_mt_ref);
	lua_setmetatable(L, -2);

	return 1;
}

/**
 * Implementation of tuple __index metamethod.
 *
 * Provides operator [] access to individual fields for integer
 * indexes, as well as searches and invokes metatable methods
 * for strings.
 */
static int
lbox_tuple_index(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	/* For integer indexes, implement [] operator */
	if (lua_isnumber(L, 2)) {
		int i = luaL_checkint(L, 2);
		const char *field = tuple_field(tuple, i);
		if (field == NULL) {
			const char *data = tuple->data;
			luaL_error(L, "%s: index %d is out of bounds (0..%d)",
				   tuplelib_name, i, mp_decode_array(&data));
		}
		luamp_decode(L, &field);
		return 1;
	}

	/* If we got a string, try to find a method for it. */
	const char *sz = luaL_checkstring(L, 2);
	lua_getmetatable(L, 1);
	lua_getfield(L, -1, sz);
	return 1;
}

static int
lbox_tuple_tostring(struct lua_State *L)
{
	/*
	 * The method does next things:
	 * 1. Calls :unpack
	 * 2. Serializes the result using yaml
	 * 3. Strips start and end of yaml document symbols
	 */

	/* unpack */
	lbox_tuple_totable(L);

	/* serialize */
	lua_replace(L, 1);
	yamlL_encode(L);

	/* strip yaml tags */
	size_t len;
	const char *str = lua_tolstring(L, -1, &len);
	assert(strlen(str) == len);
	const char *s = index(str, '[');
	const char *e = rindex(str, ']');
	assert(s != NULL && e != NULL && s + 1 <= e);
	lua_pushlstring(L, s, e - s + 1);
	return 1;
}

static void
lbox_pushtuple(struct lua_State *L, struct tuple *tuple)
{
	if (tuple) {
		struct tuple **ptr = (struct tuple **)
				lua_newuserdata(L, sizeof(*ptr));
		luaL_getmetatable(L, tuplelib_name);
		lua_setmetatable(L, -2);
		*ptr = tuple;
		tuple_ref(tuple, 1);
	} else {
		lua_pushnil(L);
	}
}

/**
 * Sequential access to tuple fields. Since tuple is a list-like
 * structure, iterating over tuple fields is faster
 * than accessing fields using an index.
 */
static int
lbox_tuple_next(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	int argc = lua_gettop(L) - 1;

	struct tuple_iterator *it = NULL;
	if (argc == 0 || (argc == 1 && lua_type(L, 2) == LUA_TNIL)) {
		it = (struct tuple_iterator *) lua_newuserdata(L, sizeof(*it));
		assert(it != NULL);
		luaL_getmetatable(L, tuple_iteratorlib_name);
		lua_setmetatable(L, -2);
		tuple_rewind(it, tuple);
	} else if (argc == 1 && lua_type(L, 2) == LUA_TUSERDATA) {
		it = (struct tuple_iterator *)
			luaL_checkudata(L, 2, tuple_iteratorlib_name);
		assert(it != NULL);
		lua_pushvalue(L, 2);
	} else {
		return luaL_error(L, "tuple.next(): bad arguments");
	}

	const char *field = tuple_next(it);
	if (field == NULL) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}

	luamp_decode(L, &field);
	return 2;
}

/** Iterator over tuple fields. Adapt lbox_tuple_next
 * to Lua iteration conventions.
 */
static int
lbox_tuple_pairs(struct lua_State *L)
{
	lua_pushcfunction(L, lbox_tuple_next);
	lua_pushvalue(L, -2); /* tuple */
	lua_pushnil(L);
	return 3;
}


/** tuple:bsize()
 *
 */
static int
lbox_tuple_bsize(struct lua_State *L)
{
	struct tuple *tuple = lua_checktuple(L, 1);
	lua_pushnumber(L, tuple->bsize);
	return 1;
}

static const struct luaL_reg lbox_tuple_meta[] = {
	{"__gc", lbox_tuple_gc},
	{"__len", lbox_tuple_len},
	{"__index", lbox_tuple_index},
	{"__tostring", lbox_tuple_tostring},
	{"next", lbox_tuple_next},
	{"pairs", lbox_tuple_pairs},
	{"slice", lbox_tuple_slice},
	{"transform", lbox_tuple_transform},
	{"find", lbox_tuple_find},
	{"findall", lbox_tuple_findall},
	{"unpack", lbox_tuple_unpack},
	{"totable", lbox_tuple_totable},
	{"bsize", lbox_tuple_bsize},
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

	RegionGuard region_guard(&fiber->gc);

	/* What else do we have on the stack? */
	if (argc > 2 && (lua_type(L, 3) != LUA_TNIL)) {
		/* Single or multi- part key. */
		struct tbuf *b = tbuf_new(&fiber->gc);
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

	RegionGuard region_guard(&fiber->gc);
	struct tbuf *b = tbuf_new(&fiber->gc);

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

/* }}} */

/** {{{ Lua I/O: facilities to intercept box output
 * and push into Lua stack.
 */

struct port_lua
{
	struct port_vtab *vtab;
	struct lua_State *L;
};

static inline struct port_lua *
port_lua(struct port *port) { return (struct port_lua *) port; }

/*
 * For addU32/dupU32 do nothing -- the only uint32_t Box can give
 * us is tuple count, and we don't need it, since we intercept
 * everything into Lua stack first.
 * @sa port_add_lua_multret
 */

static void
port_lua_add_tuple(struct port *port, struct tuple *tuple,
		   uint32_t flags __attribute__((unused)))
{
	lua_State *L = port_lua(port)->L;
	try {
		lbox_pushtuple(L, tuple);
	} catch (...) {
		tnt_raise(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
	}
}

struct port_vtab port_lua_vtab = {
	port_lua_add_tuple,
	null_port_eof,
};

static struct port *
port_lua_create(struct lua_State *L)
{
	struct port_lua *port = (struct port_lua *)
			region_alloc(&fiber->gc, sizeof(struct port_lua));
	port->vtab = &port_lua_vtab;
	port->L = L;
	return (struct port *) port;
}

static struct tuple*
lua_totuple(struct lua_State *L, int first, int last)
{
	RegionGuard region_guard(&fiber->gc);
	struct tbuf *b = tbuf_new(&fiber->gc);
	try {
		luamp_encodestack(L, b, first, last);
	} catch (const Exception &e) {
		throw;
	} catch (...) {
		tnt_raise(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
	}
	const char *data = b->data;
	if (unlikely(mp_typeof(*data) != MP_ARRAY))
		tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);
	struct tuple *tuple = tuple_new(tuple_format_ber, &data, tbuf_end(b));
	return tuple;
}

static void
port_add_lua_ret(struct port *port, struct lua_State *L, int index)
{
	struct tuple *tuple = lua_totuple(L, index, index);
	TupleGuard guard(tuple);
	port_add_tuple(port, tuple, BOX_RETURN_TUPLE);
}

/**
 * Add all elements from Lua stack to fiber iov.
 *
 * To allow clients to understand a complex return from
 * a procedure, we are compatible with SELECT protocol,
 * and return the number of return values first, and
 * then each return value as a tuple.
 *
 * If a Lua stack contains at least one scalar, each
 * value on the stack is converted to a tuple. A Lua
 * is converted to a tuple with multiple fields.
 *
 * If the stack is a Lua table, each member of which is
 * not scalar, each member of the table is converted to
 * a tuple. This way very large lists of return values can
 * be used, since Lua stack size is limited by 8000 elements,
 * while Lua table size is pretty much unlimited.
 */
static void
port_add_lua_multret(struct port *port, struct lua_State *L)
{
	int nargs = lua_gettop(L);
	/** Check if we deal with a table of tables. */
	if (nargs == 1 && lua_istable(L, 1)) {
		/*
		 * The table is not empty and consists of tables
		 * or tuples. Treat each table element as a tuple,
		 * and push it.
		 */
		lua_pushnil(L);
		int has_keys = lua_next(L, 1);
		if (has_keys  &&
		    (lua_istable(L, -1) || lua_isuserdata(L, -1))) {

			do {
				port_add_lua_ret(port, L, lua_gettop(L));
				lua_pop(L, 1);
			} while (lua_next(L, 1));
			return;
		} else if (has_keys) {
			lua_pop(L, 1);
		}
	}
	for (int i = 1; i <= nargs; ++i) {
		port_add_lua_ret(port, L, i);
	}
}

/* }}} */

/**
 * The main extension provided to Lua by Tarantool/Box --
 * ability to call INSERT/UPDATE/SELECT/DELETE from within
 * a Lua procedure.
 *
 * This is a low-level API, and it expects
 * all arguments to be packed in accordance
 * with the binary protocol format (iproto
 * header excluded).
 *
 * Signature:
 * box.process(op_code, request)
 */
static int
lbox_process(lua_State *L)
{
	uint32_t op = lua_tointeger(L, 1); /* Get the first arg. */
	size_t sz;
	const char *req = luaL_checklstring(L, 2, &sz); /* Second arg. */
	if (op == CALL) {
		/*
		 * We should not be doing a CALL from within a CALL.
		 * To invoke one stored procedure from another, one must
		 * do it in Lua directly. This deals with
		 * infinite recursion, stack overflow and such.
		 */
		return luaL_error(L, "box.process(CALL, ...) is not allowed");
	}
	int top = lua_gettop(L); /* to know how much is added by rw_callback */

	size_t allocated_size = region_used(&fiber->gc);
	struct port *port_lua = port_lua_create(L);
	try {
		struct request request;
		request_create(&request, op, req, sz);
		box_process(port_lua, &request);

		/*
		 * This only works as long as port_lua doesn't
		 * use fiber->cleanup and fiber->gc.
		 */
		region_truncate(&fiber->gc, allocated_size);
	} catch (const Exception &e) {
		region_truncate(&fiber->gc, allocated_size);
		throw;
	}
	return lua_gettop(L) - top;
}

static int
lbox_raise(lua_State *L)
{
	if (lua_gettop(L) != 2)
		luaL_error(L, "box.raise(): bad arguments");
	uint32_t code = lua_tointeger(L, 1);
	if (!code)
		luaL_error(L, "box.raise(): unknown error code");
	const char *str = lua_tostring(L, 2);
	tnt_raise(ClientError, str, code);
	return 0;
}

/**
 * A helper to find a Lua function by name and put it
 * on top of the stack.
 */
static int
box_lua_find(lua_State *L, const char *name, const char *name_end)
{
	int index = LUA_GLOBALSINDEX;
	int objstack = 0;
	const char *start = name, *end;

	while ((end = (const char *) memchr(start, '.', name_end - start))) {
		lua_checkstack(L, 3);
		lua_pushlstring(L, start, end - start);
		lua_gettable(L, index);
		if (! lua_istable(L, -1))
			tnt_raise(ClientError, ER_NO_SUCH_PROC,
				  name_end - name, name);
		start = end + 1; /* next piece of a.b.c */
		index = lua_gettop(L); /* top of the stack */
	}

	/* box.something:method */
	if ((end = (const char *) memchr(start, ':', name_end - start))) {
		lua_checkstack(L, 3);
		lua_pushlstring(L, start, end - start);
		lua_gettable(L, index);
		if (! (lua_istable(L, -1) ||
			lua_islightuserdata(L, -1) || lua_isuserdata(L, -1) ))
				tnt_raise(ClientError, ER_NO_SUCH_PROC,
					  name_end - name, name);
		start = end + 1; /* next piece of a.b.c */
		index = lua_gettop(L); /* top of the stack */
		objstack = 1;
	}


	lua_pushlstring(L, start, name_end - start);
	lua_gettable(L, index);
	if (! lua_isfunction(L, -1)) {
		/* lua_call or lua_gettable would raise a type error
		 * for us, but our own message is more verbose. */
		tnt_raise(ClientError, ER_NO_SUCH_PROC,
			  name_end - name, name);
	}
	/* setting stack that it would contain only
	 * the function pointer. */
	if (index != LUA_GLOBALSINDEX) {
		lua_replace(L, 1);
		if (objstack)
			lua_replace(L, 2);
		lua_settop(L, 1 + objstack);
	}
	return 1 + objstack;
}


/**
 * A helper to find lua stored procedures for box.call.
 * box.call iteslf is pure Lua, to avoid issues
 * with infinite call recursion smashing C
 * thread stack.
 */

static int
lbox_call_loadproc(struct lua_State *L)
{
	const char *name;
	size_t name_len;
	name = lua_tolstring(L, 1, &name_len);
	return box_lua_find(L, name, name + name_len);
}

/**
 * Invoke a Lua stored procedure from the binary protocol
 * (implementation of 'CALL' command code).
 */
void
box_lua_execute(const struct request *request, struct txn *txn,
		struct port *port)
{
	(void) txn;
	lua_State *L = lua_newthread(tarantool_L);
	LuarefGuard coro_ref(tarantool_L);

	/* proc name */
	int oc = box_lua_find(L, request->c.procname,
			 request->c.procname + request->c.procname_len);
	/* Push the rest of args (a tuple). */
	const char *args = request->c.args;
	uint32_t arg_count = mp_decode_array(&args);
	luaL_checkstack(L, arg_count, "call: out of stack");

	for (uint32_t i = 0; i < arg_count; i++) {
		luamp_decode(L, &args);
	}
	lbox_call(L, arg_count + oc - 1, LUA_MULTRET);
	/* Send results of the called procedure to the client. */
	port_add_lua_multret(port, L);
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

/**
 * Convert box.pack() format specifier to Tarantool
 * binary protocol UPDATE opcode
 */
static char format_to_opcode(char format)
{
	switch (format) {
	case '=': return 0;
	case '+': return 1;
	case '&': return 2;
	case '^': return 3;
	case '|': return 4;
	case ':': return 5;
	case '#': return 6;
	case '!': return 7;
	case '-': return 8;
	default: return format;
	}
}

/**
 * Counterpart to @a format_to_opcode
 */
static char opcode_to_format(char opcode)
{
	switch (opcode) {
	case 0: return '=';
	case 1: return '+';
	case 2: return '&';
	case 3: return '^';
	case 4: return '|';
	case 5: return ':';
	case 6: return '#';
	case 7: return '!';
	case 8: return '-';
	default: return opcode;
	}
}

/**
 * To use Tarantool/Box binary protocol primitives from Lua, we
 * need a way to pack Lua variables into a binary representation.
 * We do it by exporting a helper function
 *
 * box.pack(format, args...)
 *
 * which takes the format, which is very similar to Perl 'pack'
 * format, and a list of arguments, and returns a binary string
 * which has the arguments packed according to the format.
 *
 * For example, a typical SELECT packet packs in Lua like this:
 *
 * pkt = box.pack("iiiiiip", -- pack format
 *                         0, -- space id
 *                         0, -- index id
 *                         0, -- offset
 *                         2^32, -- limit
 *                         1, -- number of SELECT arguments
 *                         1, -- tuple cardinality
 *                         key); -- the key to use for SELECT
 *
 * @sa doc/box-protocol.txt, binary protocol description
 * @todo: implement box.unpack(format, str), for testing purposes
 */
static int
lbox_pack(struct lua_State *L)
{
	const char *format = luaL_checkstring(L, 1);
	/* first arg comes second */
	int i = 2;
	int nargs = lua_gettop(L);
	size_t size;
	const char *str;

	RegionGuard region_guard(&fiber->gc);
	struct tbuf *b = tbuf_new(&fiber->gc);

	struct luaL_field field;
	double dbl;
	float flt;
	char *data;
	while (*format) {
		if (i > nargs)
			luaL_error(L, "box.pack: argument count does not match "
				   "the format");
		luaL_tofield(L, i, &field);
		switch (*format) {
		case 'B':
		case 'b':
			/* signed and unsigned 8-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "box.pack: expected 8-bit int");

			tbuf_append(b, (char *) &field.ival, sizeof(uint8_t));
			break;
		case 'S':
		case 's':
			/* signed and unsigned 16-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "box.pack: expected 16-bit int");

			tbuf_append(b, (char *) &field.ival, sizeof(uint16_t));
			break;
		case 'n':
			/* signed and unsigned 16-bit big endian integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "box.pack: expected 16-bit int");

			field.ival = (uint16_t) htons((uint16_t) field.ival);
			tbuf_append(b, (char *) &field.ival, sizeof(uint16_t));
			break;
		case 'I':
		case 'i':
			/* signed and unsigned 32-bit integers */
			if (field.type != MP_UINT && field.ival != MP_INT)
				luaL_error(L, "box.pack: expected 32-bit int");

			tbuf_append(b, (char *) &field.ival, sizeof(uint32_t));
			break;
		case 'N':
			/* signed and unsigned 32-bit big endian integers */
			if (field.type != MP_UINT && field.ival != MP_INT)
				luaL_error(L, "box.pack: expected 32-bit int");

			field.ival = htonl(field.ival);
			tbuf_append(b, (char *) &field.ival, sizeof(uint32_t));
			break;
		case 'L':
		case 'l':
			/* signed and unsigned 64-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "box.pack: expected 64-bit int");

			tbuf_append(b, (char *) &field.ival, sizeof(uint64_t));
			break;
		case 'Q':
		case 'q':
			/* signed and unsigned 64-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "box.pack: expected 64-bit int");

			field.ival = bswap_u64(field.ival);
			tbuf_append(b, (char *) &field.ival, sizeof(uint64_t));
			break;
		case 'd':
			dbl = (double) lua_tonumber(L, i);
			tbuf_append(b, (char *) &dbl, sizeof(double));
			break;
		case 'f':
			flt = (float) lua_tonumber(L, i);
			tbuf_append(b, (char *) &flt, sizeof(float));
			break;
		case 'A':
		case 'a':
			/* A sequence of bytes */
			str = luaL_checklstring(L, i, &size);
			tbuf_append(b, str, size);
			break;
		case 'P':
		case 'p':
			luamp_encode(L, b, i);
			break;
		case 'V':
		{
			int arg_count = luaL_checkint(L, i);
			if (i + arg_count > nargs)
				luaL_error(L, "box.pack: argument count does not match "
					   "the format");
			int first = i + 1;
			int last = i + arg_count;
			i += luamp_encodestack(L, b, first, last);
			break;
		}
		case '=':
			/* update tuple set foo = bar */
		case '+':
			/* set field += val */
		case '-':
			/* set field -= val */
		case '&':
			/* set field & =val */
		case '|':
			/* set field |= val */
		case '^':
			/* set field ^= val */
		case ':':
			/* splice */
		case '#':
			/* delete field */
		case '!':
			/* insert field */
			/* field no */
			tbuf_ensure(b, sizeof(uint32_t) + 1);
			data = b->data + b->size;

			data = pack_u32(data, lua_tointeger(L, i));
			*data++ = format_to_opcode(*format);

			assert(data <= b->data + b->capacity);
			b->size = data - b->data;
			break;
		default:
			luaL_error(L, "box.pack: unsupported pack "
				   "format specifier '%c'", *format);
		}
		i++;
		format++;
	}

	lua_pushlstring(L, tbuf_str(b), b->size);

	return 1;
}

const char *
box_unpack_response(struct lua_State *L, const char *s, const char *end)
{
	uint32_t tuple_count = pick_u32(&s, end);

	/* Unpack and push tuples. */
	while (tuple_count--) {
		uint32_t bsize = pick_u32(&s, end);
		const char *tend = s + bsize;
		if (tend > end)
			tnt_raise(IllegalParams, "incorrect packet length");

		const char *t = s;
		if (unlikely(!mp_check(&s, tend)))
			tnt_raise(ClientError, ER_INVALID_MSGPACK);
		if (unlikely(mp_typeof(*t) != MP_ARRAY))
			tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);
		struct tuple *tuple = tuple_new(tuple_format_ber, &t, tend);
		lbox_pushtuple(L, tuple);
	}
	return s;
}


static int
lbox_unpack(struct lua_State *L)
{
	size_t format_size = 0;
	const char *format = luaL_checklstring(L, 1, &format_size);
	const char *f = format;

	size_t str_size = 0;
	const char *str =  luaL_checklstring(L, 2, &str_size);
	const char *end = str + str_size;
	const char *s = str;

	int save_stacksize = lua_gettop(L);

	char charbuf;
	uint8_t  u8buf;
	uint16_t u16buf;
	uint32_t u32buf;
	double dbl;
	float flt;

#define CHECK_SIZE(cur) if (unlikely((cur) >= end)) {	                \
	luaL_error(L, "box.unpack('%c'): got %d bytes (expected: %d+)",	\
		   *f, (int) (end - str), (int) 1 + ((cur) - str));	\
}
	while (*f) {
		switch (*f) {
		case 'b':
			CHECK_SIZE(s);
			u8buf = *(uint8_t *) s;
			lua_pushnumber(L, u8buf);
			s++;
			break;
		case 's':
			CHECK_SIZE(s + 1);
			u16buf = *(uint16_t *) s;
			lua_pushnumber(L, u16buf);
			s += 2;
			break;
		case 'n':
			CHECK_SIZE(s + 1);
			u16buf = ntohs(*(uint16_t *) s);
			lua_pushnumber(L, u16buf);
			s += 2;
			break;
		case 'i':
			CHECK_SIZE(s + 3);
			u32buf = *(uint32_t *) s;
			lua_pushnumber(L, u32buf);
			s += 4;
			break;
		case 'N':
			CHECK_SIZE(s + 3);
			u32buf = ntohl(*(uint32_t *) s);
			lua_pushnumber(L, u32buf);
			s += 4;
			break;
		case 'l':
			CHECK_SIZE(s + 7);
			luaL_pushnumber64(L, *(uint64_t*) s);
			s += 8;
			break;
		case 'q':
			CHECK_SIZE(s + 7);
			luaL_pushnumber64(L, bswap_u64(*(uint64_t*) s));
			s += 8;
			break;
		case 'd':
			CHECK_SIZE(s + 7);
			dbl = *(double *) s;
			lua_pushnumber(L, dbl);
			s += 8;
			break;
		case 'f':
			CHECK_SIZE(s + 3);
			flt = *(float *) s;
			lua_pushnumber(L, flt);
			s += 4;
			break;
		case 'a':
		case 'A': /* The rest of the data is a Lua string. */
			lua_pushlstring(L, s, end - s);
			s = end;
			break;
		case 'P':
		case 'p':
		{
			const char *data = s;
			if (unlikely(!mp_check(&s, end)))
				tnt_raise(ClientError, ER_INVALID_MSGPACK);
			luamp_decode(L, &data);
			assert(data == s);
			break;
		}
		case '=':
			/* update tuple set foo = bar */
		case '+':
			/* set field += val */
		case '-':
			/* set field -= val */
		case '&':
			/* set field & =val */
		case '|':
			/* set field |= val */
		case '^':
			/* set field ^= val */
		case ':':
			/* splice */
		case '#':
			/* delete field */
		case '!':
			/* insert field */
			CHECK_SIZE(s + 4);

			/* field no */
			u32buf = *(uint32_t *) s;

			/* opcode */
			charbuf = *(s + 4);
			charbuf = opcode_to_format(charbuf);
			if (charbuf != *f) {
				luaL_error(L, "box.unpack('%s'): "
					   "unexpected opcode: "
					   "offset %d, expected '%c',"
					   "found '%c'",
					   format, s - str, *f, charbuf);
			}

			lua_pushnumber(L, u32buf);
			s += 5;
			break;

		case 'R': /* Unpack server response, IPROTO format. */
		{
			s = box_unpack_response(L, s, end);
			break;
		}
		default:
			luaL_error(L, "box.unpack: unsupported "
				   "format specifier '%c'", *f);
		}
		f++;
	}

	assert(s <= end);

	if (s != end) {
		luaL_error(L, "box.unpack('%s'): too many bytes: "
			   "unpacked %d, total %d",
			   format, s - str, str_size);
	}

	return lua_gettop(L) - save_stacksize;

#undef CHECK_SIZE
}

static const struct luaL_reg boxlib[] = {
	{"process", lbox_process},
	{"call_loadproc",  lbox_call_loadproc},
	{"raise", lbox_raise},
	{"pack", lbox_pack},
	{"unpack", lbox_unpack},
	{NULL, NULL}
};

void
schema_lua_init(struct lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_newtable(L);
	lua_setfield(L, -2, "schema");
	lua_getfield(L, -1, "schema");
	lua_pushnumber(L, SC_SCHEMA_ID);
	lua_setfield(L, -2, "SCHEMA_ID");
	lua_pushnumber(L, SC_SPACE_ID);
	lua_setfield(L, -2, "SPACE_ID");
	lua_pushnumber(L, SC_INDEX_ID);
	lua_setfield(L, -2, "INDEX_ID");
	lua_pushnumber(L, SC_SYSTEM_ID_MIN);
	lua_setfield(L, -2, "SYSTEM_ID_MIN");
	lua_pushnumber(L, SC_SYSTEM_ID_MAX);
	lua_setfield(L, -2, "SYSTEM_ID_MAX");
	lua_pushnumber(L, BOX_INDEX_MAX);
	lua_setfield(L, -2, "INDEX_MAX");
	lua_pushnumber(L, BOX_SPACE_MAX);
	lua_setfield(L, -2, "SPACE_MAX");
	lua_pushnumber(L, BOX_FIELD_MAX);
	lua_setfield(L, -2, "FIELD_MAX");
	lua_pushnumber(L, BOX_INDEX_FIELD_MAX);
	lua_setfield(L, -2, "INDEX_FIELD_MAX");
	lua_pushnumber(L, BOX_INDEX_PART_MAX);
	lua_setfield(L, -2, "INDEX_PART_MAX");
	lua_pushnumber(L, BOX_NAME_MAX);
	lua_setfield(L, -2, "NAME_MAX");
	lua_pushnumber(L, FORMAT_ID_MAX);
	lua_setfield(L, -2, "FORMAT_ID_MAX");
	lua_pop(L, 2); /* box, schema */
}

void
mod_lua_init(struct lua_State *L)
{
	/* box, box.tuple */
	luaL_register_type(L, tuplelib_name, lbox_tuple_meta);
	luaL_register(L, tuplelib_name, lbox_tuplelib);
	lua_pop(L, 1);
	schema_lua_init(L);
	luaL_register_type(L, tuple_iteratorlib_name,
				    lbox_tuple_iterator_meta);
	luaL_register(L, "box", boxlib);
	lua_pop(L, 1);
	/* box.index */
	luaL_register_type(L, indexlib_name, lbox_index_meta);
	luaL_register(L, "box.index", indexlib);
	box_index_init_iterator_types(L, -2);
	lua_pop(L, 1);
	luaL_register_type(L, iteratorlib_name, lbox_iterator_meta);

	/* Load Lua extension */
	for (const char **s = lua_sources; *s; s++) {
		if (luaL_dostring(L, *s))
			panic("Error loading Lua source %.160s...: %s",
			      *s, lua_tostring(L, -1));
	}

	luamp_set_encode_extension(luamp_encode_extension_box);



	/* Precreate a metatable for tuple_unpack */
	lua_newtable(L);
	lua_pushstring(L, "_serializer_compact");
	lua_pushboolean(L, true);
	lua_settable(L, -3);
	lua_pushstring(L, "_serializer_type");
	lua_pushstring(L, "array");
	lua_settable(L, -3);
	tuple_totable_mt_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	assert(tuple_totable_mt_ref != 0);

	assert(lua_gettop(L) == 0);
}
