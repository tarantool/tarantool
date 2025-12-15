#ifndef TARANTOOL_LUAJIT_TEST_UTILS_H
#define TARANTOOL_LUAJIT_TEST_UTILS_H

#include <limits.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "luajit.h"
#include "lualib.h"

#include "test.h"

#define UTILS_UNUSED __attribute__((unused))

/* Generic init for our tests. */
static lua_State *utils_lua_init(void)
{
	lua_State *L = luaL_newstate();
	if (!L)
		bail_out("Can't init Lua state");
	/*
	 * Don't really need to waste time on the GC during
	 * library initialization, so stop the collector.
	 * Same approach as in `pmain()` in <src/luajit.c>.
	 */
	lua_gc(L, LUA_GCSTOP, 0);
	luaL_openlibs(L);
	lua_gc(L, LUA_GCRESTART, -1);
	return L;
}

/* Generic close for our tests. */
static void utils_lua_close(lua_State *L)
{
	lua_close(L);
}

/*
 * Load the Lua <file> -- the pair to the C test file.
 * Each file should return the table with functions (named the
 * same as the corresponding unit tests) to call in unit tests.
 * Script file name is given as the second argument.
 * Push the table with those functions on the Lua stack.
 */
UTILS_UNUSED static void utils_load_aux_script(lua_State *L, const char *file)
{
	/*
	 * Format script name.
	 * `__LJ_TEST_DIR__` is set via CMake.
	 */
	char script[PATH_MAX] = __LJ_TEST_DIR__;
	char *script_name = script + sizeof(__LJ_TEST_DIR__) - 1;
	/* Replace '\0' with '/'. */
	*script_name++ = '/';
	/* Append script filename. */
	strcpy(script_name, file);

	if (luaL_dofile(L, script) != LUA_OK) {
		test_comment("Can't load %s: '%s'", script,
			     lua_tostring(L, -1));
		bail_out("Can't load auxiliary script");
	}

	if (!lua_istable(L, -1))
		bail_out("Returned value from script is not a table");
}

/*
 * Accept a table on top of the Lua stack which containing the
 * function named as the unit test we currently executing.
 */
#define utils_get_aux_lfunc(L) do {					\
	lua_getfield((L), -1, __func__);				\
	if (!lua_isfunction((L), -1))					\
		bail_out("Can't get auxiliary test function");		\
} while (0)

#endif /* TARANTOOL_LUAJIT_TEST_UTILS_H */
