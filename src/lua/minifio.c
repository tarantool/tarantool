/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "lua/minifio.h"

#include <libgen.h>
#include <string.h>
#include <unistd.h>
#include <lua.h>
#include <lauxlib.h>

#include "trivia/util.h"
#include "diag.h"
#include "lua/utils.h"
#include "lua/error.h"

static char *main_script = NULL;

char *
minifio_dirname(const char *path, char *buf)
{
	char *result;
#ifdef __APPLE__
	result = dirname_r(path, buf);
#else
	/* dirname() may modify its argument. */
	memcpy(buf, path, strlen(path) + 1);
	result = dirname(buf);
#endif
	if (result != buf)
		memmove(buf, result, strlen(result) + 1);
	return buf;
}

void
minifio_set_script(const char *script)
{
	if (script == NULL) {
		free(main_script);
		main_script = NULL;
		return;
	}
	size_t len = strlen(script);
	main_script = xrealloc(main_script, len + 1);
	memcpy(main_script, script, len + 1);
}

/**
 * Push nil and an error with strerror() based message.
 */
static int
luaT_minifio_pushsyserror(struct lua_State *L)
{
	/*
	 * It is used in functions exposed into the fio module,
	 * so use "fio" diagnostics instead of "minifio".
	 */
	diag_set(SystemError, "fio");
	return luaT_push_nil_and_error(L);
}

/**
 * minifio.cwd() -- get current working directory.
 */
static int
lbox_minifio_cwd(struct lua_State *L)
{
	char *buf = (char *)lua_newuserdata(L, PATH_MAX);
	if (!buf) {
		errno = ENOMEM;
		return luaT_minifio_pushsyserror(L);
	}
	if (getcwd(buf, PATH_MAX) == NULL)
		return luaT_minifio_pushsyserror(L);
	lua_pushstring(L, buf);
	lua_remove(L, -2);
	return 1;
}

/**
 * minifio.dirname() -- get the directory part of a path.
 */
static int
lbox_minifio_dirname(struct lua_State *L)
{
	if (lua_type(L, 1) != LUA_TSTRING) {
		lua_pushliteral(L, "Usage: minifio.dirname(path)");
		return lua_error(L);
	}
	size_t len;
	const char *path = lua_tolstring(L, 1, &len);
	char *buf = (char *)lua_newuserdata(L, len + 2);
	lua_pushstring(L, minifio_dirname(path, buf));
	return 1;
}

/**
 * minifio.script() -- get path of the main script.
 *
 * Important: the path is returned verbatim as provided in the
 * process'es arguments and should be interpreted relatively to
 * current working directory *at tarantool startup*.
 *
 * The current working directory may be changed later and it'll
 * make the path value invalid. Note that the directory may be
 * changed implicitly by calling box.cfg().
 */
static int
lbox_minifio_script(struct lua_State *L)
{
	if (main_script == NULL) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushstring(L, main_script);
	return 1;
}

void
tarantool_lua_minifio_init(struct lua_State *L)
{
	static const struct luaL_Reg minifio_methods[] = {
		{"cwd", lbox_minifio_cwd},
		{"dirname", lbox_minifio_dirname},
		{"script", lbox_minifio_script},
		{NULL, NULL}
	};

	/*
	 * The loaders.builtin loader is not in effect yet.
	 * Set the minifio module into package.loaded manually.
	 */
	lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
	luaT_newmodule(L, "internal.minifio", minifio_methods);
	lua_setfield(L, -2, "internal.minifio");
	lua_pop(L, 1); /* _LOADED */
}
