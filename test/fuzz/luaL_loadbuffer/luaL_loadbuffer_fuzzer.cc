extern "C"
{
#include <signal.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
/* luaM_metrics */
#include <lmisclib.h>
}

#include "lua_grammar.pb.h"
#include "serializer.h"

#include <libprotobuf-mutator/port/protobuf.h>
#include <libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h>

#define PRINT_METRIC(desc, val, total) \
		std::cerr << (desc) << (val) \
			  << " (" << (val) * 100 / (total) << "%)" \
			  << std::endl

struct metrics {
	size_t total_num;
	size_t total_num_with_errors;
	size_t jit_snap_restore;
	size_t jit_trace_abort;
	size_t jit_trace_num;
};

static struct metrics metrics;

static inline void
print_metrics(struct metrics *metrics)
{
	if (metrics->total_num == 0)
		return;

	std::cerr << "Total number of samples: "
		  << metrics->total_num << std::endl;
	PRINT_METRIC("Total number of samples with errors: ",
		     metrics->total_num_with_errors, metrics->total_num);
	PRINT_METRIC("Total number of samples with recorded traces: ",
		     metrics->jit_trace_num, metrics->total_num);
	PRINT_METRIC("Total number of samples with snap restores: ",
		     metrics->jit_snap_restore, metrics->total_num);
	PRINT_METRIC("Total number of samples with abort traces: ",
		     metrics->jit_trace_abort, metrics->total_num);
}

/* https://www.tarantool.io/en/doc/latest/reference/tooling/luajit_getmetrics/#getmetrics-c-api */
static inline void
collect_lj_metrics(struct metrics *metrics, lua_State *L)
{
	struct luam_Metrics lj_metrics;
	luaM_metrics(L, &lj_metrics);
	if (lj_metrics.jit_snap_restore != 0)
		metrics->jit_snap_restore++;
	if (lj_metrics.jit_trace_abort != 0)
		metrics->jit_trace_abort++;
	if (lj_metrics.jit_trace_num != 0)
		metrics->jit_trace_num++;
}

void
sig_handler(int signo, siginfo_t *info, void *context)
{
	print_metrics(&metrics);
}

__attribute__((constructor))
static void
setup(void)
{
	metrics = {};
	struct sigaction act = {};
	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = &sig_handler;
	sigaction(SIGUSR1, &act, NULL);
}

__attribute__((destructor))
static void
teardown(void)
{
	print_metrics(&metrics);
}

/**
 * Get an error message from the stack, and report it to std::cerr.
 * Remove the message from the stack.
 */
static inline void
report_error(lua_State *L, const std::string &prefix)
{
	metrics.total_num_with_errors++;
	const char *verbose = ::getenv("LUA_FUZZER_VERBOSE");
	if (!verbose)
		return;

	std::string err_str = lua_tostring(L, 1);
	/* Pop error message from stack. */
	lua_pop(L, 1);
	std::cerr << prefix << " error: " << err_str << std::endl;
}

DEFINE_PROTO_FUZZER(const lua_grammar::Block &message)
{
	lua_State *L = luaL_newstate();
	if (!L)
		return;

	std::string code = luajit_fuzzer::MainBlockToString(message);

	if (::getenv("LPM_DUMP_NATIVE_INPUT") && code.size() != 0) {
		std::cout << "-------------------------" << std::endl;
		std::cout << code << std::endl;
	}

	luaL_openlibs(L);

	/*
	 * See https://luajit.org/running.html.
	 */
	luaL_dostring(L, "jit.opt.start('hotloop=1')");
	luaL_dostring(L, "jit.opt.start('hotexit=1')");
	luaL_dostring(L, "jit.opt.start('recunroll=1')");
	luaL_dostring(L, "jit.opt.start('callunroll=1')");

	if (luaL_loadbuffer(L, code.c_str(), code.size(), "fuzz") != LUA_OK) {
		report_error(L, "luaL_loadbuffer()");
		goto end;
	}

	/*
	 * Using lua_pcall (protected call) to catch errors due to
	 * wrong semantics of some generated code chunks.
	 * Mostly, generated code is not semantically correct, so it is
	 * needed to describe Lua semantics for more interesting
	 * results and fuzzer tests.
	 */
	if (lua_pcall(L, 0, 0, 0) != LUA_OK)
		report_error(L, "lua_pcall()");

end:
	metrics.total_num++;
	collect_lj_metrics(&metrics, L);
	lua_settop(L, 0);
	lua_close(L);
}
