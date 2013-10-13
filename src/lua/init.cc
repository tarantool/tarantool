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
#include "lua/plugin.h"
#include "tarantool.h"
#include "box/box.h"
#include "tbuf.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lj_obj.h>
#include <lj_ctype.h>
#include <lj_cdata.h>
#include <lj_cconv.h>
#include <lj_state.h>
} /* extern "C" */

#include <sys/types.h>
#include <dirent.h>

#include "fiber.h"
#include "lua/fiber.h"
#include "lua/admin.h"
#include "lua/errinj.h"
#include "lua/ipc.h"
#include "lua/socket.h"
#include "lua/info.h"
#include "lua/slab.h"
#include "lua/stat.h"
#include "lua/session.h"
#include "lua/cjson.h"
#include "lua/yaml.h"

#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <dirent.h>
#include <stdio.h>
#include "tarantool/plugin.h"

static RLIST_HEAD(loaded_plugins);

extern "C" {
#include <cfg/tarantool_box_cfg.h>
#include <cfg/warning.h>
} /* extern "C" */

/**
 * tarantool start-up file
 */
#define TARANTOOL_LUA_INIT_SCRIPT "init.lua"

struct lua_State *tarantool_L;

/* contents of src/lua/ files */
extern char uuid_lua[];
static const char *lua_sources[] = { uuid_lua, NULL };

/**
 * Remember the output of the administrative console in the
 * registry, to use with 'print'.
 */
void
tarantool_lua_set_out(struct lua_State *L, const struct tbuf *out)
{
	lua_pushthread(L);
	if (out)
		lua_pushlightuserdata(L, (void *) out);
	else
		lua_pushnil(L);
	lua_settable(L, LUA_REGISTRYINDEX);
}

/**
 * dup out from parent to child L. Used in fiber_create
 */
void
tarantool_lua_dup_out(struct lua_State *L, struct lua_State *child_L)
{
	lua_pushthread(L);
	lua_gettable(L, LUA_REGISTRYINDEX);
	struct tbuf *out = (struct tbuf *) lua_topointer(L, -1);
	/* pop 'out' */
	lua_pop(L, 1);
	if (out)
		tarantool_lua_set_out(child_L, out);
}


/*
 * {{{ box Lua library: common functions
 */

const char *boxlib_name = "box";

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
		/* Calculate absolute value in the stack. */
		if (idx < 0)
			idx = lua_gettop(L) + idx + 1;
		GCcdata *cd = cdataV(L->base + idx - 1);
		if (cd->ctypeid != CTID_INT64 && cd->ctypeid != CTID_UINT64) {
			luaL_error(L,
				   "lua_tointeger64: unsupported cdata type");
		}
		result = *(uint64_t*)cdataptr(cd);
		break;
	}
	default:
		luaL_error(L, "lua_tointeger64: unsupported type: %s",
			   lua_typename(L, lua_type(L, idx)));
	}

	return result;
}

static GCcdata*
luaL_pushcdata(struct lua_State *L, CTypeID id, int bits)
{
	CTState *cts = ctype_cts(L);
	CType *ct = ctype_raw(cts, id);
	CTSize sz;
	lj_ctype_info(cts, id, &sz);
	GCcdata *cd = lj_cdata_new(cts, id, bits);
	TValue *o = L->top;
	setcdataV(L, o, cd);
	lj_cconv_ct_init(cts, ct, sz, (uint8_t *) cdataptr(cd), o, 0);
	incr_top(L);
	return cd;
}

int
luaL_pushnumber64(struct lua_State *L, uint64_t val)
{
	GCcdata *cd = luaL_pushcdata(L, CTID_UINT64, 8);
	*(uint64_t*)cdataptr(cd) = val;
	return 1;
}

/** Report libev time (cheap). */
static int
lbox_time(struct lua_State *L)
{
	lua_pushnumber(L, ev_now());
	return 1;
}

/** Report libev time as 64-bit integer */
static int
lbox_time64(struct lua_State *L)
{
	luaL_pushnumber64(L, (uint64_t) ( ev_now() * 1000000 + 0.5 ) );
	return 1;
}

/**
 * descriptor for box methods
 */
static const struct luaL_reg boxlib[] = {
	{"time", lbox_time},
	{"time64", lbox_time64},
	{NULL, NULL}
};

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
 * Redefine lua 'print' built-in to print either to the log file
 * (when Lua is used inside a module) or back to the user (for the
 * administrative console).
 *
 * When printing to the log file, we use 'say_info'.  To print to
 * the administrative console, we simply append everything to the
 * 'out' buffer, which is flushed to network at the end of every
 * administrative command.
 *
 * Note: administrative console output must be YAML-compatible.
 * If this is done automatically, the result is ugly, so we
 * don't do it. Creators of Lua procedures have to do it
 * themselves. Best we can do here is to add a trailing
 * CRLF if it's forgotten.
 */
static int
lbox_print(struct lua_State *L)
{
	lua_pushthread(L);
	lua_gettable(L, LUA_REGISTRYINDEX);
	struct tbuf *out = (struct tbuf *) lua_topointer(L, -1);
	/* pop 'out' */
	lua_pop(L, 1);
	/* always output to log only */
	out = tbuf_new(&fiber->gc);
	/* serialize arguments of 'print' Lua built-in to tbuf */
	int top = lua_gettop(L);
	for (int i = 1; i <= top; i++) {
		if (lua_type(L, i) == LUA_TCDATA) {
			GCcdata *cd = cdataV(L->base + i - 1);
			const char *sz = tarantool_lua_tostring(L, i);
			int len = strlen(sz);
			int chop = (cd->ctypeid == CTID_UINT64 ? 3 : 2);
			tbuf_printf(out, "%-.*s" CRLF, len - chop, sz);
		} else
			tbuf_printf(out, "%s", tarantool_lua_tostring(L, i));
	}
	say_info("%s", tbuf_str(out));
	return 0;
}

/**
 * Redefine lua 'pcall' built-in to correctly handle exceptions,
 * produced by 'box' C functions.
 *
 * See Lua documentation on 'pcall' for additional information.
 */
static int
lbox_pcall(struct lua_State *L)
{
	/*
	 * Lua pcall() returns true/false for completion status
	 * plus whatever the called function returns.
	 */
	try {
		lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
		/* push completion status */
		lua_pushboolean(L, true);
		/* move 'true' to stack start */
		lua_insert(L, 1);
	} catch (const ClientError& e) {
		/*
		 * Note: FiberCancelException passes through this
		 * catch and thus leaves garbage on coroutine
		 * stack.
		 */
		/* pop any possible garbage */
		lua_settop(L, 0);
		/* completion status */
		lua_pushboolean(L, false);
		/* error message */
		lua_pushstring(L, e.errmsg());
	} catch (const Exception& e) {
		throw;
	} catch (...) {
		lua_settop(L, 1);
		/* completion status */
		lua_pushboolean(L, false);
		/* put the completion status below the error message. */
		lua_insert(L, -2);
	}
	return lua_gettop(L);
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
	return luaL_pushnumber64(L, result);
}

/**
 * A helper to register a single type metatable.
 */
void
tarantool_lua_register_type(struct lua_State *L, const char *type_name,
			    const struct luaL_Reg *methods)
{
	luaL_newmetatable(L, type_name);
	/*
	 * Conventionally, make the metatable point to itself
	 * in __index. If 'methods' contain a field for __index,
	 * this is a no-op.
	 */
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushstring(L, type_name);
	lua_setfield(L, -2, "__metatable");
	luaL_register(L, NULL, methods);
	lua_pop(L, 1);
}

static const struct luaL_reg errorlib [] = {
	{NULL, NULL}
};

static void
tarantool_lua_error_init(struct lua_State *L) {
	luaL_register(L, "box.error", errorlib);
	for (int i = 0; i < tnt_error_codes_enum_MAX; i++) {
		const char *name = tnt_error_codes[i].errstr;
		if (strstr(name, "UNUSED") || strstr(name, "RESERVED"))
			continue;
		lua_pushnumber(L, tnt_errcode_val(i));
		lua_setfield(L, -2, name);
	}
	lua_pop(L, 1);
}

static void
tarantool_lua_setpath(struct lua_State *L, const char *type, ...)
__attribute__((sentinel));

/**
 * Prepend the variable list of arguments to the Lua
 * package search path (or cpath, as defined in 'type').
 */
static void
tarantool_lua_setpath(struct lua_State *L, const char *type, ...)
{
	char path[PATH_MAX];
	va_list args;
	va_start(args, type);
	int off = 0;
	const char *p;
	while ((p = va_arg(args, const char*))) {
		/*
		 * If LUA_SYSPATH or LUA_SYSCPATH is an empty
		 * string, skip it.
		 */
		if (*p == '\0')
			continue;
		off += snprintf(path + off, sizeof(path) - off, "%s;", p);
	}
	va_end(args);
	lua_getglobal(L, "package");
	lua_getfield(L, -1, type);
	snprintf(path + off, sizeof(path) - off, "%s",
	         lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_pushstring(L, path);
	lua_setfield(L, -2, type);
	lua_pop(L, 1);
}

/**
 * show statistics for all loaded plugins
 */
int
plugin_stat(tarantool_plugin_stat_cb cb, void *cb_ctx)
{
	int res;
	struct tarantool_plugin *p;
	rlist_foreach_entry(p, &loaded_plugins, list) {
		res = cb(p, cb_ctx);
		if (res != 0)
			return res;
	}
	return 0;
}

static void
tarantool_load_plugin(struct lua_State *L, const char *plugin)
{
	if (strstr(plugin, ".so") == NULL)
		return;

	say_info("Loading plugin: %s", plugin);

	void *dl = dlopen(plugin, RTLD_NOW);
	if (!dl) {
		say_error("Can't load plugin %s: %s", plugin, dlerror());
		return;
	}

	struct tarantool_plugin *p = (typeof(p))dlsym(dl, "plugin_meta");

	if (!p) {
		say_error("Can't find plugin metadata in plugin %s", plugin);
		dlclose(dl);
		return;
	}

	if (p->api_version != PLUGIN_API_VERSION) {
		say_error("Plugin %s has api_version: %d but tarantool has: %d",
			plugin,
			p->api_version,
			PLUGIN_API_VERSION);
		return;
	}

	rlist_add_entry(&loaded_plugins, p, list);

	if (p->init)
		p->init(L);

	say_info("Plugin '%s' was loaded, version: %d", p->name, p->version);
}

/** Load all plugins in a plugin dir. */
static void
tarantool_plugin_dir(struct lua_State *L, const char *dir)
{
	if (!dir)
		return;
	if (!*dir)
		return;
	DIR *dh = opendir(dir);

	if (!dh)
		return;

	struct dirent *dent;
	while ((dent = readdir(dh)) != NULL) {
		if (dent->d_type != DT_REG)
			continue;
		char *path;
		(void) asprintf(&path, "%s/%s", dir, dent->d_name);
		if (!path) {
			say_error("Can't allocate memory for %s plugin dir",
				 dir);
			continue;
		}

		tarantool_load_plugin(L, path);

		free(path);
	}

	closedir(dh);
}

static void
tarantool_plugin_init(struct lua_State *L)
{
	int top = lua_gettop(L);

	char *plugins = getenv("TARANTOOL_PLUGIN_DIR");

	if (plugins) {
		plugins = strdup(plugins);
		char *ptr = plugins;
		for (;;) {
			char *divider = strchr(ptr, ':');
			if (divider == NULL) {
				tarantool_plugin_dir(L, ptr);
				break;
			}
			*divider = 0;
			tarantool_plugin_dir(L, ptr);
			ptr = divider + 1;
		}
		free(plugins);
	}

	tarantool_plugin_dir(L, PLUGIN_DIR);

	lua_settop(L, top);
}

struct lua_State *
tarantool_lua_init()
{
	lua_State *L = luaL_newstate();
	if (L == NULL)
		return L;
	luaL_openlibs(L);
	/*
	 * Search for Lua modules, apart from the standard
	 * locations, in the server script_dir and in the
	 * system-wide Tarantool paths. This way 3 types
	 * of packages become available for use: standard Lua
	 * packages, Tarantool-specific Lua libs and
	 * instance-specific Lua scripts.
	 */

	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/?.lua", cfg.script_dir);
	tarantool_lua_setpath(L, "path", path, LUA_LIBPATH,
	                      LUA_SYSPATH, NULL);
	snprintf(path, sizeof(path), "%s/?.so", cfg.script_dir);
	tarantool_lua_setpath(L, "cpath", path, LUA_LIBCPATH,
	                      LUA_SYSCPATH, NULL);

	/* Load 'ffi' extension and make it inaccessible */
	lua_getglobal(L, "require");
	lua_pushstring(L, "ffi");
	if (lua_pcall(L, 1, 0, 0) != 0)
		panic("%s", lua_tostring(L, -1));
	lua_getglobal(L, "ffi");
	/**
	 * Remember the LuaJIT FFI extension reference index
	 * to protect it from being garbage collected.
	 */
	(void) luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushnil(L);
	lua_setglobal(L, "ffi");
	luaL_register(L, boxlib_name, boxlib);
	lua_pop(L, 1);

	lua_register(L, "print", lbox_print);
	lua_register(L, "pcall", lbox_pcall);
	lua_register(L, "tonumber64", lbox_tonumber64);

	tarantool_lua_errinj_init(L);
	tarantool_lua_fiber_init(L);
	tarantool_lua_admin_init(L);
	tarantool_lua_plugin_init(L);
	tarantool_lua_cjson_init(L);
	tarantool_lua_yaml_init(L);
	tarantool_lua_info_init(L);
	tarantool_lua_slab_init(L);
	tarantool_lua_stat_init(L);
	tarantool_lua_ipc_init(L);
	tarantool_lua_socket_init(L);
	tarantool_lua_session_init(L);
	tarantool_lua_error_init(L);

	/* Load Lua extension */
	for (const char **s = lua_sources; *s; s++) {
		if (luaL_dostring(L, *s))
			panic("Error loading Lua source %.160s...: %s",
			      *s, lua_tostring(L, -1));
	}

	mod_lua_init(L);

	/* init after internal luas are processed */
	tarantool_plugin_init(L);

	/* clear possible left-overs of init */
	lua_settop(L, 0);
	return L;
}

void
tarantool_lua_close(struct lua_State *L)
{
	/* collects garbage, invoking userdata gc */
	lua_close(L);
}

/**
 * Attempt to append 'return ' before the chunk: if the chunk is
 * an expression, this pushes results of the expression onto the
 * stack. If the chunk is a statement, it won't compile. In that
 * case try to run the original string.
 */
static int
tarantool_lua_dostring(struct lua_State *L, const char *str)
{
	struct tbuf *buf = tbuf_new(&fiber->gc);
	tbuf_printf(buf, "%s%s", "return ", str);
	int r = luaL_loadstring(L, tbuf_str(buf));
	if (r) {
		/* pop the error message */
		lua_pop(L, 1);
		r = luaL_loadstring(L, str);
		if (r)
			return r;
	}
	try {
		lua_call(L, 0, LUA_MULTRET);
	} catch (const FiberCancelException& e) {
		throw;
	} catch (const Exception& e) {
		lua_settop(L, 0);
		lua_pushstring(L, e.errmsg());
		return 1;
	} catch (...) {
		return 1;
	}
	return 0;
}

static int
tarantool_lua_dofile(struct lua_State *L, const char *filename)
{
	lua_getglobal(L, "dofile");
	lua_pushstring(L, filename);
	lbox_pcall(L);
	bool result = lua_toboolean(L, 1);
	return result ? 0 : 1;
}

extern "C" {
	int yamlL_encode(lua_State*);
};

void
tarantool_lua(struct lua_State *L,
              struct tbuf *out, const char *str)
{
	tarantool_lua_set_out(L, out);
	int r = tarantool_lua_dostring(L, str);
	tarantool_lua_set_out(L, NULL);
	if (r) {
		assert(lua_gettop(L) == 1);
		const char *msg = lua_tostring(L, -1);
		msg = msg ? msg : "";
		lua_newtable(L);
		lua_pushstring(L, "error");
		lua_pushstring(L, msg);
		lua_settable(L, -3);
		lua_replace(L, 1);
		assert(lua_gettop(L) == 1);
	}
	/* Convert Lua stack to YAML and append to the given tbuf */
	int top = lua_gettop(L);
	if (top == 0) {
		tbuf_printf(out, "---\n...\n");
		lua_settop(L, 0);
		return;
	}

	lua_newtable(L);
	for (int i = 1; i <= top; i++) {
		lua_pushnumber(L, i);
		if (lua_isnil(L, i)) {
			/**
			 * When storing a nil in a Lua table,
			 * there is no way to distinguish nil
			 * value from no value. This is a trick
			 * to make sure yaml converter correctly
			 * outputs nil values on the return stack.
			 */
			lua_pushlightuserdata(L, NULL);
		} else {
			lua_pushvalue(L, i);
		}
		lua_rawset(L, -3);
	}
	lua_replace(L, 1);
	lua_settop(L, 1);

	yamlL_encode(L);
	lua_replace(L, 1);
	lua_pop(L, 1);
	tbuf_printf(out, "%s", lua_tostring(L, 1));
	lua_settop(L, 0);
}

/**
 * Check if the given literal is a number/boolean or string
 * literal. A string literal needs quotes.
 */
static bool
is_string(const char *str)
{
	if (strcmp(str, "true") == 0 || strcmp(str, "false") == 0)
	    return false;
	if (! isdigit(*str))
	    return true;
	char *endptr;
	double r = strtod(str, &endptr);
	/* -Wunused-result warning suppression */
	(void) r;
	return *endptr != '\0';
}

static int
lbox_cfg_reload(struct lua_State *L)
{
	if (reload_cfg())
		luaL_error(L, cfg_log);
	lua_pushstring(L, "ok");
	return 1;
}

/**
 * Make a new configuration available in Lua.
 * We could perhaps make Lua bindings to access the C
 * structure in question, but for now it's easier and just
 * as functional to convert the given configuration to a Lua
 * table and export the table into Lua.
 */
void
tarantool_lua_load_cfg(struct lua_State *L, struct tarantool_cfg *cfg)
{
	luaL_Buffer b;
	char *key, *value;

	luaL_buffinit(L, &b);
	tarantool_cfg_iterator_t *i = tarantool_cfg_iterator_init();
	luaL_addstring(&b,
		       "box.cfg = {}\n"
		       "setmetatable(box.cfg, {})\n"
		       "getmetatable(box.cfg).__index = "
		       "function(table, index)\n"
		       "  table[index] = {}\n"
		       "  setmetatable(table[index], getmetatable(table))\n"
		       "  return rawget(table, index)\n"
		       "end\n"
		       "getmetatable(box.cfg).__call = "
		       "function(table, index)\n"
		       "  local t = {}\n"
		       "  for i, v in pairs(table) do\n"
		       "    if type(v) ~= 'function' then\n"
		       "      t[i] = v\n"
		       "    end\n"
		       "  end\n"
		       "  return t\n"
		       "end\n");
	while ((key = tarantool_cfg_iterator_next(i, cfg, &value)) != NULL) {
		if (value == NULL)
			continue;
		const char *quote = is_string(value) ? "'" : "";
		if (strchr(key, '.') == NULL) {
			lua_pushfstring(L, "box.cfg.%s = %s%s%s\n",
					key, quote, value, quote);
			luaL_addvalue(&b);
		}
		free(value);
	}
	luaL_pushresult(&b);
	if (luaL_loadstring(L, lua_tostring(L, -1)) != 0 ||
	    lua_pcall(L, 0, 0, 0) != 0) {
		panic("%s", lua_tostring(L, -1));
	}
	lua_pop(L, 1);

	/* add box.cfg.reload() function */
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "cfg");
	lua_gettable(L, -2);
	lua_pushstring(L, "reload");
	lua_pushcfunction(L, lbox_cfg_reload);
	lua_settable(L, -3);
	lua_pop(L, 2);

	/* make box.cfg read-only */
	luaL_buffinit(L, &b);
	luaL_addstring(&b,
		       "getmetatable(box.cfg).__newindex = "
		       "function(table, index)\n"
		       "  error('Attempt to modify a read-only table')\n"
		       "end\n"
		       "getmetatable(box.cfg).__index = nil\n");
	luaL_pushresult(&b);
	if (luaL_loadstring(L, lua_tostring(L, -1)) != 0 ||
	    lua_pcall(L, 0, 0, 0) != 0) {
		panic("%s", lua_tostring(L, -1));
	}
	lua_pop(L, 1);

	/*
	 * Invoke a user-defined on_reload_configuration hook,
	 * if it exists. Do it after everything else is done.
	 */
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "on_reload_configuration");
	lua_gettable(L, -2);
	if (lua_isfunction(L, -1) && lua_pcall(L, 0, 0, 0) != 0) {
		say_error("on_reload_configuration() hook failed: %s",
			  lua_tostring(L, -1));
	}
	lua_pop(L, 1);	/* cleanup stack */
}

/**
 * Load start-up file routine.
 */
static void
load_init_script(va_list ap)
{
	struct lua_State *L = va_arg(ap, struct lua_State *);

	/*
	 * Return control to tarantool_lua_load_init_script.
	 * tarantool_lua_load_init_script when will start an auxiliary event
	 * loop and re-schedule this fiber.
	 */
	fiber_sleep(0.0);

	char path[PATH_MAX + 1];
	snprintf(path, PATH_MAX, "%s/%s",
		 cfg.script_dir, TARANTOOL_LUA_INIT_SCRIPT);

	if (access(path, F_OK) == 0) {
		say_info("loading %s", path);
		/* Execute the init file. */
		if (tarantool_lua_dofile(L, path))
			panic("%s", lua_tostring(L, -1));

		/* clear the stack from return values. */
		lua_settop(L, 0);
	}
	/*
	 * The file doesn't exist. It's OK, tarantool may
	 * have no init file.
	 */

	/*
	 * Lua script finished. Stop the auxiliary event loop and
	 * return control back to tarantool_lua_load_init_script.
	 */
	ev_break(EVBREAK_ALL);
}

/**
 * Unset functions in the Lua state which can be used to
 * execute external programs or otherwise introduce a breach
 * in security.
 *
 * @param L is a Lua State.
 */
static void
tarantool_lua_sandbox(struct lua_State *L)
{
	/*
	 * Unset some functions for security reasons:
	 * 1. Some os.* functions (like os.execute, os.exit, etc..)
	 * 2. require(), since it can be used to provide access to ffi
	 * or anything else we unset in 1.
	 */
	int result = tarantool_lua_dostring(L,
					    "os.execute = nil\n"
					    "os.exit = nil\n"
					    "os.rename = nil\n"
					    "os.tmpname = nil\n"
					    "os.remove = nil\n"
					    "io = nil\n"
					    "require = nil\n");
	if (result)
		panic("%s", lua_tostring(L, -1));
}

void
tarantool_lua_load_init_script(struct lua_State *L)
{
	/*
	 * init script can call box.fiber.yield (including implicitly via
	 * box.insert, box.update, etc...), but box.fiber.yield() today,
	 * when called from 'sched' fiber crashes the server.
	 * To work this problem around we must run init script in
	 * a separate fiber.
	 */
	struct fiber *loader = fiber_new(TARANTOOL_LUA_INIT_SCRIPT,
					 load_init_script);
	fiber_call(loader, L);

	/*
	 * Run an auxiliary event loop to re-schedule load_init_script fiber.
	 * When this fiber finishes, it will call ev_break to stop the loop.
	 */
	ev_run(0);

	/* Outside the startup file require() or ffi are not
	 * allowed.
	*/
	tarantool_lua_sandbox(tarantool_L);
}

void *
lua_region_alloc(void *ctx, size_t size)
{
	struct lua_State *L = (struct lua_State *) ctx;
	return lua_newuserdata(L, size);
}
