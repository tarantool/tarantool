#include "lauxlib.h"
#include "lmisclib.h"
#include "lua.h"
#include "luajit.h"

#include "test.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

/* XXX: Still need normal assert inside writer functions. */
#undef NDEBUG
#include <assert.h>

/* Need for skipcond for OS and ARCH. */
#include "lj_arch.h"

/* --- utils -------------------------------------------------- */

#define SYSPROF_INTERVAL_DEFAULT 100

/*
 * Yep, 8Mb. Tuned in order not to bother the platform with too
 * often flushes.
 */
#define STREAM_BUFFER_SIZE (8 * 1024 * 1024)


/* --- C Payload ---------------------------------------------- */

static double fib(double n)
{
	if (n <= 1)
		return n;
	return fib(n - 1) + fib(n - 2);
}

static int c_payload(lua_State *L)
{
	fib(luaL_checknumber(L, 1));
	lua_pushboolean(L, 1);
	return 1;
}
/* --- sysprof C API tests ------------------------------------ */

static int base(void *test_state)
{
	UNUSED(test_state);
	struct luam_Sysprof_Options opt = {};
	struct luam_Sysprof_Counters cnt = {};

	(void)opt.interval;
	(void)opt.mode;
	(void)opt.ctx;
	(void)opt.buf;
	(void)opt.len;

	luaM_sysprof_report(&cnt);

	(void)cnt.samples;
	(void)cnt.vmst_interp;
	(void)cnt.vmst_lfunc;
	(void)cnt.vmst_ffunc;
	(void)cnt.vmst_cfunc;
	(void)cnt.vmst_gc;
	(void)cnt.vmst_exit;
	(void)cnt.vmst_record;
	(void)cnt.vmst_opt;
	(void)cnt.vmst_asm;
	(void)cnt.vmst_trace;

	return TEST_EXIT_SUCCESS;
}

static int validation(void *test_state)
{
	lua_State *L = test_state;
	struct luam_Sysprof_Options opt = {};
	int status = PROFILE_SUCCESS;

	/* Unknown mode. */
	opt.mode = 0x40;
	status = luaM_sysprof_start(L, &opt);
	assert_true(status == PROFILE_ERRUSE);

	/* Buffer not configured. */
	opt.mode = LUAM_SYSPROF_CALLGRAPH;
	opt.buf = NULL;
	status = luaM_sysprof_start(L, &opt);
	assert_true(status == PROFILE_ERRUSE);

	/* Bad interval. */
	opt.mode = LUAM_SYSPROF_DEFAULT;
	opt.interval = 0;
	status = luaM_sysprof_start(L, &opt);
	assert_true(status == PROFILE_ERRUSE);

	/* Check if profiling started. */
	opt.mode = LUAM_SYSPROF_DEFAULT;
	opt.interval = SYSPROF_INTERVAL_DEFAULT;
	status = luaM_sysprof_start(L, &opt);
	assert_true(status == PROFILE_SUCCESS);

	/* Already running. */
	status = luaM_sysprof_start(L, &opt);
	assert_true(status == PROFILE_ERRRUN);

	/* Profiler stopping. */
	status = luaM_sysprof_stop(L);
	assert_true(status == PROFILE_SUCCESS);

	/* Stopping profiler which is not running. */
	status = luaM_sysprof_stop(L);
	assert_true(status == PROFILE_ERRRUN);

	return TEST_EXIT_SUCCESS;
}

/*
 * FIXME: The following two tests are disabled because sometimes
 * `backtrace` dynamically loads a platform-specific unwinder,
 * which is not signal-safe.
 */

#if 0
/*
 * Structure given as ctx to sysprof writer and on_stop callback.
 */
struct sysprof_ctx {
	/* Output file descriptor for data. */
	int fd;
	/* Buffer for data. */
	uint8_t buf[STREAM_BUFFER_SIZE];
};

/*
 * Default buffer writer function.
 * Just call fwrite to the corresponding FILE.
 */
static size_t buffer_writer_default(const void **buf_addr, size_t len,
				    void *opt)
{
	struct sysprof_ctx *ctx = opt;
	int fd = ctx->fd;
	const void * const buf_start = *buf_addr;
	const void *data = *buf_addr;
	size_t write_total = 0;

	assert(len <= STREAM_BUFFER_SIZE);

	for (;;) {
		const size_t written = write(fd, data, len - write_total);

		if (written == 0) {
			/* Re-tries write in case of EINTR. */
			if (errno != EINTR) {
				/*
				 * Will be freed as whole chunk
				 * later.
				 */
				*buf_addr = NULL;
				return write_total;
			}
			errno = 0;
			continue;
		}

		write_total += written;
		assert(write_total <= len);

		if (write_total == len)
			break;

		data = (uint8_t *)data + (ptrdiff_t)written;
	}

	*buf_addr = buf_start;
	return write_total;
}

/*
 * Default on stop callback. Just close the corresponding stream.
 */
static int on_stop_cb_default(void *opt, uint8_t *buf)
{
	UNUSED(buf);
	struct sysprof_ctx *ctx = opt;
	int fd = ctx->fd;
	free(ctx);
	return close(fd);
}

static int stream_init(struct luam_Sysprof_Options *opt)
{
	struct sysprof_ctx *ctx = calloc(1, sizeof(struct sysprof_ctx));
	if (NULL == ctx)
		return PROFILE_ERRIO;

	ctx->fd = open("/dev/null", O_WRONLY | O_CREAT, 0644);
	if (-1 == ctx->fd) {
		free(ctx);
		return PROFILE_ERRIO;
	}

	opt->ctx = ctx;
	opt->buf = ctx->buf;
	opt->len = STREAM_BUFFER_SIZE;

	return PROFILE_SUCCESS;
}

/* Get function to profile on top, call it. */
static int check_profile_func(lua_State *L)
{
	struct luam_Sysprof_Options opt = {};
	struct luam_Sysprof_Counters cnt = {};
	int status = PROFILE_ERRUSE;
	/*
	 * Since all the other modes functionality is the
	 * subset of CALLGRAPH mode, run this mode to test
	 * the profiler's behavior.
	 */
	opt.mode = LUAM_SYSPROF_CALLGRAPH;
	opt.interval = SYSPROF_INTERVAL_DEFAULT;
	stream_init(&opt);

	/*
	 * XXX: Payload function on top will not be removed if any
	 * of those assertions fail. So, the next call to the
	 * `utils_get_aux_lfunc()` will fail. It's OK, since
	 * we are already in trouble, just keep it in mind.
	 */
	assert_true(luaM_sysprof_set_writer(buffer_writer_default)
		    == PROFILE_SUCCESS);
	assert_true(luaM_sysprof_set_on_stop(on_stop_cb_default)
		    == PROFILE_SUCCESS);
	assert_true(luaM_sysprof_set_backtracer(NULL) == PROFILE_SUCCESS);

	status = luaM_sysprof_start(L, &opt);
	assert_true(status == PROFILE_SUCCESS);

	/* Run payload. */
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		test_comment("error running payload: %s", lua_tostring(L, -1));
		bail_out("error running sysprof test payload");
	}

	status = luaM_sysprof_stop(L);
	assert_true(status == PROFILE_SUCCESS);

	status = luaM_sysprof_report(&cnt);
	assert_true(status == PROFILE_SUCCESS);

	assert_true(cnt.samples > 1);
	assert_true(cnt.samples == cnt.vmst_asm +
			cnt.vmst_cfunc +
			cnt.vmst_exit +
			cnt.vmst_ffunc +
			cnt.vmst_gc +
			cnt.vmst_interp +
			cnt.vmst_lfunc +
			cnt.vmst_opt +
			cnt.vmst_record +
			cnt.vmst_trace);

	return TEST_EXIT_SUCCESS;
}
#endif

static int profile_func_jitoff(void *test_state)
{
	UNUSED(test_state);
	return todo("Need to replace backtrace with libunwind first");
#if 0
	lua_State *L = test_state;
	utils_get_aux_lfunc(L);
	(void)luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
	(void)luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_FLUSH);
	check_profile_func(L);
	(void)luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
	return TEST_EXIT_SUCCESS;
#endif
}

static int profile_func_jiton(void *test_state)
{
	UNUSED(test_state);
	return todo("Need to replace backtrace with libunwind first");
#if 0
	lua_State *L = test_state;
	utils_get_aux_lfunc(L);
	check_profile_func(L);
	return TEST_EXIT_SUCCESS;
#endif
}

int main(void)
{
#if LUAJIT_DISABLE_SYSPROF
	return skip_all("Sysprof is disabled");
#else /* LUAJIT_DISABLE_SYSPROF */
	if (LUAJIT_OS != LUAJIT_OS_LINUX)
		return skip_all("Sysprof is implemented for Linux only");
	if (LUAJIT_TARGET != LUAJIT_ARCH_X86
	    && LUAJIT_TARGET != LUAJIT_ARCH_X64)
		return skip_all("Sysprof is implemented for x86_64 only");

	lua_State *L = utils_lua_init();

	lua_pushcfunction(L, c_payload);
	lua_setfield(L, LUA_GLOBALSINDEX, "c_payload");
	utils_load_aux_script(L, "misclib-sysprof-capi-script.lua");

	const struct test_unit tgroup[] = {
		test_unit_def(base),
		test_unit_def(validation),
		test_unit_def(profile_func_jitoff),
		test_unit_def(profile_func_jiton)
	};
	const int test_result = test_run_group(tgroup, L);
	utils_lua_close(L);
	return test_result;
#endif /* LUAJIT_DISABLE_SYSPROF */
}
