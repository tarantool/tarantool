#include <lua.h>
#include <module.h>

struct yield {
	int trigger;  /* Trigger for yielding the fiber execution */
};

void yield(struct yield *state, int i)
{
	if (i < state->trigger)
		return;

	/* Fiber yields the execution for a jiffy */
	fiber_sleep(0);
}

static int init(lua_State *L)
{
	struct yield *state = lua_newuserdata(L, sizeof(struct yield));

	state->trigger = lua_tonumber(L, 1);
	return 1;
}

LUA_API int luaopen_libyield(lua_State *L)
{
	lua_pushcfunction(L, init);
	return 1;
}
