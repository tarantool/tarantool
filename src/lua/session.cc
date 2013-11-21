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
#include "lua/init.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "fiber.h"
#include "session.h"
#include "sio.h"

static const char *sessionlib_name = "box.session";
static int session_ref = 0;
extern lua_State *root_L;

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

struct lbox_session_trigger
{
	struct session_trigger *trigger;
	int ref;
};

static struct lbox_session_trigger on_connect =
	{ &session_on_connect, LUA_NOREF};
static struct lbox_session_trigger on_disconnect =
	{ &session_on_disconnect, LUA_NOREF};

static void
lbox_session_run_trigger(void *param)
{
	struct lbox_session_trigger *trigger =
			(struct lbox_session_trigger *) param;
	/* Copy the referenced callable object object stack. */
	lua_State *L = lua_newthread(tarantool_L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	lua_rawgeti(tarantool_L, LUA_REGISTRYINDEX, trigger->ref);
	/** Move the function to be called to the new coro. */
	lua_xmove(tarantool_L, L, 1);

	try {
		lua_call(L, 0, 0);
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
	} catch (const Exception& e) {
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
		throw;
	} catch (...) {
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
		tnt_raise(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
	}
}

static int
lbox_session_set_trigger(struct lua_State *L,
			 struct lbox_session_trigger *trigger)
{
	if (lua_gettop(L) != 1 ||
	    (lua_type(L, -1) != LUA_TFUNCTION &&
	     lua_type(L, -1) != LUA_TNIL)) {
		luaL_error(L, "session.on_connect(chunk): bad arguments");
	}

	/* Pop the old trigger */
	if (trigger->ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, trigger->ref);
		luaL_unref(L, LUA_REGISTRYINDEX, trigger->ref);
	} else {
		lua_pushnil(L);
	}

	/*
	 * Set or clear the trigger. Return the old value of the
	 * trigger.
	 */
	if (lua_type(L, -2) == LUA_TNIL) {
		trigger->ref = LUA_NOREF;
		trigger->trigger->trigger = NULL;
		trigger->trigger->param = NULL;
	} else {
		/* Move the trigger to the top of the stack. */
		lua_insert(L, -2);
		/* Reference the new trigger. Pops it. */
		trigger->ref = luaL_ref(L, LUA_REGISTRYINDEX);
		trigger->trigger->trigger = lbox_session_run_trigger;
		trigger->trigger->param = trigger;
	}
	/* Return the old trigger. */
	return 1;
}

static int
lbox_session_on_connect(struct lua_State *L)
{
	return lbox_session_set_trigger(L, &on_connect);
}

static int
lbox_session_on_disconnect(struct lua_State *L)
{
	return lbox_session_set_trigger(L, &on_disconnect);
}



void
session_storage_cleanup(int sid)
{
	assert(session_ref != 0);
	lua_rawgeti(root_L, LUA_REGISTRYINDEX, session_ref);
	lua_pushnil(root_L);
	lua_rawseti(root_L, -2, sid);
	lua_pop(root_L, 1);
}

static int
lbox_session_index(struct lua_State *L)
{
	if (lua_gettop(L) < 2)
		return 0;
	if (lua_isstring(L, 2) && strcmp(lua_tostring(L, 2), "storage") == 0) {
		if (!fiber->sid)
			return 0;

		assert(session_ref != 0);
		lua_rawgeti(L, LUA_REGISTRYINDEX, session_ref);

		lua_rawgeti(L, -1, fiber->sid);

		/* create session.storage on-demand */
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);			/* nil */
			lua_newtable(L);		/* new storage */
			lua_pushvalue(L, -1);		/* ref to storage */
			lua_rawseti(L, -3, fiber->sid);
		}
		lua_remove(L, -2);			/* storages storage */
		return 1;
	}
	return 0;
}

static int
lbox_session_storages(struct lua_State *L)
{
	assert(session_ref != 0);
	lua_rawgeti(L, LUA_REGISTRYINDEX, session_ref);
	return 1;
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

                /* only for testcase */
                {"storages", lbox_session_storages},
                {NULL, NULL}
        };


	luaL_register(L, sessionlib_name, sessionlib);

	lua_newtable(L);
	lua_pushliteral(L, "__index");
	lua_pushcfunction(L, lbox_session_index);
	lua_rawset(L, -3);
	lua_setmetatable(L, -2);

	lua_pop(L, 1);


	/* all lua sessions storage */
	lua_newtable(L);
	session_ref = luaL_ref(L, LUA_REGISTRYINDEX);
}
