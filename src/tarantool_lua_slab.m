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
#include "tarantool_lua_slab.h"
#include "tarantool_lua.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <salloc.h>

/** A callback passed into salloc_stat() and invoked for every slab class. */
static int
salloc_stat_lua_cb(const struct slab_class_stats *cstat, void *cb_ctx)
{
	struct lua_State *L = cb_ctx;

	/*
	 * Create a Lua table for every slab class. A class is
	 * defined by its item size.
	 */
	luaL_pushnumber64(L, cstat->item_size);
	lua_newtable(L);

	lua_pushstring(L, "slabs");
	luaL_pushnumber64(L, cstat->slabs);
	lua_settable(L, -3);

	lua_pushstring(L, "items");
	luaL_pushnumber64(L, cstat->items);
	lua_settable(L, -3);

	lua_pushstring(L, "bytes_used");
	luaL_pushnumber64(L, cstat->bytes_used);
	lua_settable(L, -3);

	lua_pushstring(L, "bytes_free");
	luaL_pushnumber64(L, cstat->bytes_free);
	lua_settable(L, -3);

	lua_pushstring(L, "item_size");
	luaL_pushnumber64(L, cstat->item_size);
	lua_settable(L, -3);

	lua_settable(L, -3);

	return 0;
}

static int
lbox_slab_slabs(struct lua_State *L)
{
	lua_newtable(L);
	salloc_stat(salloc_stat_lua_cb, NULL, L);
	return 1;
}

static int
lbox_slab_arena_used(struct lua_State *L)
{
	struct slab_arena_stats astat;
	salloc_stat(NULL, &astat, NULL);
	luaL_pushnumber64(L, astat.used);
	return 1;
}

static int
lbox_slab_arena_size(struct lua_State *L)
{
	struct slab_arena_stats astat;
	salloc_stat(NULL, &astat, NULL);
	luaL_pushnumber64(L, astat.size);
	return 1;
}

static int
lbox_slab_call(struct lua_State *L)
{
	struct slab_arena_stats astat;

	lua_newtable(L);
	lua_pushstring(L, "slabs");
	lua_newtable(L);
	salloc_stat(salloc_stat_lua_cb, &astat, L);
	lua_settable(L, -3);

	lua_pushstring(L, "arena_used");
	luaL_pushnumber64(L, astat.used);
	lua_settable(L, -3);

	lua_pushstring(L, "arena_size");
	luaL_pushnumber64(L, astat.size);
	lua_settable(L, -3);

	return 1;
}

static const struct luaL_reg lbox_slab_dynamic_meta [] = {
	{"slabs", lbox_slab_slabs},
	{"arena_used", lbox_slab_arena_used},
	{"arena_size", lbox_slab_arena_size},
	{NULL, NULL}
};

static int
lbox_slab_index(struct lua_State *L)
{
	lua_pushvalue(L, -1);			/* dup key */
	lua_gettable(L, lua_upvalueindex(1));   /* table[key] */

	if (!lua_isfunction(L, -1)) {
		/* If key is not found, leave nil on the stack. */
		return 1;
	}

	lua_call(L, 0, 1);
	lua_remove(L, -2);
	return 1;
}

/** Initialize box.slab package. */
void
tarantool_lua_slab_init(struct lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");

	lua_pushstring(L, "slab");
	lua_newtable(L);		/* box.slab */

	lua_newtable(L);
	lua_pushstring(L, "__call");
	lua_pushcfunction(L, lbox_slab_call);
	lua_settable(L, -3);
	lua_pushstring(L, "__index");
	lua_newtable(L);
	luaL_register(L, NULL, lbox_slab_dynamic_meta);
	lua_pushcclosure(L, lbox_slab_index, 1);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);

	lua_settable(L, -3);    /* box.slab = created table */
	lua_pop(L, 1);          /* cleanup stack */
}
