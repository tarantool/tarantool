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


#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <string.h>

#include "box_lua_stat.h"
#include <stat.h>


static int _one_stat_item(const char *name, i64 value, int rps, void *udata) {
	struct lua_State *L = udata;

	lua_pushstring(L, name);
	lua_newtable(L);

	lua_pushstring(L, "rps");
	lua_pushnumber(L, rps);
	lua_settable(L, -3);

	lua_pushstring(L, "total");
	lua_pushnumber(L, value);
	lua_settable(L, -3);

	lua_settable(L, -3);

	return 0;
}


struct _seek_udata {
	const char *key;
	struct lua_State *L;
};

static int _seek_stat_item(const char *name, i64 value, int rps, void *udata) {

	struct _seek_udata *sst = udata;
	if (strcmp(name, sst->key) != 0)
		return 0;

	lua_newtable(sst->L);

	lua_pushstring(sst->L, "rps");
	lua_pushnumber(sst->L, rps);
	lua_settable(sst->L, -3);

	lua_pushstring(sst->L, "total");
	lua_pushnumber(sst->L, value);
	lua_settable(sst->L, -3);

	return 1;
}

static int lbox_stat_index(struct lua_State *L) {


	struct _seek_udata sst;
	sst.key = lua_tolstring(L, -1, NULL);
	sst.L = L;

	return stat_foreach(_seek_stat_item, &sst);
}


static int lbox_stat_full(struct lua_State *L) {
	lua_newtable(L);

	stat_foreach(_one_stat_item, L);

	return 1;
}

static const struct luaL_reg lbox_stat_meta [] = {
	{"__index", lbox_stat_index},
	{"__call",  lbox_stat_full},
	{NULL, NULL}
};


void box_lua_stat_init(struct lua_State *L) {
	lua_getfield(L, LUA_GLOBALSINDEX, "box");

	lua_pushstring(L, "stat");
	lua_newtable(L);

	lua_newtable(L);
	luaL_register(L, NULL, lbox_stat_meta);
	lua_setmetatable(L, -2);


	lua_settable(L, -3);    /* box.stat = created table */
	lua_pop(L, 1);          /* cleanup stack */
}

