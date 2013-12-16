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
#include "lua/plugin.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
} /* extern "C" */

#include <plugin.h>
#include "lua/utils.h"

static int
plugin_lua_stat_cb(struct tarantool_plugin *p, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State *) cb_ctx;
	lua_pushstring(L, p->name);
	lua_newtable(L);
	lua_pushstring(L, "version");
	luaL_pushnumber64(L, p->version);
	lua_settable(L, -3);
	lua_settable(L, -3);
	return 0;
}

static int
lbox_plugin_info(struct lua_State *L)
{
	lua_newtable(L);
	lua_pushstring(L, "plugin");
	lua_newtable(L);
	plugin_foreach(plugin_lua_stat_cb, L);
	lua_settable(L, -3);
	return 1;
}

/** Initialize box.plugin package. */

void
tarantool_lua_plugin_init(struct lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "plugin");
	lua_newtable(L);

	lua_pushstring(L, "info");
	lua_pushcfunction(L, lbox_plugin_info);
	lua_settable(L, -3);

	lua_settable(L, -3);
	lua_pop(L, 1);
}
