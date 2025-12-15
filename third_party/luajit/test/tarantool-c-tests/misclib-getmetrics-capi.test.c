#include "lua.h"
#include "luajit.h"
#include "lauxlib.h"
#include "lmisclib.h"

#include "test.h"
#include "utils.h"

/* Need for skipcond for BSD and JIT. */
#include "lj_arch.h"

static int base(void *test_state)
{
	lua_State *L = test_state;
	struct luam_Metrics metrics;
	luaM_metrics(L, &metrics);

	/*
	 * Just check structure format, not values that fields
	 * contain.
	 */
	(void)metrics.strhash_hit;
	(void)metrics.strhash_miss;

	(void)metrics.gc_strnum;
	(void)metrics.gc_tabnum;
	(void)metrics.gc_udatanum;
	(void)metrics.gc_cdatanum;

	(void)metrics.gc_total;
	(void)metrics.gc_freed;
	(void)metrics.gc_allocated;

	(void)metrics.gc_steps_pause;
	(void)metrics.gc_steps_propagate;
	(void)metrics.gc_steps_atomic;
	(void)metrics.gc_steps_sweepstring;
	(void)metrics.gc_steps_sweep;
	(void)metrics.gc_steps_finalize;

	(void)metrics.jit_snap_restore;
	(void)metrics.jit_trace_abort;
	(void)metrics.jit_mcode_size;
	(void)metrics.jit_trace_num;

	return TEST_EXIT_SUCCESS;
}

static int gc_allocated_freed(void *test_state)
{
	lua_State *L = test_state;
	struct luam_Metrics oldm, newm;
	/* Force up garbage collect all dead objects. */
	lua_gc(L, LUA_GCCOLLECT, 0);

	luaM_metrics(L, &oldm);
	/* Simple garbage generation. */
	if (luaL_dostring(L, "local i = 0 for j = 1, 10 do i = i + j end"))
		bail_out("failed to translate Lua code snippet");
	lua_gc(L, LUA_GCCOLLECT, 0);
	luaM_metrics(L, &newm);
	assert_true(newm.gc_allocated - oldm.gc_allocated > 0);
	assert_true(newm.gc_freed - oldm.gc_freed > 0);

	return TEST_EXIT_SUCCESS;
}

static int gc_steps(void *test_state)
{
	lua_State *L = test_state;
	struct luam_Metrics oldm, newm;
	/*
	 * Some garbage has already happened before the next line,
	 * i.e. during frontend processing Lua test chunk.
	 * Let's put a full garbage collection cycle on top
	 * of that, and confirm that non-null values are reported
	 * (we are not yet interested in actual numbers):
	 */
	lua_gc(L, LUA_GCCOLLECT, 0);

	luaM_metrics(L, &oldm);
	assert_true(oldm.gc_steps_pause > 0);
	assert_true(oldm.gc_steps_propagate > 0);
	assert_true(oldm.gc_steps_atomic > 0);
	assert_true(oldm.gc_steps_sweepstring > 0);
	assert_true(oldm.gc_steps_sweep > 0);
	/* Nothing to finalize, skipped. */
	assert_true(oldm.gc_steps_finalize == 0);

	/*
	 * As long as we don't create new Lua objects
	 * consequent call should return the same values:
	 */
	luaM_metrics(L, &newm);
	assert_sizet_equal(newm.gc_steps_pause, oldm.gc_steps_pause);
	assert_sizet_equal(newm.gc_steps_propagate, oldm.gc_steps_propagate);
	assert_sizet_equal(newm.gc_steps_atomic, oldm.gc_steps_atomic);
	assert_sizet_equal(newm.gc_steps_sweepstring,
			   oldm.gc_steps_sweepstring);
	assert_sizet_equal(newm.gc_steps_sweep, oldm.gc_steps_sweep);
	/* Nothing to finalize, skipped. */
	assert_true(newm.gc_steps_finalize == 0);
	oldm = newm;

	/*
	 * Now the last phase: run full GC once and make sure that
	 * everything is being reported as expected:
	 */
	lua_gc(L, LUA_GCCOLLECT, 0);
	luaM_metrics(L, &newm);
	assert_true(newm.gc_steps_pause - oldm.gc_steps_pause == 1);
	assert_true(newm.gc_steps_propagate - oldm.gc_steps_propagate >= 1);
	assert_true(newm.gc_steps_atomic - oldm.gc_steps_atomic == 1);
	assert_true(newm.gc_steps_sweepstring - oldm.gc_steps_sweepstring >= 1);
	assert_true(newm.gc_steps_sweep - oldm.gc_steps_sweep >= 1);
	/* Nothing to finalize, skipped. */
	assert_true(newm.gc_steps_finalize == 0);
	oldm = newm;

	/*
	 * Now let's run three GC cycles to ensure that
	 * increment was not a lucky coincidence.
	 */
	lua_gc(L, LUA_GCCOLLECT, 0);
	lua_gc(L, LUA_GCCOLLECT, 0);
	lua_gc(L, LUA_GCCOLLECT, 0);
	luaM_metrics(L, &newm);
	assert_true(newm.gc_steps_pause - oldm.gc_steps_pause == 3);
	assert_true(newm.gc_steps_propagate - oldm.gc_steps_propagate >= 3);
	assert_true(newm.gc_steps_atomic - oldm.gc_steps_atomic == 3);
	assert_true(newm.gc_steps_sweepstring - oldm.gc_steps_sweepstring >= 3);
	assert_true(newm.gc_steps_sweep - oldm.gc_steps_sweep >= 3);
	/* Nothing to finalize, skipped. */
	assert_true(newm.gc_steps_finalize == 0);

	return TEST_EXIT_SUCCESS;
}

static int objcount(void *test_state)
{
	lua_State *L = test_state;
	struct luam_Metrics oldm, newm;
	if (!LJ_HASJIT)
		return skip("Test requires JIT enabled");

	utils_get_aux_lfunc(L);

	/* Force up garbage collect all dead objects. */
	lua_gc(L, LUA_GCCOLLECT, 0);

	luaM_metrics(L, &oldm);
	/* Generate garbage. Argument is iterations amount. */
	lua_pushnumber(L, 1000);
	lua_call(L, 1, 0);
	lua_gc(L, LUA_GCCOLLECT, 0);
	luaM_metrics(L, &newm);
	assert_sizet_equal(newm.gc_strnum, oldm.gc_strnum);
	assert_sizet_equal(newm.gc_tabnum, oldm.gc_tabnum);
	assert_sizet_equal(newm.gc_udatanum, oldm.gc_udatanum);
	assert_sizet_equal(newm.gc_cdatanum, oldm.gc_cdatanum);

	return TEST_EXIT_SUCCESS;
}

static int objcount_cdata_decrement(void *test_state)
{
	lua_State *L = test_state;
	/*
	 * cdata decrement test.
	 * See https://github.com/tarantool/tarantool/issues/5820.
	 */
	struct luam_Metrics oldm, newm;
	utils_get_aux_lfunc(L);

	/* Force up garbage collect all dead objects. */
	lua_gc(L, LUA_GCCOLLECT, 0);

	luaM_metrics(L, &oldm);
	/*
	 * The function generates and collects cdata with
	 * LJ_GC_CDATA_FIN flag.
	 */
	lua_call(L, 0, 0);
	luaM_metrics(L, &newm);
	assert_sizet_equal(newm.gc_cdatanum, oldm.gc_cdatanum);

	return TEST_EXIT_SUCCESS;
}

/*
 * Get function to call to generate the corresponding snapshot
 * restores on top of the Lua stack. Function returns the amount
 * of snapshot restorations expected.
 * Clear stack after call.
 */
static void check_snap_restores(lua_State *L)
{
	struct luam_Metrics oldm, newm;
	luaM_metrics(L, &oldm);
	/* Generate snapshots. */
	lua_call(L, 0, 1);
	int n = lua_gettop(L);
	/*
	 * The first value is the table with functions,
	 * the second is number of snapshot restores.
	 */
	if (n != 2 || !lua_isnumber(L, -1))
		bail_out("incorrect return value: 1 number is required");
	size_t snap_restores = lua_tonumber(L, -1);
	luaM_metrics(L, &newm);
	/*
	 * Remove `snap_restores` from stack.
	 * Must be done before potential assert and exit from
	 * the test.
	 */
	lua_pop(L, 1);
	assert_true(newm.jit_snap_restore - oldm.jit_snap_restore
		    == snap_restores);
}

static int snap_restores_direct_exit(void *test_state)
{
	lua_State *L = test_state;
	utils_get_aux_lfunc(L);
	check_snap_restores(L);
	return TEST_EXIT_SUCCESS;
}

static int snap_restores_direct_exit_scalar(void *test_state)
{
	lua_State *L = test_state;
	utils_get_aux_lfunc(L);
	check_snap_restores(L);
	return TEST_EXIT_SUCCESS;
}

static int snap_restores_side_exit_compiled(void *test_state)
{
	lua_State *L = test_state;
	utils_get_aux_lfunc(L);
	check_snap_restores(L);
	return TEST_EXIT_SUCCESS;
}

static int snap_restores_side_exit_not_compiled(void *test_state)
{
	lua_State *L = test_state;
	utils_get_aux_lfunc(L);
	check_snap_restores(L);
	return TEST_EXIT_SUCCESS;
}

static int snap_restores_group(void *test_state)
{
	if (!LJ_HASJIT)
		return skip("Test requires JIT enabled");
	const struct test_unit tgroup[] = {
		test_unit_def(snap_restores_direct_exit),
		test_unit_def(snap_restores_direct_exit_scalar),
		test_unit_def(snap_restores_side_exit_compiled),
		test_unit_def(snap_restores_side_exit_not_compiled)
	};
	return test_run_group(tgroup, test_state);
}

static int strhash(void *test_state)
{
	lua_State *L = test_state;
	struct luam_Metrics oldm, newm;
	lua_pushstring(L, "strhash_hit");
	luaM_metrics(L, &oldm);
	lua_pushstring(L, "strhash_hit");
	lua_pushstring(L, "new_str");
	luaM_metrics(L, &newm);
	/* Remove pushed strings. */
	lua_pop(L, 3);
	assert_true(newm.strhash_hit - oldm.strhash_hit == 1);
	assert_true(newm.strhash_miss - oldm.strhash_miss == 1);
	return TEST_EXIT_SUCCESS;
}

static int tracenum_base(void *test_state)
{
	lua_State *L = test_state;
	if (!LJ_HASJIT)
		return skip("Test requires JIT enabled");
	struct luam_Metrics metrics;
	utils_get_aux_lfunc(L);

	luaJIT_setmode(L, 0, LUAJIT_MODE_FLUSH);
	/* Force up garbage collect all dead objects. */
	lua_gc(L, LUA_GCCOLLECT, 0);

	luaM_metrics(L, &metrics);
	assert_true(metrics.jit_trace_num == 0);

	/* Generate traces. */
	lua_call(L, 0, 1);
	int n = lua_gettop(L);
	/*
	 * The first value is the table with functions,
	 * the second is the amount of traces.
	 */
	if (n != 2 || !lua_isnumber(L, -1))
		bail_out("incorrect return value: 1 number is required");
	size_t jit_trace_num = lua_tonumber(L, -1);
	luaM_metrics(L, &metrics);
	/* Remove `jit_trace_num` from Lua stack. */
	lua_pop(L, 1);

	assert_sizet_equal(metrics.jit_trace_num, jit_trace_num);

	luaJIT_setmode(L, 0, LUAJIT_MODE_FLUSH);
	/* Force up garbage collect all dead objects. */
	lua_gc(L, LUA_GCCOLLECT, 0);
	luaM_metrics(L, &metrics);
	assert_true(metrics.jit_trace_num == 0);

	return TEST_EXIT_SUCCESS;
}

int main(void)
{
	if (LUAJIT_OS == LUAJIT_OS_BSD)
		return skip_all("Disabled on *BSD due to #4819");

	lua_State *L = utils_lua_init();

	utils_load_aux_script(L, "misclib-getmetrics-capi-script.lua");
	const struct test_unit tgroup[] = {
		test_unit_def(base),
		test_unit_def(gc_allocated_freed),
		test_unit_def(gc_steps),
		test_unit_def(objcount),
		test_unit_def(objcount_cdata_decrement),
		test_unit_def(snap_restores_group),
		test_unit_def(strhash),
		test_unit_def(tracenum_base)
	};
	const int test_result = test_run_group(tgroup, L);
	utils_lua_close(L);
	return test_result;
}
