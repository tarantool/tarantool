#include "lua.h"
#include "lauxlib.h"

#include "test.h"
#include "utils.h"

/* Need for skipcond for BSD and JIT. */
#include "lj_arch.h"

/* XXX: Still need normal assert for sanity checks. */
#undef NDEBUG
#include <assert.h>

/*
 * Test file to demonstrate a segmentation fault, when a C
 * function is used as a VM handler for trace events in LuaJIT:
 *
 * Program received signal SIGSEGV, Segmentation fault.
 * 0x000055555557e77d in trace_abort (J=0x7ffff7f9b6b8) at lj_trace.c:615
 * 615         lj_vmevent_send(L, TRACE,
 * (gdb) bt
 *
 * See details in https://github.com/LuaJIT/LuaJIT/issues/1087.
 */

static int nop(lua_State *L)
{
	UNUSED(L);
	return 0;
}

/* Note, `event == NULL` disables the corresponding handler. */
static void jit_attach(lua_State *L, lua_CFunction cb, const char *event)
{
	lua_getglobal(L, "jit");
	lua_getfield(L, -1, "attach");
	lua_pushcfunction(L, cb);
	if (event != NULL)
		lua_pushstring(L, event);
	else
		lua_pushnil(L);
	lua_pcall(L, 2, 0, 0);
}

static int handle_luafunc_frame(void *test_state)
{
	/* Setup. */
	lua_State *L = test_state;
	jit_attach(L, nop, "trace");

	/* Loading and executing of a broken Lua code. */
	int rc = luaL_dostring(L, "repeat until nil > 1");
	assert(rc == 1);

	/* The Lua chunk generates a Lua frame. */
	rc = luaL_dostring(L, "return function() end");
	assert(rc == 0);

	/* Teardown. */
	lua_settop(L, 0);

	return TEST_EXIT_SUCCESS;
}

static int cframe(lua_State *L)
{
	int rc = luaL_dostring(L, "repeat until nil > 1");
	assert(rc == 1);
	/* Remove errmsg. */
	lua_pop(L, 1);

	lua_pushcfunction(L, nop);
	lua_call(L, 0, 0);

	return 0;
}

static int handle_c_frame(void *test_state)
{
	/* Setup. */
	lua_State *L = test_state;
	jit_attach(L, nop, "trace");

	lua_pushcfunction(L, cframe);
	lua_call(L, 0, 0);

	/* Teardown. */
	lua_settop(L, 0);

	return TEST_EXIT_SUCCESS;
}

static int handle_cont_frame(void *test_state)
{
	const char lua_chunk[] =
		"local t = setmetatable({}, { __index = global_f })"
		"for i = 1, 4 do"
		"    _ = t[1]"
		"end";

	/* Setup. */
	lua_State *L = test_state;
	jit_attach(L, nop, "trace");

	/*
	 * The number 32767 is `REF_DROP - REF_BIAS`. This is the
	 * maximum possible IR amount, so the trace is always
	 * aborted.
	 */
	int res = luaL_dostring(L, "jit.opt.start('minstitch=32767')");
	assert(res == 0);
	lua_pushcfunction(L, nop);
	lua_setglobal(L, "global_f");

	res = luaL_dostring(L, lua_chunk);
	assert(res == 0);

	/* Teardown. */
	lua_settop(L, 0);
	res = luaL_dostring(L, "jit.opt.start('minstitch=0')");
	assert(res == 0);

	return TEST_EXIT_SUCCESS;
}

static int handle_bottom_frame(void *test_state)
{
	lua_State *L = test_state;

	/* Attach VM call handler. */
	jit_attach(L, nop, "trace");

	/* Load a Lua code that generate a trace abort. */
	int rc = luaL_dostring(L, "repeat until nil > 1");
	assert(rc == 1);

	/*
	 * Note, protected call is used inside `jit_attach()` to
	 * return to the VM on disabling the handler. Then
	 * a segmentation fault is triggered.
	 */
	jit_attach(L, nop, NULL);

	/* Teardown. */
	lua_settop(L, 0);

	return TEST_EXIT_SUCCESS;
}

int main(void)
{
	if (!LJ_HASJIT)
		return skip_all("JIT is disabled");

	if (LUAJIT_OS == LUAJIT_OS_BSD)
		return skip_all("Disabled on *BSD due to #4819");

	lua_State *L = utils_lua_init();
	const struct test_unit tgroup[] = {
		test_unit_def(handle_luafunc_frame),
		test_unit_def(handle_bottom_frame),
		test_unit_def(handle_cont_frame),
		test_unit_def(handle_c_frame),
	};
	int res = luaL_dostring(L, "jit.opt.start('hotloop=1')");
	assert(res == 0);
	const int test_result = test_run_group(tgroup, L);
	utils_lua_close(L);
	return test_result;
}
