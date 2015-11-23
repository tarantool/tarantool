/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#include "sophia.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua/utils.h"

#if 0
/**
 * When user invokes box.sophia(), return a table of key/value
 * pairs containing the current info.
 */
static int
lbox_sophia_call(struct lua_State *L)
{
	lua_newtable(L);
	sophia_info(lbox_sophia_cb, (void*)L);
	return 1;
}

#if 0
static int
lbox_sophia_index(struct lua_State *L)
{
	lbox_sophia_call(L);
	//lua_pushvalue(L, -2);
	lua_gettable(L, -2);
	return 1;
}
#endif

/** Initialize box.sophia package. */
void
box_lua_sophia_init(struct lua_State *L)
{
	/*
	static const struct luaL_reg sophialib [] = {
		{NULL, NULL}
	};
	*/

	//luaL_register_module(L, "box.sophia", sophialib);
	lua_register(L, "box.sophia", lbox_sophia_call);

	//lbox_sophia_call(L);

	/*
	lua_newtable(L);

	lua_pushstring(L, "__index");
	lua_pushcfunction(L, lbox_sophia_index);
	lua_settable(L, -3);

	lua_pushstring(L, "__serialize");
	lua_pushcfunction(L, lbox_sophia_call);
	lua_settable(L, -3);
	*/

	//lua_setmetatable(L, -2);
	lua_pop(L, 1);
}
#endif

#if 0
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
#endif

typedef void (*sophia_info_f)(const char*, const char*, void *);

extern int sophia_info(const char *name, sophia_info_f, void *);

static void
lbox_sophia_cb_index(const char *key, const char *value, void *arg)
{
	(void) key;
	struct lua_State *L;
	L = (struct lua_State*)arg;
	if (value == NULL) {
		lua_pushnil(L);
		return;
	}
	lua_pushstring(L, value);
}

static int
lbox_sophia_index(struct lua_State *L)
{
	luaL_checkstring(L, -1);
	const char *name = lua_tostring(L, -1);
	return sophia_info(name, lbox_sophia_cb_index, (void*)L);
}

static void
lbox_sophia_cb(const char *key, const char *value, void *arg)
{
	struct lua_State *L;
	L = (struct lua_State*)arg;
	if (value == NULL)
		return;
	lua_pushstring(L, key);
	lua_pushstring(L, value);
	lua_settable(L, -3);
}

static int
lbox_sophia_call(struct lua_State *L)
{
	lua_newtable(L);
	sophia_info(NULL, lbox_sophia_cb, (void*)L);
	return 1;
}

static const struct luaL_reg lbox_sophia_meta [] = {
	{"__index", lbox_sophia_index},
	{"__call",  lbox_sophia_call},
	{NULL, NULL}
};

void
box_lua_sophia_init(struct lua_State *L)
{
	static const struct luaL_reg sophialib [] = {
		{NULL, NULL}
	};

	luaL_register_module(L, "box.sophia", sophialib);

	lua_newtable(L);
	luaL_register(L, NULL, lbox_sophia_meta);
	lua_setmetatable(L, -2);

	lua_pop(L, 1); /* sophia module */
}
