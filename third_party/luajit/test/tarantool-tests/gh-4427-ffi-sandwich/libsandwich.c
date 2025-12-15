#include <lua.h>
#include <lauxlib.h>

struct sandwich {
	lua_State *L; /* Coroutine saved for a Lua call */
	int ref;      /* Anchor to the Lua function to be run */
	int trigger;  /* Trigger for switching to Lua call */
};

int increment(struct sandwich *state, int i)
{
	if (i < state->trigger)
		return i + 1;

	/* Sandwich is triggered and Lua increment function is called */
	lua_pushnumber(state->L, state->ref);
	lua_gettable(state->L, LUA_REGISTRYINDEX);
	lua_pushnumber(state->L, i);
	lua_call(state->L, 1, 1);
	return lua_tonumber(state->L, -1);
}

#define STRUCT_SANDWICH_MT "struct sandwich"

static int init(lua_State *L)
{
	struct sandwich *state = lua_newuserdata(L, sizeof(struct sandwich));

	luaL_getmetatable(L, STRUCT_SANDWICH_MT);
	lua_setmetatable(L, -2);

	/* Lua increment function to be called when sandwich is triggered */
	if (luaL_dostring(L, "return function(i) return i + 1 end"))
		luaL_error(L, "failed to translate Lua increment function");

	state->ref = luaL_ref(L, LUA_REGISTRYINDEX);
	state->L = L;
	state->trigger = lua_tonumber(L, 1);
	return 1;
}

static int fin(lua_State *L)
{
	struct sandwich *state = luaL_checkudata(L, 1, STRUCT_SANDWICH_MT);

	/* Release the anchored increment function */
	luaL_unref(L, LUA_REGISTRYINDEX, state->ref);
	return 0;
}

LUA_API int luaopen_libsandwich(lua_State *L)
{
	luaL_newmetatable(L, STRUCT_SANDWICH_MT);
	lua_pushcfunction(L, fin);
	lua_setfield(L, -2, "__gc");

	lua_pushcfunction(L, init);
	return 1;
}
