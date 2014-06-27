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
	struct tuple *tuple = lua_istuple(L, idx);
	if (tuple != NULL) {
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
	if (first == last && (lua_istable(L, first) || lua_istuple(L, first))) {
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

	RegionGuard region_guard(&fiber()->gc);

	/*
	 * Prepare UPDATE expression
	 */
	struct tbuf *b = tbuf_new(&fiber()->gc);
	luamp_encode_array(b, op_cnt);
	if (len > 0) {
		luamp_encode_array(b, 3);
		luamp_encode_str(b, "#", 1);
		luamp_encode_uint(b, offset);
		luamp_encode_uint(b, len);
	}

	for (int i = argc ; i > 3; i--) {
		luamp_encode_array(b, 3);
		luamp_encode_str(b, "!", 1);
		luamp_encode_uint(b, offset);
		luamp_encode(L, b, i);
	}

	/* Execute tuple_update */
	struct tuple *new_tuple = tuple_update(tuple_format_ber,
					       tuple_update_region_alloc,
					       &fiber()->gc,
					       tuple, tbuf_str(b), tbuf_end(b),
					       0);
	lbox_pushtuple(L, new_tuple);
	return 1;
}

void
lbox_pushtuple(struct lua_State *L, struct tuple *tuple)
{
	if (tuple) {
		assert(CTID_CONST_STRUCT_TUPLE_REF != 0);
		struct tuple **ptr = (struct tuple **) luaL_pushcdata(L,
			CTID_CONST_STRUCT_TUPLE_REF, sizeof(struct tuple *));
		*ptr = tuple;
		lua_pushcfunction(L, lbox_tuple_gc);
		luaL_setcdatagc(L, -2);
		tuple_ref(tuple, 1);
	} else {
		return lua_pushnil(L);
	}
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


struct tuple*
lua_totuple(struct lua_State *L, int first, int last)
{
	RegionGuard region_guard(&fiber()->gc);
	struct tbuf *b = tbuf_new(&fiber()->gc);
	try {
		luamp_encodestack(L, b, first, last);
	} catch (Exception *e) {
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
	/* export C functions to Lua */
	luaL_newmetatable(L, tuplelib_name);
	luaL_register(L, NULL, lbox_tuple_meta);
	/* save Lua/C functions to the global variable (cleaned by tuple.lua) */
	lua_setglobal(L, "cfuncs");
	luaL_register_type(L, tuple_iteratorlib_name,
			   lbox_tuple_iterator_meta);
	luaL_register(L, tuplelib_name, lbox_tuplelib);
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
