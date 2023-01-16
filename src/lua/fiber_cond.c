/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "lua/fiber_cond.h"

#include <tarantool_ev.h>

#include "lua/utils.h"
#include "fiber.h"

#include <fiber_cond.h>

static const char cond_typename[] = "fiber.cond";

static int
luaT_fiber_cond_new(struct lua_State *L)
{
	struct fiber_cond *e = lua_newuserdata(L, sizeof(*e));
	if (e == NULL)
		luaL_error(L, "fiber.cond: not enough memory");
	fiber_cond_create(e);
	luaL_getmetatable(L, cond_typename);
	lua_setmetatable(L, -2);
	return 1;
}

static struct fiber_cond *
luaT_isfibercond(struct lua_State *L, int index)
{
	if (index > lua_gettop(L))
		return NULL;
	return luaL_checkudata(L, index, cond_typename);
}

struct fiber_cond *
luaT_checkfibercond(struct lua_State *L, int index)
{
	struct fiber_cond *fiber_cond = luaT_isfibercond(L, index);
	if (fiber_cond == NULL) {
		luaL_error(L, "Invalid argument #%d "
			   "(fiber.cond expected, got %s)", index,
			   lua_typename(L, lua_type(L, index)));
	}
	return fiber_cond;
}

static int
luaT_fiber_cond_gc(struct lua_State *L)
{
	struct fiber_cond *fiber_cond = luaT_isfibercond(L, 1);
	if (fiber_cond == NULL)
		luaL_error(L, "usage: cond:destroy()");
	fiber_cond_destroy(fiber_cond);
	return 0;
}

static int
luaT_fiber_cond_signal(struct lua_State *L)
{
	struct fiber_cond *fiber_cond = luaT_isfibercond(L, 1);
	if (fiber_cond == NULL)
		luaL_error(L, "usage: cond:signal()");
	fiber_cond_signal(fiber_cond);
	return 0;
}

static int
luaT_fiber_cond_broadcast(struct lua_State *L)
{
	struct fiber_cond *fiber_cond = luaT_isfibercond(L, 1);
	if (fiber_cond == NULL)
		luaL_error(L, "usage: cond:broadcast()");
	fiber_cond_broadcast(fiber_cond);
	return 0;
}

static int
luaT_fiber_cond_wait(struct lua_State *L)
{
	int rc;
	struct fiber_cond *e = luaT_isfibercond(L, 1);
	if (e == NULL)
		luaL_error(L, "usage: cond:wait([timeout])");
	ev_tstamp timeout = TIMEOUT_INFINITY;
	if (!lua_isnoneornil(L, 2)) {
		if (!lua_isnumber(L, 2) ||
		    (timeout = lua_tonumber(L, 2)) < .0) {
			luaL_error(L, "usage: cond:wait([timeout])");
		}
	}
	rc = fiber_cond_wait_timeout(e, timeout);
	if (rc != 0)
		luaL_testcancel(L);
	lua_pushboolean(L, rc == 0);
	return 1;
}

static int
luaT_fiber_cond_tostring(struct lua_State *L)
{
	struct fiber_cond *cond = luaT_checkfibercond(L, 1);
	if (cond == NULL)
		luaL_error(L, "usage: tostring(cond)");
	lua_pushstring(L, "cond");
	return 1;
}

void
tarantool_lua_fiber_cond_init(struct lua_State *L)
{
	static const struct luaL_Reg cond_meta[] = {
		{"__gc",	luaT_fiber_cond_gc},
		{"__tostring",	luaT_fiber_cond_tostring},
		{"signal",	luaT_fiber_cond_signal},
		{"broadcast",	luaT_fiber_cond_broadcast},
		{"wait",	luaT_fiber_cond_wait},
		{NULL, NULL}
	};
	luaL_register_type(L, cond_typename, cond_meta);

	/* Get fiber module. */
	lua_getfield(L, LUA_GLOBALSINDEX, "require");
	lua_pushstring(L, "fiber");
	lua_call(L, 1, 1);

	static const struct luaL_Reg cond_lib[] = {
		{"cond",	luaT_fiber_cond_new},
		{NULL, NULL}
	};

	luaL_setfuncs(L, cond_lib, 0);

	/* Pop fiber module. */
	lua_pop(L, 1);
}
