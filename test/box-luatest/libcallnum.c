#include <lua.h>
#include <module.h>

static int
call_metamethod(lua_State *L)
{
	lua_pushnumber(L, 64);
	return 1;
}

/**
 * Require returns a callable number.
 */
LUA_API int
luaopen_libcallnum(lua_State *L)
{
	lua_pushnumber(L, 42);
	lua_createtable(L, 0, 1);
	lua_pushstring(L, "__call");
	lua_pushcfunction(L, call_metamethod);
	lua_settable(L, -3);
	lua_setmetatable(L, -2);
	return 1;
}
