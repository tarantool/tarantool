#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int main()
{
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	@try {
		luaL_error(L, "test");
#if defined(__clang__)
	} @catch (...) {
#else /* !defined(__clang__) */
	} @catch (id allOthers) {
#endif
		printf("exception handled\n");
	}
	lua_close(L);

	return 0;
}
