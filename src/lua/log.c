/*
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
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
#include "log.h"

#include "lua/serializer.h"
#include "lua/utils.h"
#include "trivia/util.h"

#include <lua.h>
#include <datetime.h>
#include <fiber.h>

static const char *loglib_name = "log";

/*
 * Return the default context for the logger.
 *
 * The function returns a table with the following fields:
 * - module: the name of the module which calls the function;
 *   if the module name is not provided or not a string, the field is absent;
 *   if the module name is provided, the field is a string;
 *
 * - time: the current time as a datetime object;
 * - pid: the process ID;
 * - cord_name: the name of the current coroutine;
 * - fiber_id: the ID of the current fiber;
 * - fiber_name: the name of the current fiber;
 */
int
log_get_default_context(struct lua_State *L)
{
	assert(lua_gettop(L) < 1 || lua_istable(L, 1));

	char *module_name = NULL;
	if (lua_gettop(L) == 1) {
		lua_getfield(L, 1, "name");
		if (lua_type(L, -1) == LUA_TSTRING) {
			module_name = (char *)lua_tostring(L, -1);
		}
		lua_pop(L, 2);
	}

	struct datetime tm;
	datetime_now(&tm);

	lua_createtable(L, 0, 1);

	if (module_name != NULL) {
		lua_pushstring(L, module_name);
		lua_setfield(L, -2, "module");
	}

	luaT_pushdatetime(L, &tm);
	lua_setfield(L, -2, "time");

	lua_pushinteger(L, getpid());
	lua_setfield(L, -2, "pid");

	lua_pushstring(L, cord()->name);
	lua_setfield(L, -2, "cord_name");

	lua_pushinteger(L, fiber()->fid);
	lua_setfield(L, -2, "fiber_id");

	lua_pushstring(L, fiber()->name);
	lua_setfield(L, -2, "fiber_name");

	luaL_setmaphint(L, -1);

	return 1;
}

/*
 * Methods for the log module.
 */
static const struct luaL_Reg loglib[] = {
	{NULL, NULL}
};

/*
 * Internal methods for the log module.
 */
static const struct luaL_Reg loglib_internal[] = {
	{"default_context", log_get_default_context},
	{NULL, NULL}
};

/*
 * Initialize methods for the log module.
 */
void
tarantool_lua_log_init(struct lua_State *L)
{
	luaT_newmodule(L, loglib_name, loglib);

	lua_pushstring(L, "_internal");
	lua_newtable(L);
	for (const luaL_Reg *r = loglib_internal; r->name; r++) {
		lua_pushcfunction(L, r->func);
		lua_setfield(L, -2, r->name);
	}
	lua_settable(L, -3);

	lua_pop(L, 1);
}
