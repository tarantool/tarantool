#include "lua.h"
#include "lauxlib.h"

#include "test.h"
#include "utils.h"

/*
 * XXX: The "lj_obj.h" header is included to calculate the
 * number of stack slots used from the bottom of the stack.
 * XXX: The "lj_arch.h" header is included for the skipcond.
 */
#include "lj_arch.h"
#include "lj_obj.h"

static int cur_slots = -1;

static int fill_stack(lua_State *L)
{
	cur_slots = L->base - tvref(L->stack);

	while(lua_gettop(L) < LUAI_MAXSTACK) {
		cur_slots += 1;
		lua_pushinteger(L, 42);
	}

	return 0;
}

#if !LJ_NO_UNWIND
static int immediate_yield(lua_State *L)
{
	return lua_yield(L, 0);
}

static int overflow_suspended_coro(lua_State *L)
{
	lua_State *newL = lua_newthread(L);
	lua_pushcfunction(newL, immediate_yield);
	lua_resume(newL, 0);
	fill_stack(newL);
	return 0;
}
#endif

static int premature_stackoverflow(void *test_state)
{
	lua_State *L = test_state;
	lua_cpcall(L, fill_stack, NULL);
	assert_true(cur_slots == LUAI_MAXSTACK - 1);
	return TEST_EXIT_SUCCESS;
}

/*
 * XXX: This test should fail neither before the patch
 * nor after it.
 */
static int stackoverflow_during_stackoverflow(void *test_state)
{
	lua_State *L = test_state;
	/*
	 * XXX: `fill_stack` acts here as its own error handler,
	 * causing the second stack overflow.
	 */
	lua_pushcfunction(L, fill_stack);
	lua_pushcfunction(L, fill_stack);
	int status = lua_pcall(L, 0, 0, -2);
	assert_true(status == LUA_ERRERR);
	return TEST_EXIT_SUCCESS;
}

static int stackoverflow_on_suspended_coro(void *test_state)
{
#if LJ_NO_UNWIND
	UNUSED(test_state);
	return skip("Internal unwinding can't catch this exception");
#else
	lua_State *L = test_state;
	int status = lua_cpcall(L, overflow_suspended_coro, NULL);
	assert_true(status == LUA_ERRRUN);
	return TEST_EXIT_SUCCESS;
#endif
}

int main(void)
{
	lua_State *L = utils_lua_init();
	const struct test_unit tgroup[] = {
		test_unit_def(premature_stackoverflow),
		test_unit_def(stackoverflow_during_stackoverflow),
		test_unit_def(stackoverflow_on_suspended_coro),
	};
	const int test_result = test_run_group(tgroup, L);
	utils_lua_close(L);
	return test_result;
}
