/*
 * Copyright (C) 2011 Yuriy Vostrikov
 * Copyright (C) 2011 Konstantin Osipov
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
#include <tarantool_lua.h>
#include "request.h"
#include "txn.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "pickle.h"
#include "tuple.h"
#include "space.h"
#include "port.h"

/* contents of box.lua */
extern const char box_lua[];

/**
 * All box connections share the same Lua state. We use
 * Lua coroutines (lua_newthread()) to have multiple
 * procedures running at the same time.
 */
lua_State *root_L;

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

static inline struct box_tuple *
lua_checktuple(struct lua_State *L, int narg)
{
	return *(void **) luaL_checkudata(L, narg, tuplelib_name);
}

struct box_tuple *
lua_istuple(struct lua_State *L, int narg)
{
	if (lua_getmetatable(L, narg) == 0)
		return NULL;
	luaL_getmetatable(L, tuplelib_name);
	struct box_tuple *tuple = 0;
	if (lua_equal(L, -1, -2))
		tuple = * (void **) lua_touserdata(L, narg);
	lua_pop(L, 2);
	return tuple;
}

static int
lbox_tuple_gc(struct lua_State *L)
{
	struct box_tuple *tuple = lua_checktuple(L, 1);
	tuple_ref(tuple, -1);
	return 0;
}

static int
lbox_tuple_len(struct lua_State *L)
{
	struct box_tuple *tuple = lua_checktuple(L, 1);
	lua_pushnumber(L, tuple->field_count);
	return 1;
}

static int
lbox_tuple_slice(struct lua_State *L)
{
	struct box_tuple *tuple = lua_checktuple(L, 1);
	int argc = lua_gettop(L) - 1;
	int start, end;

	/*
	 * Prepare the range. The second argument is optional.
	 * If the end is beyond tuple size, adjust it.
	 * If no arguments, or start > end, return an error.
	 */
	if (argc == 0 || argc > 2)
		luaL_error(L, "tuple.slice(): bad arguments");
	start = lua_tointeger(L, 2);
	if (start < 0)
		start += tuple->field_count;
	if (argc == 2) {
		end = lua_tointeger(L, 3);
		if (end < 0)
			end += tuple->field_count;
		else if (end > tuple->field_count)
			end = tuple->field_count;
	} else {
		end = tuple->field_count;
	}
	if (end <= start)
		luaL_error(L, "tuple.slice(): start must be less than end");

	u8 *field = tuple->data;
	int fieldno = 0;
	int stop = end - 1;

	while (field < tuple->data + tuple->bsize) {
		size_t len = load_varint32((void **) &field);
		if (fieldno >= start) {
			lua_pushlstring(L, (char *) field, len);
			if (fieldno == stop)
				break;
		}
		field += len;
		fieldno += 1;
	}
	return end - start;
}

static int
lbox_tuple_unpack(struct lua_State *L)
{
	struct box_tuple *tuple = lua_checktuple(L, 1);
	u8 *field = tuple->data;

	while (field < tuple->data + tuple->bsize) {
		size_t len = load_varint32((void **) &field);
		lua_pushlstring(L, (char *) field, len);
		field += len;
	}
	assert(lua_gettop(L) == tuple->field_count + 1);
	return tuple->field_count;
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
	struct box_tuple *tuple = lua_checktuple(L, 1);
	/* For integer indexes, implement [] operator */
	if (lua_isnumber(L, 2)) {
		int i = luaL_checkint(L, 2);
		if (i >= tuple->field_count)
			luaL_error(L, "%s: index %d is out of bounds (0..%d)",
				   tuplelib_name, i, tuple->field_count-1);
		void *field = tuple_field(tuple, i);
		u32 len = load_varint32(&field);
		lua_pushlstring(L, field, len);
		return 1;
	}
	/* If we got a string, try to find a method for it. */
	lua_getmetatable(L, 1);
	lua_getfield(L, -1, lua_tostring(L, 2));
	return 1;
}

static int
lbox_tuple_tostring(struct lua_State *L)
{
	struct box_tuple *tuple = lua_checktuple(L, 1);
	/* @todo: print the tuple */
	struct tbuf *tbuf = tbuf_alloc(fiber->gc_pool);
	tuple_print(tbuf, tuple->field_count, tuple->data);
	lua_pushlstring(L, tbuf->data, tbuf->size);
	return 1;
}

static void
lbox_pushtuple(struct lua_State *L, struct box_tuple *tuple)
{
	if (tuple) {
		void **ptr = lua_newuserdata(L, sizeof(void *));
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
	struct box_tuple *tuple = lua_checktuple(L, 1);
	int argc = lua_gettop(L) - 1;
	u8 *field = NULL;
	size_t len;

	if (argc == 0 || (argc == 1 && lua_type(L, 2) == LUA_TNIL))
		field = tuple->data;
	else if (argc == 1 && lua_islightuserdata(L, 2))
		field = lua_touserdata(L, 2);
	else
		luaL_error(L, "tuple.next(): bad arguments");

	(void)field;
	assert(field >= tuple->data);
	if (field < tuple->data + tuple->bsize) {
		len = load_varint32((void **) &field);
		lua_pushlightuserdata(L, field + len);
		lua_pushlstring(L, (char *) field, len);
		return 2;
	}
	lua_pushnil(L);
	return  1;
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

static const struct luaL_reg lbox_tuple_meta [] = {
	{"__gc", lbox_tuple_gc},
	{"__len", lbox_tuple_len},
	{"__index", lbox_tuple_index},
	{"__tostring", lbox_tuple_tostring},
	{"next", lbox_tuple_next},
	{"pairs", lbox_tuple_pairs},
	{"slice", lbox_tuple_slice},
	{"unpack", lbox_tuple_unpack},
	{NULL, NULL}
};

/* }}} */

/** {{{ box.index Lua library: access to spaces and indexes
 */

static const char *indexlib_name = "box.index";
static const char *iteratorlib_name = "box.index.iterator";

static struct iterator *
lua_checkiterator(struct lua_State *L, int i)
{
	struct iterator **it = luaL_checkudata(L, i, iteratorlib_name);
	assert(it != NULL);
	return *it;
}

static void
lbox_pushiterator(struct lua_State *L, struct iterator *it)
{
	void **ptr = lua_newuserdata(L, sizeof(void *));
	luaL_getmetatable(L, iteratorlib_name);
	lua_setmetatable(L, -2);
	*ptr = it;
}

static int
lbox_iterator_gc(struct lua_State *L)
{
	struct iterator *it = lua_checkiterator(L, -1);
	it->free(it);
	return 0;
}

static Index *
lua_checkindex(struct lua_State *L, int i)
{
	Index **index = luaL_checkudata(L, i, indexlib_name);
	assert(index != NULL);
	return *index;
}

static int
lbox_index_new(struct lua_State *L)
{
	int n = luaL_checkint(L, 1); /* get space id */
	int idx = luaL_checkint(L, 2); /* get index id in */
	/* locate the appropriate index */
	if (n >= BOX_SPACE_MAX || !space[n].enabled ||
	    idx >= BOX_INDEX_MAX || space[n].index[idx] == nil)
		tnt_raise(LoggedError, :ER_NO_SUCH_INDEX, idx, n);
	/* create a userdata object */
	void **ptr = lua_newuserdata(L, sizeof(void *));
	*ptr = space[n].index[idx];
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
	lua_pushinteger(L, [index size]);
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
	lbox_pushtuple(L, [index min]);
	return 1;
}

static int
lbox_index_max(struct lua_State *L)
{
	Index *index = lua_checkindex(L, 1);
	lbox_pushtuple(L, [index max]);
	return 1;
}

/**
 * Convert an element on Lua stack to a part of an index
 * key.
 *
 * Lua type system has strings, numbers, booleans, tables,
 * userdata objects. Tarantool indexes only support 32/64-bit
 * integers and strings.
 *
 * Instead of considering each Tarantool <-> Lua type pair,
 * here we follow the approach similar to one in lbox_pack()
 * (see tarantool_lua.m):
 *
 * Lua numbers are converted to 32 or 64 bit integers,
 * if key part is integer. In all other cases,
 * Lua types are converted to Lua strings, and these
 * strings are used as key parts.
 */

void append_key_part(struct lua_State *L, int i,
		     struct tbuf *tbuf, enum field_data_type type)
{
	const char *str;
	size_t size;
	u32 v_u32;
	u64 v_u64;

	if (lua_type(L, i) == LUA_TNUMBER) {
		if (type == NUM64) {
			v_u64 = (u64) lua_tonumber(L, i);
			str = (char *) &v_u64;
			size = sizeof(u64);
		} else {
			v_u32 = (u32) lua_tointeger(L, i);
			str = (char *) &v_u32;
			size = sizeof(u32);
		}
	} else {
		str = luaL_checklstring(L, i, &size);
	}
	write_varint32(tbuf, size);
	tbuf_append(tbuf, str, size);
}

/**
 * Lua iterator over a Taratnool/Box index.
 *
 *	(iteration_state, tuple) = index.next(index, [iteration_state])
 *	(iteration_state, tuple) = index.prev(index, [iteration_state])
 *
 * When [iteration_state] is absent or nil
 * returns a pointer to a new iterator and
 * to the first tuple (or nil, if the index is
 * empty).
 *
 * When [iteration_state] is a userdata,
 * i.e. we're inside an iteration loop, retrieves
 * the next tuple from the iterator.
 *
 * Otherwise, [iteration_state] can be used to seed
 * the iterator with one or several Lua scalars
 * (numbers, strings) and start iteration from an
 * offset.
 */
static int
lbox_index_move(struct lua_State *L, enum iterator_type type)
{
	Index *index = lua_checkindex(L, 1);
	int argc = lua_gettop(L) - 1;
	struct iterator *it = NULL;
	if (argc == 0 || (argc == 1 && lua_type(L, 2) == LUA_TNIL)) {
		/*
		 * If there is nothing or nil on top of the stack,
		 * start iteration from the beginning (ITER_FORWARD) or
		 * end (ITER_REVERSE).
		 */
		it = [index allocIterator];
		[index initIterator: it :type];
		lbox_pushiterator(L, it);
	} else if (argc > 1 || lua_type(L, 2) != LUA_TUSERDATA) {
		/*
		 * We've got something different from iterator's
		 * userdata: must be a key to start iteration from
		 * an offset. Seed the iterator with this key.
		 */
		int field_count;
		void *key;

		if (argc == 1 && lua_type(L, 2) == LUA_TUSERDATA) {
			/* Searching by tuple. */
			struct box_tuple *tuple = lua_checktuple(L, 2);
			key = tuple->data;
			field_count = tuple->field_count;
		} else {
			/* Single or multi- part key. */
			field_count = argc;
			struct tbuf *data = tbuf_alloc(fiber->gc_pool);
			for (int i = 0; i < argc; ++i)
				append_key_part(L, i + 2, data,
						index->key_def->parts[i].type);
			key = data->data;
		}
		/*
		 * We allow partially specified keys for TREE
		 * indexes. HASH indexes can only use single-part
		 * keys.
		*/
		assert(field_count != 0);
		if (field_count > index->key_def->part_count)
			luaL_error(L, "index.next(): key part count (%d) "
				   "does not match index field count (%d)",
				   field_count, index->key_def->part_count);
		it = [index allocIterator];
		[index initIteratorByKey: it :type :key :field_count];
		lbox_pushiterator(L, it);
	} else { /* 1 item on the stack and it's a userdata. */
		it = lua_checkiterator(L, 2);
	}
	struct box_tuple *tuple = it->next(it);
	/* If tuple is NULL, pushes nil as end indicator. */
	lbox_pushtuple(L, tuple);
	return tuple ? 2 : 1;
}

/**
 * Lua forward index iterator function.
 * See lbox_index_move comment for a functional
 * description.
 */
static int
lbox_index_next(struct lua_State *L)
{
	return lbox_index_move(L, ITER_FORWARD);
}

/**
 * Lua reverse index iterator function.
 * See lbox_index_move comment for a functional
 * description.
 */
static int
lbox_index_prev(struct lua_State *L)
{
	return lbox_index_move(L, ITER_REVERSE);
}

static const struct luaL_reg lbox_index_meta[] = {
	{"__tostring", lbox_index_tostring},
	{"__len", lbox_index_len},
	{"part_count", lbox_index_part_count},
	{"min", lbox_index_min},
	{"max", lbox_index_max},
	{"next", lbox_index_next},
	{"prev", lbox_index_prev},
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

static void
port_lua_dup_u32(u32 u32 __attribute__((unused)))
{
	/*
	 * Do nothing -- the only u32 Box can give us is
	 * tuple count, and we don't need it, since we intercept
	 * everything into Lua stack first.
	 * @sa iov_add_multret
	 */
}

static void
port_lua_add_u32(u32 *p_u32 __attribute__((unused)))
{
	/* See the comment in port_lua_dup_u32. */
}

static void
port_lua_add_tuple(struct box_tuple *tuple)
{
	struct lua_State *L = in_txn()->L;
	lbox_pushtuple(L, tuple);
}

static void
port_lua_add_lua_multret(struct lua_State *L)
{
	/*
	 * We cannot issue a CALL request from within a CALL
	 * request. Instead users should call Lua procedures
	 * directly.
	 */
	assert(false);
	(void) L;
}

static struct port port_lua = {
	port_lua_add_u32,
	port_lua_dup_u32,
	port_lua_add_tuple,
	port_lua_add_lua_multret
};

/* }}} */

static inline struct box_txn *
txn_enter_lua(lua_State *L)
{
	struct box_txn *old_txn = in_txn();
	fiber->mod_data.txn = NULL;
	struct box_txn *txn = fiber->mod_data.txn = txn_begin();
	txn->port = &port_lua;
	txn->L = L;
	return old_txn;
}

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
static int lbox_process(lua_State *L)
{
	u32 op = lua_tointeger(L, 1); /* Get the first arg. */
	struct tbuf req;
	size_t sz;
	req.data = (char *) luaL_checklstring(L, 2, &sz); /* Second arg. */
	req.capacity = req.size = sz;
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
	struct box_txn *old_txn = txn_enter_lua(L);
	@try {
		rw_callback(op, &req);
	} @finally {
		fiber->mod_data.txn = old_txn;
		ptruncate(fiber->gc_pool, allocated_size);
	}
	return lua_gettop(L) - top;
}

static const struct luaL_reg boxlib[] = {
	{"process", lbox_process},
	{NULL, NULL}
};

/**
 * A helper to find a Lua function by name and put it
 * on top of the stack.
 */
static
void box_lua_find(lua_State *L, const char *name, const char *name_end)
{
	int index = LUA_GLOBALSINDEX;
	const char *start = name, *end;

	while ((end = memchr(start, '.', name_end - start))) {
		lua_checkstack(L, 3);
		lua_pushlstring(L, start, end - start);
		lua_gettable(L, index);
		if (! lua_istable(L, -1))
			tnt_raise(ClientError, :ER_NO_SUCH_PROC,
				  name_end - name, name);
		start = end + 1; /* next piece of a.b.c */
		index = lua_gettop(L); /* top of the stack */
	}
	lua_pushlstring(L, start, name_end - start);
	lua_gettable(L, index);
	if (! lua_isfunction(L, -1)) {
		/* lua_call or lua_gettable would raise a type error
		 * for us, but our own message is more verbose. */
		tnt_raise(ClientError, :ER_NO_SUCH_PROC,
			  name_end - name, name);
	}
	if (index != LUA_GLOBALSINDEX)
		lua_remove(L, index);
}


static int
box_lua_panic(struct lua_State *L)
{
	tnt_raise(ClientError, :ER_PROC_LUA, lua_tostring(L, -1));
	return 0;
}

/**
 * Invoke a Lua stored procedure from the binary protocol
 * (implementation of 'CALL' command code).
 */
void do_call(struct box_txn *txn, struct tbuf *data)
{
	lua_State *L = lua_newthread(root_L);
	int coro_ref = luaL_ref(root_L, LUA_REGISTRYINDEX);

	@try {
		u32 field_len = read_varint32(data);
		void *field = read_str(data, field_len); /* proc name */
		box_lua_find(L, field, field + field_len);
		/* Push the rest of args (a tuple). */
		u32 nargs = read_u32(data);
		luaL_checkstack(L, nargs, "call: out of stack");
		for (int i = 0; i < nargs; i++) {
			field_len = read_varint32(data);
			field = read_str(data, field_len);
			lua_pushlstring(L, field, field_len);
		}
		lua_call(L, nargs, LUA_MULTRET);
		/* Send results of the called procedure to the client. */
		txn->port->add_lua_multret(L);
	} @finally {
		/*
		 * Allow the used coro to be garbage collected.
		 * @todo: cache and reuse it instead.
		 */
		luaL_unref(root_L, LUA_REGISTRYINDEX, coro_ref);
	}
}

struct lua_State *
mod_lua_init(struct lua_State *L)
{
	lua_atpanic(L, box_lua_panic);
	/* box, box.tuple */
	tarantool_lua_register_type(L, tuplelib_name, lbox_tuple_meta);
	luaL_register(L, "box", boxlib);
	lua_pop(L, 1);
	/* box.index */
	tarantool_lua_register_type(L, indexlib_name, lbox_index_meta);
	luaL_register(L, "box.index", indexlib);
	lua_pop(L, 1);
	tarantool_lua_register_type(L, iteratorlib_name, lbox_iterator_meta);
	/* Load box.lua */
	if (luaL_dostring(L, box_lua))
		panic("Error loading box.lua: %s", lua_tostring(L, -1));
	assert(lua_gettop(L) == 0);
	return L;
}

void box_lua_init()
{
	root_L = tarantool_L;
}

/**
 * vim: foldmethod=marker
 */
