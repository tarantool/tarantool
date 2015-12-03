/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "lua/init.h"
#include "lua/utils.h"
#include "main.h"
#if defined(__FreeBSD__) || defined(__APPLE__)
#include "libgen.h"
#endif

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lj_cdata.h>
#include <luajit.h>

#include <fiber.h>
#include "coeio.h"
#include "lua/fiber.h"
#include "lua/ipc.h"
#include "lua/errno.h"
#include "lua/socket.h"
#include "lua/utils.h"
#include "third_party/lua-cjson/lua_cjson.h"
#include "third_party/lua-yaml/lyaml.h"
#include "lua/msgpack.h"
#include "lua/pickle.h"
#include "lua/fio.h"
#include <small/ibuf.h>

#include <readline/readline.h>
#include <readline/history.h>

/**
 * The single Lua state of the transaction processor (tx) thread.
 */
struct lua_State *tarantool_L;
static struct ibuf tarantool_lua_ibuf_body;
struct ibuf *tarantool_lua_ibuf = &tarantool_lua_ibuf_body;
/**
 * The fiber running the startup Lua script
 */
struct fiber *script_fiber;
bool start_loop = true;

/* contents of src/lua/ files */
extern char strict_lua[],
	uuid_lua[],
	msgpackffi_lua[],
	fun_lua[],
	digest_lua[],
	init_lua[],
	buffer_lua[],
	fiber_lua[],
	log_lua[],
	uri_lua[],
	socket_lua[],
	console_lua[],
	help_lua[],
	help_en_US_lua[],
	tap_lua[],
	fio_lua[],
	/* jit.* library */
	vmdef_lua[],
	bc_lua[],
	bcsave_lua[],
	dis_x86_lua[],
	dis_x64_lua[],
	dump_lua[],
	csv_lua[],
	v_lua[],
	clock_lua[],
	title_lua[],
	p_lua[], /* LuaJIT 2.1 profiler */
	zone_lua[] /* LuaJIT 2.1 profiler */;

static const char *lua_modules[] = {
	/* Make it first to affect load of all other modules */
	"strict", strict_lua,
	"tarantool", init_lua,
	"fiber", fiber_lua,
	"buffer", buffer_lua,
	"msgpackffi", msgpackffi_lua,
	"fun", fun_lua,
	"digest", digest_lua,
	"uuid", uuid_lua,
	"log", log_lua,
	"uri", uri_lua,
	"fio", fio_lua,
	"csv", csv_lua,
	"clock", clock_lua,
	"socket", socket_lua,
	"console", console_lua,
	"title", title_lua,
	"tap", tap_lua,
	"help.en_US", help_en_US_lua,
	"help", help_lua,
	/* jit.* library */
	"jit.vmdef", vmdef_lua,
	"jit.bc", bc_lua,
	"jit.bcsave", bcsave_lua,
	"jit.dis_x86", dis_x86_lua,
	"jit.dis_x64", dis_x64_lua,
	"jit.dump", dump_lua,
	"jit.v", v_lua,
	/* Profiler */
	"jit.p", p_lua,
	"jit.zone", zone_lua,
	NULL
};

/*
 * {{{ box Lua library: common functions
 */

/**
 * Convert lua number or string to lua cdata 64bit number.
 */
static int
lbox_tonumber64(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "tonumber64: wrong number of arguments");

	switch (lua_type(L, 1)) {
	case LUA_TNUMBER:
		return 1;
	case LUA_TSTRING:
	{
		const char *arg = luaL_checkstring(L, 1);
		char *arge;
		errno = 0;
		unsigned long long result = strtoull(arg, &arge, 10);
		if (errno == 0 && arge != arg) {
			luaL_pushuint64(L, result);
			return 1;
		}
		break;
	}
	case LUA_TCDATA:
	{
		uint32_t ctypeid = 0;
		luaL_checkcdata(L, 1, &ctypeid);
		if (ctypeid >= CTID_INT8 && ctypeid <= CTID_DOUBLE) {
			lua_pushvalue(L, 1);
			return 1;
		}
		break;
	}
	}
	lua_pushnil(L);
	return 1;
}

static int
lbox_coredump(struct lua_State *L __attribute__((unused)))
{
	coredump(60);
	lua_pushstring(L, "ok");
	return 1;
}

/* }}} */

/*
 * {{{ console library
 */

static ssize_t
readline_cb(va_list ap)
{
	const char **line = va_arg(ap, const char **);
	const char *prompt = va_arg(ap, const char *);
	/*
	 * libeio threads blocks all signals by default. Therefore, nobody
	 * can interrupt read(2) syscall inside readline() to correctly
	 * cleanup resources and restore terminal state. In case of signal
	 * a signal_cb(), a ev watcher in tarantool.cc will stop event
	 * loop and and stop entire process by exiting the main thread.
	 * rl_cleanup_after_signal() is called from tarantool_lua_free()
	 * in order to restore terminal state.
	 */
	*line = readline(prompt);
	return 0;
}

static int
tarantool_console_readline(struct lua_State *L)
{
	const char *prompt = ">";
	if (lua_gettop(L) > 0) {
		if (!lua_isstring(L, 1))
			luaL_error(L, "console.readline([prompt])");
		prompt = lua_tostring(L, 1);
	}

	char *line;
	if (coio_call(readline_cb, &line, prompt) != 0) {
		lua_pushnil(L);
		return 1;
	}

	if (!line) {
		lua_pushnil(L);
	} else {
		/* Make sure the line doesn't leak. */
		static __thread char *line_buf = NULL;
		if (line_buf)
			free(line_buf);

		line_buf = line;
		lua_pushstring(L, line);

		free(line_buf);
		line_buf = NULL;
	}

	return 1;
}

static int
tarantool_console_add_history(struct lua_State *L)
{
	if (lua_gettop(L) < 1 || !lua_isstring(L, 1))
		luaL_error(L, "console.add_history(string)");

	add_history(lua_tostring(L, 1));
	return 0;
}

/* }}} */

/**
 * Prepend the variable list of arguments to the Lua
 * package search path
 */
static void
tarantool_lua_setpaths(struct lua_State *L)
{
	const char *home = getenv("HOME");
	lua_getglobal(L, "package");
	int top = lua_gettop(L);
	lua_pushliteral(L, "./?.lua;");
	lua_pushliteral(L, "./?/init.lua;");
	if (home != NULL) {
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/share/lua/5.1/?.lua;");
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/share/lua/5.1/?/init.lua;");
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/share/lua/?.lua;");
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/share/lua/?/init.lua;");
	}
	lua_pushliteral(L, MODULE_LUAPATH ";");
	lua_getfield(L, top, "path");
	lua_concat(L, lua_gettop(L) - top);
	lua_setfield(L, top, "path");

	lua_pushliteral(L, "./?." TARANTOOL_LIBEXT ";");
	if (home != NULL) {
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/lib/lua/5.1/?." TARANTOOL_LIBEXT ";");
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/lib/lua/?." TARANTOOL_LIBEXT ";");
	}
	lua_pushliteral(L, MODULE_LIBPATH ";");
	lua_getfield(L, top, "cpath");
	lua_concat(L, lua_gettop(L) - top);
	lua_setfield(L, top, "cpath");

	assert(lua_gettop(L) == top);
	lua_pop(L, 1); /* package */
}

static int
luaopen_tarantool(lua_State *L)
{
	/* Set _G._TARANTOOL (like _VERSION) */
	lua_pushstring(L, tarantool_version());
	lua_setfield(L, LUA_GLOBALSINDEX, "_TARANTOOL");

	static const struct luaL_reg initlib[] = {
		{NULL, NULL}
	};
	luaL_register_module(L, "tarantool", initlib);

	/* version */
	lua_pushstring(L, tarantool_version());
	lua_setfield(L, -2, "version");

	/* build */
	lua_pushstring(L, "build");
	lua_newtable(L);

	/* build.target */
	lua_pushstring(L, "target");
	lua_pushstring(L, BUILD_INFO);
	lua_settable(L, -3);

	/* build.options */
	lua_pushstring(L, "options");
	lua_pushstring(L, BUILD_OPTIONS);
	lua_settable(L, -3);

	/* build.compiler */
	lua_pushstring(L, "compiler");
	lua_pushstring(L, COMPILER_INFO);
	lua_settable(L, -3);

	/* build.mod_format */
	lua_pushstring(L, "mod_format");
	lua_pushstring(L, TARANTOOL_LIBEXT);
	lua_settable(L, -3);

	/* build.flags */
	lua_pushstring(L, "flags");
	lua_pushstring(L, TARANTOOL_C_FLAGS);
	lua_settable(L, -3);

	lua_settable(L, -3);    /* box.info.build */
	return 1;
}

void
tarantool_lua_init(const char *tarantool_bin, int argc, char **argv)
{
	lua_State *L = luaL_newstate();
	if (L == NULL) {
		panic("failed to initialize Lua");
	}
	ibuf_create(tarantool_lua_ibuf, tarantool_lua_slab_cache(), 16000);
	luaL_openlibs(L);
	tarantool_lua_setpaths(L);

	/* Initialize ffi to enable luaL_pushcdata/luaL_checkcdata functions */
	luaL_loadstring(L, "return require('ffi')");
	lua_call(L, 0, 0);

	lua_register(L, "tonumber64", lbox_tonumber64);
	lua_register(L, "coredump", lbox_coredump);

	tarantool_lua_utils_init(L);
	tarantool_lua_fiber_init(L);
	tarantool_lua_ipc_init(L);
	tarantool_lua_errno_init(L);
	tarantool_lua_fio_init(L);
	tarantool_lua_socket_init(L);
	tarantool_lua_pickle_init(L);
	luaopen_msgpack(L);
	lua_pop(L, 1);
	luaopen_yaml(L);
	lua_pop(L, 1);
	luaopen_json(L);
	lua_pop(L, 1);
	luaopen_msgpack(L);
	lua_pop(L, 1);

#if defined(HAVE_GNU_READLINE)
	/*
	 * Disable libreadline signals handlers. All signals are handled in
	 * main thread by libev watchers.
	 */
	rl_catch_signals = 0;
	rl_catch_sigwinch = 0;
#endif
	static const struct luaL_reg consolelib[] = {
		{"readline", tarantool_console_readline},
		{"add_history", tarantool_console_add_history},
		{NULL, NULL}
	};
	luaL_register_module(L, "console", consolelib);
	lua_pop(L, 1);

	lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
	for (const char **s = lua_modules; *s; s += 2) {
		const char *modname = *s;
		const char *modsrc = *(s + 1);
		const char *modfile = lua_pushfstring(L,
			"@builtin/%s.lua", modname);
		if (luaL_loadbuffer(L, modsrc, strlen(modsrc), modfile))
			panic("Error loading Lua module %s...: %s",
			      modname, lua_tostring(L, -1));
		lua_pushstring(L, modname);
		lua_call(L, 1, 1);
		if (!lua_isnil(L, -1)) {
			lua_setfield(L, -3, modname); /* package.loaded.modname = t */
		} else {
			lua_pop(L, 1); /* nil */
		}
		lua_pop(L, 1); /* chunkname */
	}
	lua_pop(L, 1); /* _LOADED */

	luaopen_tarantool(L);
	lua_pop(L, 1);

	lua_newtable(L);
	lua_pushinteger(L, -1);
	lua_pushstring(L, tarantool_bin);
	lua_settable(L, -3);
	for (int i = 0; i < argc; i++) {
		lua_pushinteger(L, i);
		lua_pushstring(L, argv[i]);
		lua_settable(L, -3);
	}
	lua_setfield(L, LUA_GLOBALSINDEX, "arg");

#ifdef NDEBUG
	/* Unload strict after boot in release mode */
	if (luaL_dostring(L, "require('strict').off()") != 0)
		panic("Failed to unload 'strict' Lua module");
#endif /* NDEBUG */

	/* clear possible left-overs of init */
	lua_settop(L, 0);
	tarantool_L = L;
}

char *history = NULL;

struct slab_cache *
tarantool_lua_slab_cache()
{
	return &cord()->slabc;
}

/**
 * Execute start-up script.
 */
static void
run_script(va_list ap)
{
	struct lua_State *L = va_arg(ap, struct lua_State *);
	const char *path = va_arg(ap, const char *);
	int argc = va_arg(ap, int);
	char **argv = va_arg(ap, char **);

	/*
	 * Return control to tarantool_lua_run_script.
	 * tarantool_lua_run_script then will start an auxiliary event
	 * loop and re-schedule this fiber.
	 */
	fiber_sleep(0.0);

	if (access(path, F_OK) == 0) {
		/* Execute script. */
		if (luaL_loadfile(L, path) != 0)
			panic("%s", lua_tostring(L, -1));
	} else if (!isatty(STDIN_FILENO)) {
		/* Execute stdin */
		if (luaL_loadfile(L, NULL) != 0)
			panic("%s", lua_tostring(L, -1));
	} else {
		say_crit("version %s\ntype 'help' for interactive help",
			 tarantool_version());
		/* get console.start from package.loaded */
		lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
		lua_getfield(L, -1, "console");
		lua_getfield(L, -1, "start");
		lua_remove(L, -2); /* remove package.loaded.console */
		lua_remove(L, -2); /* remove package.loaded */
		start_loop = false;
	}
	lua_checkstack(L, argc - 1);
	for (int i = 1; i < argc; i++)
		lua_pushstring(L, argv[i]);
	if (lbox_call(L, lua_gettop(L) - 1, 0) != 0) {
		struct error *e = diag_last_error(&fiber()->diag);
		panic("%s", e->errmsg);
	}

	/* clear the stack from return values. */
	lua_settop(L, 0);
	/*
	 * Lua script finished. Stop the auxiliary event loop and
	 * return control back to tarantool_lua_run_script.
	 */
	ev_break(loop(), EVBREAK_ALL);
}

void
tarantool_lua_run_script(char *path, int argc, char **argv)
{
	const char *title = path ? basename(path) : "interactive";
	/*
	 * init script can call box.fiber.yield (including implicitly via
	 * box.insert, box.update, etc...), but box.fiber.yield() today,
	 * when called from 'sched' fiber crashes the server.
	 * To work this problem around we must run init script in
	 * a separate fiber.
	 */
	script_fiber = fiber_new(title, run_script);
	if (script_fiber == NULL)
		panic("%s", diag_last_error(diag_get())->errmsg);
	fiber_start(script_fiber, tarantool_L, path, argc, argv);

	/*
	 * Run an auxiliary event loop to re-schedule run_script fiber.
	 * When this fiber finishes, it will call ev_break to stop the loop.
	 */
	ev_run(loop(), 0);
	/* The fiber running the startup script has ended. */
	script_fiber = NULL;
}

void
tarantool_lua_free()
{
	/*
	 * Some part of the start script panicked, and called
	 * exit().  The call stack in this case leads us back to
	 * luaL_call() in run_script(). Trying to free a Lua state
	 * from within luaL_call() is not the smartest idea (@sa
	 * gh-612).
	 */
	if (script_fiber)
		return;
	/*
	 * Got to be done prior to anything else, since GC
	 * handlers can refer to other subsystems (e.g. fibers).
	 */
	if (tarantool_L) {
		/* collects garbage, invoking userdata gc */
		lua_close(tarantool_L);
	}
	tarantool_L = NULL;

#if 0
	/* Temporarily moved to tarantool_free(), tarantool_lua_free() not
	 * being called due to cleanup order issues
	 */
	if (isatty(STDIN_FILENO)) {
		/* See comments in readline_cb() */
		rl_cleanup_after_signal();
	}
#endif
}

