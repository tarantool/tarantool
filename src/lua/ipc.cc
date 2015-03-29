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

#include "lua/ipc.h"
#include <stdlib.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
} /* extern "C" */

#include <ipc.h>
#include "lua/utils.h"

static const char channel_lib[]   = "fiber.channel";

#define BROADCAST_MASK	(((size_t)1) << (CHAR_BIT * sizeof(size_t) - 1))

/******************** channel ***************************/

static int
lbox_ipc_channel(struct lua_State *L)
{
	lua_Integer size = 1;

	if (lua_gettop(L) > 0) {
		if (lua_gettop(L) != 1 || !lua_isnumber(L, 1))
			luaL_error(L, "fiber.channel(size): bad arguments");

		size = lua_tointeger(L, -1);
		if (size < 0)
			luaL_error(L, "fiber.channel(size): negative size");
	}
	struct ipc_channel *ch = ipc_channel_new(size);
	if (!ch)
		luaL_error(L, "fiber.channel: Not enough memory");

	void **ptr = (void **) lua_newuserdata(L, sizeof(void *));
	luaL_getmetatable(L, channel_lib);

	lua_pushstring(L, "rid");	/* first object id */
	lua_pushnumber(L, 1);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);
	*ptr = ch;
	return 1;
}

static inline struct ipc_channel *
lbox_check_channel(struct lua_State *L, int narg)
{
	return *(struct ipc_channel **) luaL_checkudata(L, narg, channel_lib);
}

static int
lbox_ipc_channel_gc(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		return 0;
	struct ipc_channel *ch = lbox_check_channel(L, -1);
	ipc_channel_delete(ch);
	return 0;
}

static int
lbox_ipc_channel_is_full(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: channel:is_full()");
	struct ipc_channel *ch = lbox_check_channel(L, -1);
	lua_pushboolean(L, ipc_channel_is_full(ch));
	return 1;
}

static int
lbox_ipc_channel_is_empty(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: channel:is_empty()");
	struct ipc_channel *ch = lbox_check_channel(L, -1);
	lua_pushboolean(L, ipc_channel_is_empty(ch));
	return 1;
}

static int
lbox_ipc_channel_put(struct lua_State *L)
{
	ev_tstamp timeout = 0;
	int top = lua_gettop(L);
	struct ipc_channel *ch;

	switch (top) {
	case 2:
		timeout = TIMEOUT_INFINITY;
		break;
	case 3:
		if (!lua_isnumber(L, -1))
			luaL_error(L, "timeout must be a number");
		timeout = lua_tonumber(L, -1);
		if (timeout < 0)
			luaL_error(L, "wrong timeout");
		break;
	default:
		luaL_error(L, "usage: channel:put(var [, timeout])");
	}
	ch = lbox_check_channel(L, -top);

	lua_pushvalue(L, 2);
	size_t vref = luaL_ref(L, LUA_REGISTRYINDEX);

	int retval;
	if (ipc_channel_put_timeout(ch, (void *)vref, timeout) == 0) {
		retval = 1;
	} else {
		/* timed out or closed */
		luaL_unref(L, LUA_REGISTRYINDEX, vref);
		retval = 0;
	}

	lua_settop(L, top);
	lua_pushboolean(L, retval);
	return 1;
}

static int
lbox_ipc_channel_get(struct lua_State *L)
{
	int top = lua_gettop(L);
	ev_tstamp timeout;

	if (top > 2 || top < 1 || !lua_isuserdata(L, -top))
		luaL_error(L, "usage: channel:get([timeout])");

	if (top == 2) {
		if (!lua_isnumber(L, 2))
			luaL_error(L, "timeout must be a number");
		timeout = lua_tonumber(L, 2);
		if (timeout < 0)
			luaL_error(L, "wrong timeout");
	} else {
		timeout = TIMEOUT_INFINITY;
	}

	struct ipc_channel *ch = lbox_check_channel(L, 1);

	size_t vref = (size_t)ipc_channel_get_timeout(ch, timeout);

	if (!vref) {
		/* timed out or closed */
		lua_pushnil(L);
		return 1;
	}
	if (vref & BROADCAST_MASK) {
		vref &= ~BROADCAST_MASK;
		lua_rawgeti(L, LUA_REGISTRYINDEX, vref);
		return 1;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, vref);
	luaL_unref(L, LUA_REGISTRYINDEX, vref);
	return 1;
}

static int
lbox_ipc_channel_broadcast(struct lua_State *L)
{
	struct ipc_channel *ch;

	if (lua_gettop(L) != 2)
		luaL_error(L, "usage: channel:broadcast(variable)");

	ch = lbox_check_channel(L, 1);

	if (!ipc_channel_has_readers(ch))
		return lbox_ipc_channel_put(L);


	lua_pushvalue(L, 2);
	size_t vref = luaL_ref(L, LUA_REGISTRYINDEX);
	int count = ipc_channel_broadcast(ch, (void *)(vref | BROADCAST_MASK));
	luaL_unref(L, LUA_REGISTRYINDEX, vref);
	lua_pushnumber(L, count);
	return 1;
}

static int
lbox_ipc_channel_has_readers(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "usage: channel:has_readers()");
	struct ipc_channel *ch = lbox_check_channel(L, -1);
	lua_pushboolean(L, ipc_channel_has_readers(ch));
	return 1;
}

static int
lbox_ipc_channel_has_writers(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "usage: channel:has_writers()");
	struct ipc_channel *ch = lbox_check_channel(L, -1);
	lua_pushboolean(L, ipc_channel_has_writers(ch));
	return 1;
}

static int
lbox_ipc_channel_size(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "usage: channel:has_writers()");
	struct ipc_channel *ch = lbox_check_channel(L, -1);
	lua_pushinteger(L, ipc_channel_size(ch));
	return 1;
}

static int
lbox_ipc_channel_count(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "usage: channel:has_writers()");
	struct ipc_channel *ch = lbox_check_channel(L, -1);
	lua_pushinteger(L, ipc_channel_count(ch));
	return 1;
}

static int
lbox_ipc_channel_close(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "usage: channel:close()");
	struct ipc_channel *ch = lbox_check_channel(L, 1);
	ipc_channel_close(ch);
	return 0;
}

static int
lbox_ipc_channel_is_closed(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "usage: channel:is_closed()");
	struct ipc_channel *ch = lbox_check_channel(L, 1);
	if (ipc_channel_is_closed(ch))
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}

void
tarantool_lua_ipc_init(struct lua_State *L)
{
	static const struct luaL_reg channel_meta[] = {
		{"__gc",	lbox_ipc_channel_gc},
		{"is_full",	lbox_ipc_channel_is_full},
		{"is_empty",	lbox_ipc_channel_is_empty},
		{"put",		lbox_ipc_channel_put},
		{"get",		lbox_ipc_channel_get},
		{"broadcast",	lbox_ipc_channel_broadcast},
		{"has_readers",	lbox_ipc_channel_has_readers},
		{"has_writers",	lbox_ipc_channel_has_writers},
		{"count",	lbox_ipc_channel_count},
		{"size",	lbox_ipc_channel_size},
		{"close",	lbox_ipc_channel_close},
		{"is_closed",	lbox_ipc_channel_is_closed},
		{NULL, NULL}
	};
	luaL_register_type(L, channel_lib, channel_meta);

	static const struct luaL_reg ipc_meta[] = {
		{"channel",	lbox_ipc_channel},
		{NULL, NULL}
	};

	luaL_register_module(L, "fiber", ipc_meta);
	lua_pop(L, 1);
}
