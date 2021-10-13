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
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__) || defined(__OpenBSD__)
#include <libgen.h>
#endif

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lj_cdata.h>
#include <luajit.h>

#include <fiber.h>
#include "version.h"
#include "backtrace.h"
#include "coio.h"
#include "lua/fiber.h"
#include "lua/fiber_cond.h"
#include "lua/fiber_channel.h"
#include "lua/errno.h"
#include "lua/socket.h"
#include "lua/utils.h"
#include "lua/serializer.h"
#include <lua-cjson/lua_cjson.h>
#include <lua-yaml/lyaml.h>
#include "lua/msgpack.h"
#include "lua/pickle.h"
#include "lua/fio.h"
#include "lua/popen.h"
#include "lua/httpc.h"
#include "lua/utf8.h"
#include "lua/swim.h"
#include "lua/decimal.h"
#include "digest.h"
#include "errinj.h"
#include <small/ibuf.h>

#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

/**
 * The single Lua state of the transaction processor (tx) thread.
 */
struct lua_State *tarantool_L;
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
	crypto_lua[],
	digest_lua[],
	debug_lua[],
	init_lua[],
	buffer_lua[],
	errno_lua[],
	fiber_lua[],
	httpc_lua[],
	log_lua[],
	uri_lua[],
	socket_lua[],
	help_lua[],
	help_en_US_lua[],
	tap_lua[],
	fio_lua[],
	error_lua[],
	argparse_lua[],
	iconv_lua[],
	/* jit.* library */
	vmdef_lua[],
	bc_lua[],
	bcsave_lua[],
	dis_arm64_lua[],
	dis_x86_lua[],
	dis_x64_lua[],
	dump_lua[],
	csv_lua[],
	v_lua[],
	clock_lua[],
	title_lua[],
	env_lua[],
	pwd_lua[],
	table_lua[],
	trigger_lua[],
	string_lua[],
	swim_lua[],
	p_lua[], /* LuaJIT 2.1 profiler */
	zone_lua[], /* LuaJIT 2.1 profiler */
	/* tools.* libraries. */
	bufread_lua[],
	symtab_lua[],
	parse_lua[],
	process_lua[],
	humanize_lua[],
	memprof_lua[],
	datetime_lua[]
;

static const char *lua_modules[] = {
	/* Make it first to affect load of all other modules */
	"strict", strict_lua,
	"fun", fun_lua,
	"debug", debug_lua,
	"tarantool", init_lua,
	"errno", errno_lua,
	"fiber", fiber_lua,
	"env", env_lua,
	"buffer", buffer_lua,
	"string", string_lua,
	"table", table_lua,
	"msgpackffi", msgpackffi_lua,
	"crypto", crypto_lua,
	"digest", digest_lua,
	"uuid", uuid_lua,
	"log", log_lua,
	"uri", uri_lua,
	"fio", fio_lua,
	"error", error_lua,
	"csv", csv_lua,
	"clock", clock_lua,
	"socket", socket_lua,
	"title", title_lua,
	"tap", tap_lua,
	"help.en_US", help_en_US_lua,
	"help", help_lua,
	"internal.argparse", argparse_lua,
	"internal.trigger", trigger_lua,
	"pwd", pwd_lua,
	"http.client", httpc_lua,
	"iconv", iconv_lua,
	"swim", swim_lua,
	/* jit.* library */
	"jit.vmdef", vmdef_lua,
	"jit.bc", bc_lua,
	"jit.bcsave", bcsave_lua,
	"jit.dis_arm64", dis_arm64_lua,
	"jit.dis_x86", dis_x86_lua,
	"jit.dis_x64", dis_x64_lua,
	"jit.dump", dump_lua,
	"jit.v", v_lua,
	/* Profiler */
	"jit.p", p_lua,
	"jit.zone", zone_lua,
	/* tools.* libraries. Order is important. */
	"utils.bufread", bufread_lua,
	"utils.symtab", symtab_lua,
	"memprof.parse", parse_lua,
	"memprof.process", process_lua,
	"memprof.humanize", humanize_lua,
	"memprof", memprof_lua,
	"datetime", datetime_lua,
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
	luaL_checkany(L, 1);
	int base = luaL_optint(L, 2, -1);
	luaL_argcheck(L, (2 <= base && base <= 36) || base == -1, 2,
		      "base out of range");
	switch (lua_type(L, 1)) {
	case LUA_TNUMBER:
		base = (base == -1 ? 10 : base);
		if (base != 10)
			return luaL_argerror(L, 1, "string expected");
		lua_settop(L, 1); /* return original value as is */
		return 1;
	case LUA_TSTRING:
	{
		size_t argl = 0;
		const char *arg = luaL_checklstring(L, 1, &argl);
		/* Trim whitespaces at begin/end */
		while (argl > 0 && isspace(arg[argl - 1])) {
			argl--;
		}
		while (isspace(*arg)) {
			arg++; argl--;
		}

		/*
		 * Check if we're parsing custom format:
		 * 1) '0x' or '0X' trim in case of base == 16 or base == -1
		 * 2) '0b' or '0B' trim in case of base == 2  or base == -1
		 * 3) '-' for negative numbers
		 * 4) LL, ULL, LLU - trim, but only for base == 2 or
		 *    base == 16 or base == -1. For consistency do not bother
		 *    with any non-common bases, since user may have specified
		 *    base >= 22, in which case 'L' will be a digit.
		 */
		char negative = 0;
		if (arg[0] == '-') {
			arg++; argl--;
			negative = 1;
		}
		if (argl > 2 && arg[0] == '0') {
			if ((arg[1] == 'x' || arg[1] == 'X') &&
			    (base == 16 || base == -1)) {
				base = 16; arg += 2; argl -= 2;
			} else if ((arg[1] == 'b' || arg[1] == 'B') &&
			           (base == 2 || base == -1)) {
				base = 2;  arg += 2; argl -= 2;
			}
		}
		bool ull = false;
		if (argl > 2 && (base == 2 || base == 16 || base == -1)) {
			if (arg[argl - 1] == 'u' || arg[argl - 1] == 'U') {
				ull = true;
				--argl;
			}
			if ((arg[argl - 1] == 'l' || arg[argl - 1] == 'L') &&
			    (arg[argl - 2] == 'l' || arg[argl - 2] == 'L'))
				argl -= 2;
			else {
				ull = false;
				goto skip;
			}
			if (!ull && (arg[argl - 1] == 'u' ||
				     arg[argl - 1] == 'U')) {
				ull = true;
				--argl;
			}
		}
skip:		base = (base == -1 ? 10 : base);
		errno = 0;
		char *arge;
		unsigned long long result = strtoull(arg, &arge, base);
		if (errno == 0 && arge == arg + argl) {
			if (argl == 0) {
				lua_pushnil(L);
			} else if (negative) {
				/*
				 * To test overflow, consider
				 *  result > -INT64_MIN;
				 *  result - 1 > -INT64_MIN - 1;
				 * Assumption:
				 *  INT64_MAX == -(INT64_MIN + 1);
				 * Finally,
				 *  result - 1 > INT64_MAX;
				 */
				if (ull)
					luaL_pushuint64(L, (UINT64_MAX - result) + 1);
				else if (result != 0 && result - 1 > INT64_MAX)
					lua_pushnil(L);
				else
					luaL_pushint64(L, -result);
			} else {
				luaL_pushuint64(L, result);
			}
			return 1;
		}
		break;
	} /* LUA_TSTRING */
	case LUA_TCDATA:
	{
		base = (base == -1 ? 10 : base);
		if (base != 10)
			return luaL_argerror(L, 1, "string expected");
		uint32_t ctypeid = 0;
		luaL_checkcdata(L, 1, &ctypeid);
		if (ctypeid >= CTID_INT8 && ctypeid <= CTID_DOUBLE) {
			lua_pushvalue(L, 1);
			return 1;
		}
		break;
	} /* LUA_TCDATA */
	}
	lua_pushnil(L);
	return 1;
}

/* }}} */

/**
 * Original LuaJIT/Lua logic: <luajit/src/lib_package.c - function setpath>
 *
 * 1) If environment variable 'envname' is empty, it uses only <default value>
 * 2) Otherwise:
 *    - If it contains ';;', then ';;' is replaced with ';'<default value>';'
 *    - Otherwise is uses only what's inside this value.
 **/
static void
tarantool_lua_pushpath_env(struct lua_State *L, const char *envname)
{
	const char *path = getenv(envname);
	if (path != NULL) {
		const char *def = lua_tostring(L, -1);
		path = luaL_gsub(L, path, ";;", ";\1;");
		luaL_gsub(L, path, "\1", def);
		lua_remove(L, -2);
		lua_remove(L, -2);
	}
}

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
	/* overwrite standard paths */
	lua_concat(L, lua_gettop(L) - top);
	tarantool_lua_pushpath_env(L, "LUA_PATH");
	lua_setfield(L, top, "path");

	if (home != NULL) {
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/lib/lua/5.1/?" MODULE_LIBSUFFIX ";");
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/lib/lua/?" MODULE_LIBSUFFIX ";");
	}
	lua_pushliteral(L, MODULE_LIBPATH ";");
	/* overwrite standard paths */
	lua_concat(L, lua_gettop(L) - top);
	tarantool_lua_pushpath_env(L, "LUA_CPATH");
	lua_setfield(L, top, "cpath");

	assert(lua_gettop(L) == top);
	lua_pop(L, 1); /* package */
}

static int
tarantool_panic_handler(lua_State *L) {
	const char *problem = lua_tostring(L, -1);
#ifdef ENABLE_BACKTRACE
	print_backtrace();
#endif /* ENABLE_BACKTRACE */
	say_crit("%s", problem);
	int level = 1;
	lua_Debug ar;
	while (lua_getstack(L, level++, &ar) == 1) {
		if (lua_getinfo(L, "nSl", &ar) == 0)
			break;
		say_crit("#%d %s (%s), %s:%d", level,
			 ar.name, ar.namewhat,
			 ar.short_src, ar.currentline);
	}
	return 1;
}

static int
luaopen_tarantool(lua_State *L)
{
	/* Set _G._TARANTOOL (like _VERSION) */
	lua_pushstring(L, tarantool_version());
	lua_setfield(L, LUA_GLOBALSINDEX, "_TARANTOOL");

	static const struct luaL_Reg initlib[] = {
		{NULL, NULL}
	};
	luaL_register_module(L, "tarantool", initlib);

	/* package */
	lua_pushstring(L, tarantool_package());
	lua_setfield(L, -2, "package");

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
	luaL_openlibs(L);
	tarantool_lua_setpaths(L);

	/* Initialize ffi to enable luaL_pushcdata/luaL_checkcdata functions */
	luaL_loadstring(L, "return require('ffi')");
	lua_call(L, 0, 0);
	lua_register(L, "tonumber64", lbox_tonumber64);

	tarantool_lua_utf8_init(L);
	tarantool_lua_utils_init(L);
	tarantool_lua_fiber_init(L);
	tarantool_lua_fiber_cond_init(L);
	tarantool_lua_fiber_channel_init(L);
	tarantool_lua_errno_init(L);
	tarantool_lua_error_init(L);
	tarantool_lua_fio_init(L);
	tarantool_lua_popen_init(L);
	tarantool_lua_socket_init(L);
	tarantool_lua_pickle_init(L);
	tarantool_lua_digest_init(L);
	tarantool_lua_serializer_init(L);
	tarantool_lua_swim_init(L);
	tarantool_lua_decimal_init(L);
	luaopen_http_client_driver(L);
	lua_pop(L, 1);
	luaopen_msgpack(L);
	lua_pop(L, 1);
	luaopen_yaml(L);
	lua_pop(L, 1);
	luaopen_json(L);
	lua_pop(L, 1);
#if defined(HAVE_GNU_READLINE)
	/*
	 * Disable libreadline signals handlers. All signals are handled in
	 * main thread by libev watchers.
	 */
	rl_catch_signals = 0;
	rl_catch_sigwinch = 0;
#endif

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

	lua_atpanic(L, tarantool_panic_handler);
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
 * Push argument and call a function on the top of Lua stack
 */
static int
lua_main(lua_State *L, int argc, char **argv)
{
	assert(lua_isfunction(L, -1));
	lua_checkstack(L, argc - 1);
	for (int i = 1; i < argc; i++)
		lua_pushstring(L, argv[i]);
	int rc = luaT_call(L, lua_gettop(L) - 1, 0);
	/* clear the stack from return values. */
	lua_settop(L, 0);
	return rc;
}

/**
 * Execute start-up script.
 */
static int
run_script_f(va_list ap)
{
	struct lua_State *L = va_arg(ap, struct lua_State *);
	const char *path = va_arg(ap, const char *);
	bool interactive = va_arg(ap, int);
	int optc = va_arg(ap, int);
	const char **optv = va_arg(ap, const char **);
	int argc = va_arg(ap, int);
	char **argv = va_arg(ap, char **);
	/*
	 * An error is returned via an external diag. A caller
	 * can't use fiber_join(), because the script can call
	 * os.exit(). That call makes the script runner fiber
	 * never really dead. It never returns from its function.
	 */
	struct diag *diag = va_arg(ap, struct diag *);
	bool aux_loop_is_run = false;
	bool is_option_e_ran = false;

	/*
	 * Load libraries and execute chunks passed by -l and -e
	 * command line options
	 */
	for (int i = 0; i < optc; i += 2) {
		assert(optv[i][0] == '-' && optv[i][2] == '\0');
		switch (optv[i][1]) {
		case 'l':
			/*
			 * Load library
			 */
			lua_getglobal(L, "require");
			lua_pushstring(L, optv[i + 1]);
			if (luaT_call(L, 1, 1) != 0)
				goto error;
			/* Non-standard: set name = require('name') */
			lua_setglobal(L, optv[i + 1]);
			lua_settop(L, 0);
			break;
		case 'e':
			/*
			 * Execute chunk
			 */
			if (luaL_loadbuffer(L, optv[i + 1], strlen(optv[i + 1]),
					    "=(command line)") != 0)
				goto luajit_error;
			if (luaT_call(L, 0, 0) != 0)
				goto error;
			lua_settop(L, 0);
			is_option_e_ran = true;
			break;
		default:
			unreachable(); /* checked by getopt() in main() */
		}
	}

	/*
	 * Return control to tarantool_lua_run_script.
	 * tarantool_lua_run_script then will start an auxiliary event
	 * loop and re-schedule this fiber.
	 */
	fiber_sleep(0.0);
	aux_loop_is_run = true;

	int is_a_tty = isatty(STDIN_FILENO);
	/*
	 * Override return value of isatty(STDIN_FILENO) if
	 * ERRINJ_STDIN_ISATTY enabled (iparam not set to default).
	 * Use iparam in such case, standard return value otherwise.
	 * Integer param of errinj is used in order to set different
	 * return values.
	*/
	ERROR_INJECT_INT(ERRINJ_STDIN_ISATTY, inj->iparam >= 0, {
		is_a_tty = inj->iparam;
	});

	if (path && strcmp(path, "-") != 0 && access(path, F_OK) == 0) {
		/* Execute script. */
		if (luaL_loadfile(L, path) != 0)
			goto luajit_error;
		if (lua_main(L, argc, argv) != 0)
			goto error;
	} else if (!is_a_tty || (path && strcmp(path, "-") == 0)) {
		/* Execute stdin */
		if (luaL_loadfile(L, NULL) != 0)
			goto luajit_error;
		if (lua_main(L, argc, argv) != 0)
			goto error;
	} else if (!is_option_e_ran) {
		interactive = true;
	}

	/*
	 * Start interactive mode in any of the cases:
	 * - it was explicitly requested by "-i" option;
	 * - stdin is TTY and there are no script (-e is considered as a script).
	 */
	if (interactive) {
		say_crit("%s %s\ntype 'help' for interactive help",
			 tarantool_package(), tarantool_version());
		/* get console.start from package.loaded */
		lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
		lua_getfield(L, -1, "console");
		lua_getfield(L, -1, "start");
		lua_remove(L, -2); /* remove package.loaded.console */
		lua_remove(L, -2); /* remove package.loaded */
		start_loop = false;
		if (lua_main(L, argc, argv) != 0)
			goto error;
	}
	/*
	 * Lua script finished. Stop the auxiliary event loop and
	 * return control back to tarantool_lua_run_script.
	 */
end:
	/*
	 * Auxiliary loop in tarantool_lua_run_script
	 * should start (ev_run()) before this fiber
	 * invokes ev_break().
	 */
	if (!aux_loop_is_run)
		fiber_sleep(0.0);
	ev_break(loop(), EVBREAK_ALL);
	return 0;

luajit_error:
	diag_set(LuajitError, lua_tostring(L, -1));
error:
	diag_move(diag_get(), diag);
	goto end;
}

int
tarantool_lua_run_script(char *path, bool interactive,
			 int optc, const char **optv, int argc, char **argv)
{
	const char *title = path ? basename(path) : "interactive";
	/*
	 * init script can call box.fiber.yield (including implicitly via
	 * box.insert, box.update, etc...), but box.fiber.yield() today,
	 * when called from 'sched' fiber crashes the server.
	 * To work this problem around we must run init script in
	 * a separate fiber.
	 */

	script_fiber = fiber_new(title, run_script_f);
	if (script_fiber == NULL)
		panic("%s", diag_last_error(diag_get())->errmsg);
	script_fiber->storage.lua.stack = tarantool_L;
	/*
	 * Create a new diag on the stack. Don't pass fiber's diag, because it
	 * might be overwritten by libev callbacks invoked in the scheduler
	 * fiber (which is this), and therefore can't be used as a sign of fail
	 * in the script itself.
	 */
	struct diag script_diag;
	diag_create(&script_diag);
	fiber_start(script_fiber, tarantool_L, path, interactive,
		    optc, optv, argc, argv, &script_diag);

	/*
	 * Run an auxiliary event loop to re-schedule run_script fiber.
	 * When this fiber finishes, it will call ev_break to stop the loop.
	 */
	if (start_loop)
		ev_run(loop(), 0);
	/* The fiber running the startup script has ended. */
	script_fiber = NULL;
	diag_move(&script_diag, diag_get());
	diag_destroy(&script_diag);
	/*
	 * Result can't be obtained via fiber_join - script fiber
	 * never dies if os.exit() was called. This is why diag
	 * is checked explicitly.
	 */
	return diag_is_empty(diag_get()) ? 0 : -1;
}

void
tarantool_lua_free()
{
	tarantool_lua_utf8_free();
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
		/*
		 * Restore terminal state. Doesn't hurt if exiting not
		 * due to a signal.
		 */
		rl_cleanup_after_signal();
	}
#endif
}
