#include "lua.h"
#include "test.h"
#include "utils.h"

/*
 * This test demonstrates an uncleared Lua stack after the
 * initialization of the error handler for GC finalizers.
 * See https://github.com/luajit/luajit/issues/991 for
 * details.
 */

static int stack_is_clean(void *test_state)
{
	lua_State *L = test_state;
	assert_true(lua_gettop(L) == 0);
	return TEST_EXIT_SUCCESS;
}

int main(void)
{
	lua_State *L = utils_lua_init();

	const struct test_unit tgroup[] = {
		test_unit_def(stack_is_clean)
	};

	const int test_result = test_run_group(tgroup, L);
	utils_lua_close(L);
	return test_result;
}
