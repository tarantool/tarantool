/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include "lua/info.h"
#include "info/info.h"
#include "lua/utils.h"

static void
luaT_info_begin(struct info_handler *info)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_newtable(L);
}

static void
luaT_info_end(struct info_handler *info)
{
	(void) info;
}

static void
luaT_info_begin_table(struct info_handler *info, const char *key)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_pushstring(L, key);
	lua_newtable(L);
}

static void
luaT_info_end_table(struct info_handler *info)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_settable(L, -3);
}

static void
luaT_info_append_double(struct info_handler *info,
			const char *key, double value)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_pushstring(L, key);
	lua_pushnumber(L, value);
	lua_settable(L, -3);
}

static void
luaT_info_append_int(struct info_handler *info, const char *key,
		     int64_t value)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_pushstring(L, key);
	luaL_pushint64(L, value);
	lua_settable(L, -3);
}

static void
luaT_info_append_str(struct info_handler *info, const char *key,
		   const char *value)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_pushstring(L, key);
	lua_pushstring(L, value);
	lua_settable(L, -3);
}

void
luaT_info_handler_create(struct info_handler *h, struct lua_State *L)
{
	static struct info_handler_vtab lua_vtab = {
		.begin = luaT_info_begin,
		.end = luaT_info_end,
		.begin_table = luaT_info_begin_table,
		.end_table = luaT_info_end_table,
		.append_int = luaT_info_append_int,
		.append_str = luaT_info_append_str,
		.append_double = luaT_info_append_double
	};
	h->vtab = &lua_vtab;
	h->ctx = L;
}
