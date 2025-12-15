#include "lauxlib.h"
#include "lmisclib.h"
#include "lua.h"

#include "test.h"
#include "utils.h"

/* XXX: Still need normal assert inside <tracee> and helpers. */
#undef NDEBUG
#include <assert.h>

/*
 * XXX: The test is *very* Linux/x86_64 specific. Fortunately, so
 * does the sampling profiler. <lj_arch.h> is needed for LUAJIT_OS
 * and LUAJIT_TARGET.
 */
#include "lj_arch.h"

#if LUAJIT_OS == LUAJIT_OS_LINUX && LUAJIT_TARGET == LUAJIT_ARCH_X64

#include <signal.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * XXX: The test makes sysprof collect the particular event
 * (FFUNC) at the particular instruction (<lj_fff_res1>) to
 * reproduce the issue #8594. Hence, it's enough to call
 * <tostring> fast function (this is done in <tracee> function).
 * To emit SIGPROF right at <lj_fff_res1> in scope of <tostring>
 * fast function, the managed execution is implemented in the
 * function <tracer>: <int 3> instruction is poisoned as the first
 * instruction at <lj_ff_tostring> to stop <tracee> at the
 * beginning of the fast function; <tracer> resumes the <tracee>;
 * the same hack is done for <lj_fff_res1>. When the <tracee> hits
 * the interruption at <lj_fff_res1>, SIGPROF is emitted while
 * resuming the <tracee>. As a result, sysprof collects the full
 * backtrace with <tostring> fast function as the topmost frame.
 *
 * See more info here:
 * * https://man7.org/linux/man-pages/man2/ptrace.2.html
 * * https://c9x.me/x86/html/file_module_x86_id_142.html
 * * https://github.com/tarantool/tarantool/issues/8594
 * * https://github.com/tarantool/tarantool/issues/9387
 */

#define MESSAGE "Canary is alive"
#define LUACALL "local a = tostring('" MESSAGE "') return a"

/* XXX: Resolve the necessary addresses from VM engine. */
extern void *lj_ff_tostring(void);
extern void *lj_fff_res1(void);

/* Sysprof dummy stream helpers. {{{ */

/*
 * Yep, 8Mb. Tuned in order not to bother the platform with too
 * often flushes.
 */
#define STREAM_BUFFER_SIZE (8 * 1024 * 1024)

struct dummy_ctx {
	/* Buffer for data recorded by sysprof. */
	uint8_t buf[STREAM_BUFFER_SIZE];
};

static struct dummy_ctx context;

static int stream_new(struct luam_Sysprof_Options *options)
{
	/* Set dummy context. */
	options->ctx = &context;
	options->buf = (uint8_t *)&context.buf;
	options->len = STREAM_BUFFER_SIZE;
	return PROFILE_SUCCESS;
}

static int stream_delete(void *rawctx, uint8_t *buf)
{
	assert(rawctx == &context);
	/* XXX: No need to release context memory. Just return. */
	return PROFILE_SUCCESS;
}

static size_t stream_writer(const void **buf_addr, size_t len, void *rawctx)
{
	assert(rawctx == &context);
	/* Do nothing, just return back to the profiler. */
	return STREAM_BUFFER_SIZE;
}

/* }}} Sysprof dummy stream helpers. */

static int tracee(const char *luacode)
{
	struct luam_Sysprof_Counters counters = {};
	struct luam_Sysprof_Options opt = {
		/* Collect full backtraces per event. */
		.mode = LUAM_SYSPROF_CALLGRAPH,
		/*
		 * XXX: Setting the "endless timer". The test
		 * requires the single event to be streamed at
		 * <lj_fff_res1> instruction, so to avoid spoiling
		 * the stream with other unwanted events, the
		 * timer is set to some unreachable point, so the
		 * profiler will be guaranteed to stop before any
		 * event is emitted.
		 */
		.interval = -1ULL,
	};

	/* Allow tracing for this process. */
	if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
		perror("Failed to turn the calling process into a tracee");
		return EXIT_FAILURE;
	}

	/*
	 * XXX: Allow parent (which is our tracer now) to observe
	 * our signal-delivery-stop (i.e. the tracee is ready).
	 * For more info see ptrace(2), "Attaching and detaching".
	 */
	raise(SIGSTOP);

	lua_State *L = utils_lua_init();

	/* Customize and start profiler. */
	assert(stream_new(&opt) == PROFILE_SUCCESS);
	assert(luaM_sysprof_set_writer(stream_writer) == PROFILE_SUCCESS);
	assert(luaM_sysprof_set_on_stop(stream_delete) == PROFILE_SUCCESS);
	assert(luaM_sysprof_start(L, &opt) == PROFILE_SUCCESS);

	/* TODO: Make this part test-agnostic. */
	assert(luaL_dostring(L, luacode) == LUA_OK);
	assert(strcmp(lua_tostring(L, -1), MESSAGE) == 0);

	/* Terminate profiler. */
	assert(luaM_sysprof_stop(L) == PROFILE_SUCCESS);

	/*
	 * XXX: The only event to be streamed must be FFUNC at
	 * the <lj_fff_res1> instruction.
	 * TODO: Make this part test-agnostic.
	 */
	assert(luaM_sysprof_report(&counters) == PROFILE_SUCCESS);
	assert(counters.samples == 1);
	assert(counters.vmst_ffunc == 1);

	/* Terminate Lua universe. */
	utils_lua_close(L);

	return EXIT_SUCCESS;
}

static void wait_alive(pid_t chpid)
{
	int wstatus;

	/* Wait <chpid> tracee signal-delivery-stop. */
	waitpid(chpid, &wstatus, 0);

	/* Check the tracee is still alive and just stopped. */
	assert(!WIFEXITED(wstatus));
	assert(!WIFSIGNALED(wstatus));
	assert(WIFSTOPPED(wstatus));
}

const uint8_t INT3 = 0xCC;
static inline unsigned long int3poison(unsigned long instruction)
{
	const size_t int3bits = sizeof(INT3) * 8;
	const unsigned long int3mask = -1UL >> int3bits << int3bits;
	return (instruction & int3mask) | INT3;
}

static void continue_until(pid_t chpid, void *addr)
{
	struct user_regs_struct regs;

	/* Obtain the instructions at the <addr>. */
	unsigned long data = ptrace(PTRACE_PEEKTEXT, chpid, addr, NULL);
	/*
	 * Emit the <int 3> instruction to the <addr>.
	 * XXX: <int 3> is poisoned as the LSB to the <data>
	 * obtained from the <addr> above.
	 */
	ptrace(PTRACE_POKETEXT, chpid, addr, int3poison(data));

	/* Resume <chpid> tracee until SIGTRAP occurs. */
	ptrace(PTRACE_CONT, chpid, NULL, NULL);

	/*
	 * Wait <chpid> tracee signal-delivery-stop and check
	 * whether it's still alive and just stopped via SIGTRAP.
	 */
	wait_alive(chpid);

	/* Obtain GPR set to tweak RIP for further execution. */
	ptrace(PTRACE_GETREGS, chpid, NULL, &regs);
	/*
	 * Make sure we indeed are stopped at <addr>.
	 * XXX: RIP points right after <int 3> instruction.
	 */
	assert(regs.rip == (long)addr + sizeof(INT3));

	/*
	 * XXX: Restore the original instruction at <addr> and
	 * "rewind" RIP by <int 3> size to "replay" the poisoned
	 * instruction at the <addr>.
	 */
	regs.rip -= sizeof(INT3);
	ptrace(PTRACE_SETREGS, chpid, NULL, &regs);
	ptrace(PTRACE_POKETEXT, chpid, addr, data);
}

static int tracer(pid_t chpid)
{
	int wstatus;

	/* Wait until <chpid> tracee is ready. */
	wait_alive(chpid);

	/*
	 * Resume <chpid> tracee until <lj_ff_tostring>.
	 * The tracee is alive and stopped by SIGTRAP if
	 * <continue_until> returns.
	 */
	continue_until(chpid, lj_ff_tostring);

	/*
	 * Resume <chpid> tracee until <lj_fff_res1>.
	 * The tracee is alive and stopped by SIGTRAP if
	 * <continue_until> returns.
	 */
	continue_until(chpid, lj_fff_res1);

	/* Send SIGPROF to make sysprof collect the event. */
	ptrace(PTRACE_CONT, chpid, 0, SIGPROF);

	/* Wait until <chpid> tracee successfully exits. */
	waitpid(chpid, &wstatus, 0);
	assert_true(WIFEXITED(wstatus));

	return TEST_EXIT_SUCCESS;
}

static int test_tostring_call(void *ctx)
{
#if LUAJIT_USE_VALGRIND
	UNUSED(ctx);
	UNUSED(tracer);
	UNUSED(tracee);
	return skip("Disabled with Valgrind (Timeout)");
#else
	pid_t chpid = fork();
	switch(chpid) {
	case -1:
		bail_out("Tracee fork failed");
	case 0:
		/*
		 * XXX: Tracee has to <exit> instead of <return>
		 * to avoid duplicate reports in <test_run_group>.
		 * Test assertions are used only in the <tracer>,
		 * so the <tracer> ought to report whether the
		 * test succeeded or not.
		 */
		exit(tracee(LUACALL));
	default:
		return tracer(chpid);
	}
#endif
}

#else /* LUAJIT_OS == LUAJIT_OS_LINUX && LUAJIT_TARGET == LUAJIT_ARCH_X64 */

static int test_tostring_call(void *ctx)
{
	return skip("sysprof is implemented for Linux/x86_64 only");
}

#endif /* LUAJIT_OS == LUAJIT_OS_LINUX && LUAJIT_TARGET == LUAJIT_ARCH_X64 */

int main(void)
{
#if LUAJIT_DISABLE_SYSPROF
	return skip_all("Sysprof is disabled");
#else /* LUAJIT_DISABLE_SYSPROF */
	const struct test_unit tgroup[] = {
		test_unit_def(test_tostring_call),
	};
	return test_run_group(tgroup, NULL);
#endif /* LUAJIT_DISABLE_SYSPROF */
}
