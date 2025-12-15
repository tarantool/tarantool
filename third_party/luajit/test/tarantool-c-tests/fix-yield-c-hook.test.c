#include "lua.h"

#include "test.h"
#include "utils.h"

/*
 * This test demonstrates LuaJIT's incorrect behaviour, when
 * calling `lua_yield()` inside a C hook.
 * See https://www.freelists.org/post/luajit/BUG-Unable-to-yield-in-a-debug-hook-in-latest-21-beta
 * for details.
 */

static lua_State *main_L = NULL;

static void yield(lua_State *L, lua_Debug *ar)
{
	UNUSED(ar);
	/* Wait for the other coroutine and yield. */
	if (L != main_L)
		lua_yield(L, 0);
}

static int yield_in_c_hook(void *test_state)
{
	lua_State *L = test_state;
	utils_get_aux_lfunc(L);
	lua_sethook(L, yield, LUA_MASKLINE, 0);
	lua_call(L, 0, 0);
	/* Remove hook. */
	lua_sethook(L, yield, 0, 0);
	return TEST_EXIT_SUCCESS;
}

int main(void)
{
	lua_State *L = utils_lua_init();
	utils_load_aux_script(L, "fix-yield-c-hook-script.lua");
	main_L = L;

	const struct test_unit tgroup[] = {
		test_unit_def(yield_in_c_hook)
	};

	const int test_result = test_run_group(tgroup, L);
	utils_lua_close(L);
	return test_result;
}
