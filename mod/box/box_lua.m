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
/* use a full path to avoid clashes with system lua */
#include "third_party/luajit/src/lua.h"
#include "third_party/luajit/src/lauxlib.h"
#include "third_party/luajit/src/lualib.h"

#include "pickle.h"

/**
 * All box connections share the same Lua state. We use
 * Lua coroutines (lua_newthread()) to have multiple
 * procedures running at the same time.
 */
lua_State *root_L;

/**
 * The main extention provided to Lua by
 * Tarantool -- ability to call INSERT/UPDATE/SELECT/DELETE
 * from within a Lua procedure.
 *
 * This is a low-level API, and it expects
 * all arguments to be packed in accordance
 * with the binary protocol format (iproto
 * header excluded).
 */
static int lbox_process(lua_State *L)
{
	u32 op = luaL_checkint(L, 1);
	struct tbuf req;
	size_t sz;
	req.data = (char *) luaL_checklstring(L, 2, &sz);
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
	@try {
		rw_callback(op, &req);
	} @catch (ClientError *e) {
		return luaL_error(L, "%d:%s", e->errcode, e->errmsg);
	}
	return 0; /* nothing is added to the stack */
}

static const struct luaL_reg boxlib[] = {
	{"process", lbox_process},
	{NULL, NULL}
};


/**
 * A helper to find a Lua stored procedure by name and put it
 * on top of the stack.
 */
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
	if (! lua_isfunction(L, -1))
		tnt_raise(ClientError, :ER_NO_SUCH_PROC,
			  name_end - name, name);
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
	int coro_index = lua_gettop(root_L);

	@try {
		u32 field_len = read_varint32(data);
		void *field = read_str(data, field_len); /* proc name */
		box_lua_find(L, field, field + field_len);
		lua_checkstack(L, 1);
		/* Push the rest of args (a tuple) as is. */
		lua_pushlstring(L, data->data, data->len);

		lua_call(L, 1, LUA_MULTRET);
	} @finally {
		/*
		 * Allow the used coro to be garbage collected.
		 * @todo: cache and reuse it instead.
		 */
		lua_remove(root_L, coro_index);
	}
}

struct lua_State *
mod_lua_init(struct lua_State *L)
{
	luaL_register(L, "box", boxlib);
	lua_atpanic(L, box_lua_panic);
	return L;
}

void box_lua_init()
{
	root_L = tarantool_lua_init();
}
