/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "lua/utils.h"

#include <lauxlib.h> /* luaL_error */

#define LUA_MEMORY_LIMIT_DEFAULT (2ULL * 1024 * 1024 * 1024)

/**
 * Default allocator function which is wrapped into a new one
 * with the Lua memory limit checker.
 */
static lua_Alloc orig_alloc;
/**
 * Memory limit for LuaJIT.
 */
static size_t memory_limit = LUA_MEMORY_LIMIT_DEFAULT;
/**
 * Amount of memory used by LuaJIT.
 */
static size_t used;

/**
 * Lua custom memory allocation function. It extends the original
 * one with a memory counter and a limit check.
 */
static void *
alloc_with_limit(void *ud, void *ptr, size_t osize, size_t nsize)
{
	size_t new_used = used + nsize - osize;
	if (new_used > memory_limit) {
		/*
		 * Returning NULL results in the "not enough
		 * memory" error.
		 */
		return NULL;
	}

	void *result = orig_alloc(ud, ptr, osize, nsize);

	if (result != NULL || nsize == 0)
		used = new_used;

	/*
	 * Result may be NULL, in this case "not enough memory"
	 * is raised.
	 */
	return result;
}

static int
set_alloc_limit(size_t new_memory_limit)
{
	if (new_memory_limit < used) {
		diag_set(LuajitError, "Cannot limit the Lua memory with values "
			 "less than the currently allocated amount");
		return -1;
	}
	memory_limit = new_memory_limit;
	return 0;
}

void
luaT_initalloc(struct lua_State *L)
{
	assert(L != NULL);

	used = luaL_getgctotal(L);

	void *orig_ud;
	orig_alloc = lua_getallocf(L, &orig_ud);

	assert(orig_alloc != NULL);

	lua_setallocf(L, alloc_with_limit, orig_ud);
}

/**
 * alloc.getlimit() -- get the allocator memory limit.
 */
static int
lbox_alloc_getlimit(struct lua_State *L)
{
	lua_pushinteger(L, memory_limit);
	return 1;
}

/**
 * alloc.setlimit() -- set the allocator memory limit.
 * Returns old memory limit.
 */
static int
lbox_alloc_setlimit(struct lua_State *L)
{
	if (lua_gettop(L) < 1) {
		diag_set(IllegalParams, "Usage: alloc.setlimit(amount)");
		return luaT_error(L);
	}

	ssize_t amount = luaL_checkinteger(L, 1);

	if (amount < 0) {
		diag_set(IllegalParams, "Invalid memory limit: "
			 "the value must be >= 0");
		return luaT_error(L);
	}

	size_t old_memory_limit = memory_limit;

	if (set_alloc_limit(amount) < 0)
		return luaT_error(L);

	lua_pushinteger(L, old_memory_limit);
	return 1;
}

/**
 * alloc.used() -- get the amount of the allocated memory.
 */
static int
lbox_alloc_used(struct lua_State *L)
{
	lua_pushinteger(L, used);
	return 1;
}

/**
 * alloc.unused() -- get the amount of the unused memory.
 */
static int
lbox_alloc_unused(struct lua_State *L)
{
	lua_pushinteger(L, memory_limit - used);
	return 1;
}

void
tarantool_lua_alloc_init(struct lua_State *L)
{
	static const struct luaL_Reg alloc_methods[] = {
		{"setlimit", lbox_alloc_setlimit},
		{"getlimit", lbox_alloc_getlimit},
		{"used",     lbox_alloc_used},
		{"unused",   lbox_alloc_unused},
		{NULL, NULL}
	};

	luaT_newmodule(L, "internal.alloc", alloc_methods);
	lua_pop(L, 1);
}
