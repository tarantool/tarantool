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

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "fiber.h"
#include "session.h"
#include "sio.h"
#include <scoped_guard.h>
#include "../box/box_lua.h"

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
	lua_pushnumber(L, fiber->sid);
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
 * Pretty print peer name.
 */
static int
lbox_session_peer(struct lua_State *L)
{
	if (lua_gettop(L) > 1)
		luaL_error(L, "session.peer(sid): bad arguments");

	uint32_t sid = lua_gettop(L) == 1 ?
		luaL_checkint(L, -1) : fiber->sid;

	int fd = session_fd(sid);
	struct sockaddr_in addr;
	sio_getpeername(fd, &addr);

	lua_pushstring(L, sio_strfaddr(&addr));
	return 1;
}


/**
 * run on_connect|on_disconnect trigger with lua context
 */
static void
lbox_session_run_trigger_luactx(struct lua_State *L, va_list ap)
{
	struct lua_trigger *lt = va_arg(ap, struct lua_trigger *);
	lua_rawgeti(L, LUA_REGISTRYINDEX, lt->ref);
	lua_call(L, 0, 0);
}

/**
 * run on_connect|on_disconnect trigger
 */
static void
lbox_session_run_trigger(struct trigger *trg, void * /* event */)
{
	struct lbox_session_trigger *trigger =
			(struct lbox_session_trigger *) trg;
	/* Copy the referenced callable object object stack. */
	lua_State *L = lua_newthread(tarantool_L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	lua_rawgeti(tarantool_L, LUA_REGISTRYINDEX, trigger->ref);
	/** Move the function to be called to the new coro. */
	lua_xmove(tarantool_L, L, 1);

	try {
		lbox_call(L, 0, 0);
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
	} catch (const Exception& e) {
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
		throw;
	}
}

/**
 * set/reset/get trigger
 */
static int
lbox_session_set_trigger(struct lua_State *L, struct rlist *list)
{
	int top = lua_gettop(L);
	if (top > 1 || (top && !lua_isfunction(L, 1) && !lua_isnil(L, 1)))
		luaL_error(L, "session.on_connect(chunk): bad arguments");

	struct lua_trigger *current = 0;
	struct trigger *trigger;
	rlist_foreach_entry(trigger, list, link) {
		if (trigger->run == lbox_session_run_trigger) {
			current = (struct lua_trigger *)trigger;
			break;
		}
	}

	/* get current trigger */
	if (top == 0) {
		if (current)
			lua_rawgeti(L, LUA_REGISTRYINDEX, current->ref);
		else
			lua_pushnil(L);
		return 1;
	}

	/* cleanup the trigger */
	if (lua_isnil(L, 1)) {
		if (current) {
			trigger_clear(&current->trigger);
			luaL_unref(L, LUA_REGISTRYINDEX, current->ref);
			free(current);
		}
		lua_pushnil(L);
		return 1;
	}

	/* set new trigger */
	lua_pushvalue(L, 1);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);


	if (current) {
		luaL_unref(L, LUA_REGISTRYINDEX, current->ref);
		current->ref = ref;
	} else {
		current = (struct lua_trigger *)
			malloc(sizeof(struct lua_trigger));
		if (!current) {
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
			luaL_error(L, "Can't allocate memory for trigger");
		}
		current->trigger.destroy = NULL;
		current->trigger.run = lbox_session_run_trigger;
		current->ref = ref;
		trigger_set(list, &current->trigger);
	}

	lua_pushvalue(L, 1);
	return 1;

}

static int
lbox_session_on_connect(struct lua_State *L)
{
	return lbox_session_set_trigger(L, &session_on_connect);
}

static int
lbox_session_on_disconnect(struct lua_State *L)
{
	return lbox_session_set_trigger(L, &session_on_disconnect);
}


void
tarantool_lua_session_init(struct lua_State *L)
{
	static const struct luaL_reg sessionlib[] = {
		{"id", lbox_session_id},
		{"exists", lbox_session_exists},
		{"peer", lbox_session_peer},
		{"on_connect", lbox_session_on_connect},
		{"on_disconnect", lbox_session_on_disconnect},
		{NULL, NULL}
	};
	luaL_register(L, sessionlib_name, sessionlib);
	lua_pop(L, 1);
}
