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
#include "lua/session.h"
#include "lua/utils.h"
#include "lua/trigger.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <fiber.h>
#include <session.h>
#include <sio.h>

static const char *sessionlib_name = "box.session";

/**
 * Return a unique monotonic session
 * identifier. The identifier can be used
 * to check whether or not a session is alive.
 * 0 means there is no session (e.g.
 * a procedure is running in a detached
 * fiber).
 */
static int
lbox_session_id(struct lua_State *L)
{
	lua_pushnumber(L, fiber->session ? fiber->session->id : 0);
	return 1;
}

/**
 * Check whether or not a session exists.
 */
static int
lbox_session_exists(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "session.exists(sid): bad arguments");

	uint32_t sid = luaL_checkint(L, -1);
	lua_pushnumber(L, session_exists(sid));
	return 1;
}

/**
 * Check whether or not a session exists.
 */
static int
lbox_session_fd(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "session.fd(sid): bad arguments");

	uint32_t sid = luaL_checkint(L, -1);
	lua_pushnumber(L, session_fd(sid));
	return 1;
}


/**
 * Pretty print peer name.
 */
static int
lbox_session_peer(struct lua_State *L)
{
	if (lua_gettop(L) > 1)
		luaL_error(L, "session.peer(sid): bad arguments");

	uint32_t sid = lua_gettop(L) == 1 ?
		luaL_checkint(L, -1) : fiber->session->id;

	int fd = session_fd(sid);
	struct sockaddr_in addr;
	sio_getpeername(fd, &addr);

	lua_pushstring(L, sio_strfaddr(&addr));
	return 1;
}

/**
 * run on_connect|on_disconnect trigger
 */
static void
lbox_session_run_trigger(struct trigger *trigger, void * /* event */)
{
	lua_State *L = lua_newthread(tarantool_L);
	LuarefGuard coro_guard(tarantool_L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, (intptr_t) trigger->data);
	lbox_call(L, 0, 0);
}

static int
lbox_session_on_connect(struct lua_State *L)
{
	return lbox_trigger_reset(L, 2, &session_on_connect,
				  lbox_session_run_trigger);
}

static int
lbox_session_on_disconnect(struct lua_State *L)
{
	return lbox_trigger_reset(L, 2, &session_on_disconnect,
				  lbox_session_run_trigger);
}

void
session_storage_cleanup(int sid)
{
	static int ref = LUA_REFNIL;
	struct lua_State *L = tarantool_L;

	int top = lua_gettop(L);

	if (ref == LUA_REFNIL) {
		lua_getfield(L, LUA_GLOBALSINDEX, "box");
		lua_getfield(L, -1, "session");
		lua_getmetatable(L, -1);
		lua_getfield(L, -1, "aggregate_storage");
		ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref);

	lua_pushnil(L);
	lua_rawseti(L, -2, sid);
	lua_settop(L, top);
}

void
tarantool_lua_session_init(struct lua_State *L)
{
	static const struct luaL_reg sessionlib[] = {
		{"id", lbox_session_id},
		{"fd", lbox_session_fd},
		{"exists", lbox_session_exists},
		{"peer", lbox_session_peer},
		{"on_connect", lbox_session_on_connect},
		{"on_disconnect", lbox_session_on_disconnect},
		{NULL, NULL}
	};
	luaL_register(L, sessionlib_name, sessionlib);
	lua_pop(L, 1);
}
