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
#include "lua/utils.h"

extern struct rmean *rmean_box;
extern struct rmean *rmean_error;
/** network statistics (iproto & cbus) */
extern struct rmean *rmean_net;
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
lbox_stat_reset(struct lua_State *L)
{
	(void)L;
	box_reset_stat();
	iproto_reset_stat();
	return 0;
}

static int
lbox_stat_net_index(struct lua_State *L)
{
	luaL_checkstring(L, -1);
	return rmean_foreach(rmean_net, seek_stat_item, L);
}

static int
lbox_stat_net_call(struct lua_State *L)
{
	lua_newtable(L);
	rmean_foreach(rmean_net, set_stat_item, L);
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
		{"reset", lbox_stat_reset},
		{NULL, NULL}
	};

	luaL_register_module(L, "box.stat", statlib);

	lua_newtable(L);
	luaL_register(L, NULL, lbox_stat_meta);
	lua_setmetatable(L, -2);
	lua_pop(L, 1); /* stat module */


	luaL_register_module(L, "box.stat.net", statlib);

	lua_newtable(L);
	luaL_register(L, NULL, lbox_stat_net_meta);
	lua_setmetatable(L, -2);
	lua_pop(L, 1); /* stat net module */
}

