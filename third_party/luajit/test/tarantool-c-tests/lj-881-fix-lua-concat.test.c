#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "test.h"
#include "utils.h"

/*
 * This test demonstrates LuaJIT's incorrect behaviour when
 * calling `lua_concat()` with userdata with the __concat metamethod.
 * See https://github.com/LuaJIT/LuaJIT/issues/881 for details.
 */

#define TYPE_NAME "int"

#define TEST_VALUE 100
#define TOSTR(s) #s
#define CONCAT(A, B) A TOSTR(B)

static int __concat(lua_State *L)
{
	const char *s = luaL_checkstring(L, 1);
	int *n = (int *)luaL_checkudata(L, 2, TYPE_NAME);
	/* Do non-default concatenation. */
	lua_pushfstring(L, "%s + %d", s, *n);
	return 1;
}

static const luaL_Reg mt[] = {
	{ "__concat", __concat },
	{ NULL, NULL}
};

static int lua_concat_testcase(void *test_state)
{
	/* Setup. */
	lua_State *L = test_state;
	const int top = 4;

	/* Create metatable and put it to the Lua registry. */
	luaL_newmetatable(L, TYPE_NAME);
	/* Fill metatable. */
	luaL_register(L, 0, mt);
	lua_pop(L, 1);

	assert_int_equal(lua_gettop(L), 0);

	lua_pushliteral(L, "C");
	lua_pushliteral(L, "B");
	lua_pushliteral(L, "A");

	int *n = (int *)lua_newuserdata(L, sizeof(*n));
	*n = 100;

	luaL_getmetatable(L, TYPE_NAME);
	lua_setmetatable(L, -2);

	assert_int_equal(lua_gettop(L), top);

	/* Test body. */

	/*
	 * void lua_concat (lua_State *L, int n);
	 *
	 * Concatenates the n values at the top of the stack,
	 * pops them, and leaves the result at the top. If n is 1,
	 * the result is the single value on the stack; if n is 0,
	 * the result is the empty string [1].
	 *
	 * 1. https://www.lua.org/manual/5.1/manual.html
	 */

	/* Concatenate two elements on the top. */
	lua_concat(L, 2);

	const char *str = lua_tostring(L, -1);
	assert_int_equal(lua_gettop(L), top - 2 + 1);
	const char expected_str[] = CONCAT("A + ", TEST_VALUE);
	assert_str_equal(str, expected_str);

	/* Teardown. */
	lua_settop(L, 0);

	return TEST_EXIT_SUCCESS;
}

int main(void)
{
	lua_State *L = utils_lua_init();
	const struct test_unit tgroup[] = {
		test_unit_def(lua_concat_testcase),
	};
	const int test_result = test_run_group(tgroup, L);
	utils_lua_close(L);

	return test_result;
}
