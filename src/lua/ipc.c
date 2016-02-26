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
#include "lua/ipc.h"
#include "lua/fiber.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

/* Help CC understand control flow better, prevent warnings about
 * uninitialized variables. */
int luaL_error(lua_State *L, const char *fmt, ...) __attribute__((__noreturn__));

#include <ipc.h>
#include "lua/utils.h"
#include <fiber.h>

static const char ipc_lib[]   = "ipc";

/******************** channel ***************************/

static int
lbox_ipc_channel(struct lua_State *L)
{
	lua_Integer size = 0;

	if (lua_gettop(L) > 0) {
		if (lua_gettop(L) != 1 || !lua_isnumber(L, 1))
			luaL_error(L, "fiber.channel(size): bad arguments");

		size = lua_tointeger(L, -1);
		if (size < 0)
			luaL_error(L, "fiber.channel(size): negative size");
	}
	struct ipc_channel *ch = (struct ipc_channel *)
		lua_newuserdata(L, ipc_channel_memsize(size));
	if (ch == NULL)
		luaL_error(L, "fiber.channel: not enough memory");
	ipc_channel_create(ch, size);

	luaL_getmetatable(L, ipc_lib);

	lua_setmetatable(L, -2);
	return 1;
}

static inline struct ipc_channel *
lbox_check_channel(struct lua_State *L, int narg, const char *source)
{
	void *ch;
	int top = lua_gettop(L);

	if (narg > top || top + narg < 0 ||
	    (ch = luaL_checkudata(L, narg, ipc_lib)) == NULL) {

		luaL_error(L, "usage: %s", source);
	}
	return (struct ipc_channel *) ch;
}

static int
lbox_ipc_channel_gc(struct lua_State *L)
{
	struct ipc_channel *ch = (struct ipc_channel *)
		luaL_checkudata(L, -1, ipc_lib);
	if (ch)
		ipc_channel_destroy(ch);
	return 0;
}

static int
lbox_ipc_channel_is_full(struct lua_State *L)
{
	struct ipc_channel *ch = lbox_check_channel(L, -1, "channel:is_full()");
	lua_pushboolean(L, ipc_channel_is_full(ch));
	return 1;
}

static int
lbox_ipc_channel_is_empty(struct lua_State *L)
{
	struct ipc_channel *ch = lbox_check_channel(L, -1,
						    "channel:is_empty()");
	lua_pushboolean(L, ipc_channel_is_empty(ch));
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
lbox_ipc_channel_put(struct lua_State *L)
{
	int rc = -1;
	struct ipc_channel *ch =
		lbox_check_channel(L, 1, "channel:put(var, [, timeout])");
	ev_tstamp timeout;

	switch (lua_gettop(L)) {
	case 2:
		timeout = TIMEOUT_INFINITY;
		break;
	case 3:
		if (lua_isnumber(L, -1)) {
			timeout = lua_tonumber(L, -1);
			if (timeout >= 0)
				/** Leave var on top of the stack. */
				lua_pop(L, 1);
				break;
		}
		/** Fall through. */
	default:
		luaL_error(L, "usage: channel:put(var [, timeout])");
	}

	struct ipc_value *value = ipc_value_new();
	if (value == NULL)
		goto end;

	value->base.destroy = lua_ipc_value_destroy;
	/* The referenced value is on top of the stack. */
	value->i = luaL_ref(L, LUA_REGISTRYINDEX);

	rc = ipc_channel_put_msg_timeout(ch, &value->base, timeout);
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
lbox_ipc_channel_get(struct lua_State *L)
{
	struct ipc_channel *ch =
		lbox_check_channel(L, 1, "channel:get([timeout])");
	ev_tstamp timeout;

	switch (lua_gettop(L)) {
	case 1:
		timeout = TIMEOUT_INFINITY;
		break;
	case 2:
		if (lua_isnumber(L, -1)) {
			timeout = lua_tonumber(L, -1);
			if (timeout >= 0)
				break;
		}
		/** Fall through. */
	default:
		luaL_error(L, "usage: channel:get([timeout])");
	}

	struct ipc_value *value;
	if (ipc_channel_get_msg_timeout(ch, (struct ipc_msg **) &value,
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
lbox_ipc_channel_has_readers(struct lua_State *L)
{
	struct ipc_channel *ch = lbox_check_channel(L, -1,
						    "channel:has_readers()");
	lua_pushboolean(L, ipc_channel_has_readers(ch));
	return 1;
}

static int
lbox_ipc_channel_has_writers(struct lua_State *L)
{
	struct ipc_channel *ch = lbox_check_channel(L, -1,
						    "channel:has_writers()");
	lua_pushboolean(L, ipc_channel_has_writers(ch));
	return 1;
}

static int
lbox_ipc_channel_size(struct lua_State *L)
{
	struct ipc_channel *ch = lbox_check_channel(L, -1, "channel:size()");
	lua_pushinteger(L, ipc_channel_size(ch));
	return 1;
}

static int
lbox_ipc_channel_count(struct lua_State *L)
{
	struct ipc_channel *ch = lbox_check_channel(L, -1, "channel:count()");
	lua_pushinteger(L, ipc_channel_count(ch));
	return 1;
}

static int
lbox_ipc_channel_close(struct lua_State *L)
{
	struct ipc_channel *ch = lbox_check_channel(L, 1, "channel:close()");
	/* Shutdown the channel for writing and wakeup waiters */
	ipc_channel_close(ch);
	return 0;
}

static int
lbox_ipc_channel_is_closed(struct lua_State *L)
{
	struct ipc_channel *ch = lbox_check_channel(L, 1,
						    "channel:is_closed()");
	lua_pushboolean(L, ipc_channel_is_closed(ch));
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
		{"has_readers",	lbox_ipc_channel_has_readers},
		{"has_writers",	lbox_ipc_channel_has_writers},
		{"count",	lbox_ipc_channel_count},
		{"size",	lbox_ipc_channel_size},
		{"close",	lbox_ipc_channel_close},
		{"is_closed",	lbox_ipc_channel_is_closed},
		{NULL, NULL}
	};
	luaL_register_type(L, ipc_lib, channel_meta);

	static const struct luaL_reg ipc_meta[] = {
		{"channel",	lbox_ipc_channel},
		{NULL, NULL}
	};

	luaL_register_module(L, "fiber", ipc_meta);
	lua_pop(L, 1);
}
