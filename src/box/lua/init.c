/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/lua/init.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua/utils.h" /* luaT_error() */

#include "box/box.h"
#include "box/txn.h"

#include "box/lua/error.h"
#include "box/lua/tuple.h"
#include "box/lua/call.h"
#include "box/lua/slab.h"
#include "box/lua/index.h"
#include "box/lua/space.h"
#include "box/lua/misc.h"
#include "box/lua/stat.h"
#include "box/lua/info.h"
#include "box/lua/session.h"
#include "box/lua/net_box.h"
#include "box/lua/cfg.h"
#include "box/lua/xlog.h"
#include "box/lua/console.h"

extern char session_lua[],
	tuple_lua[],
	schema_lua[],
	load_cfg_lua[],
	snapshot_daemon_lua[],
	net_box_lua[],
	upgrade_lua[],
	console_lua[];

static const char *lua_sources[] = {
	"box/session", session_lua,
	"box/tuple", tuple_lua,
	"box/schema", schema_lua,
	"box/snapshot_daemon", snapshot_daemon_lua,
	"box/upgrade", upgrade_lua,
	"box/net_box", net_box_lua,
	"box/console", console_lua,
	"box/load_cfg", load_cfg_lua,
	NULL
};

static int
lbox_commit(lua_State *L)
{
	if (box_txn_commit() != 0)
		return luaT_error(L);
	return 0;
}

static int
lbox_rollback(lua_State *L)
{
	(void)L;
	if (box_txn_rollback() != 0)
		return luaT_error(L);
	return 0;
}

static int
lbox_snapshot(struct lua_State *L)
{
	int ret = box_snapshot();
	if (ret == 0) {
		lua_pushstring(L, "ok");
		return 1;
	}
	return luaT_error(L);
}

static const struct luaL_reg boxlib[] = {
	{"commit", lbox_commit},
	{"rollback", lbox_rollback},
	{"snapshot", lbox_snapshot},
	{NULL, NULL}
};

#include "say.h"

void
box_lua_init(struct lua_State *L)
{
	/* Use luaL_register() to set _G.box */
	luaL_register(L, "box", boxlib);
	lua_pop(L, 1);

	box_lua_error_init(L);
	box_lua_tuple_init(L);
	box_lua_call_init(L);
	box_lua_cfg_init(L);
	box_lua_slab_init(L);
	box_lua_index_init(L);
	box_lua_space_init(L);
	box_lua_misc_init(L);
	box_lua_info_init(L);
	box_lua_stat_init(L);
	box_lua_session_init(L);
	box_lua_xlog_init(L);
	luaopen_net_box(L);
	lua_pop(L, 1);
	tarantool_lua_console_init(L);
	lua_pop(L, 1);

	/* Load Lua extension */
	for (const char **s = lua_sources; *s; s += 2) {
		const char *modname = *s;
		const char *modsrc = *(s + 1);
		const char *modfile = lua_pushfstring(L,
			"@builtin/%s.lua", modname);
		if (luaL_loadbuffer(L, modsrc, strlen(modsrc), modfile))
			panic("Error loading Lua module %s...: %s",
			      modname, lua_tostring(L, -1));
		lua_call(L, 0, 0);
		lua_pop(L, 1); /* modfile */
	}

	assert(lua_gettop(L) == 0);
}
