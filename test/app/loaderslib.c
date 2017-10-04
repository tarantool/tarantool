#include <lua.h>

LUA_API int
luaopen_loaderslib(lua_State *L)
{
	lua_pushliteral(L, "success");
	return 1;
}
