
/*
 * Test for determining luajit behavior on current platform.
 *
 * Luajit uses different stack unwinding mechanisms on x86/PPC
 * and x64 and a compilation flags.
 *
 * Using default mechanism under x86 (32-bit) can cause troubles
 * with raising and propogation exceptions from error handlers -
 * it starts recursively call panic error handler.
 *
*/

#include <cstdlib>
#include <lua.hpp>

static int panic = 0;

static int lua_panic_cb(lua_State *L) {
	if (!panic++)
		throw 0;
	abort();
	return 0;
}

int
main(int argc, char * argv[])
{
	lua_State *L = luaL_newstate();
	if (L == NULL)
		return 1;
	lua_atpanic(L, lua_panic_cb);
	try {
		lua_pushstring(L, "uncallable");
		lua_call(L, 0, LUA_MULTRET);
	} catch (...) {
		/* if we luck, we should get here. */
	}
	lua_close(L);
	return 0;
}
