#include "lua.h"
#include "lauxlib.h"

/* Need for skipcond for OS and ARCH. */
#include "lj_arch.h"

#include "test.h"

#if LJ_HASSYSPROF && !defined(LUAJIT_USE_VALGRIND)
#include "utils.h"
#endif /* LJ_HASSYSPROF && !defined(LUAJIT_USE_VALGRIND) */

#include <signal.h>
#include <unistd.h>

/*
 * Check that there is no assertion failure during the dump of the
 * sample outside the VM.
 */
static int stream_trace_assert(void *test_state)
{
	lua_State *L = test_state;
	(void)luaL_dostring(L,
		"misc.sysprof.start({mode = 'C', path = '/dev/null'})");

	pid_t self_pid = getpid();
	/* Dump the single sample outside the VM. */
	kill(self_pid, SIGPROF);

	/* No assertion fail -- stop the profiler and exit. */
	(void)luaL_dostring(L, "misc.sysprof.stop()");
	return TEST_EXIT_SUCCESS;
}

int main(void)
{
#if LUAJIT_USE_VALGRIND
	UNUSED(stream_trace_assert);
	return skip_all("Disabled due to #10803");
#elif !LJ_HASSYSPROF
	UNUSED(stream_trace_assert);
	return skip_all("Sysprof is disabled");
#else /* LUAJIT_DISABLE_SYSPROF */
	if (LUAJIT_OS != LUAJIT_OS_LINUX)
		return skip_all("Sysprof is implemented for Linux only");
	if (LUAJIT_TARGET != LUAJIT_ARCH_X86
	    && LUAJIT_TARGET != LUAJIT_ARCH_X64)
		return skip_all("Sysprof is implemented for x86_64 only");

	lua_State *L = utils_lua_init();

	const struct test_unit tgroup[] = {
		test_unit_def(stream_trace_assert)
	};
	const int test_result = test_run_group(tgroup, L);
	utils_lua_close(L);
	return test_result;
#endif /* LUAJIT_DISABLE_SYSPROF */
}
