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

#include "ifc_lua.h"
#include "ifc.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "tarantool_lua.h"
#include <stdlib.h>
#include <say.h>

static const char semaphore_lib[] = "box.ifc.semaphore";
static const char mutex_lib[]     = "box.ifc.mutex";
static const char channel_lib[]   = "box.ifc.channel";

/******************** semaphore ***************************/

static int
lbox_fiber_semaphore(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isnumber(L, 1))
		luaL_error(L, "fiber.semaphore(count): bad arguments");

	struct fiber_semaphore *sm = fiber_semaphore_alloc();
	if (!sm)
		luaL_error(L, "fiber.semaphore: Not enough memory");
	fiber_semaphore_init(sm, lua_tointeger(L, -1));



	void **ptr = lua_newuserdata(L, sizeof(void *));
	luaL_getmetatable(L, semaphore_lib);
	lua_setmetatable(L, -2);
	*ptr = sm;
	return 1;
}

static inline struct fiber_semaphore *
lbox_check_semaphore(struct lua_State *L, int narg)
{
	return * (void **) luaL_checkudata(L, narg, semaphore_lib);
}

static int
lbox_fiber_semaphore_up(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: semaphore:up()");


	struct fiber_semaphore *sm = lbox_check_semaphore(L, -1);
	fiber_semaphore_up(sm);
	lua_pushnumber(L, fiber_semaphore_counter(sm));
	return 1;
}

static int
lbox_fiber_semaphore_down(struct lua_State *L)
{
	lua_Number timeout;
	struct fiber_semaphore *sm;


	int top = lua_gettop(L);

	if (top < 1 || top > 2 || !lua_isuserdata(L, -top))
		luaL_error(L, "usage: semaphore:down([timeout])");

	if (top == 2) {
		if (!lua_isnumber(L, -1))
			luaL_error(L, "timeout must be number");
		timeout = lua_tonumber(L, -1);
	} else {
		timeout = 0;
	}


	sm = lbox_check_semaphore(L, -top);
	if (fiber_semaphore_down_timeout(sm, timeout) == 0)
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}

static int
lbox_fiber_semaphore_counter(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: semaphore()");


	struct fiber_semaphore *sm = lbox_check_semaphore(L, -1);

	lua_pushnumber(L, fiber_semaphore_counter(sm));
	return 1;
}

static int
lbox_fiber_semaphore_gc(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		return 0;

	struct fiber_semaphore *sm = lbox_check_semaphore(L, -1);
	free(sm);
	return 0;
}


static int
lbox_fiber_trydown(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: semaphore:trydown()");

	struct fiber_semaphore *sm = lbox_check_semaphore(L, -1);
	if (fiber_semaphore_trydown(sm) == 0)
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}

/******************** mutex ***************************/

static int
lbox_fiber_mutex(struct lua_State *L)
{
	say_info(":%s()", __func__);
	struct fiber_mutex *m = fiber_mutex_alloc();
	if (!m)
		luaL_error(L, "fiber.mutex: Not enough memory");
	fiber_mutex_init(m);

	void **ptr = lua_newuserdata(L, sizeof(void *));
	luaL_getmetatable(L, mutex_lib);
	lua_setmetatable(L, -2);
	*ptr = m;
	return 1;
}

static inline struct fiber_mutex *
lbox_check_mutex(struct lua_State *L, int narg)
{
	return * (void **) luaL_checkudata(L, narg, mutex_lib);
}


static int
lbox_fiber_mutex_gc(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		return 0;
	struct fiber_mutex *m = lbox_check_mutex(L, -1);
	free(m);
	return 0;
}

static int
lbox_fiber_mutex_locked(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: mutex()");
	struct fiber_mutex *m = lbox_check_mutex(L, -1);

	lua_pushboolean(L, fiber_mutex_islocked(m));
	return 1;
}


static int
lbox_fiber_mutex_lock(struct lua_State *L)
{
	int top = lua_gettop(L);
	if (top < 1 || top > 2 || !lua_isuserdata(L, -top))
		luaL_error(L, "usage: mutex:lock([timeout])");
	struct fiber_mutex *m = lbox_check_mutex(L, -top);
	lua_Number timeout;
	if (top == 2) {
		if (!lua_isnumber(L, -1))
			luaL_error(L, "timeout must be number");
		timeout = lua_tonumber(L, -1);
	} else {
		timeout = 0;
	}

	if (fiber_mutex_lock_timeout(m, timeout) == 0)
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}


static int
lbox_fiber_mutex_unlock(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: mutex:unlock()");
	struct fiber_mutex *m = lbox_check_mutex(L, -1);

	fiber_mutex_unlock(m);
	return 0;
}


static int
lbox_fiber_mutex_trylock(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: mutex:trylock()");
	struct fiber_mutex *m = lbox_check_mutex(L, -1);

	lua_pushboolean(L, fiber_mutex_trylock(m) == 0);
	return 1;
}


/******************** channel ***************************/

static int
lbox_fiber_channel(struct lua_State *L)
{
	say_info(":%s()", __func__);

	lua_Integer size = 1;

	if (lua_gettop(L) > 0) {
		if (lua_gettop(L) != 1 || !lua_isnumber(L, 1))
			luaL_error(L, "fiber.channel(size): bad arguments");

		size = lua_tointeger(L, -1);
		if (size < 0)
			luaL_error(L, "fiber.channel(size): negative size");
	}
	struct fiber_channel *ch = fiber_channel_alloc(size);
	if (!ch)
		luaL_error(L, "fiber.channel: Not enough memory");
	fiber_channel_init(ch);

	void **ptr = lua_newuserdata(L, sizeof(void *));
	luaL_getmetatable(L, channel_lib);

	lua_pushstring(L, "rid");	/* first object id */
	lua_pushnumber(L, 1);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);
	*ptr = ch;
	return 1;
}

static inline struct fiber_channel *
lbox_check_channel(struct lua_State *L, int narg)
{
	return * (void **) luaL_checkudata(L, narg, channel_lib);
}


static int
lbox_fiber_channel_gc(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		return 0;
	struct fiber_channel *ch = lbox_check_channel(L, -1);
	free(ch);
	return 0;
}


static int
lbox_fiber_channel_isfull(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: channel:is_full()");
	struct fiber_channel *ch = lbox_check_channel(L, -1);
	if (fiber_channel_isfull(ch))
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}

static int
lbox_fiber_channel_isempty(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: channel:is_empty()");
	struct fiber_channel *ch = lbox_check_channel(L, -1);
	if (fiber_channel_isempty(ch))
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}

static int
lbox_fiber_channel_put(struct lua_State *L)
{
	double timeout;
	int top = lua_gettop(L);
	struct fiber_channel *ch;

	switch(top) {
		case 2:
			timeout = 0;
			break;
		case 3:
			if (!lua_isnumber(L, -1))
				luaL_error(L, "timeout must be number");
			timeout = lua_tonumber(L, -1);
			if (timeout < 0)
				luaL_error(L, "wrong timeout");
			break;
		default:
			luaL_error(L, "usage: channel:put(var [, timeout])");


	}
	ch = lbox_check_channel(L, -top);

	lua_getmetatable(L, -top);

	lua_pushstring(L, "rid");
	lua_gettable(L, -2);

	lua_Integer rid = lua_tointeger(L, -1);
	if (rid < 0x7FFFFFFF)
		rid++;
	else
		rid = 1;

	lua_pushstring(L, "rid");	/* update object id */
	lua_pushnumber(L, rid);
	lua_settable(L, -4);

	lua_pushnumber(L, rid);
	lua_pushvalue(L, 2);
	lua_settable(L, -4);
	lua_settop(L, top);


	if (fiber_channel_put_timeout(ch, (void *)rid, timeout) == 0)
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}

static int
lbox_fiber_channel_get(struct lua_State *L)
{
	int top = lua_gettop(L);
	double timeout;

	if (top > 2 || top < 1 || !lua_isuserdata(L, -top))
		luaL_error(L, "usage: channel:get([timeout])");

	if (top == 2) {
		if (!lua_isnumber(L, 2))
			luaL_error(L, "timeout must be number");
		timeout = lua_tonumber(L, 2);
		if (timeout < 0)
			luaL_error(L, "wrong timeout");
	} else {
		timeout = 0;
	}

	struct fiber_channel *ch = lbox_check_channel(L, 1);

	lua_Integer rid = (lua_Integer)fiber_channel_get_timeout(ch, timeout);

	if (!rid) {
		lua_pushnil(L);
		return 1;
	}

	lua_getmetatable(L, 1);

	lua_pushstring(L, "broadcast_message");
	lua_gettable(L, -2);

	if (lua_isnil(L, -1)) {	/* common messages */
		lua_pop(L, 1);		/* nil */

		lua_pushnumber(L, rid);		/* extract and delete value */
		lua_gettable(L, -2);

		lua_pushnumber(L, rid);
		lua_pushnil(L);
		lua_settable(L, -4);
	}

	lua_remove(L, -2);	/* cleanup stack (metatable) */
	return 1;
}



static int
lbox_fiber_channel_broadcast(struct lua_State *L)
{
	struct fiber_channel *ch;

	if (lua_gettop(L) != 2)
		luaL_error(L, "usage: channel:broadcast(variable)");

	ch = lbox_check_channel(L, -2);

	lua_getmetatable(L, -2);			/* 3 */

	lua_pushstring(L, "broadcast_message");		/* 4 */

	/* save old value */
	lua_pushstring(L, "broadcast_message");
	lua_gettable(L, 3);				/* 5 */

	lua_pushstring(L, "broadcast_message");		/* save object */
	lua_pushvalue(L, 2);
	lua_settable(L, 3);

	int count = fiber_channel_broadcast(ch, (void *)1);

	lua_settable(L, 3);

	lua_pop(L, 1);		/* stack cleanup */
	lua_pushnumber(L, count);

	return 1;
}



void
fiber_ifc_lua_init(struct lua_State *L)
{
	static const struct luaL_reg semaphore_meta[] = {
		{"__call",	lbox_fiber_semaphore_counter},
		{"__gc",	lbox_fiber_semaphore_gc},
		{"up",		lbox_fiber_semaphore_up},
		{"down",	lbox_fiber_semaphore_down},
		{"trydown",	lbox_fiber_trydown},
		{NULL, NULL}
	};

	tarantool_lua_register_type(L, semaphore_lib, semaphore_meta);


	static const struct luaL_reg mutex_meta[] = {
		{"__gc",	lbox_fiber_mutex_gc},
		{"__call",	lbox_fiber_mutex_locked},
		{"locked",	lbox_fiber_mutex_locked},
		{"lock",	lbox_fiber_mutex_lock},
		{"unlock",	lbox_fiber_mutex_unlock},
		{"trylock",	lbox_fiber_mutex_trylock},
		{NULL, NULL}
	};
	tarantool_lua_register_type(L, mutex_lib, mutex_meta);

	static const struct luaL_reg channel_meta[] = {
		{"__gc",	lbox_fiber_channel_gc},
		{"is_full",	lbox_fiber_channel_isfull},
		{"is_empty",	lbox_fiber_channel_isempty},
		{"put",		lbox_fiber_channel_put},
		{"get",		lbox_fiber_channel_get},
		{"broadcast",	lbox_fiber_channel_broadcast},
		{NULL, NULL}
	};
	tarantool_lua_register_type(L, channel_lib, channel_meta);



	static const struct luaL_reg ifc_meta[] = {
		{"semaphore",	lbox_fiber_semaphore},
		{"mutex",	lbox_fiber_mutex},
		{"channel",	lbox_fiber_channel},
		{NULL, NULL}
	};


	lua_getfield(L, LUA_GLOBALSINDEX, "box");

	lua_pushstring(L, "ifc");
	lua_newtable(L);			/* box.ifc table */
	luaL_register(L, NULL, ifc_meta);
	lua_settable(L, -3);
	lua_pop(L, 1);
}

