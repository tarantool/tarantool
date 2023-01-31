/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "lua/minifio.h"

#include <unistd.h>
#include <lua.h>
#include <lauxlib.h>

#include "diag.h"
#include "lua/utils.h"
#include "lua/error.h"

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

void
tarantool_lua_minifio_init(struct lua_State *L)
{
	static const struct luaL_Reg minifio_methods[] = {
		{"cwd", lbox_minifio_cwd},
		{NULL, NULL}
	};

	luaT_newmodule(L, "internal.minifio", minifio_methods);
	lua_pop(L, 1);
}
