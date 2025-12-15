#include <lua.h>
#include <lauxlib.h>

/*
 * Function with the signature similar to Lua <pcall> builtin,
 * that routes the flow through C frame.
 */
static int proxycall(lua_State *L)
{
	lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
	return lua_gettop(L);
}

static const struct luaL_Reg libproxy[] = {
	{"proxycall", proxycall},
	{NULL, NULL}
};

LUA_API int luaopen_libproxy(lua_State *L)
{
	luaL_register(L, "libproxy", libproxy);
	return 1;
}
