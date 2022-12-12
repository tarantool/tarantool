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
#define IO_LUA_UDATA_NAME       "httpc_io"

#include "http_parser/http_parser.h"
#include <httpc.h>
#include "say.h"
#include "lua/utils.h"
#include "lua/httpc.h"
#include "core/fiber.h"

/** Internal util types
 * {{{
 */

/**
 * The stream input/output request.
 */
struct httpc_io {
	/** HTTP request. */
	struct httpc_request *req;
};

/**
 * }}}
 */

/** Internal util functions
 * {{{
 */

static inline struct httpc_env*
luaT_httpc_checkenv(lua_State *L)
{
	return (struct httpc_env *)
			luaL_checkudata(L, 1, DRIVER_LUA_UDATA_NAME);
}

static inline struct httpc_io*
luaT_httpc_checkio(lua_State *L)
{
	return (struct httpc_io *)
			luaL_checkudata(L, 1, IO_LUA_UDATA_NAME);
}

static inline int
httpc_io_create(lua_State *L, struct httpc_request *req)
{
	struct httpc_io *io = (struct httpc_io *)
			lua_newuserdata(L, sizeof(struct httpc_io));
	if (io == NULL)
		return luaL_error(L, "lua_newuserdata failed: httpc_io");
	memset(io, 0, sizeof(*io));
	io->req = req;

	luaL_getmetatable(L, IO_LUA_UDATA_NAME);
	lua_setmetatable(L, -2);

	return 1;
}

static inline int
httpc_io_destroy(struct httpc_io *io)
{
	httpc_request_delete(io->req);
	return 0;
}

static inline void
lua_add_key_u64(lua_State *L, const char *key, uint64_t value)
{
	lua_pushstring(L, key);
	lua_pushinteger(L, value);
	lua_settable(L, -3);
}

static int
parse_headers(lua_State *L, const char *buffer, size_t len,
	      int max_header_name_len)
{
	struct http_parser parser;
	http_parser_create(&parser);
	parser.hdr_name = (char *) calloc(max_header_name_len, sizeof(char));
	if (parser.hdr_name == NULL) {
		diag_set(OutOfMemory, max_header_name_len * sizeof(char),
			 "malloc", "hdr_name");
		return -1;
	}
	const char *end_buf = buffer + len;
	lua_pushstring(L, "headers");
	lua_newtable(L);
	while (true) {
		int rc = http_parse_header_line(&parser, &buffer, end_buf,
						max_header_name_len);
		if (rc == HTTP_PARSE_INVALID || rc == HTTP_PARSE_CONTINUE) {
			continue;
		}
		if (rc == HTTP_PARSE_DONE) {
			break;
		}
		if (rc == HTTP_PARSE_OK) {
			lua_pushlstring(L, parser.hdr_name,
					parser.hdr_name_idx);

			/* check value of header, if exists */
			lua_pushlstring(L, parser.hdr_name,
					parser.hdr_name_idx);
			lua_gettable(L, -3);
			int value_len = parser.hdr_value_end -
						parser.hdr_value_start;
			/* table of values to handle duplicates*/
			if (lua_isnil(L, -1)) {
				lua_pop(L, 1);
				lua_newtable(L);
				lua_pushinteger(L, 1);
				lua_pushlstring(L, parser.hdr_value_start,
						value_len);
				lua_settable(L, -3);
			} else if (lua_istable(L, -1)) {
				lua_pushinteger(L, lua_objlen(L, -1) + 1);
				lua_pushlstring(L, parser.hdr_value_start,
						value_len);
				lua_settable(L, -3);
			}
			/*headers[parser.header] = {value}*/
			lua_settable(L, -3);
		}
	}

	free(parser.hdr_name);

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
	return 0;
}
/* }}}
 */

/** lib Lua API {{{
 */

enum { MAX_HTTP_HEADER_NAME_LEN = 32 };

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

	const char *body = NULL;
	size_t body_len = 0;
	double timeout = TIMEOUT_INFINITY;
	int max_header_name_length = MAX_HTTP_HEADER_NAME_LEN;
	if (lua_isstring(L, 4)) {
		body = lua_tolstring(L, 4, &body_len);
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
		if (lua_istable(L, -1) == 0) {
			httpc_request_delete(req);
			return luaL_error(L, "opts.headers should be a table");
		}
		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {
			int header_type = lua_type(L, -1);
			if (header_type != LUA_TSTRING) {
				httpc_request_delete(req);
				return luaL_error(L, "opts.headers values "
						  "should be strings");
			}
			if (lua_type(L, -2) != LUA_TSTRING) {
				httpc_request_delete(req);
				return luaL_error(L, "opts.headers keys should "
						  "be strings");
			}
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

	lua_getfield(L, 5, "proxy");
	if (!lua_isnil(L, -1))
		httpc_set_proxy(req, lua_tostring(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "proxy_port");
	if (!lua_isnil(L, -1))
		httpc_set_proxy_port(req, (long) lua_tonumber(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "proxy_user_pwd");
	if (!lua_isnil(L, -1))
		httpc_set_proxy_user_pwd(req, lua_tostring(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "no_proxy");
	if (!lua_isnil(L, -1))
		httpc_set_no_proxy(req, lua_tostring(L, -1));
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

	httpc_set_keepalive(req, keepalive_idle, keepalive_interval);

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

	lua_getfield(L, 5, "max_header_name_length");
	if (!lua_isnil(L, -1))
		max_header_name_length = lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, 5, "verbose");
	if (!lua_isnil(L, -1) && lua_isboolean(L, -1))
		httpc_set_verbose(req, lua_toboolean(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "interface");
	if (!lua_isnil(L, -1))
		httpc_set_interface(req, lua_tostring(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "follow_location");
	if (!lua_isnil(L, -1) && lua_isboolean(L, -1))
		httpc_set_follow_location(req, lua_toboolean(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, 5, "accept_encoding");
	if (!lua_isnil(L, -1))
		httpc_set_accept_encoding(req, lua_tostring(L, -1));
	lua_pop(L, 1);

	bool chunked = false;
	lua_getfield(L, 5, "chunked");
	if (!lua_isnil(L, -1) && lua_isboolean(L, -1))
		chunked = lua_toboolean(L, -1);
	lua_pop(L, 1);

	if (chunked) {
		if (httpc_set_io(req, method) != 0) {
			httpc_request_delete(req);
			return luaT_error(L);
		}

		if (httpc_request_start(req, timeout) != 0) {
			httpc_request_delete(req);
			return luaT_error(L);
		}

		if (body_len > 0 && httpc_request_io_write(req, body, body_len,
							   timeout) == -1) {
			httpc_request_delete(req);
			return luaT_error(L);
		}

		lua_newtable(L);

		lua_pushstring(L, "_internal");

		lua_newtable(L);
		lua_pushstring(L, "io");
		httpc_io_create(L, req);
		lua_settable(L, -3);

		lua_settable(L, -3);
	} else {
		if (body_len > 0 && httpc_set_body(req, body, body_len) != 0) {
			httpc_request_delete(req);
			return luaT_error(L);
		}

		if (httpc_execute(req, timeout) != 0) {
			httpc_request_delete(req);
			return luaT_error(L);
		}

		lua_newtable(L);

		size_t rbody_len = region_used(&req->recv);
		if (rbody_len > 0) {
			char *rbody = region_join(&req->recv, rbody_len);
			if (rbody == NULL) {
				diag_set(OutOfMemory, rbody_len, "region",
					 "body");
				httpc_request_delete(req);
				return luaT_error(L);
			}
			lua_pushstring(L, "body");
			lua_pushlstring(L, rbody, rbody_len);
			lua_settable(L, -3);
		}
	}

	if (!req->curl_request.in_progress) {
		lua_pushstring(L, "status");
		lua_pushinteger(L, req->status);
		lua_settable(L, -3);

		lua_pushstring(L, "reason");
		lua_pushstring(L, req->reason);
		lua_settable(L, -3);
	}

	size_t headers_len = region_used(&req->resp_headers);
	if (headers_len > 0) {
		char *headers = region_join(&req->resp_headers, headers_len);
		if (headers == NULL) {
			diag_set(OutOfMemory, headers_len, "region", "headers");
			httpc_request_delete(req);
			return luaT_error(L);
		}
		if (parse_headers(L, headers, headers_len,
			max_header_name_length) < 0)
			diag_log();
	}

	if (!chunked) {
		/* clean up immediately */
		httpc_request_delete(req);
	}

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
	long max_total_conns = luaL_checklong(L, 2);
	if (httpc_env_create(ctx, max_conns, max_total_conns) != 0)
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

/**
 * Read from a stream input/output request.
 */
static int
luaT_httpc_io_read(lua_State *L)
{
	struct httpc_io *io = luaT_httpc_checkio(L);
	struct httpc_request *req = io->req;
	uint32_t ctypeid = 0;
	char *buf = *(char **)luaL_checkcdata(L, 2, &ctypeid);
	ssize_t len = luaL_checkinteger(L, 3);
	double timeout = luaL_checknumber(L, 4);

	if (len <= 0) {
		diag_set(IllegalParams, "io: payload length must be >= 0");
		return luaT_error(L);
	}

	if (timeout < 0) {
		diag_set(IllegalParams, "io: timeout must be >= 0");
		return luaT_error(L);
	}

	ssize_t res = httpc_request_io_read(req, buf, len, timeout);
	if (res < 0) {
		return luaT_error(L);
	}

	lua_pushinteger(L, res);
	return 1;
}

/**
 * Write to a stream input/output request.
 */
static int
luaT_httpc_io_write(lua_State *L)
{
	struct httpc_io *io = luaT_httpc_checkio(L);
	const char *buf = lua_tostring(L, 2);
	uint32_t ctypeid = 0;
	if (buf == NULL)
		buf = *(const char **)luaL_checkcdata(L, 2, &ctypeid);
	ssize_t len = lua_tonumber(L, 3);
	double timeout = luaL_checknumber(L, 4);

	if (len < 0) {
		diag_set(IllegalParams, "io: payload length must be >= 0");
		return luaT_error(L);
	}

	if (timeout < 0) {
		diag_set(IllegalParams, "io: timeout must be >= 0");
		return luaT_error(L);
	}

	struct httpc_request *req = io->req;
	ssize_t res = httpc_request_io_write(req, buf, len, timeout);
	if (res < 0)
		return luaT_error(L);

	lua_pushinteger(L, res);
	return 1;
}

/**
 * Close a stream input/output request.
 */
static int
luaT_httpc_io_finish(lua_State *L)
{
	struct httpc_io *io = luaT_httpc_checkio(L);
	double timeout = luaL_checknumber(L, 2);

	if (timeout < 0) {
		diag_set(IllegalParams, "io: timeout must be >= 0");
		return luaT_error(L);
	}

	httpc_request_io_finish(io->req, timeout);

	lua_pushinteger(L, io->req->status);
	lua_pushstring(L, io->req->reason);
	return 2;
}

/**
 * GC cleanup a stream input/output request.
 */
static int
luaT_httpc_io_cleanup(lua_State *L)
{
	httpc_io_destroy(luaT_httpc_checkio(L));

	/** remove all methods operating on io */
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

static const struct luaL_Reg Io[] = {
	{"read", luaT_httpc_io_read},
	{"write", luaT_httpc_io_write},
	{"finish", luaT_httpc_io_finish},
	{"__gc", luaT_httpc_io_cleanup},
	{NULL, NULL}
};

/*
 * Lib initializer
 */
LUA_API int
luaopen_http_client_driver(lua_State *L)
{
	luaL_register_type(L, DRIVER_LUA_UDATA_NAME, Client);
	luaL_register_type(L, IO_LUA_UDATA_NAME, Io);
	luaT_newmodule(L, "http.client.lib", Module);
	return 1;
}
