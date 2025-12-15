#include <lua.h>
#include <lauxlib.h>
#include <string.h>

static void spoil_cframe(void)
{
	/*
	 * We need to map CFRAME_SIZE + lua_call frame size.
	 * 1 Kb is totally enough with overhead.
	 */
	char a[1024];
	/*
	 * XXX: Need a value >= 0 on C stack to be interpreted as
	 * its own errfunc, not inherited. Just put the big
	 * address (0x7f7f7f7f).
	 */
	memset(a, 0x7f, sizeof(a));
}

static int cframe_func(lua_State *L)
{
	lua_gc(L, LUA_GCCOLLECT, 0);
	return 0;
}

static int call_cframe_func(lua_State *L)
{
	lua_pushcfunction(L, cframe_func);
	spoil_cframe();
	lua_call(L, 0, 0);
	return 0;
}

static int test_handle_err(lua_State *L)
{
	/*
	 * Not interested in the result, just want to be sure that
	 * unwinding in `finderrfunc()` works correctly.
	 */
	lua_cpcall(L, call_cframe_func, NULL);
	lua_pushboolean(L, 1);
	return 1;
}

static const struct luaL_Reg mixcframe[] = {
	{"test_handle_err", test_handle_err},
	{NULL, NULL}
};

LUA_API int luaopen_libmixcframe(lua_State *L)
{
	luaL_register(L, "mixcframe", mixcframe);
	return 1;
}

