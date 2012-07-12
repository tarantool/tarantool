#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int main()
{
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	@try {
		luaL_error(L, "test");
	} @catch (...) {
		printf("exception handled\n");
	}
	lua_close(L);
}
