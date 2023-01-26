/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "lua/tweaks.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdbool.h>
#include <stddef.h>

#include "core/tweaks.h"
#include "diag.h"
#include "lua/utils.h"
#include "trivia/util.h"

/**
 * Pushes a tweak value to Lua stack and returns the number of values pushed.
 */
static int
luaT_push_tweak_value(struct lua_State *L, const struct tweak_value *v)
{
	switch (v->type) {
	case TWEAK_VALUE_BOOL:
		lua_pushboolean(L, v->bval);
		return 1;
	case TWEAK_VALUE_INT:
		lua_pushinteger(L, v->ival);
		return 1;
	case TWEAK_VALUE_STR:
		lua_pushstring(L, v->sval);
		return 1;
	default:
		unreachable();
	}
}

/**
 * Looks up a tweak value by name (2nd arg) and pushes it to Lua stack.
 * Returns nil if there's no such tweak.
 */
static int
luaT_tweaks_index(struct lua_State *L)
{
	const char *name = luaL_checkstring(L, 2);
	struct tweak *t = tweak_find(name);
	if (t == NULL) {
		lua_pushnil(L);
		return 1;
	}
	struct tweak_value v;
	tweak_get(t, &v);
	return luaT_push_tweak_value(L, &v);
}

/**
 * Updates a tweak value given its name (2nd arg) and the new value (3rd arg).
 * Raises an error if there's no such tweak or the value is invalid.
 */
static int
luaT_tweaks_newindex(struct lua_State *L)
{
	const char *name = luaL_checkstring(L, 2);
	struct tweak *t = tweak_find(name);
	if (t == NULL) {
		diag_set(IllegalParams, "No such option");
		return luaT_error(L);
	}
	struct tweak_value v;
	switch (lua_type(L, 3)) {
	case LUA_TBOOLEAN:
		v.type = TWEAK_VALUE_BOOL;
		v.bval = lua_toboolean(L, 3);
		break;
	case LUA_TNUMBER:
		v.type = TWEAK_VALUE_INT;
		v.ival = lua_tointeger(L, 3);
		if (lua_tonumber(L, 3) != v.ival)
			goto invalid_value;
		break;
	case LUA_TSTRING:
		v.type = TWEAK_VALUE_STR;
		v.sval = lua_tostring(L, 3);
		break;
	default:
invalid_value:
		diag_set(IllegalParams,
			 "Value must be boolean, integer, or string");
		return luaT_error(L);
	}
	if (tweak_set(t, &v) != 0)
		return luaT_error(L);
	return 0;
}

static bool
luaT_tweaks_serialize_foreach_cb(const char *name, struct tweak *t, void *arg)
{
	struct lua_State *L = arg;
	struct tweak_value v;
	tweak_get(t, &v);
	luaT_push_tweak_value(L, &v);
	lua_setfield(L, -2, name);
	return true;
}

/**
 * Pushes a table that maps tweak names to their values to Lua stack.
 */
static int
luaT_tweaks_serialize(struct lua_State *L)
{
	lua_newtable(L);
	tweak_foreach(luaT_tweaks_serialize_foreach_cb, L);
	return 1;
}

void
tarantool_lua_tweaks_init(struct lua_State *L)
{
	const struct luaL_Reg module_funcs[] = {
		{NULL, NULL},
	};
	const struct luaL_Reg module_mt_funcs[] = {
		{"__index", luaT_tweaks_index},
		{"__newindex", luaT_tweaks_newindex},
		{"__serialize", luaT_tweaks_serialize},
		{"__autocomplete", luaT_tweaks_serialize},
		{NULL, NULL},
	};
	luaT_newmodule(L, "internal.tweaks", module_funcs);
	lua_newtable(L);
	luaL_setfuncs(L, module_mt_funcs, 0);
	lua_setmetatable(L, -2);
	lua_pop(L, 1);
}
