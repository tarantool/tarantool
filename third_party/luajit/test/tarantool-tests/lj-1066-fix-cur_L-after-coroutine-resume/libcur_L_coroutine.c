#include "lua.h"
#include "lauxlib.h"

static int error_after_coroutine_return(lua_State *L)
{
	lua_State *innerL = lua_newthread(L);
	luaL_loadstring(innerL, "return");
	lua_pcall(innerL, 0, 0, 0);
	luaL_error(L, "my fancy error");
	return 0;
}

static const struct luaL_Reg libcur_L_coroutine[] = {
	{"error_after_coroutine_return", error_after_coroutine_return},
	{NULL, NULL}
};

LUA_API int luaopen_libcur_L_coroutine(lua_State *L)
{
	luaL_register(L, "libcur_L_coroutine", libcur_L_coroutine);
	return 1;
}
