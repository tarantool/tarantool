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
#include "lua/fiber_channel.h"

#include "lua/fiber.h"
#include <trivia/util.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

/* Help CC understand control flow better, prevent warnings about
 * uninitialized variables. */
NORETURN int
luaL_error(lua_State *L, const char *fmt, ...);

#include "lua/utils.h"
#include <fiber.h>
#include <fiber_channel.h>

static const char channel_typename[] = "fiber.channel";

static int
luaT_fiber_channel(struct lua_State *L)
{
	lua_Integer size = 0;

	if (lua_isnoneornil(L, 1)) {
		size = 0;
	} else if (lua_isnumber(L, 1)) {
		size = lua_tointeger(L, -1);
		if (size < 0)
			luaL_error(L, "fiber.channel(size): negative size");
	} else {
		luaL_error(L, "fiber.channel(size): bad arguments");
	}

	struct fiber_channel *ch = (struct fiber_channel *)
		lua_newuserdata(L, fiber_channel_memsize(size));
	if (ch == NULL)
		luaL_error(L, "fiber.channel: not enough memory");
	fiber_channel_create(ch, size);

	luaL_getmetatable(L, channel_typename);

	lua_setmetatable(L, -2);
	return 1;
}

static inline struct fiber_channel *
luaT_checkfiberchannel(struct lua_State *L, int index, const char *source)
{
	assert(index > 0);
	if (index > lua_gettop(L))
		luaL_error(L, "usage: %s", source);
	/* Note: checkudata errs on mismatch, no point in checking res */
	return (struct fiber_channel *) luaL_checkudata(L, index,
							channel_typename);
}

static int
luaT_fiber_channel_gc(struct lua_State *L)
{
	struct fiber_channel *ch = (struct fiber_channel *)
		luaL_checkudata(L, -1, channel_typename);
	if (ch)
		fiber_channel_destroy(ch);
	return 0;
}

static int
luaT_fiber_channel_is_full(struct lua_State *L)
{
	struct fiber_channel *ch = luaT_checkfiberchannel(L, 1, "channel:is_full()");
	lua_pushboolean(L, fiber_channel_is_full(ch));
	return 1;
}

static int
luaT_fiber_channel_is_empty(struct lua_State *L)
{
	struct fiber_channel *ch = luaT_checkfiberchannel(L, 1,
						    "channel:is_empty()");
	lua_pushboolean(L, fiber_channel_is_empty(ch));
	return 1;
}

static void
lua_ipc_value_destroy(struct ipc_msg *base)
{
	struct ipc_value *value = (struct ipc_value *) base;
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, value->i);
	ipc_value_delete(base);
}

static int
luaT_fiber_channel_put(struct lua_State *L)
{
	static const char usage[] = "channel:put(var [, timeout])";
	int rc = -1;
	struct fiber_channel *ch =
		luaT_checkfiberchannel(L, 1, usage);
	ev_tstamp timeout;

	/* val */
	if (lua_gettop(L) < 2)
		luaL_error(L, "usage: %s", usage);

	/* timeout (optional) */
	if (lua_isnoneornil(L, 3)) {
		timeout = TIMEOUT_INFINITY;
	} else if (lua_isnumber(L, 3)) {
		timeout = lua_tonumber(L, 3);
		if (timeout < 0)
			luaL_error(L, "usage: %s", usage);
	} else {
		luaL_error(L, "usage: %s", usage);
	}

	struct ipc_value *value = ipc_value_new();
	if (value == NULL)
		goto end;

	value->base.destroy = lua_ipc_value_destroy;
	lua_pushvalue(L, 2);
	value->i = luaL_ref(L, LUA_REGISTRYINDEX);

	rc = fiber_channel_put_msg_timeout(ch, &value->base, timeout);
	if (rc) {
		value->base.destroy(&value->base);
#if 0
		/* Treat everything except timeout as error. */
		if (!type_cast(TimedOut, diag_last_error(&fiber()->diag)))
			diag_raise();
#else
		luaL_testcancel(L);
#endif
	}
end:
	lua_pushboolean(L, rc == 0);
	return 1;
}

static int
luaT_fiber_channel_get(struct lua_State *L)
{
	static const char usage[] = "channel:get([timeout])";
	struct fiber_channel *ch =
		luaT_checkfiberchannel(L, 1, usage);
	ev_tstamp timeout;

	/* timeout (optional) */
	if (lua_isnoneornil(L, 2)) {
		timeout = TIMEOUT_INFINITY;
	} else if (lua_isnumber(L, 2)) {
		timeout = lua_tonumber(L, 2);
		if (timeout < 0)
			luaL_error(L, "usage: %s", usage);
	} else {
		luaL_error(L, "usage: %s", usage);
	}

	struct ipc_value *value;
	if (fiber_channel_get_msg_timeout(ch, (struct ipc_msg **) &value,
					timeout)) {
#if 0
		/* Treat everything except timeout as error. */
		if (!type_cast(TimedOut, diag_last_error(&fiber()->diag)))
			diag_raise();
#else
		luaL_testcancel(L);
#endif
		lua_pushnil(L);
		return 1;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, value->i);
	value->base.destroy(&value->base);
	return 1;
}

static int
luaT_fiber_channel_has_readers(struct lua_State *L)
{
	struct fiber_channel *ch = luaT_checkfiberchannel(L, 1,
						    "channel:has_readers()");
	lua_pushboolean(L, fiber_channel_has_readers(ch));
	return 1;
}

static int
luaT_fiber_channel_has_writers(struct lua_State *L)
{
	struct fiber_channel *ch = luaT_checkfiberchannel(L, 1,
						    "channel:has_writers()");
	lua_pushboolean(L, fiber_channel_has_writers(ch));
	return 1;
}

static int
luaT_fiber_channel_size(struct lua_State *L)
{
	struct fiber_channel *ch = luaT_checkfiberchannel(L, 1, "channel:size()");
	lua_pushinteger(L, fiber_channel_size(ch));
	return 1;
}

static int
luaT_fiber_channel_count(struct lua_State *L)
{
	struct fiber_channel *ch = luaT_checkfiberchannel(L, 1, "channel:count()");
	lua_pushinteger(L, fiber_channel_count(ch));
	return 1;
}

static int
luaT_fiber_channel_close(struct lua_State *L)
{
	struct fiber_channel *ch = luaT_checkfiberchannel(L, 1, "channel:close()");
	/* Shutdown the channel for writing and wakeup waiters */
	fiber_channel_close(ch);
	return 0;
}

static int
luaT_fiber_channel_is_closed(struct lua_State *L)
{
	struct fiber_channel *ch = luaT_checkfiberchannel(L, 1,
						    "channel:is_closed()");
	lua_pushboolean(L, fiber_channel_is_closed(ch));
	return 1;
}

static int
luaT_fiber_channel_to_string(struct lua_State *L)
{
	struct fiber_channel *ch = luaT_checkfiberchannel(L, 1, "");
	if (fiber_channel_is_closed(ch)) {
		lua_pushstring(L, "channel: closed");
	} else {
		lua_pushfstring(L, "channel: %d", (int)fiber_channel_count(ch));
	}
	return 1;
}

void
tarantool_lua_fiber_channel_init(struct lua_State *L)
{
	static const struct luaL_Reg channel_meta[] = {
		{"__gc",	luaT_fiber_channel_gc},
		{"__tostring",	luaT_fiber_channel_to_string},
		{"is_full",	luaT_fiber_channel_is_full},
		{"is_empty",	luaT_fiber_channel_is_empty},
		{"put",		luaT_fiber_channel_put},
		{"get",		luaT_fiber_channel_get},
		{"has_readers",	luaT_fiber_channel_has_readers},
		{"has_writers",	luaT_fiber_channel_has_writers},
		{"count",	luaT_fiber_channel_count},
		{"size",	luaT_fiber_channel_size},
		{"close",	luaT_fiber_channel_close},
		{"is_closed",	luaT_fiber_channel_is_closed},
		{NULL, NULL}
	};
	luaL_register_type(L, channel_typename, channel_meta);

	static const struct luaL_Reg ipc_lib[] = {
		{"channel",	luaT_fiber_channel},
		{NULL, NULL}
	};

	luaL_register_module(L, "fiber", ipc_lib);
	lua_pop(L, 1);
}
