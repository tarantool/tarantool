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
#include "lua/init.h"
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
#include <lib/bit/bit.h>
} /* extern "C" */

#include "pickle.h"
#include "tuple.h"
#include "space.h"
#include "port.h"
#include "tbuf.h"
#include "scoped_guard.h"

/* contents of box.lua, misc.lua, box.net.lua respectively */
extern char box_lua[], box_net_lua[], misc_lua[], sql_lua[];
static const char *lua_sources[] = { box_lua, box_net_lua, misc_lua, sql_lua, NULL };

/**
 * All box connections share the same Lua state. We use
 * Lua coroutines (lua_newthread()) to have multiple
 * procedures running at the same time.
 */
static lua_State *root_L;

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

static void
lbox_pushtuple(struct lua_State *L, struct tuple *tuple);

static struct tuple *
lua_totuple(struct lua_State *L, int index);

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
	if (argc < 1)
		luaL_error(L, "tuple.new(): bad arguments");
	struct tuple *tuple = lua_totuple(L, 1);
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
	lua_pushnumber(L, tuple->field_count);
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

	offset = lua_tointeger(L, 2);
	if (offset >= 0 && offset < tuple->field_count) {
		start = offset;
	} else if (offset < 0 && -offset <= tuple->field_count) {
		start = offset + tuple->field_count;
	} else {
		return luaL_error(L, "tuple.slice(): start >= field count");
	}

	if (argc == 2) {
		offset = lua_tointeger(L, 3);
		if (offset > 0 && offset <= tuple->field_count) {
			end = offset;
		} else if (offset < 0 && -offset < tuple->field_count) {
			end = offset + tuple->field_count;
		} else {
			return luaL_error(L, "tuple.slice(): end > field count");
		}
	} else {
		end = tuple->field_count;
	}
	if (end <= start)
		return luaL_error(L, "tuple.slice(): start must be less than end");

	struct tuple_iterator it;
	tuple_rewind(&it, tuple);
	const char *field;
	uint32_t len;

	assert(start < tuple->field_count);
	uint32_t field_no = start;
	field = tuple_seek(&it, start, &len);
	while (field && field_no < end) {
		lua_pushlstring(L, field, len);
		++field_no;
		field = tuple_next(&it, &len);
	}
	assert(field_no == end);
	return end - start;
}

/** A single value on the Lua stack. */
struct lua_field {
	const char *data;
	uint32_t len;
	union {
		uint32_t u32;
		uint64_t u64;
	};
	enum field_type type;
};

/**
 * Convert a value on the lua stack to a Tarantool data type.
 */
static void
lua_tofield(lua_State *L, int i, struct lua_field *field)
{
	double num;
	size_t size;
	switch (lua_type(L, i)) {
	case LUA_TNUMBER:
		num = lua_tonumber(L, i);
		if (num <= UINT32_MAX && num >= INT32_MIN) {
			field->u32 = (uint32_t) num;
			field->data = (const char *) &field->u32;
			field->len = sizeof(uint32_t);
			field->type = NUM;
			return;
		} else {
			field->u64 = (uint64_t) num;
			field->data = (const char *) &field->u64;
			field->len = sizeof(uint64_t);
			field->type = NUM64;
			return;
		}
	case LUA_TCDATA:
		field->u64 = tarantool_lua_tointeger64(L, i);
		field->data = (const char *) &field->u64;
		field->len = sizeof(uint64_t);
		field->type = NUM64;
		return;
	case LUA_TBOOLEAN:
		if (lua_toboolean(L, i)) {
			field->data = "true";
			field->len = 4;
		} else {
			field->data = "false";
			field->len = 5;
		}
		field->type = STRING;
		return;
	case LUA_TNIL:
		field->data = "nil";
		field->len = 3;
		field->type = STRING;
		return;
	case LUA_TSTRING:
		field->data = lua_tolstring(L, i, &size);
		field->len = (uint32_t) size;
		field->type = STRING;
		return;
	default:
		field->data = NULL;
		field->len = 0;
		field->type = UNKNOWN;
		return;
	}
}

/**
 * @brief A wrapper for lua_tofield that raises an error if Lua type can not
 * be converted to lua_field structure
 * @param L stack
 * @param index stack index
 * @param field conversion result
 * @sa lua_tofield()
 */
static inline void
lua_checkfield(lua_State *L, int i, struct lua_field *field)
{
	lua_tofield(L, i, field);
	if (unlikely(field->type == UNKNOWN))
		luaL_error(L, "unsupported Lua type '%s'",
			   lua_typename(L, lua_type(L, i)));
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

	/* validate offset and len */
	if (offset < 0) {
		if (-offset > tuple->field_count)
			luaL_error(L, "tuple.transform(): offset is out of bound");
		offset += tuple->field_count;
	} else if (offset > tuple->field_count) {
		offset = tuple->field_count;
	}
	if (field_count < 0)
		luaL_error(L, "tuple.transform(): len is negative");
	if (field_count > tuple->field_count - offset)
		field_count = tuple->field_count - offset;

	assert(offset + field_count <= tuple->field_count);

	/*
	 * Calculate the number of operations and length of UPDATE expression
	 */
	uint32_t op_cnt = 0;
	if (offset < tuple->field_count && field_count > 0)
		op_cnt++;
	if (argc > 3)
		op_cnt += argc - 3;

	if (op_cnt == 0) {
		/* tuple_update() does not accept an empty operation list. */
		lbox_pushtuple(L, tuple);
		return 1;
	}

	PallocGuard palloc_guard(fiber->gc_pool);

	/*
	 * Prepare UPDATE expression
	 */
	struct tbuf *b = tbuf_new(fiber->gc_pool);
	tbuf_append(b, (char *) &op_cnt, sizeof(op_cnt));
	if (field_count > 0) {
		tbuf_ensure(b, 2 * sizeof(uint32_t) + 1 + 5);

		/* offset */
		char *data = pack_u32(b->data + b->size, offset);

		/* operation */
		*(data++) = UPDATE_OP_DELETE;

		/* field: count */
		data = pack_varint32(data, sizeof(uint32_t));
		data = pack_u32(data, field_count);

		assert(data <= b->data + b->capacity);
		b->size = data - b->data;
	}

	for (int i = argc ; i > 3; i--) {
		struct lua_field field;
		lua_checkfield(L, i, &field);
		tbuf_ensure(b, sizeof(uint32_t) + 1 + 5 + field.len);

		/* offset */
		char *data = pack_u32(b->data + b->size, offset);

		/* operation */
		*data++ = UPDATE_OP_INSERT;

		/* field */
		data = pack_lstr(data, field.data, field.len);

		assert(data <= b->data + b->capacity);
		b->size = data - b->data;
	}

	/* Execute tuple_update */
	struct tuple *new_tuple = tuple_update(tuple_format_ber,
					       palloc_region_alloc,
					       fiber->gc_pool,
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
tuple_find(struct lua_State *L, struct tuple *tuple, size_t offset,
	   const char *key, size_t key_size,
	   bool all)
{
	int top = lua_gettop(L);
	int idx = offset;

	struct tuple_iterator it;
	tuple_rewind(&it, tuple);
	uint32_t len;
	const char *field = tuple_seek(&it, idx, &len);
	for (; field; field = tuple_next(&it, &len)) {
		if (len == key_size && (memcmp(field, key, len) == 0)) {
			lua_pushinteger(L, idx);
			if (!all)
				break;
		}
		idx++;
	}
	return lua_gettop(L) - top;
}

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

	struct lua_field field;
	lua_checkfield(L, argc, &field);

	return tuple_find(L, tuple, offset, field.data, field.len, all);
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

	uint32_t len;
	while ((field = tuple_next(&it, &len)))
		lua_pushlstring(L, field, len);

	assert(lua_gettop(L) == argc + tuple->field_count);
	return tuple->field_count;
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
	uint32_t len;
	while ((field = tuple_next(&it, &len))) {
		lua_pushnumber(L, index++);
		lua_pushlstring(L, field, len);
		lua_rawset(L, -3);
	}
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
		if (i >= tuple->field_count)
			luaL_error(L, "%s: index %d is out of bounds (0..%d)",
				   tuplelib_name, i, tuple->field_count-1);
		uint32_t len = 0;
		const char *field = tuple_field(tuple, i, &len);
		lua_pushlstring(L, field, len);
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
	struct tuple *tuple = lua_checktuple(L, 1);
	/* @todo: print the tuple */
	size_t allocated = palloc_allocated(fiber->gc_pool);
	struct tbuf *tbuf = tbuf_new(fiber->gc_pool);
	tuple_print(tbuf, tuple);
	lua_pushlstring(L, tbuf->data, tbuf->size);
	ptruncate(fiber->gc_pool, allocated);
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
		assert (it != NULL);
		luaL_getmetatable(L, tuple_iteratorlib_name);
		lua_setmetatable(L, -2);
		tuple_rewind(it, tuple);
	} else if (argc == 1 && lua_type(L, 2) == LUA_TUSERDATA) {
		it = (struct tuple_iterator *)
			luaL_checkudata(L, 2, tuple_iteratorlib_name);
		assert (it != NULL);
		lua_pushvalue(L, 2);
	} else {
		return luaL_error(L, "tuple.next(): bad arguments");
	}

	uint32_t len;
	const char *field = tuple_next(it, &len);
	if (field == NULL) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}

	lua_pushlstring(L, field, len);
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
		  const char *key, size_t size, int part_count)
{
	struct lbox_iterator_udata {
		struct iterator *it;
		char key[];
	};

	struct lbox_iterator_udata *udata = (struct lbox_iterator_udata *)
		lua_newuserdata(L, sizeof(*udata) + size);
	luaL_getmetatable(L, iteratorlib_name);
	lua_setmetatable(L, -2);

	udata->it = it;
	if (key) {
		memcpy(udata->key, key, size);
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
	Index **index = (Index **) luaL_checkudata(L, i, indexlib_name);
	assert(index != NULL);
	return *index;
}

static int
lbox_index_new(struct lua_State *L)
{
	int n = luaL_checkint(L, 1); /* get space id */
	int idx = luaL_checkint(L, 2); /* get index id in */
	/* locate the appropriate index */
	struct space *sp = space_find(n);
	Index *index = index_find(sp, idx);

	/* create a userdata object */
	void **ptr = (void **) lua_newuserdata(L, sizeof(void *));
	*ptr = index;
	/* set userdata object metatable to indexlib */
	luaL_getmetatable(L, indexlib_name);
	lua_setmetatable(L, -2);

	return 1;
}

static int
lbox_index_tostring(struct lua_State *L)
{
	Index *index = lua_checkindex(L, 1);
	lua_pushfstring(L, "index %d in space %d",
			index_n(index), space_n(index->space));
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
	PallocGuard palloc_guard(fiber->gc_pool);
	enum iterator_type type = ITER_ALL;
	uint32_t key_part_count = 0;
	const char *key = NULL;
	size_t key_size = 0;
	if (argc == 1 || (argc == 2 && lua_type(L, 2) == LUA_TNIL)) {
		/*
		 * Nothing or nil on top of the stack,
		 * iteration over entire range from the
		 * beginning (ITER_ALL).
		 */
	} else {
		type = (enum iterator_type) luaL_checkint(L, 2);
		if (type < ITER_ALL || type >= iterator_type_MAX)
			luaL_error(L, "unknown iterator type: %d", type);
		/* What else do we have on the stack? */
		if (argc == 2 || (argc == 3 && lua_type(L, 3) == LUA_TNIL)) {
			/* Nothing */
		} else if (argc == 3 && lua_type(L, 3) == LUA_TUSERDATA) {
			/* Tuple. */
			struct tbuf *b = tbuf_new(fiber->gc_pool);
			struct tuple *tuple = lua_checktuple(L, 2);
			key_part_count = tuple->field_count;
			tuple_to_tbuf(tuple, b);
			key = b->data;
			key_size = b->size;
		} else {
			/* Single or multi- part key. */
			struct tbuf *b = tbuf_new(fiber->gc_pool);
			key_part_count = argc - 2;
			struct lua_field field;
			for (uint32_t i = 0; i < key_part_count; i++) {
				lua_checkfield(L, i + 3, &field);
				tbuf_ensure(b, field.len + 5);
				char *data = pack_lstr(b->data + b->size,
							field.data, field.len);
				b->size = data - b->data;
			}
			key = b->data;
			key_size = b->size;
		}
		/*
		 * We allow partially specified keys for TREE
		 * indexes. HASH indexes can only use single-part
		 * keys.
		*/
		if (key_part_count > index->key_def->part_count)
			luaL_error(L, "Key part count %d"
				   " is greater than index part count %d",
				   key_part_count, index->key_def->part_count);
		if (key_size == 0)
			key = NULL;
	}
	struct iterator *it = index->allocIterator();
	lbox_pushiterator(L, index, it, type, key, key_size,
			  key_part_count);
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
	int argc = lua_gettop(L) - 1;
	if (argc == 0)
		luaL_error(L, "index.count(): one or more arguments expected");

	/* preparing single or multi-part key */
	PallocGuard palloc_guard(fiber->gc_pool);
	uint32_t key_part_count = 0;
	const char *key = NULL;
	if (argc == 1 && lua_type(L, 2) == LUA_TUSERDATA) {
		/* Searching by tuple. */
		struct tuple *tuple = lua_checktuple(L, 2);
		struct tbuf *b = tbuf_new(fiber->gc_pool);
		tuple_to_tbuf(tuple, b);
		key_part_count = tuple->field_count;
		key = b->data;
	} else {
		/* Single or multi- part key. */
		struct tbuf *b = tbuf_new(fiber->gc_pool);
		key_part_count = argc;
		struct lua_field field;
		for (uint32_t i = 0; i < key_part_count; i++) {
			lua_checkfield(L, i + 2, &field);
			tbuf_ensure(b, field.len + 5);
			char *data = pack_lstr(b->data + b->size,
						field.data, field.len);
			b->size = data - b->data;
		}
		key = b->data;
	}
	if (key_part_count == 0)
		key = NULL;
	uint32_t count = 0;

	key_validate(index->key_def, ITER_EQ, key, key_part_count);
	/* Prepare index iterator */
	struct iterator *it = index->position();
	index->initIterator(it, ITER_EQ, key, key_part_count);
	/* Iterate over the index and count tuples. */
	struct tuple *tuple;
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
	{"new", lbox_index_new},
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
			palloc(fiber->gc_pool, sizeof(struct port_lua));
	port->vtab = &port_lua_vtab;
	port->L = L;
	return (struct port *) port;
}

/**
 * Convert a Lua table to a tuple with as little
 * overhead as possible.
 */
static struct tuple *
lua_table_to_tuple(struct lua_State *L, int index)
{
	uint32_t field_count = 0;
	uint32_t tuple_len = 0;
	struct lua_field field;

	/** First go: calculate tuple length. */
	lua_pushnil(L);  /* first key */
	while (lua_next(L, index) != 0) {
		++field_count;

		lua_tofield(L, -1, &field);
		if (field.type == UNKNOWN) {
			tnt_raise(ClientError, ER_PROC_RET,
				  lua_typename(L, lua_type(L, -1)));
		}
		tuple_len += field.len + varint32_sizeof(field.len);
		lua_pop(L, 1);
	}
	struct tuple *tuple = tuple_alloc(tuple_format_ber, tuple_len);
	/*
	 * Important: from here and on if there is an exception,
	 * the tuple is leaked.
	 */
	tuple->field_count = field_count;
	char *pos = tuple->data;

	/* Second go: store data in the tuple. */

	lua_pushnil(L);  /* first key */
	while (lua_next(L, index) != 0) {
		lua_tofield(L, -1, &field);
		pos = pack_lstr(pos, field.data, field.len);
		lua_pop(L, 1);
	}
	return tuple;
}

static struct tuple*
lua_totuple(struct lua_State *L, int index)
{
	int type = lua_type(L, index);
	struct tuple *tuple;
	struct lua_field field;
	lua_tofield(L, index, &field);
	if (field.type != UNKNOWN) {
		tuple = tuple_alloc(tuple_format_ber,
				    field.len + varint32_sizeof(field.len));
		tuple->field_count = 1;
		pack_lstr(tuple->data, field.data, field.len);
		return tuple;
	}
	switch (type) {
	case LUA_TTABLE:
	{
		return lua_table_to_tuple(L, index);
	}
	case LUA_TUSERDATA:
	{
		tuple = lua_istuple(L, index);
		if (tuple)
			return tuple;
	}
	default:
		/*
		 * LUA_TNONE, LUA_TTABLE, LUA_THREAD, LUA_TFUNCTION
		 */
		tnt_raise(ClientError, ER_PROC_RET, lua_typename(L, type));
		break;
	}
}

static void
port_add_lua_ret(struct port *port, struct lua_State *L, int index)
{
	struct tuple *tuple = lua_totuple(L, index);
	auto scoped_guard = make_scoped_guard([=] {
		if (tuple->refs == 0)
			tuple_free(tuple);
	});
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

	size_t allocated_size = palloc_allocated(fiber->gc_pool);
	struct port *port_lua = port_lua_create(L);
	try {
		box_process(port_lua, op, req, sz);

		/*
		 * This only works as long as port_lua doesn't
		 * use fiber->cleanup and fiber->gc_pool.
		 */
		ptruncate(fiber->gc_pool, allocated_size);
	} catch (const Exception& e) {
		ptruncate(fiber->gc_pool, allocated_size);
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
static void
box_lua_find(lua_State *L, const char *name, const char *name_end)
{
	int index = LUA_GLOBALSINDEX;
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
		lua_settop(L, 1);
	}
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
	box_lua_find(L, name, name + name_len);
	return 1;
}

/**
 * Invoke a Lua stored procedure from the binary protocol
 * (implementation of 'CALL' command code).
 */
void
box_lua_execute(struct request *request, struct port *port)
{
	const char **reqpos = &request->data;
	const char *reqend = request->data + request->len;
	lua_State *L = lua_newthread(root_L);
	int coro_ref = luaL_ref(root_L, LUA_REGISTRYINDEX);
	/* Request flags: not used. */
	(void) (pick_u32(reqpos, reqend));

	try {
		auto scoped_guard = make_scoped_guard([=] {
			/*
			 * Allow the used coro to be garbage collected.
			 * @todo: cache and reuse it instead.
			 */
			luaL_unref(root_L, LUA_REGISTRYINDEX, coro_ref);
		});

		uint32_t field_len;
		/* proc name */
		const char *field = pick_field_str(reqpos, reqend, &field_len);
		box_lua_find(L, field, field + field_len);
		/* Push the rest of args (a tuple). */
		uint32_t nargs = pick_u32(reqpos, reqend);
		luaL_checkstack(L, nargs, "call: out of stack");
		for (int i = 0; i < nargs; i++) {
			field = pick_field_str(reqpos, reqend, &field_len);
			lua_pushlstring(L, field, field_len);
		}
		lua_call(L, nargs, LUA_MULTRET);
		/* Send results of the called procedure to the client. */
		port_add_lua_multret(port, L);
	} catch (const Exception& e) {
		throw;
	} catch (...) {
		tnt_raise(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
	}
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

static int
luaL_packsize(struct lua_State *L, int index)
{
	switch (lua_type(L, index)) {
	case LUA_TNUMBER:
	case LUA_TCDATA:
	case LUA_TSTRING:
		return 1;
	case LUA_TUSERDATA:
	{
		struct tuple *t = lua_istuple(L, index);
		if (t == NULL)
			luaL_error(L, "box.pack: unsupported type");
		return t->field_count;
	}
	case LUA_TTABLE:
	{
		int size = 0;
		lua_pushnil(L);
		while (lua_next(L, index) != 0) {
			/* Sic: use absolute index. */
			size += luaL_packsize(L, lua_gettop(L));
			lua_pop(L, 1);
		}
		return size;
	}
	default:
		luaL_error(L, "box.pack: unsupported type");
	}
	return 0;
}

static void
luaL_packvalue(struct lua_State *L, struct tbuf *b, int index)
{
	struct lua_field field;
	lua_tofield(L, index, &field);
	if (field.type != UNKNOWN) {
		tbuf_ensure(b, field.len + 5);
		char *data = pack_lstr(b->data + b->size,
					field.data, field.len);
		b->size = data - b->data;
		return;
	}

	switch (lua_type(L, index)) {
	case LUA_TUSERDATA:
	{
		struct tuple *tuple = lua_istuple(L, index);
		if (tuple == NULL)
			luaL_error(L, "box.pack: unsupported type");
		tuple_to_tbuf(tuple, b);
		return;
	}
	case LUA_TTABLE:
	{
		lua_pushnil(L);
		while (lua_next(L, index) != 0) {
			/* Sic: use absolute index. */
			luaL_packvalue(L, b, lua_gettop(L));
			lua_pop(L, 1);
		}
		return;
	}
	default:
		luaL_error(L, "box.pack: unsupported type");
		return;
	}
}

static void
luaL_packstack(struct lua_State *L, struct tbuf *b, int first, int last)
{
	int size = 0;
	/* sic: if arg_count is 0, first > last */
	for (int i = first; i <= last; ++i)
		size += luaL_packsize(L, i);

	tbuf_ensure(b, size + sizeof(size));
	tbuf_append(b, (char *) &size, sizeof(size));
	for (int i = first; i <= last; ++i)
		luaL_packvalue(L, b, i);
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

	PallocGuard palloc_guard(fiber->gc_pool);
	struct tbuf *b = tbuf_new(fiber->gc_pool);

	struct lua_field field;
	double dbl;
	float flt;
	char *data;
	while (*format) {
		if (i > nargs)
			luaL_error(L, "box.pack: argument count does not match "
				   "the format");
		lua_tofield(L, i, &field);
		switch (*format) {
		case 'B':
		case 'b':
			/* signed and unsigned 8-bit integers */
			if (field.type != NUM || field.u32 > UINT8_MAX)
				luaL_error(L, "box.pack: expected 8-bit int");
			tbuf_append(b, (char *) &field.u32, sizeof(uint8_t));
			break;
		case 'S':
		case 's':
			/* signed and unsigned 16-bit integers */
			if (field.type != NUM || field.u32 > UINT16_MAX)
				luaL_error(L, "box.pack: expected 16-bit int");
			tbuf_append(b, (char *) &field.u32, sizeof(uint16_t));
			break;
		case 'n':
			/* signed and unsigned 16-bit big endian integers */
			if (field.type != NUM || field.u32 > UINT16_MAX)
				luaL_error(L, "box.pack: expected 16-bit int");
			field.u32 = (uint16_t) htons((uint16_t) field.u32);
			tbuf_append(b, (char *) &field.u32, sizeof(uint16_t));
			break;
		case 'I':
		case 'i':
			/* signed and unsigned 32-bit integers */
			if (field.type != NUM)
				luaL_error(L, "box.pack: expected 32-bit int");
			tbuf_append(b, (char *) &field.u32, sizeof(uint32_t));
			break;
		case 'N':
			/* signed and unsigned 32-bit big endian integers */
			if (field.type != NUM)
				luaL_error(L, "box.pack: expected 32-bit int");
			field.u32 = htonl(field.u32);
			tbuf_append(b, (char *) &field.u32, sizeof(uint32_t));
			break;
		case 'L':
		case 'l':
			/* signed and unsigned 64-bit integers */
			if (field.type == NUM) {
				/* extend 32-bit value to 64-bit */
				field.u64 = field.u32;
			} else if (field.type != NUM64) {
				luaL_error(L, "box.pack: expected 64-bit int");
			}
			tbuf_append(b, (char *) &field.u64, sizeof(uint64_t));
			break;
		case 'Q':
		case 'q':
			/* signed and unsigned 64-bit integers */
			if (field.type == NUM) {
				/* extend 32-bit value to 64-bit */
				field.u64 = field.u32;
			} else if (field.type != NUM64){
				luaL_error(L, "box.pack: expected 64-bit int");
			}
			field.u64 = bswap_u64(field.u64);
			tbuf_append(b, (char *) &field.u64, sizeof(uint64_t));
			break;
		case 'd':
			dbl = (double) lua_tonumber(L, i);
			tbuf_append(b, (char *) &dbl, sizeof(double));
			break;
		case 'f':
			flt = (float) lua_tonumber(L, i);
			tbuf_append(b, (char *) &flt, sizeof(float));
			break;
		case 'w':
			/* Perl 'pack' BER-encoded integer */
			if (field.type != NUM)
				luaL_error(L, "box.pack: expected 32-bit int");

			tbuf_ensure(b, 5);
			data = pack_varint32(b->data + b->size, field.u32);
			assert(data <= b->data + b->capacity);
			b->size = data - b->data;
			break;
		case 'A':
		case 'a':
			/* A sequence of bytes */
			str = luaL_checklstring(L, i, &size);
			tbuf_append(b, str, size);
			break;
		case 'P':
		case 'p':
			luaL_packvalue(L, b, i);
			break;
		case 'V':
		{
			int arg_count = luaL_checkint(L, i);
			if (i + arg_count > nargs)
				luaL_error(L, "box.pack: argument count does not match "
					   "the format");
			luaL_packstack(L, b, i + 1, i + arg_count);
			i += arg_count;
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
		uint32_t field_count = pick_u32(&s, end);
		const char *tend = s + bsize;
		if (tend > end)
			tnt_raise(IllegalParams, "incorrect packet length");

		struct tuple *tuple = tuple_new(tuple_format_ber,
						field_count, &s, tend);
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
		case 'w':
			/* pick_varint32 throws exception on error. */
			u32buf = pick_varint32(&s, end);
			lua_pushnumber(L, u32buf);
			break;

		case 'a':
		case 'A': /* The rest of the data is a Lua string. */
			lua_pushlstring(L, s, end - s);
			s = end;
			break;
		case 'P':
		case 'p':
			/* pick_varint32 throws exception on error. */
			u32buf = pick_varint32(&s, end);
			CHECK_SIZE(s + u32buf - 1);
			lua_pushlstring(L, s, u32buf);
			s += u32buf;
			break;
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
mod_lua_init(struct lua_State *L)
{
	/* box, box.tuple */
	tarantool_lua_register_type(L, tuplelib_name, lbox_tuple_meta);
	luaL_register(L, tuplelib_name, lbox_tuplelib);
	lua_pop(L, 1);
	tarantool_lua_register_type(L, tuple_iteratorlib_name,
				    lbox_tuple_iterator_meta);
	luaL_register(L, "box", boxlib);
	lua_pop(L, 1);
	/* box.index */
	tarantool_lua_register_type(L, indexlib_name, lbox_index_meta);
	luaL_register(L, "box.index", indexlib);
	box_index_init_iterator_types(L, -2);
	lua_pop(L, 1);
	tarantool_lua_register_type(L, iteratorlib_name, lbox_iterator_meta);

	/* Load Lua extension */
	for (const char **s = lua_sources; *s; s++) {
		if (luaL_dostring(L, *s))
			panic("Error loading Lua source %.160s...: %s",
			      *s, lua_tostring(L, -1));
	}

	assert(lua_gettop(L) == 0);

	root_L = L;
}
