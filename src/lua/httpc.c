/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * Unique name for userdata metatables
 */
#define DRIVER_LUA_UDATA_NAME	"httpc"

#include <http_parser.h>
#include "src/httpc.h"
#include "say.h"
#include "lua/utils.h"
#include "lua/httpc.h"
#include "src/fiber.h"

/** Internal util functions
 * {{{
 */

static inline struct httpc_env*
luaT_httpc_checkenv(lua_State *L)
{
	return (struct httpc_env *)
			luaL_checkudata(L, 1, DRIVER_LUA_UDATA_NAME);
}

static inline void
lua_add_key_u64(lua_State *L, const char *key, uint64_t value)
{
	lua_pushstring(L, key);
	lua_pushinteger(L, value);
	lua_settable(L, -3);
}

static void
parse_headers(lua_State *L, char *buffer, size_t len)
{
	struct http_parser parser;
	char *end_buf = buffer + len;
	lua_pushstring(L, "headers");
	lua_newtable(L);
	while (true) {
		int rc = http_parse_header_line(&parser, &buffer, end_buf);
		if (rc == HTTP_PARSE_INVALID) {
			continue;
		}
		if (rc == HTTP_PARSE_DONE) {
			break;
		}

		if (rc == HTTP_PARSE_OK) {
			lua_pushlstring(L, parser.header_name,
					parser.header_name_idx);

			/* check value of header, if exists */
			lua_pushlstring(L, parser.header_name,
					parser.header_name_idx);
			lua_gettable(L, -3);
			int value_len = parser.header_value_end -
						parser.header_value_start;
			/* table of values to handle duplicates*/
			if (lua_isnil(L, -1)) {
				lua_pop(L, 1);
				lua_newtable(L);
				lua_pushinteger(L, 1);
				lua_pushlstring(L, parser.header_value_start,
						value_len);
				lua_settable(L, -3);
			} else if (lua_istable(L, -1)) {
				lua_pushinteger(L, lua_objlen(L, -1) + 1);
				lua_pushlstring(L, parser.header_value_start,
						value_len);
				lua_settable(L, -3);
			}
			/*headers[parser.header] = {value}*/
			lua_settable(L, -3);
		}
	}

	/* headers */
	lua_settable(L, -3);

	lua_pushstring(L, "proto");

	lua_newtable(L);
	lua_pushinteger(L, 1);
	lua_pushinteger(L, (parser.http_major > 0) ? parser.http_major: 0);
	lua_settable(L, -3);

	lua_pushinteger(L, 2);
	lua_pushinteger(L, (parser.http_minor > 0) ? parser.http_minor: 0);
	lua_settable(L, -3);

	/* proto */
	lua_settable(L, -3);
}
/* }}}
 */

/** lib Lua API {{{
 */

static int
luaT_httpc_request(lua_State *L)
{
	struct httpc_env *ctx = luaT_httpc_checkenv(L);
	if (ctx == NULL)
		return luaL_error(L, "can't get httpc environment");

	const char *method = luaL_checkstring(L, 2);
	const char *url  = luaL_checkstring(L, 3);

	struct httpc_request *req = httpc_request_new(ctx, method, url);
	if (req == NULL)
		return luaT_error(L);

	double timeout = TIMEOUT_INFINITY;

	if (lua_isstring(L, 4)) {
		size_t len = 0;
		const char *body = lua_tolstring(L, 4, &len);
		if (len > 0 && httpc_set_body(req, body, len) != 0) {
			httpc_request_delete(req);
			return luaT_error(L);
		}
	} else if (!lua_isnil(L, 4)) {
		httpc_request_delete(req);
		return luaL_error(L, "fourth argument must be a string");
	}

	if (!lua_istable(L, 5)) {
		httpc_request_delete(req);
		return luaL_error(L, "fifth argument must be a table");
	}

	lua_getfield(L, 5, "headers");
	if (!lua_isnil(L, -1)) {
		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {
			if (httpc_set_header(req, "%s: %s",
					     lua_tostring(L, -2),
					     lua_tostring(L, -1)) < 0) {
				httpc_request_delete(req);
				return luaT_error(L);
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	lua_getfield(L, 5, "ca_path");
	if (!lua_isnil(L, -1))
		httpc_set_ca_path(req, lua_tostring(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "ca_file");
	if (!lua_isnil(L, -1))
		httpc_set_ca_file(req, lua_tostring(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "unix_socket");
	if (!lua_isnil(L, -1)) {
		if(httpc_set_unix_socket(req, lua_tostring(L, -1))) {
			httpc_request_delete(req);
			return luaT_error(L);
		}
	}
	lua_pop(L, 1);

	lua_getfield(L, 5, "verify_host");
	if (!lua_isnil(L, -1))
		httpc_set_verify_host(req, lua_toboolean(L, -1) == 1 ? 2 : 0);
	lua_pop(L, 1);

	lua_getfield(L, 5, "verify_peer");
	if (!lua_isnil(L, -1))
		httpc_set_verify_peer(req, lua_toboolean(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "ssl_key");
	if (!lua_isnil(L, -1))
		httpc_set_ssl_key(req, lua_tostring(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "ssl_cert");
	if (!lua_isnil(L, -1))
		httpc_set_ssl_cert(req, lua_tostring(L, -1));
	lua_pop(L, 1);

	long keepalive_idle = 0;
	long keepalive_interval = 0;

	lua_getfield(L, 5, "keepalive_idle");
	if (!lua_isnil(L, -1))
		keepalive_idle = (long) lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, 5, "keepalive_interval");
	if (!lua_isnil(L, -1))
		keepalive_interval = (long) lua_tonumber(L, -1);
	lua_pop(L, 1);

	if (httpc_set_keepalive(req, keepalive_idle,
				keepalive_interval) < 0) {
		httpc_request_delete(req);
		return luaT_error(L);
	}

	lua_getfield(L, 5, "low_speed_limit");
	if (!lua_isnil(L, -1))
		httpc_set_low_speed_limit(req,
				(long) lua_tonumber(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "low_speed_time");
	if (!lua_isnil(L, -1))
		httpc_set_low_speed_time(req,
				(long) lua_tonumber(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "timeout");
	if (!lua_isnil(L, -1))
		timeout = lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, 5, "verbose");
	if (!lua_isnil(L, -1) && lua_isboolean(L, -1))
		httpc_set_verbose(req, true);
	lua_pop(L, 1);

	if (httpc_execute(req, timeout) != 0) {
		httpc_request_delete(req);
		return luaT_error(L);
	}

	lua_newtable(L);

	lua_pushstring(L, "status");
	lua_pushinteger(L, req->status);
	lua_settable(L, -3);

	lua_pushstring(L, "reason");
	lua_pushstring(L, req->reason);
	lua_settable(L, -3);

	size_t headers_len = region_used(&req->resp_headers);
	if (headers_len > 0) {
		char *headers = region_join(&req->resp_headers, headers_len);
		if (headers == NULL) {
			diag_set(OutOfMemory, headers_len, "region", "headers");
			httpc_request_delete(req);
			return luaT_error(L);
		}
		parse_headers(L, headers, headers_len);
	}

	size_t body_len = region_used(&req->resp_body);
	if (body_len > 0) {
		char *body = region_join(&req->resp_body, body_len);
		if (body == NULL) {
			diag_set(OutOfMemory, body_len, "region", "body");
			httpc_request_delete(req);
			return luaT_error(L);
		}
		lua_pushstring(L, "body");
		lua_pushlstring(L, body, body_len);
		lua_settable(L, -3);
	}

	/* clean up */
	httpc_request_delete(req);
	return 1;
}

static int
luaT_httpc_stat(lua_State *L)
{
	struct httpc_env *ctx = luaT_httpc_checkenv(L);
	if (ctx == NULL)
		return luaL_error(L, "can't get httpc environment");

	lua_newtable(L);
	lua_add_key_u64(L, "active_requests",
			(uint64_t) ctx->curl_env.stat.active_requests);
	lua_add_key_u64(L, "sockets_added",
			(uint64_t) ctx->curl_env.stat.sockets_added);
	lua_add_key_u64(L, "sockets_deleted",
			(uint64_t) ctx->curl_env.stat.sockets_deleted);
	lua_add_key_u64(L, "total_requests",
			ctx->stat.total_requests);
	lua_add_key_u64(L, "http_200_responses",
			ctx->stat.http_200_responses);
	lua_add_key_u64(L, "http_other_responses",
			ctx->stat.http_other_responses);
	lua_add_key_u64(L, "failed_requests",
			(uint64_t) ctx->stat.failed_requests);

	return 1;
}

static int
luaT_httpc_new(lua_State *L)
{
	struct httpc_env *ctx = (struct httpc_env *)
			lua_newuserdata(L, sizeof(struct httpc_env));
	if (ctx == NULL)
		return luaL_error(L, "lua_newuserdata failed: httpc_env");

	long max_conns = luaL_checklong(L, 1);
	if (httpc_env_create(ctx, max_conns) != 0)
		return luaT_error(L);

	luaL_getmetatable(L, DRIVER_LUA_UDATA_NAME);
	lua_setmetatable(L, -2);

	return 1;
}

static int
luaT_httpc_cleanup(lua_State *L)
{
	httpc_env_destroy(luaT_httpc_checkenv(L));

	/** remove all methods operating on ctx */
	lua_newtable(L);
	lua_setmetatable(L, -2);

	lua_pushboolean(L, true);
	lua_pushinteger(L, 0);
	return 2;
}

/*
 * }}}
 */

/*
 * Lists of exporting: object and/or functions to the Lua
 */

static const struct luaL_Reg Module[] = {
	{"new", luaT_httpc_new},
	{NULL, NULL}
};

static const struct luaL_Reg Client[] = {
	{"request", luaT_httpc_request},
	{"stat", luaT_httpc_stat},
	{"__gc", luaT_httpc_cleanup},
	{NULL, NULL}
};

/*
 * Lib initializer
 */
LUA_API int
luaopen_http_client_driver(lua_State *L)
{
	luaL_register_type(L, DRIVER_LUA_UDATA_NAME, Client);
	luaL_register_module(L, "http.client", Module);
	return 1;
}
