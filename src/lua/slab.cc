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
#include "lua/slab.h"
#include "lua/utils.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
} /* extern "C" */

#include <salloc.h>

/** A callback passed into salloc_stat() and invoked for every slab class. */
static int
salloc_stat_lua_cb(const struct slab_cache_stats *cstat, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State *) cb_ctx;

	/*
	 * Create a Lua table for every slab class. A class is
	 * defined by its item size.
	 */
	lua_pushnumber(L, cstat->item_size);
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
lbox_slab_info(struct lua_State *L)
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

static int
lbox_slab_check(struct lua_State *L __attribute__((unused)))
{
	slab_validate();
	return 0;
}

/** Initialize box.slab package. */
void
tarantool_lua_slab_init(struct lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "slab");
	lua_newtable(L);

	lua_pushstring(L, "info");
	lua_pushcfunction(L, lbox_slab_info);
	lua_settable(L, -3);

	lua_pushstring(L, "check");
	lua_pushcfunction(L, lbox_slab_check);
	lua_settable(L, -3);

	lua_settable(L, -3);
	lua_pop(L, 1);
}
