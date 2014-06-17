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

#include "info.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
} /* extern "C" */

#include "box/replica.h"
#include "box/recovery.h"
#include "box/cluster.h"
#include "tarantool.h"
#include "box/box.h"
#include "lua/utils.h"

static int
lbox_info_recovery_lag(struct lua_State *L)
{
	if (recovery_state->remote)
		lua_pushnumber(L, recovery_state->remote->recovery_lag);
	else
		lua_pushnumber(L, 0);
	return 1;
}

static int
lbox_info_recovery_last_update_tstamp(struct lua_State *L)
{
	if (recovery_state->remote)
		lua_pushnumber(L,
			recovery_state->remote->recovery_last_update_tstamp);
	else
		lua_pushnumber(L, 0);
	return 1;
}

static int
lbox_info_node(struct lua_State *L)
{
	if (recovery_state->node_id == 0) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, 0, 2);
	lua_pushliteral(L, "id");
	lua_pushinteger(L, recovery_state->node_id);
	lua_settable(L, -3);
	lua_pushliteral(L, "uuid");
	lua_pushlstring(L, tt_uuid_str(&recovery_state->node_uuid), UUID_STR_LEN);
	lua_settable(L, -3);
	lua_pushliteral(L, "lsn");
	luaL_pushnumber64(L, vclock_get(&recovery_state->vclock,
					recovery_state->node_id));
	lua_settable(L, -3);

	return 1;
}

static int
lbox_info_vclock(struct lua_State *L)
{
	lua_createtable(L, 0, vclock_size(&recovery_state->vclock));
	vclock_foreach(&recovery_state->vclock, it) {
		lua_pushinteger(L, it.id);
		luaL_pushnumber64(L, it.lsn);
		lua_settable(L, -3);
	}

	return 1;
}

static int
lbox_info_status(struct lua_State *L)
{
	lua_pushstring(L, box_status());
	return 1;
}

static int
lbox_info_uptime(struct lua_State *L)
{
	lua_pushnumber(L, (unsigned)tarantool_uptime() + 1);
	return 1;
}

static int
lbox_info_snapshot_pid(struct lua_State *L)
{
	lua_pushnumber(L, snapshot_pid);
	return 1;
}

static int
lbox_info_logger_pid(struct lua_State *L)
{
	lua_pushnumber(L, logger_pid);
	return 1;
}


static const struct luaL_reg
lbox_info_dynamic_meta [] =
{
	{"recovery_lag", lbox_info_recovery_lag},
	{"recovery_last_update", lbox_info_recovery_last_update_tstamp},
	{"vclock", lbox_info_vclock},
	{"node", lbox_info_node},
	{"status", lbox_info_status},
	{"uptime", lbox_info_uptime},
	{"snapshot_pid", lbox_info_snapshot_pid},
	{"logger_pid", lbox_info_logger_pid},
	{NULL, NULL}
};

/** Evaluate box.info.* function value and push it on the stack. */
static int
lbox_info_index(struct lua_State *L)
{
	lua_pushvalue(L, -1);			/* dup key */
	lua_gettable(L, lua_upvalueindex(1));   /* table[key] */

	if (!lua_isfunction(L, -1)) {
		/* No such key. Leave nil is on the stack. */
		return 1;
	}

	lua_call(L, 0, 1);
	lua_remove(L, -2);
	return 1;
}

/** Push a bunch of compile-time or start-time constants into a Lua table. */
static void
lbox_info_init_static_values(struct lua_State *L)
{
	/* tarantool version */
	lua_pushstring(L, "version");
	lua_pushstring(L, tarantool_version());
	lua_settable(L, -3);

	/* pid */
	lua_pushstring(L, "pid");
	lua_pushnumber(L, getpid());
	lua_settable(L, -3);

	/* build */
	lua_pushstring(L, "build");
	lua_newtable(L);

	/* box.info.build.target */
	lua_pushstring(L, "target");
	lua_pushstring(L, BUILD_INFO);
	lua_settable(L, -3);

	/* box.info.build.options */
	lua_pushstring(L, "options");
	lua_pushstring(L, BUILD_OPTIONS);
	lua_settable(L, -3);

	/* box.info.build.compiler */
	lua_pushstring(L, "compiler");
	lua_pushstring(L, COMPILER_INFO);
	lua_settable(L, -3);

	/* box.info.build.flags */
	lua_pushstring(L, "flags");
	lua_pushstring(L, TARANTOOL_C_FLAGS);
	lua_settable(L, -3);

	lua_settable(L, -3);    /* box.info.build */
}

/**
 * When user invokes box.info(), return a table of key/value
 * pairs containing the current info.
 */
static int
lbox_info_call(struct lua_State *L)
{
	lua_newtable(L);
	lbox_info_init_static_values(L);
	for (int i = 0; lbox_info_dynamic_meta[i].name; i++) {
		lua_pushstring(L, lbox_info_dynamic_meta[i].name);
		lbox_info_dynamic_meta[i].func(L);
		lua_settable(L, -3);
	}
	return 1;
}

/** Initialize box.info package. */
void
box_lua_info_init(struct lua_State *L)
{
	static const struct luaL_reg infolib [] = {
		{NULL, NULL}
	};

	luaL_register(L, "box.info", infolib);

	lua_newtable(L);		/* metatable for info */

	lua_pushstring(L, "__index");

	lua_newtable(L);
	luaL_register(L, NULL, lbox_info_dynamic_meta); /* table for __index */
	lua_pushcclosure(L, lbox_info_index, 1);
	lua_settable(L, -3);

	lua_pushstring(L, "__call");
	lua_pushcfunction(L, lbox_info_call);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);

	lbox_info_init_static_values(L);

	lua_pop(L, 1); /* info module */
}
