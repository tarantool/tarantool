/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/lua/error.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
} /* extern "C" */

#include <fiber.h>
#include <errinj.h>

#include "lua/utils.h"
#include "box/error.h"

static void
luaT_error_create(lua_State *L, int top_base)
{
	uint32_t code = 0;
	const char *reason = NULL;
	const char *file = "";
	unsigned line = 0;
	lua_Debug info;
	int top = lua_gettop(L);
	if (top >= top_base && lua_type(L, top_base) == LUA_TNUMBER) {
		code = lua_tonumber(L, top_base);
		reason = tnt_errcode_desc(code);
		if (top > top_base) {
			/* Call string.format(reason, ...) to format message */
			lua_getglobal(L, "string");
			if (lua_isnil(L, -1))
				goto raise;
			lua_getfield(L, -1, "format");
			if (lua_isnil(L, -1))
				goto raise;
			lua_pushstring(L, reason);
			for (int i = top_base + 1; i <= top; i++)
				lua_pushvalue(L, i);
			lua_call(L, top - top_base + 1, 1);
			reason = lua_tostring(L, -1);
		} else if (strchr(reason, '%') != NULL) {
			/* Missing arguments to format string */
			luaL_error(L, "box.error(): bad arguments");
		}
	} else if (top == top_base) {
		if (lua_istable(L, top_base)) {
			/* A special case that rethrows raw error (used by net.box) */
			lua_getfield(L, top_base, "code");
			code = lua_tonumber(L, -1);
			lua_pop(L, 1);
			lua_getfield(L, top_base, "reason");
			reason = lua_tostring(L, -1);
			if (reason == NULL)
				reason = "";
			lua_pop(L, 1);
		} else if (luaL_iserror(L, top_base)) {
			lua_error(L);
			return;
		}
	} else {
		luaL_error(L, "box.error(): bad arguments");
	}

raise:
	if (lua_getstack(L, 1, &info) && lua_getinfo(L, "Sl", &info)) {
		if (*info.short_src) {
			file = info.short_src;
		} else if (*info.source) {
			file = info.source;
		} else {
			file = "eval";
		}
		line = info.currentline;
	}
	say_debug("box.error() at %s:%i", file, line);
	box_error_set(file, line, code, "%s", reason);
}

static int
luaT_error_call(lua_State *L)
{
	if (lua_gettop(L) <= 1) {
		/* Re-throw saved exceptions if any. */
		if (box_error_last())
			luaT_error(L);
		return 0;
	}
	luaT_error_create(L, 2);
	return luaT_error(L);
}

static int
luaT_error_last(lua_State *L)
{
	if (lua_gettop(L) >= 1)
		luaL_error(L, "box.error.last(): bad arguments");

	struct error *e = box_error_last();
	if (e == NULL) {
		lua_pushnil(L);
		return 1;
	}

	luaT_pusherror(L, e);
	return 1;
}

static int
luaT_error_new(lua_State *L)
{
	if (lua_gettop(L) == 0)
		return luaL_error(L, "Usage: box.error.new(code, args)");
	luaT_error_create(L, 1);
	lua_settop(L, 0);
	return luaT_error_last(L);
}

static int
luaT_error_clear(lua_State *L)
{
	if (lua_gettop(L) >= 1)
		luaL_error(L, "box.error.clear(): bad arguments");

	box_error_clear();
	return 0;
}

static int
lbox_errinj_set(struct lua_State *L)
{
	char *name = (char*)luaL_checkstring(L, 1);
	struct errinj *errinj;
	errinj = errinj_by_name(name);
	if (errinj == NULL) {
		say_error("%s", name);
		lua_pushfstring(L, "error: can't find error injection '%s'", name);
		return 1;
	}
	switch (errinj->type) {
	case ERRINJ_BOOL:
		errinj->bparam = lua_toboolean(L, 2);
		break;
	case ERRINJ_INT:
		errinj->iparam = luaL_checkint64(L, 2);
		break;
	case ERRINJ_DOUBLE:
		errinj->dparam = lua_tonumber(L, 2);
		break;
	default:
		lua_pushfstring(L, "error: unknown injection type '%s'", name);
		return 1;
	}

	lua_pushstring(L, "ok");
	return 1;
}

static inline int
lbox_errinj_cb(struct errinj *e, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State*)cb_ctx;
	lua_pushstring(L, e->name);
	lua_newtable(L);
	lua_pushstring(L, "state");
	switch (e->type) {
	case ERRINJ_BOOL:
		lua_pushboolean(L, e->bparam);
		break;
	case ERRINJ_INT:
		luaL_pushint64(L, e->iparam);
		break;
	case ERRINJ_DOUBLE:
		lua_pushnumber(L, e->dparam);
		break;
	default:
		unreachable();
	}
	lua_settable(L, -3);
	lua_settable(L, -3);
	return 0;
}

static int
lbox_errinj_info(struct lua_State *L)
{
	lua_newtable(L);
	errinj_foreach(lbox_errinj_cb, L);
	return 1;
}

void
box_lua_error_init(struct lua_State *L) {
	static const struct luaL_Reg errorlib[] = {
		{NULL, NULL}
	};
	luaL_register_module(L, "box.error", errorlib);
	for (int i = 0; i < box_error_code_MAX; i++) {
		const char *name = box_error_codes[i].errstr;
		if (strstr(name, "UNUSED") || strstr(name, "RESERVED"))
			continue;
		assert(strncmp(name, "ER_", 3) == 0);
		lua_pushnumber(L, i);
		/* cut ER_ prefix from constant */
		lua_setfield(L, -2, name + 3);
	}
	lua_newtable(L);
	{
		lua_pushcfunction(L, luaT_error_call);
		lua_setfield(L, -2, "__call");

		lua_newtable(L);
		{
			lua_pushcfunction(L, luaT_error_last);
			lua_setfield(L, -2, "last");
		}
		{
			lua_pushcfunction(L, luaT_error_clear);
			lua_setfield(L, -2, "clear");
		}
		{
			lua_pushcfunction(L, luaT_error_new);
			lua_setfield(L, -2, "new");
		}
		lua_setfield(L, -2, "__index");
	}
	lua_setmetatable(L, -2);

	lua_pop(L, 1);

	static const struct luaL_Reg errinjlib[] = {
		{"info", lbox_errinj_info},
		{"set", lbox_errinj_set},
		{NULL, NULL}
	};
	/* box.error.injection is not set by register_module */
	luaL_register_module(L, "box.error.injection", errinjlib);
	lua_pop(L, 1);
}
