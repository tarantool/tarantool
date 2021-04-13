/*
 *
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
#include "stat.h"

#include <string.h>
#include <rmean.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "box/box.h"
#include "box/iproto.h"
#include "box/engine.h"
#include "box/vinyl.h"
#include "box/sql.h"
#include "info/info.h"
#include "lua/info.h"
#include "lua/utils.h"

extern struct rmean *rmean_box;
extern struct rmean *rmean_error;
extern struct rmean *rmean_tx_wal_bus;

static void
fill_stat_item(struct lua_State *L, int rps, int64_t total)
{
	lua_pushstring(L, "rps");
	lua_pushnumber(L, rps);
	lua_settable(L, -3);

	lua_pushstring(L, "total");
	lua_pushnumber(L, total);
	lua_settable(L, -3);
}

static int
set_stat_item(const char *name, int rps, int64_t total, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State *) cb_ctx;

	lua_pushstring(L, name);
	lua_newtable(L);

	fill_stat_item(L, rps, total);

	lua_settable(L, -3);

	return 0;
}

/**
 * A stat_foreach() callback used to handle access to e.g.
 * box.stats.DELETE.
 */
static int
seek_stat_item(const char *name, int rps, int64_t total, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State *) cb_ctx;
	if (strcmp(name, lua_tostring(L, -1)) != 0)
		return 0;

	lua_newtable(L);
	fill_stat_item(L, rps, total);

	return 1;
}

static int
lbox_stat_index(struct lua_State *L)
{
	luaL_checkstring(L, -1);
	int res = rmean_foreach(rmean_box, seek_stat_item, L);
	if (res)
		return res;
	return rmean_foreach(rmean_error, seek_stat_item, L);
}

static int
lbox_stat_call(struct lua_State *L)
{
	lua_newtable(L);
	rmean_foreach(rmean_box, set_stat_item, L);
	rmean_foreach(rmean_error, set_stat_item, L);
	return 1;
}

static int
lbox_stat_vinyl(struct lua_State *L)
{
	struct info_handler h;
	luaT_info_handler_create(&h, L);
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_stat(vinyl, &h);
	return 1;
}

static int
lbox_stat_reset(struct lua_State *L)
{
	(void)L;
	box_reset_stat();
	iproto_reset_stat();
	return 0;
}

/**
 * Push a table with a network metric to a Lua stack.
 *
 * Expects one argument with a name of a needed metric. The pushed
 * table contains some subset of 'total', 'rps', 'current' fields.
 *
 * Metrics are the same as in lbox_stat_net_call().
 */
static int
lbox_stat_net_index(struct lua_State *L)
{
	const char *key = luaL_checkstring(L, -1);
	if (iproto_rmean_foreach(seek_stat_item, L) == 0)
		return 0;

	if (strcmp(key, "CONNECTIONS") == 0) {
		lua_pushstring(L, "current");
		lua_pushnumber(L, iproto_connection_count());
		lua_rawset(L, -3);
	} else if (strcmp(key, "REQUESTS") == 0) {
		lua_pushstring(L, "current");
		lua_pushnumber(L, iproto_request_count());
		lua_rawset(L, -3);
	}
	return 1;
}

/**
 * Push a table of network metrics to a Lua stack.
 *
 * Metrics and their fields are:
 *
 * - SENT (packets): total, rps;
 * - RECEIVED (packets): total, rps;
 * - CONNECTIONS: current.
 *
 * These fields have the following meaning:
 *
 * - total -- amount of events since start;
 * - rps -- amount of events per second, mean over last 5 seconds;
 * - current -- amount of resources currently hold (say, number of
 *   open connections).
 */
static int
lbox_stat_net_call(struct lua_State *L)
{
	lua_newtable(L);
	iproto_rmean_foreach(set_stat_item, L);

	lua_pushstring(L, "CONNECTIONS");
	lua_rawget(L, -2);
	lua_pushstring(L, "current");
	lua_pushnumber(L, iproto_connection_count());
	lua_rawset(L, -3);
	lua_pop(L, 1);

	lua_pushstring(L, "REQUESTS");
	lua_rawget(L, -2);
	lua_pushstring(L, "current");
	lua_pushnumber(L, iproto_request_count());
	lua_rawset(L, -3);
	lua_pop(L, 1);

	return 1;
}

static int
lbox_stat_sql(struct lua_State *L)
{
	struct info_handler info;
	luaT_info_handler_create(&info, L);
	sql_debug_info(&info);
	return 1;
}

static const struct luaL_Reg lbox_stat_meta [] = {
	{"__index", lbox_stat_index},
	{"__call",  lbox_stat_call},
	{NULL, NULL}
};

static const struct luaL_Reg lbox_stat_net_meta [] = {
	{"__index", lbox_stat_net_index},
	{"__call",  lbox_stat_net_call},
	{NULL, NULL}
};

/** Initialize box.stat package. */
void
box_lua_stat_init(struct lua_State *L)
{
	static const struct luaL_Reg statlib [] = {
		{"vinyl", lbox_stat_vinyl},
		{"reset", lbox_stat_reset},
		{"sql", lbox_stat_sql},
		{NULL, NULL}
	};

	luaL_register_module(L, "box.stat", statlib);

	lua_newtable(L);
	luaL_register(L, NULL, lbox_stat_meta);
	lua_setmetatable(L, -2);
	lua_pop(L, 1); /* stat module */

	static const struct luaL_Reg netstatlib [] = {
		{NULL, NULL}
	};

	luaL_register_module(L, "box.stat.net", netstatlib);

	lua_newtable(L);
	luaL_register(L, NULL, lbox_stat_net_meta);
	lua_setmetatable(L, -2);
	lua_pop(L, 1); /* stat net module */
}

