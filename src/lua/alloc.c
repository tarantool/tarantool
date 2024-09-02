/*
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 *
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
#include "lua/utils.h"

#include <lauxlib.h> /* luaL_error */

#define LUA_MEMORY_MIN (256LL * 1024 * 1024)
#define LUA_MEMORY_DEFAULT (2LL * 1024 * 1024 * 1024)

/**
 * The default allocator function which is wrapped into a new one
 * with the Lua memory limit checker.
 */
static lua_Alloc orig_alloc;
/**
 * The maximum allowed LuaJIT memory limit.
 */
static size_t memory = LUA_MEMORY_DEFAULT;
/**
 * Currently allocated memory amount by LuaJIT.
 */
static size_t allocated;

/**
 * Lua custom memory allocation function. It extends the original
 * one with a memory counter and a limit check.
 */
static void *
luaT_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	size_t new_allocated = allocated + nsize - osize;
	if (new_allocated > memory) {
		diag_set(OutOfMemory, nsize, "luaT_alloc", "Lua objects");
		return NULL;
	}

	void *result = orig_alloc(ud, ptr, osize, nsize);

	if (result != NULL || nsize == 0)
		allocated = new_allocated;
	else
		diag_set(OutOfMemory, nsize, "luaT_alloc", "Lua objects");

	return result;
}

int
luaT_setallocmemory(size_t amount)
{
	if (amount < LUA_MEMORY_MIN) {
		diag_set(LuajitError, "Cannot limit the Lua memory with values "
			 "less than 256MB");
		return -1;
	}
	if (amount < allocated) {
		diag_set(LuajitError, "Cannot limit the Lua memory with values "
			 "less than the currently allocated amount");
		return -1;
	}
	memory = amount;
	return 0;
}

void
luaT_initalloc(struct lua_State *L)
{
	assert(L != NULL);

	allocated = luaL_getgctotal(L);

	void *orig_ud;
	orig_alloc = lua_getallocf(L, &orig_ud);

	assert(orig_alloc != NULL);

	lua_setallocf(L, luaT_alloc, orig_ud);
}

/**
 * alloc.setmemory() -- set the allocator memory limit.
 */
static int
lbox_alloc_setmemory(struct lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "Usage: alloc.setmemory(amount)");

	long long amount = lua_tointeger(L, 1);

	if (amount < 0) {
		diag_set(IllegalParams, "Invalid memory amount: "
			 "the value must be >= 0");
		return luaT_error(L);
	}

	if (luaT_setallocmemory(amount) < 0)
		return luaT_error(L);

	lua_pushinteger(L, memory);
	return 1;
}

void
tarantool_lua_alloc_init(struct lua_State *L)
{
	static const struct luaL_Reg alloc_methods[] = {
		{"setmemory", lbox_alloc_setmemory},
		{NULL, NULL}
	};

	luaT_newmodule(L, "internal.alloc", alloc_methods);
	lua_pop(L, 1);
}
