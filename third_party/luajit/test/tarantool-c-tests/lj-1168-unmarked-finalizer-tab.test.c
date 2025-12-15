#include "lua.h"
#include "lauxlib.h"

#include "test.h"

/*
 * This test demonstrates LuaJIT's incorrect behaviour on
 * loading Lua chunk with cdata numbers.
 * See https://github.com/LuaJIT/LuaJIT/issues/1168 for details.
 *
 * The GC is driving forward during parsing of the Lua chunk
 * (`test_chunk`). The chunk contains a single cdata object with
 * a number. That leads to the opening of the FFI library
 * on-demand during the parsing of this number. After the FFI
 * library is open, `ffi.gc` has the finalizer table as its
 * environment. But, there is no FFI module table anywhere to
 * anchor the `ffi.gc` itself, and the `lua_State` object is
 * marked after the function is removed from it. Hence, after the
 * atomic phase, the table is considered dead and collected. Since
 * the table is collected, the usage of its nodes in the
 * `lj_gc_finalize_cdata` leads to heap-use-after-free.
 */

const char buff[] = "return 1LL";

/*
 * lua_close is a part of testcase, so testcase creates
 * its own Lua state and closes it at the end.
 */
static int unmarked_finalizer_tab_gcstart(void *test_state)
{
	/* Shared Lua state is not needed. */
	UNUSED(test_state);

	/* Setup. */
	lua_State *L = luaL_newstate();

	/* Set GC at the start. */
	lua_gc(L, LUA_GCCOLLECT, 0);

	/* Not trigger GC during `lua_openffi()`. */
	lua_gc(L, LUA_GCSTOP, 0);

	/*
	 * The terminating '\0' is considered by parser as part of
	 * the input, so we must chomp it.
	 */
	int res = luaL_loadbufferx(L, buff, sizeof(buff) - 1,
				   "test_chunk", "t");
	if (res != LUA_OK) {
		test_comment("error loading Lua chunk: %s",
			     lua_tostring(L, -1));
		bail_out("error loading Lua chunk");
	}

	/* Finish GC cycle to collect the finalizer table. */
	while (!lua_gc(L, LUA_GCSTEP, -1));

	/* Teardown. */
	lua_settop(L, 0);
	lua_close(L);

	return TEST_EXIT_SUCCESS;
}

static int
unmarked_finalizer_tab_gcmark(void *test_state)
{
	/* Shared Lua state is not needed. */
	UNUSED(test_state);

	/* Setup. */
	lua_State *L = luaL_newstate();

	/* Set GC at the start. */
	lua_gc(L, LUA_GCCOLLECT, 0);

	/*
	 * Default step is too big -- one step ends after the
	 * atomic phase.
	 */
	lua_gc(L, LUA_GCSETSTEPMUL, 1);

	/* Skip marking roots. */
	lua_gc(L, LUA_GCSTEP, 1);

	/* Not trigger GC during `lua_openffi()`. */
	lua_gc(L, LUA_GCSTOP, 0);

	/*
	 * The terminating '\0' is considered by parser as part of
	 * the input, so we must chomp it.
	 */
	int res = luaL_loadbufferx(L, buff, sizeof(buff) - 1,
				   "test_chunk", "t");
	if (res != LUA_OK) {
		test_comment("error loading Lua chunk: %s",
			     lua_tostring(L, -1));
		bail_out("error loading Lua chunk");
	}

	/* Finish GC cycle to collect the finalizer table. */
	while (!lua_gc(L, LUA_GCSTEP, -1));

	/* Teardown. */
	lua_settop(L, 0);
	lua_close(L);

	return TEST_EXIT_SUCCESS;
}

int main(void)
{
	const struct test_unit tgroup[] = {
		test_unit_def(unmarked_finalizer_tab_gcstart),
		test_unit_def(unmarked_finalizer_tab_gcmark),
	};
	const int test_result = test_run_group(tgroup, NULL);

	return test_result;
}
