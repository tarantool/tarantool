#include "lua.h"
#include "lauxlib.h"

static int allocate_userdata(lua_State *L)
{
	lua_newuserdata(L, 1);
	return 1;
}

static const struct luaL_Reg testoomframe[] = {
	{"allocate_userdata", allocate_userdata},
	{NULL, NULL}
};

LUA_API int luaopen_testoomframe(lua_State *L)
{
	luaL_register(L, "testoomframe", testoomframe);
	return 1;
}
