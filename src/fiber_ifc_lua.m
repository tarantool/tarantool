#include "fiber_ifc_lua.h"
#include "fiber_ifc.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "tarantool_lua.h"
#include <stdlib.h>
#include <say.h>

static const char semaphore_lib[] = "box.fiber.ifc.semaphore";
static const char mutex_lib[]     = "box.fiber.ifc.mutex";
static const char channel_lib[]   = "box.fiber.ifc.channel";

/******************** semaphore ***************************/

static inline struct fiber_semaphore *
lbox_check_semaphore(struct lua_State *L, int narg)
{
	return * (void **) luaL_checkudata(L, narg, semaphore_lib);
}

static int
lbox_fiber_semaphore_up(struct lua_State *L)
{
	say_info(":%s()", __func__);
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
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: semaphore:down()");


	struct fiber_semaphore *sm = lbox_check_semaphore(L, -1);
	fiber_semaphore_down(sm);
	lua_pushnumber(L, fiber_semaphore_counter(sm));
	return 1;
}

static int
lbox_fiber_semaphore_counter(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: semaphore()");


	struct fiber_semaphore *sm = lbox_check_semaphore(L, -1);


	lua_pop(L, -1);
	lua_pushnumber(L, fiber_semaphore_counter(sm));
	return 1;
}

static int
lbox_fiber_semaphore_gc(struct lua_State *L)
{
	say_info(":%s()", __func__);
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

static inline struct fiber_mutex *
lbox_check_mutex(struct lua_State *L, int narg)
{
	return * (void **) luaL_checkudata(L, narg, mutex_lib);
}


static int
lbox_fiber_mutex_gc(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		return 0;
	struct fiber_mutex *m = lbox_check_mutex(L, -1);
	free(m);
	return 0;
}

static int
lbox_fiber_mutex_isfree(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: mutex()");
	struct fiber_mutex *m = lbox_check_mutex(L, -1);

	lua_pushboolean(L, fiber_mutex_isfree(m));
	return 1;
}


static int
lbox_fiber_mutex_lock(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: mutex:lock()");
	struct fiber_mutex *m = lbox_check_mutex(L, -1);

	fiber_mutex_lock(m);
	return 0;
}


static int
lbox_fiber_mutex_unlock(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: mutex:unlock()");
	struct fiber_mutex *m = lbox_check_mutex(L, -1);

	fiber_mutex_unlock(m);
	return 0;
}


static int
lbox_fiber_mutex_trylock(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: mutex:trylock()");
	struct fiber_mutex *m = lbox_check_mutex(L, -1);

	lua_pushboolean(L, fiber_mutex_trylock(m) == 0);
	return 1;
}


/******************** channel ***************************/

static inline struct fiber_channel *
lbox_check_channel(struct lua_State *L, int narg)
{
	return * (void **) luaL_checkudata(L, narg, channel_lib);
}


static int
lbox_fiber_channel_gc(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		return 0;
	struct fiber_channel *ch = lbox_check_channel(L, -1);
	free(ch);
	return 0;
}


static int lbox_fiber_channel_isfull(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: channel:is_full()");
	struct fiber_channel *ch = lbox_check_channel(L, -1);
	if (fiber_channel_isfull(ch))
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}

static int lbox_fiber_channel_isempty(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, 1))
		luaL_error(L, "usage: channel:is_empty()");
	struct fiber_channel *ch = lbox_check_channel(L, -1);
	if (fiber_channel_isempty(ch))
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}

static int lbox_fiber_channel_put(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 2 || !lua_isuserdata(L, -2))
		luaL_error(L, "usage: channel:put(variable)");

	struct fiber_channel *ch = lbox_check_channel(L, -2);


	lua_getmetatable(L, -2);

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
	lua_settop(L, 2);

	lua_pushvalue(L, -1);


	fiber_channel_put(ch, (void *)rid);
	return 1;
}

static int lbox_fiber_channel_get(struct lua_State *L)
{
	say_info(":%s()", __func__);
	if (lua_gettop(L) != 1 || !lua_isuserdata(L, -1))
		luaL_error(L, "usage: channel:put(variable)");

	struct fiber_channel *ch = lbox_check_channel(L, -1);

	lua_getmetatable(L, -1);
	lua_Integer rid = (lua_Integer)fiber_channel_get(ch);
	lua_pushnumber(L, rid);
	lua_gettable(L, -2);

	lua_pushnumber(L, rid);
	lua_pushnil(L);
	lua_settable(L, -4);
	lua_remove(L, -2);

	return 1;
}

/******************** public functions ***************************/

int
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


int
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


int
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
	lua_pushnumber(L, 0);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);
	*ptr = ch;
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
		{"__call",	lbox_fiber_mutex_isfree},
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
		{NULL, NULL}
	};
	tarantool_lua_register_type(L, channel_lib, channel_meta);

}

