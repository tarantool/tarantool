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
#include "lua/msgpack.h"
#include "box/error.h"
#include "mpstream/mpstream.h"

/**
 * Get key-value pair from Lua stack and set it as a payload field of the
 * `error'. If the field with given key existed before, it is overwritten.
 * The Lua value is encoded to MsgPack. Input stack: [-2] key; [-1] value.
 */
static void
luaT_error_add_payload(lua_State *L, struct error *error)
{
	/* Ignore built-in error fields. */
	const char *key = lua_tostring(L, -2);
	static const char *const ignore_keys[] = {
		"type", "message", "trace", "prev", "base_type",
		"code", "reason", "errno", "custom_type"
	};
	for (size_t i = 0; i < lengthof(ignore_keys); i++) {
		if (strcmp(key, ignore_keys[i]) == 0)
			return;
	}
	struct region *gc = &fiber()->gc;
	size_t used = region_used(gc);
	struct mpstream stream;
	mpstream_init(&stream, gc, region_reserve_cb, region_alloc_cb,
		      luamp_error, L);
	if (luamp_encode(L, luaL_msgpack_default, &stream, -1) != 0) {
		region_truncate(gc, used);
		return;
	}
	mpstream_flush(&stream);
	size_t size = region_used(gc) - used;
	const char *mp_value = (const char *)xregion_join(gc, size);
	error_payload_set_mp(&error->payload, key, mp_value, size);
	region_truncate(gc, used);
}

/**
 * In case the error is constructed from a table, retrieves the reason.
 *
 * @param L Lua state
 * @param index table index on Lua stack
 *
 * @return reason
 * @retval "" failed to retrieve reason
 */
static const char *
error_create_table_case_get_reason(lua_State *L, int index)
{
	lua_rawgeti(L, index, 1);
	const char *reason = lua_tostring(L, -1);
	if (reason != NULL)
		return reason;
	lua_getfield(L, index, "message");
	reason = lua_tostring(L, -1);
	if (reason != NULL)
		return reason;
	lua_getfield(L, index, "reason");
	reason = lua_tostring(L, -1);
	return reason != NULL ? reason : "";
}

/**
 * Parse Lua arguments (they can come as single table or as
 * separate members) and construct struct error with given values.
 *
 * Can be used either the 'code' (numeric) for create a ClientError
 * error with corresponding message (the format is predefined)
 * and type or the 'type' (string) for create a CustomError error
 * with custom type and desired message.
 *
 *     box.error(code, reason args)
 *     box.error({code = num, reason = string, ...})
 *     box.error(type, reason format string, reason args)
 *     box.error({type = string, code = num, reason = string, ...})
 *
 * In case one of arguments is missing its corresponding field
 * in struct error is filled with default value.
 */
static struct error *
luaT_error_create(lua_State *L, int top_base)
{
	uint32_t code = 0;
	const char *custom_type = NULL;
	const char *reason = NULL;
	const char *file = "";
	unsigned line = 0;
	lua_Debug info;
	int top = lua_gettop(L);
	int top_type = lua_type(L, top_base);
	if (top >= top_base && (top_type == LUA_TNUMBER ||
				top_type == LUA_TSTRING)) {
		/* Shift of the "reason args". */
		int shift = 1;
		if (top_type == LUA_TNUMBER) {
			code = lua_tonumber(L, top_base);
			reason = tnt_errcode_desc(code);
		} else {
			custom_type = lua_tostring(L, top_base);
			/*
			 * For the CustomError, the message format
			 * must be set via a function argument.
			 */
			if (lua_type(L, top_base + 1) != LUA_TSTRING)
				return NULL;
			reason = lua_tostring(L, top_base + 1);
			shift = 2;
		}
		if (top > top_base) {
			/* Call string.format(reason, ...) to format message */
			lua_getglobal(L, "string");
			if (lua_isnil(L, -1))
				goto raise;
			lua_getfield(L, -1, "format");
			if (lua_isnil(L, -1))
				goto raise;
			lua_pushstring(L, reason);
			int nargs = 1;
			for (int i = top_base + shift; i <= top; ++i, ++nargs)
				lua_pushvalue(L, i);
			lua_call(L, nargs, 1);
			reason = lua_tostring(L, -1);
		} else if (strchr(reason, '%') != NULL) {
			/* Missing arguments to format string */
			return NULL;
		}
	} else if (top == top_base && top_type == LUA_TTABLE) {
		lua_getfield(L, top_base, "code");
		if (!lua_isnil(L, -1))
			code = lua_tonumber(L, -1);
		reason = error_create_table_case_get_reason(L, top_base);
		lua_getfield(L, top_base, "type");
		if (!lua_isnil(L, -1))
			custom_type = lua_tostring(L, -1);
	} else {
		return NULL;
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
	struct error *error;
	error = box_error_new(file, line, code, custom_type, "%s", reason);
	/*
	 * Add custom payload fields to the `error' if any.
	 */
	if (top_type == LUA_TTABLE) {
		/*
		 * Table is in the stack at index `top', push the first key for
		 * iteration over the table.
		 */
		lua_pushnil(L);
		while (lua_next(L, top) != 0) {
			int key_type = lua_type(L, -2);
			if (key_type == LUA_TSTRING)
				luaT_error_add_payload(L, error);
			/* Remove the value, keep the key for next iteration. */
			lua_pop(L, 1);
		}
		/* Remove the table. */
		lua_pop(L, 1);
	}
	return error;
}

static int
luaT_error_call(lua_State *L)
{
	if (lua_gettop(L) <= 1) {
		/* Re-throw saved exceptions if any. */
		if (box_error_last())
			return luaT_error(L);
		return 0;
	}
	struct error *e = NULL;
	if (lua_gettop(L) == 2) {
		e = luaL_iserror(L, 2);
		if (e != NULL) {
			/* Re-set error to diag area. */
			diag_set_error(&fiber()->diag, e);
			return lua_error(L);
		}
	}
	e = luaT_error_create(L, 2);
	if (e == NULL)
		return luaL_error(L, "box.error(): bad arguments");
	diag_set_error(&fiber()->diag, e);
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
	struct error *e;
	if (lua_gettop(L) == 0 || (e = luaT_error_create(L, 1)) == NULL) {
		return luaL_error(L, "Usage: box.error.new(code, args) or "\
				  "box.error.new(type, args)");
	}
	lua_settop(L, 0);
	luaT_pusherror(L, e);
	return 1;
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
luaT_error_set(struct lua_State *L)
{
	if (lua_gettop(L) == 0)
		return luaL_error(L, "Usage: box.error.set(error)");
	struct error *e = luaL_checkerror(L, 1);
	diag_set_error(&fiber()->diag, e);
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

static int
lbox_errinj_push_value(struct lua_State *L, const struct errinj *e)
{
	switch (e->type) {
	case ERRINJ_BOOL:
		lua_pushboolean(L, e->bparam);
		return 1;
	case ERRINJ_INT:
		luaL_pushint64(L, e->iparam);
		return 1;
	case ERRINJ_DOUBLE:
		lua_pushnumber(L, e->dparam);
		return 1;
	default:
		unreachable();
		return 0;
	}
}

static int
lbox_errinj_get(struct lua_State *L)
{
	char *name = (char*)luaL_checkstring(L, 1);
	struct errinj *e = errinj_by_name(name);
	if (e != NULL)
		return lbox_errinj_push_value(L, e);
	lua_pushfstring(L, "error: can't find error injection '%s'", name);
	return 1;
}

static inline int
lbox_errinj_cb(struct errinj *e, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State*)cb_ctx;
	lua_pushstring(L, e->name);
	lua_newtable(L);
	lua_pushstring(L, "state");
	lbox_errinj_push_value(L, e);
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
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.error", 0);
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
		{
			lua_pushcfunction(L, luaT_error_set);
			lua_setfield(L, -2, "set");
		}
		lua_setfield(L, -2, "__index");
	}
	lua_setmetatable(L, -2);

	lua_pop(L, 1);

	static const struct luaL_Reg errinjlib[] = {
		{"info", lbox_errinj_info},
		{"set", lbox_errinj_set},
		{"get", lbox_errinj_get},
		{NULL, NULL}
	};
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.error.injection", 0);
	luaL_setfuncs(L, errinjlib, 0);
	lua_pop(L, 1);
}
