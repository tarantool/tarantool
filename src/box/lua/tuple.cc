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
#include "box/lua/tuple.h"
#include "box/lua/slab.h"
#include "box/tuple.h"
#include "box/tuple_update.h"
#include "fiber.h"
#include "tbuf.h"
#include "lua/utils.h"
#include "lua/msgpack.h"
#include "third_party/lua-yaml/lyaml.h"

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
int
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
	(void) argc;
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

void
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


struct tuple*
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
	struct tuple *tuple = tuple_new(tuple_format_ber, data, tbuf_end(b));
	return tuple;
}

/* }}} */

void
box_lua_tuple_init(struct lua_State *L)
{
	luaL_register_type(L, tuplelib_name, lbox_tuple_meta);
	luaL_register_type(L, tuple_iteratorlib_name,
			   lbox_tuple_iterator_meta);
	luaL_register(L, tuplelib_name, lbox_tuplelib);
	lua_pop(L, 1);

	luamp_set_encode_extension(luamp_encode_extension_box);

	/* Precreate a metatable for tuple_unpack */
	lua_newtable(L);
	lua_pushstring(L, "_serializer_compact");
	lua_pushboolean(L, true);
	lua_settable(L, -3);
	tuple_totable_mt_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	assert(tuple_totable_mt_ref != 0);

	box_lua_slab_init(L);
}

