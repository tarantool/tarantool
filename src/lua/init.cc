/*
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
#include "tarantool.h"
#include "box/box.h"
#include "tbuf.h"
#if defined(__FreeBSD__) || defined(__APPLE__)
#include "libgen.h"
#endif

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lj_cdata.h>
} /* extern "C" */


#include <fiber.h>
#include <scoped_guard.h>
#include "coeio.h"
#include "lua/fiber.h"
#include "lua/ipc.h"
#include "lua/errno.h"
#include "lua/bsdsocket.h"
#include "lua/utils.h"
#include "third_party/lua-cjson/lua_cjson.h"
#include "third_party/lua-yaml/lyaml.h"
#include "lua/msgpack.h"
#include "lua/pickle.h"
#include "lua/fio.h"

#include <ctype.h>
#include "small/region.h"
#include <stdio.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

struct lua_State *tarantool_L;

/* contents of src/lua/ files */
extern char uuid_lua[],
	msgpackffi_lua[],
	fun_lua[],
	digest_lua[],
	init_lua[],
	log_lua[],
	uri_lua[],
	bsdsocket_lua[],
	console_lua[],
	box_net_box_lua[],
	help_lua[],
	help_en_US_lua[],
	tap_lua[],
	fio_lua[];

static const char *lua_sources[] = {
	init_lua,
	NULL
};

static const char *lua_modules[] = {
	"msgpackffi", msgpackffi_lua,
	"fun", fun_lua,
	"digest", digest_lua,
	"uuid", uuid_lua,
	"log", log_lua,
	"uri", uri_lua,
	"fio", fio_lua,
	"socket", bsdsocket_lua,
	"net.box", box_net_box_lua,
	"console", console_lua,
	"tap", tap_lua,
	"help.en_US", help_en_US_lua,
	"help", help_lua,
	NULL
};

/*
 * {{{ box Lua library: common functions
 */

uint64_t
tarantool_lua_tointeger64(struct lua_State *L, int idx)
{
	uint64_t result = 0;

	switch (lua_type(L, idx)) {
	case LUA_TNUMBER:
		result = lua_tonumber(L, idx);
		break;
	case LUA_TSTRING:
	{
		const char *arg = luaL_checkstring(L, idx);
		char *arge;
		errno = 0;
		result = strtoull(arg, &arge, 10);
		if (errno != 0 || arge == arg)
			luaL_error(L, "lua_tointeger64: bad argument");
		break;
	}
	case LUA_TCDATA:
	{
		uint32_t ctypeid = 0;
		void *cdata = luaL_checkcdata(L, idx, &ctypeid);
		if (ctypeid != CTID_INT64 && ctypeid != CTID_UINT64) {
			luaL_error(L,
				   "lua_tointeger64: unsupported cdata type");
		}
		result = *(uint64_t*)cdata;
		break;
	}
	default:
		luaL_error(L, "lua_tointeger64: unsupported type: %s",
			   lua_typename(L, lua_type(L, idx)));
	}

	return result;
}
const char *
tarantool_lua_tostring(struct lua_State *L, int index)
{
	/* we need an absolute index */
	if (index < 0)
		index = lua_gettop(L) + index + 1;
	lua_getglobal(L, "tostring");
	lua_pushvalue(L, index);
	/* pops both "tostring" and its argument */
	lua_call(L, 1, 1);
	lua_replace(L, index);
	return lua_tostring(L, index);
}

/**
 * Convert lua number or string to lua cdata 64bit number.
 */
static int
lbox_tonumber64(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "tonumber64: wrong number of arguments");
	uint64_t result = tarantool_lua_tointeger64(L, 1);
	*(uint64_t *) luaL_pushcdata(L, CTID_UINT64,
				     sizeof(uint64_t)) = result;
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
	coeio_custom(readline_cb, TIMEOUT_INFINITY, &line, prompt);
	auto scoped_guard = make_scoped_guard([&] { free(line); });
	if (!line) {
		lua_pushnil(L);
	} else {
		lua_pushstring(L, line);
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

	lua_pushliteral(L, "./?.so;");
	if (home != NULL) {
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/lib/lua/5.1/?.so;");
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/lib/lua/?.so;");
	}
	lua_pushliteral(L, MODULE_LIBPATH ";");
	lua_getfield(L, top, "cpath");
	lua_concat(L, lua_gettop(L) - top);
	lua_setfield(L, top, "cpath");

	assert(lua_gettop(L) == top);
	lua_pop(L, 1); /* package */
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
	lua_register(L, "coredump", lbox_coredump);

	tarantool_lua_utils_init(L);
	tarantool_lua_fiber_init(L);
	tarantool_lua_ipc_init(L);
	tarantool_lua_errno_init(L);
	tarantool_lua_fio_init(L);
	tarantool_lua_bsdsocket_init(L);
	tarantool_lua_pickle_init(L);
	luaopen_msgpack(L);
	lua_pop(L, 1);
	luaopen_yaml(L);
	lua_pop(L, 1);
	luaopen_json(L);
	lua_pop(L, 1);

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
		lua_call(L, 0, 1);
		lua_setfield(L, -3, modname); /* package.loaded.modname = t */
		lua_pop(L, 1); /* chunkname */
	}
	lua_pop(L, 1); /* _LOADED */

	/* Load Lua extension */
	for (const char **s = lua_sources; *s; s++) {
		if (luaL_dostring(L, *s))
			panic("Error loading Lua source %.160s...: %s",
			      *s, lua_tostring(L, -1));
	}

	box_lua_init(L);


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

	/* clear possible left-overs of init */
	lua_settop(L, 0);
	tarantool_L = L;
}

char *history = NULL;

extern "C" const char *
tarantool_error_message(void)
{
	assert(cord()->exception != NULL); /* called only from error handler */
	return cord()->exception->errmsg();
}

/**
 * Execute start-up script.
 */
static void
run_script(va_list ap)
{
	struct lua_State *L = va_arg(ap, struct lua_State *);
	const char *path = va_arg(ap, const char *);

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
	}
	try {
		lbox_call(L, lua_gettop(L) - 1, 0);
	} catch (ClientError *e) {
		panic("%s", e->errmsg());
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
tarantool_lua_run_script(char *path)
{
	const char *title = path ? basename(path) : "interactive";
	/*
	 * init script can call box.fiber.yield (including implicitly via
	 * box.insert, box.update, etc...), but box.fiber.yield() today,
	 * when called from 'sched' fiber crashes the server.
	 * To work this problem around we must run init script in
	 * a separate fiber.
	 */
	struct fiber *loader = fiber_new(title, run_script);
	fiber_call(loader, tarantool_L, path);

	/*
	 * Run an auxiliary event loop to re-schedule run_script fiber.
	 * When this fiber finishes, it will call ev_break to stop the loop.
	 */
	ev_run(loop(), 0);
}

void
tarantool_lua_free()
{
	/*
	 * Got to be done prior to anything else, since GC
	 * handlers can refer to other subsystems (e.g. fibers).
	 */
	if (tarantool_L) {
		/* collects garbage, invoking userdata gc */
		lua_close(tarantool_L);
	}
	tarantool_L = NULL;
}

