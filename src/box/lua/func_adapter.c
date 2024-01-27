/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/lua/func_adapter.h"
#include "box/lua/tuple.h"
#include "box/tuple.h"
#include "core/func_adapter.h"
#include "lua/msgpack.h"
#include "lua/utils.h"

#include "box/port.h"

/**
 * Specialization of func_adapter for Lua functions and other callable objects.
 */
struct func_adapter_lua {
	/**
	 * Virtual table.
	 */
	const struct func_adapter_vtab *vtab;
	/**
	 * Reference to the function in Lua registry.
	 */
	int func_ref;
};

/**
 * Call the function with ports.
 */
static int
func_adapter_lua_call(struct func_adapter *func, struct port *args, struct port *ret)
{
	struct func_adapter_lua *lua_func =
		(struct func_adapter_lua *)func;
	int coro_ref = LUA_REFNIL;
	struct lua_State *L = fiber_lua_state(fiber());
	if (L == NULL) {
		L = luaT_newthread(tarantool_L);
		if (L == NULL)
			panic("Cannot create Lua thread");
		coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	}
	int top_svp = lua_gettop(L);

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_func->func_ref);
	if (args != NULL)
		port_dump_lua(args, L, PORT_DUMP_LUA_MODE_FLAT);

	int nargs = lua_gettop(L) - top_svp - 1;
	bool ok = luaT_call(L, nargs, LUA_MULTRET) == 0;

	if (!ok || ret == NULL) {
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
	} else {
		port_lua_create_at(ret, L, top_svp + 1);
		struct port_lua *port_lua = (struct port_lua *)ret;
		port_lua->ref = coro_ref;
	}
	return ok ? 0 : -1;
}

/**
 * Virtual destructor.
 */
static void
func_adapter_lua_destroy(struct func_adapter *func_base)
{
	struct func_adapter_lua *func = (struct func_adapter_lua *)func_base;
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, func->func_ref);
	free(func);
}

void
func_adapter_lua_get_func(struct func_adapter *func, lua_State *L)
{
	assert(func != NULL);
	assert(func->vtab->destroy == func_adapter_lua_destroy);
	struct func_adapter_lua *lua_func =
		(struct func_adapter_lua *)func;
	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_func->func_ref);
}

struct func_adapter *
func_adapter_lua_create(lua_State *L, int idx)
{
	static const struct func_adapter_vtab vtab = {
		.call = func_adapter_lua_call,
		.destroy = func_adapter_lua_destroy,
	};
	struct func_adapter_lua *func = xmalloc(sizeof(*func));
	assert(luaL_iscallable(L, idx));
	lua_pushvalue(L, idx);
	func->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	func->vtab = &vtab;
	return (struct func_adapter *)func;
}
