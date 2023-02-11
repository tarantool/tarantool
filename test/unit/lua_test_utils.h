/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Incapsulates steps we should do to create a Lua state suitable
 * to initialize some built-in Lua modules.
 */
static inline struct lua_State *
luaT_newteststate(void)
{
	struct lua_State *L = luaL_newstate();
	luaL_openlibs(L);

	/* luaT_newmodule() assumes that this table exists. */
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "_TARANTOOL_BUILTIN");

	return L;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
