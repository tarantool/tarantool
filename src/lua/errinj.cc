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

#include "lua/errinj.h"

#include <errinj.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
} /* extern "C" */

static int
lbox_errinj_set(struct lua_State *L)
{
	char *name = (char*)luaL_checkstring(L, 1);
	bool state = lua_toboolean(L, 2);
	if (errinj_set_byname(name, state)) {
		lua_pushfstring(L, "error: can't find error injection '%s'", name);
		return 1;
	}
	lua_pushstring(L, "ok");
	return 1;
}

static inline int
lbox_errinj_cb(struct errinj *e, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State*)cb_ctx;
	lua_pushstring(L, e->name);
	lua_newtable(L);
	lua_pushstring(L, "state");
	lua_pushboolean(L, e->state);
	lua_settable(L, -3);
	lua_settable(L, -3);
	return 0;
}

static int
lbox_errinj_info(struct lua_State *L)
{
	lua_newtable(L);
	errinj_foreach(lbox_errinj_cb, L);
	return 1;
}

static const struct luaL_reg errinjlib[] = {
	{"info", lbox_errinj_info},
	{"set", lbox_errinj_set},
	{NULL, NULL}
};

/** Initialize box.errinj package. */
void
tarantool_lua_errinj_init(struct lua_State *L)
{
	luaL_register(L, "box.errinj", errinjlib);
	lua_pop(L, 1);
}
