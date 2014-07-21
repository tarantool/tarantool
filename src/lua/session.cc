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
#include "box/access.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <fiber.h>
#include <session.h>
#include <sio.h>

static const char *sessionlib_name = "session";

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
	lua_pushnumber(L, session()->id);
	return 1;
}

/** Session user id. */
static int
lbox_session_uid(struct lua_State *L)
{
	lua_pushnumber(L, session()->uid);
	return 1;
}

/** Session user id. */
static int
lbox_session_user(struct lua_State *L)
{
	struct user *user = user_cache_find(session()->uid);
	if (user)
		lua_pushstring(L, user->name);
	else
		lua_pushnil(L);
	return 1;
}

/** Session user id. */
static int
lbox_session_su(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "session.su(): bad arguments");
	struct session *session = session();
	if (session == NULL)
		luaL_error(L, "session.su(): session does not exit");
	struct user *user;
	if (lua_type(L, 1) == LUA_TSTRING) {
		size_t len;
		const char *name = lua_tolstring(L, 1, &len);
		user = user_by_name(name, len);
		if (user == NULL)
			tnt_raise(ClientError, ER_NO_SUCH_USER, name);
	} else {
		uint32_t uid = lua_tointeger(L, 1);;
		user = user_cache_find(uid);
		if (user == NULL) {
			tnt_raise(ClientError, ER_NO_SUCH_USER,
				  int2str(uid));
		}
	}
	session_set_user(session, user->auth_token, user->uid);
	return 0;
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
		luaL_checkint(L, -1) : session()->id;

	int fd = session_fd(sid);
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	sio_getpeername(fd, (struct sockaddr *)&addr, &addrlen);

	lua_pushstring(L, sio_strfaddr((struct sockaddr *)&addr, addrlen));
	return 1;
}

static int
lbox_session_delimiter(struct lua_State *L)
{
	if (session() == NULL)
		luaL_error(L, "session.delimiter(): session does not exit");

	if (lua_gettop(L) < 1) {
		/* Get delimiter */
		lua_pushstring(L, session()->delim);
		return 1;
	}

	/* Set delimiter */
	if (lua_type(L, 1) != LUA_TSTRING)
		luaL_error(L, "session.delimiter(string): expected a string");

	snprintf(session()->delim, SESSION_DELIM_SIZE, "%s",
		 lua_tostring(L, 1));
	return 0;
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
		lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
		if (!lua_istable(L, -1))
			goto exit;
		lua_getfield(L, -1, "session");
		if (!lua_istable(L, -1))
			goto exit;
		lua_getmetatable(L, -1);
		if (!lua_istable(L, -1))
			goto exit;
		lua_getfield(L, -1, "aggregate_storage");
		if (!lua_istable(L, -1))
			goto exit;
		ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref);

	lua_pushnil(L);
	lua_rawseti(L, -2, sid);
exit:
	lua_settop(L, top);
}

void
tarantool_lua_session_init(struct lua_State *L)
{
	static const struct luaL_reg sessionlib[] = {
		{"id", lbox_session_id},
		{"uid", lbox_session_uid},
		{"user", lbox_session_user},
		{"su", lbox_session_su},
		{"fd", lbox_session_fd},
		{"exists", lbox_session_exists},
		{"peer", lbox_session_peer},
		{"delimiter", lbox_session_delimiter},
		{"on_connect", lbox_session_on_connect},
		{"on_disconnect", lbox_session_on_disconnect},
		{NULL, NULL}
	};
	luaL_register_module(L, sessionlib_name, sessionlib);
	lua_pop(L, 1);
}
