#include <lua.h>
#include <lauxlib.h>

#undef NDEBUG
#include <assert.h>

static lua_State *old_L = NULL;

int throw_error_at_old_thread(lua_State *cur_L)
{
	lua_error(old_L);
	/* Unreachable. */
	return 0;
}

static int error_from_other_thread(lua_State *L)
{
	lua_State *next_cur_L = lua_newthread(L);
	old_L = L;
	/* Remove thread. */
	lua_pop(L, 1);
	/* Do not show frame slot as return result after error. */
	lua_pushnil(L);
	lua_pushcfunction(next_cur_L, throw_error_at_old_thread);
	lua_call(next_cur_L, 0, 0);
	/* Unreachable. */
	assert(0);
	return 0;
}

static const struct luaL_Reg libcur_L[] = {
	{"error_from_other_thread", error_from_other_thread},
	{NULL, NULL}
};

LUA_API int luaopen_libcur_L(lua_State *L)
{
	luaL_register(L, "libcur_L", libcur_L);
	return 1;
}
