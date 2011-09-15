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
#include "tarantool.h"
#include "box.h"
/* use a full path to avoid clashes with system Lua */
#include "third_party/luajit/src/lua.h"
#include "third_party/luajit/src/lauxlib.h"
#include "third_party/luajit/src/lualib.h"

#include "pickle.h"
#include "tuple.h"

/* contents of box.lua */
extern const char _binary_box_lua_start;

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

static inline struct box_tuple *
lua_istuple(struct lua_State *L, int narg)
{
	struct box_tuple *tuple = 0;
	lua_getmetatable(L, narg);
	luaL_getmetatable(L, tuplelib_name);
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
	lua_pushnumber(L, tuple->cardinality);
	return 1;
}

static int
lbox_tuple_index(struct lua_State *L)
{
	struct box_tuple *tuple = lua_checktuple(L, 1);
	int i = luaL_checkint(L, 2);
	if (i >= tuple->cardinality)
		luaL_error(L, "%s: index %d is out of bounds (0..%d)",
			   tuplelib_name, i, tuple->cardinality-1);
	void *field = tuple_field(tuple, i);
	u32 len = load_varint32(&field);
	lua_pushlstring(L, field, len);
	return 1;
}

static int
lbox_tuple_tostring(struct lua_State *L)
{
	struct box_tuple *tuple = lua_checktuple(L, 1);
	/* @todo: print the tuple */
	struct tbuf *tbuf = tbuf_alloc(fiber->gc_pool);
	tuple_print(tbuf, tuple->cardinality, tuple->data);
	lua_pushlstring(L, tbuf->data, tbuf->len);
	return 1;
}

static const struct luaL_reg lbox_tuple_meta [] = {
	{"__gc", lbox_tuple_gc},
	{"__len", lbox_tuple_len},
	{"__index", lbox_tuple_index},
	{"__tostring", lbox_tuple_tostring},
	{NULL, NULL}
};

/* }}} */

/**
 * {{{ Lua box.index library: access to spaces and indexes
 */

static const char *indexlib_name = "box.index";

static struct index *
lua_checkindex(struct lua_State *L, int i)
{
	struct index **index = luaL_checkudata(L, i, indexlib_name);
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
	    idx >= BOX_INDEX_MAX || space[n].index[idx].key_cardinality == 0)
		tnt_raise(LoggedError, :ER_NO_SUCH_INDEX, idx, n);
	/* create a userdata object */
	void **ptr = lua_newuserdata(L, sizeof(void *));
	*ptr = &space[n].index[idx];
	/* set userdata object metatable to indexlib */
	luaL_getmetatable(L, indexlib_name);
	lua_setmetatable(L, -2);
	return 1;
}

static int
lbox_index_tostring(struct lua_State *L)
{
	struct index *index = lua_checkindex(L, 1);
	lua_pushfstring(L, "index %d in space %d",
			index->n, index->space->n);
	return 1;
}

static int
lbox_index_len(struct lua_State *L)
{
	struct index *index = lua_checkindex(L, 1);
	lua_pushinteger(L, index->size(index));
	return 1;
}

static const struct luaL_reg lbox_index_meta[] = {
	{"__tostring", lbox_index_tostring},
	{"__len", lbox_index_len},
	{NULL, NULL}
};

static const struct luaL_reg indexlib [] = {
	{"new", lbox_index_new},
	{NULL, NULL}
};

/* }}} */

/** {{{ Lua I/O: facilities to intercept box output
 * and push into Lua stack and the opposite: append Lua types
 * to fiber IOV.
 */

void iov_add_ret(struct lua_State *L, int index)
{
	int type = lua_type(L, index);
	struct box_tuple *tuple;
	switch (type) {
	case LUA_TNUMBER:
	case LUA_TSTRING:
	{
		size_t len;
		const char *str = lua_tolstring(L, index, &len);
		tuple = tuple_alloc(len + varint32_sizeof(len));
		tuple->cardinality = 1;
		memcpy(save_varint32(tuple->data, len), str, len);
		break;
	}
	case LUA_TNIL:
	case LUA_TBOOLEAN:
	{
		const char *str = tarantool_lua_tostring(L, index);
		size_t len = strlen(str);
		tuple = tuple_alloc(len + varint32_sizeof(len));
		tuple->cardinality = 1;
		memcpy(save_varint32(tuple->data, len), str, len);
		break;
	}
	case LUA_TUSERDATA:
		tuple = lua_istuple(L, index);
		if (tuple)
			break;
	default:
		/*
		 * LUA_TNONE, LUA_TTABLE, LUA_THREAD, LUA_TFUNCTION
		 */
		tnt_raise(ClientError, :ER_PROC_RET, lua_typename(L, type));
		break;
	}
	tuple_txn_ref(in_txn(), tuple);
	iov_add(&tuple->bsize, tuple_len(tuple));
}

/**
 * Add all elements from Lua stack to fiber iov.
 *
 * To allow clients to understand a complex return from
 * a procedure, we are compatible with SELECT protocol,
 * and return the number of return values first, and
 * then each return value as a tuple.
 */
void iov_add_multret(struct lua_State *L)
{
	int nargs = lua_gettop(L);
	iov_dup(&nargs, sizeof(u32));
	for (int i = 1; i <= nargs; ++i)
		iov_add_ret(L, i);
}

static void
box_lua_dup_u32(u32 u32)
{
	lua_pushinteger(in_txn()->L, u32);
}

static void
box_lua_add_u32(u32 *p_u32)
{
	box_lua_dup_u32(*p_u32); /* xxx: this can't be done properly in Lua */
}

static void
box_lua_add_tuple(struct box_tuple *tuple)
{
	struct lua_State *L = in_txn()->L;
	void **ptr = lua_newuserdata(L, sizeof(void *));
	luaL_getmetatable(L, tuplelib_name);
	lua_setmetatable(L, -2);
	*ptr = tuple;
	tuple_ref(tuple, 1);
}

static struct box_out box_out_lua = {
	box_lua_add_u32,
	box_lua_dup_u32,
	box_lua_add_tuple
};

/* }}} */

static inline struct box_txn *
txn_enter_lua(lua_State *L)
{
	struct box_txn *old_txn = in_txn();
	fiber->mod_data.txn = NULL;
	struct box_txn *txn = fiber->mod_data.txn = txn_begin();
	txn->out = &box_out_lua;
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
	req.size = req.len = sz;
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

	struct box_txn *old_txn = txn_enter_lua(L);
	@try {
		rw_callback(op, &req);
	} @finally {
		fiber->mod_data.txn = old_txn;
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
void box_lua_call(struct box_txn *txn __attribute__((unused)),
		  struct tbuf *data)
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
		iov_add_multret(L);
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
	luaL_register(L, "box", boxlib);
	luaL_newmetatable(L, tuplelib_name);
	lua_pushstring(L, tuplelib_name);
	lua_setfield(L, -2, "__metatable");
	luaL_register(L, NULL, lbox_tuple_meta);
	lua_pop(L, 2);
	/* box.index */
	luaL_newmetatable(L, indexlib_name);
	lua_pushstring(L, indexlib_name);
	lua_setfield(L, -2, "__metatable");
	luaL_register(L, NULL, lbox_index_meta);
	luaL_register(L, "box.index", indexlib);
	lua_pop(L, 2);
	/* Load box.lua */
	if (luaL_dostring(L, &_binary_box_lua_start))
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
