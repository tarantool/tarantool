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
#include "net_box.h"
#include <sys/socket.h>

#include <small/ibuf.h>
#include <msgpuck.h> /* mp_store_u32() */
#include "scramble.h"

#include "box/iproto_constants.h"
#include "box/lua/tuple.h" /* luamp_convert_tuple() / luamp_convert_key() */
#include "box/xrow.h"
#include "box/tuple.h"
#include "box/execute.h"

#include "lua/msgpack.h"
#include <base64.h>

#include "coio.h"
#include "box/errcode.h"
#include "lua/fiber.h"
#include "mpstream/mpstream.h"
#include "misc.h" /* lbox_check_tuple_format() */

#define cfg luaL_msgpack_default

enum netbox_method {
	NETBOX_PING        = 0,
	NETBOX_CALL_16     = 1,
	NETBOX_CALL_17     = 2,
	NETBOX_EVAL        = 3,
	NETBOX_INSERT      = 4,
	NETBOX_REPLACE     = 5,
	NETBOX_DELETE      = 6,
	NETBOX_UPDATE      = 7,
	NETBOX_UPSERT      = 8,
	NETBOX_SELECT      = 9,
	NETBOX_EXECUTE     = 10,
	NETBOX_PREPARE     = 11,
	NETBOX_UNPREPARE   = 12,
	NETBOX_GET         = 13,
	NETBOX_MIN         = 14,
	NETBOX_MAX         = 15,
	NETBOX_COUNT       = 16,
	NETBOX_INJECT      = 17,
	netbox_method_MAX
};

static inline size_t
netbox_prepare_request(struct mpstream *stream, uint64_t sync,
		       enum iproto_type type)
{
	/* Remember initial size of ibuf (see netbox_encode_request()) */
	struct ibuf *ibuf = stream->ctx;
	size_t used = ibuf_used(ibuf);

	/* Reserve and skip space for fixheader */
	size_t fixheader_size = mp_sizeof_uint(UINT32_MAX);
	mpstream_reserve(stream, fixheader_size);
	mpstream_advance(stream, fixheader_size);

	/* encode header */
	mpstream_encode_map(stream, 2);

	mpstream_encode_uint(stream, IPROTO_SYNC);
	mpstream_encode_uint(stream, sync);

	mpstream_encode_uint(stream, IPROTO_REQUEST_TYPE);
	mpstream_encode_uint(stream, type);

	/* Caller should remember how many bytes was used in ibuf */
	return used;
}

static inline void
netbox_encode_request(struct mpstream *stream, size_t initial_size)
{
	mpstream_flush(stream);

	struct ibuf *ibuf = (struct ibuf *) stream->ctx;

	/*
	 * Calculation the start position in ibuf by getting current size
	 * and then substracting initial size. Since we don't touch
	 * ibuf->rpos during encoding this approach should always work
	 * even on realloc or memmove inside ibuf.
	 */
	size_t fixheader_size = mp_sizeof_uint(UINT32_MAX);
	size_t used = ibuf_used(ibuf);
	assert(initial_size + fixheader_size <= used);
	size_t total_size = used - initial_size;
	char *fixheader = ibuf->wpos - total_size;
	assert(fixheader >= ibuf->rpos);

	/* patch skipped len */
	*(fixheader++) = 0xce;
	/* fixheader size is not included */
	mp_store_u32(fixheader, total_size - fixheader_size);
}

static void
netbox_encode_ping(lua_State *L, int idx, struct mpstream *stream,
		   uint64_t sync)
{
	(void)L;
	(void)idx;
	size_t svp = netbox_prepare_request(stream, sync, IPROTO_PING);
	netbox_encode_request(stream, svp);
}

static int
netbox_encode_auth(lua_State *L)
{
	if (lua_gettop(L) < 5) {
		return luaL_error(L, "Usage: netbox.encode_update(ibuf, sync, "
				     "user, password, greeting)");
	}
	struct ibuf *ibuf = (struct ibuf *)lua_topointer(L, 1);
	uint64_t sync = luaL_touint64(L, 2);

	struct mpstream stream;
	mpstream_init(&stream, ibuf, ibuf_reserve_cb, ibuf_alloc_cb,
		      luamp_error, L);
	size_t svp = netbox_prepare_request(&stream, sync, IPROTO_AUTH);

	size_t user_len;
	const char *user = lua_tolstring(L, 3, &user_len);
	size_t password_len;
	const char *password = lua_tolstring(L, 4, &password_len);
	size_t salt_len;
	const char *salt = lua_tolstring(L, 5, &salt_len);
	if (salt_len < SCRAMBLE_SIZE)
		return luaL_error(L, "Invalid salt");

	/* Adapted from xrow_encode_auth() */
	mpstream_encode_map(&stream, password != NULL ? 2 : 1);
	mpstream_encode_uint(&stream, IPROTO_USER_NAME);
	mpstream_encode_strn(&stream, user, user_len);
	if (password != NULL) { /* password can be omitted */
		char scramble[SCRAMBLE_SIZE];
		scramble_prepare(scramble, salt, password, password_len);
		mpstream_encode_uint(&stream, IPROTO_TUPLE);
		mpstream_encode_array(&stream, 2);
		mpstream_encode_str(&stream, "chap-sha1");
		mpstream_encode_strn(&stream, scramble, SCRAMBLE_SIZE);
	}

	netbox_encode_request(&stream, svp);
	return 0;
}

static void
netbox_encode_call_impl(lua_State *L, int idx, struct mpstream *stream,
			uint64_t sync, enum iproto_type type)
{
	/* Lua stack at idx: function_name, args */
	size_t svp = netbox_prepare_request(stream, sync, type);

	mpstream_encode_map(stream, 2);

	/* encode proc name */
	size_t name_len;
	const char *name = lua_tolstring(L, idx, &name_len);
	mpstream_encode_uint(stream, IPROTO_FUNCTION_NAME);
	mpstream_encode_strn(stream, name, name_len);

	/* encode args */
	mpstream_encode_uint(stream, IPROTO_TUPLE);
	luamp_encode_tuple(L, cfg, stream, idx + 1);

	netbox_encode_request(stream, svp);
}

static void
netbox_encode_call_16(lua_State *L, int idx, struct mpstream *stream,
		      uint64_t sync)
{
	netbox_encode_call_impl(L, idx, stream, sync, IPROTO_CALL_16);
}

static void
netbox_encode_call(lua_State *L, int idx, struct mpstream *stream,
		   uint64_t sync)
{
	netbox_encode_call_impl(L, idx, stream, sync, IPROTO_CALL);
}

static void
netbox_encode_eval(lua_State *L, int idx, struct mpstream *stream,
		   uint64_t sync)
{
	/* Lua stack at idx: expr, args */
	size_t svp = netbox_prepare_request(stream, sync, IPROTO_EVAL);

	mpstream_encode_map(stream, 2);

	/* encode expr */
	size_t expr_len;
	const char *expr = lua_tolstring(L, idx, &expr_len);
	mpstream_encode_uint(stream, IPROTO_EXPR);
	mpstream_encode_strn(stream, expr, expr_len);

	/* encode args */
	mpstream_encode_uint(stream, IPROTO_TUPLE);
	luamp_encode_tuple(L, cfg, stream, idx + 1);

	netbox_encode_request(stream, svp);
}

static void
netbox_encode_select(lua_State *L, int idx, struct mpstream *stream,
		     uint64_t sync)
{
	/* Lua stack at idx: space_id, index_id, iterator, offset, limit, key */
	size_t svp = netbox_prepare_request(stream, sync, IPROTO_SELECT);

	mpstream_encode_map(stream, 6);

	uint32_t space_id = lua_tonumber(L, idx);
	uint32_t index_id = lua_tonumber(L, idx + 1);
	int iterator = lua_tointeger(L, idx + 2);
	uint32_t offset = lua_tonumber(L, idx + 3);
	uint32_t limit = lua_tonumber(L, idx + 4);

	/* encode space_id */
	mpstream_encode_uint(stream, IPROTO_SPACE_ID);
	mpstream_encode_uint(stream, space_id);

	/* encode index_id */
	mpstream_encode_uint(stream, IPROTO_INDEX_ID);
	mpstream_encode_uint(stream, index_id);

	/* encode iterator */
	mpstream_encode_uint(stream, IPROTO_ITERATOR);
	mpstream_encode_uint(stream, iterator);

	/* encode offset */
	mpstream_encode_uint(stream, IPROTO_OFFSET);
	mpstream_encode_uint(stream, offset);

	/* encode limit */
	mpstream_encode_uint(stream, IPROTO_LIMIT);
	mpstream_encode_uint(stream, limit);

	/* encode key */
	mpstream_encode_uint(stream, IPROTO_KEY);
	luamp_convert_key(L, cfg, stream, idx + 5);

	netbox_encode_request(stream, svp);
}

static void
netbox_encode_insert_or_replace(lua_State *L, int idx, struct mpstream *stream,
				uint64_t sync, enum iproto_type type)
{
	/* Lua stack at idx: space_id, tuple */
	size_t svp = netbox_prepare_request(stream, sync, type);

	mpstream_encode_map(stream, 2);

	/* encode space_id */
	uint32_t space_id = lua_tonumber(L, idx);
	mpstream_encode_uint(stream, IPROTO_SPACE_ID);
	mpstream_encode_uint(stream, space_id);

	/* encode args */
	mpstream_encode_uint(stream, IPROTO_TUPLE);
	luamp_encode_tuple(L, cfg, stream, idx + 1);

	netbox_encode_request(stream, svp);
}

static void
netbox_encode_insert(lua_State *L, int idx, struct mpstream *stream,
		     uint64_t sync)
{
	netbox_encode_insert_or_replace(L, idx, stream, sync, IPROTO_INSERT);
}

static void
netbox_encode_replace(lua_State *L, int idx, struct mpstream *stream,
		      uint64_t sync)
{
	netbox_encode_insert_or_replace(L, idx, stream, sync, IPROTO_REPLACE);
}

static void
netbox_encode_delete(lua_State *L, int idx, struct mpstream *stream,
		     uint64_t sync)
{
	/* Lua stack at idx: space_id, index_id, key */
	size_t svp = netbox_prepare_request(stream, sync, IPROTO_DELETE);

	mpstream_encode_map(stream, 3);

	/* encode space_id */
	uint32_t space_id = lua_tonumber(L, idx);
	mpstream_encode_uint(stream, IPROTO_SPACE_ID);
	mpstream_encode_uint(stream, space_id);

	/* encode space_id */
	uint32_t index_id = lua_tonumber(L, idx + 1);
	mpstream_encode_uint(stream, IPROTO_INDEX_ID);
	mpstream_encode_uint(stream, index_id);

	/* encode key */
	mpstream_encode_uint(stream, IPROTO_KEY);
	luamp_convert_key(L, cfg, stream, idx + 2);

	netbox_encode_request(stream, svp);
}

static void
netbox_encode_update(lua_State *L, int idx, struct mpstream *stream,
		     uint64_t sync)
{
	/* Lua stack at idx: space_id, index_id, key, ops */
	size_t svp = netbox_prepare_request(stream, sync, IPROTO_UPDATE);

	mpstream_encode_map(stream, 5);

	/* encode space_id */
	uint32_t space_id = lua_tonumber(L, idx);
	mpstream_encode_uint(stream, IPROTO_SPACE_ID);
	mpstream_encode_uint(stream, space_id);

	/* encode index_id */
	uint32_t index_id = lua_tonumber(L, idx + 1);
	mpstream_encode_uint(stream, IPROTO_INDEX_ID);
	mpstream_encode_uint(stream, index_id);

	/* encode index_id */
	mpstream_encode_uint(stream, IPROTO_INDEX_BASE);
	mpstream_encode_uint(stream, 1);

	/* encode key */
	mpstream_encode_uint(stream, IPROTO_KEY);
	luamp_convert_key(L, cfg, stream, idx + 2);

	/* encode ops */
	mpstream_encode_uint(stream, IPROTO_TUPLE);
	luamp_encode_tuple(L, cfg, stream, idx + 3);

	netbox_encode_request(stream, svp);
}

static void
netbox_encode_upsert(lua_State *L, int idx, struct mpstream *stream,
		     uint64_t sync)
{
	/* Lua stack at idx: space_id, tuple, ops */
	size_t svp = netbox_prepare_request(stream, sync, IPROTO_UPSERT);

	mpstream_encode_map(stream, 4);

	/* encode space_id */
	uint32_t space_id = lua_tonumber(L, idx);
	mpstream_encode_uint(stream, IPROTO_SPACE_ID);
	mpstream_encode_uint(stream, space_id);

	/* encode index_base */
	mpstream_encode_uint(stream, IPROTO_INDEX_BASE);
	mpstream_encode_uint(stream, 1);

	/* encode tuple */
	mpstream_encode_uint(stream, IPROTO_TUPLE);
	luamp_encode_tuple(L, cfg, stream, idx + 1);

	/* encode ops */
	mpstream_encode_uint(stream, IPROTO_OPS);
	luamp_encode_tuple(L, cfg, stream, idx + 2);

	netbox_encode_request(stream, svp);
}

static int
netbox_decode_greeting(lua_State *L)
{
	struct greeting greeting;
	size_t len;
	const char *buf = NULL;
	char uuid_buf[UUID_STR_LEN + 1];

	if (lua_isstring(L, 1))
		buf = lua_tolstring(L, 1, &len);

	if (buf == NULL || len != IPROTO_GREETING_SIZE ||
		greeting_decode(buf, &greeting) != 0) {

		lua_pushboolean(L, 0);
		lua_pushstring(L, "Invalid greeting");
		return 2;
	}

	lua_newtable(L);
	lua_pushinteger(L, greeting.version_id);
	lua_setfield(L, -2, "version_id");
	lua_pushstring(L, greeting.protocol);
	lua_setfield(L, -2, "protocol");
	lua_pushlstring(L, greeting.salt, greeting.salt_len);
	lua_setfield(L, -2, "salt");

	tt_uuid_to_string(&greeting.uuid, uuid_buf);
	lua_pushstring(L, uuid_buf);
	lua_setfield(L, -2, "uuid");

	return 1;
}

/**
 * communicate(fd, send_buf, recv_buf, limit_or_boundary, timeout)
 *  -> errno, error
 *  -> nil, limit/boundary_pos
 *
 * The need for this function arises from not wanting to
 * have more than one watcher for a single fd, and thus issue
 * redundant epoll_ctl(EPOLLCTL_ADD) for it when doing both
 * reading and writing.
 *
 * Instead, this function takes an fd, input and output buffer,
 * and does sending and receiving on it in a single event loop
 * interaction.
 */
static int
netbox_communicate(lua_State *L)
{
	uint32_t fd = lua_tonumber(L, 1);
	const int NETBOX_READAHEAD = 16320;
	struct ibuf *send_buf = (struct ibuf *) lua_topointer(L, 2);
	struct ibuf *recv_buf = (struct ibuf *) lua_topointer(L, 3);

	/* limit or boundary */
	size_t limit = SIZE_MAX;
	const void *boundary = NULL;
	size_t boundary_len;

	if (lua_type(L, 4) == LUA_TSTRING)
		boundary = lua_tolstring(L, 4, &boundary_len);
	else
		limit = lua_tonumber(L, 4);

	/* timeout */
	ev_tstamp timeout = TIMEOUT_INFINITY;
	if (lua_type(L, 5) == LUA_TNUMBER)
		timeout = lua_tonumber(L, 5);
	if (timeout < 0) {
		lua_pushinteger(L, ER_TIMEOUT);
		lua_pushstring(L, "Timeout exceeded");
		return 2;
	}
	int revents = COIO_READ;
	while (true) {
		/* reader serviced first */
check_limit:
		if (ibuf_used(recv_buf) >= limit) {
			lua_pushnil(L);
			lua_pushinteger(L, (lua_Integer)limit);
			return 2;
		}
		const char *p;
		if (boundary != NULL && (p = memmem(
					recv_buf->rpos,
					ibuf_used(recv_buf),
					boundary, boundary_len)) != NULL) {
			lua_pushnil(L);
			lua_pushinteger(L, (lua_Integer)(
					p - recv_buf->rpos));
			return 2;
		}

		while (revents & COIO_READ) {
			void *p = ibuf_reserve(recv_buf, NETBOX_READAHEAD);
			if (p == NULL)
				luaL_error(L, "out of memory");
			ssize_t rc = recv(
				fd, recv_buf->wpos, ibuf_unused(recv_buf), 0);
			if (rc == 0) {
				lua_pushinteger(L, ER_NO_CONNECTION);
				lua_pushstring(L, "Peer closed");
				return 2;
			} if (rc > 0) {
				recv_buf->wpos += rc;
				goto check_limit;
			} else if (errno == EAGAIN || errno == EWOULDBLOCK)
				revents &= ~COIO_READ;
			else if (errno != EINTR)
				goto handle_error;
		}

		while ((revents & COIO_WRITE) && ibuf_used(send_buf) != 0) {
			ssize_t rc = send(
				fd, send_buf->rpos, ibuf_used(send_buf), 0);
			if (rc >= 0)
				send_buf->rpos += rc;
			else if (errno == EAGAIN || errno == EWOULDBLOCK)
				revents &= ~COIO_WRITE;
			else if (errno != EINTR)
				goto handle_error;
		}

		ev_tstamp deadline = ev_monotonic_now(loop()) + timeout;
		ERROR_INJECT_YIELD(ERRINJ_NETBOX_IO_DELAY);
		revents = coio_wait(fd, EV_READ | (ibuf_used(send_buf) != 0 ?
				EV_WRITE : 0), timeout);
		luaL_testcancel(L);
		timeout = deadline - ev_monotonic_now(loop());
		timeout = MAX(0.0, timeout);
		if (revents == 0 && timeout == 0.0) {
			lua_pushinteger(L, ER_TIMEOUT);
			lua_pushstring(L, "Timeout exceeded");
			return 2;
		}
	}
handle_error:
	lua_pushinteger(L, ER_NO_CONNECTION);
	lua_pushstring(L, strerror(errno));
	return 2;
}

static void
netbox_encode_execute(lua_State *L, int idx, struct mpstream *stream,
		      uint64_t sync)
{
	/* Lua stack at idx: query, parameters, options */
	size_t svp = netbox_prepare_request(stream, sync, IPROTO_EXECUTE);

	mpstream_encode_map(stream, 3);

	if (lua_type(L, idx) == LUA_TNUMBER) {
		uint32_t query_id = lua_tointeger(L, idx);
		mpstream_encode_uint(stream, IPROTO_STMT_ID);
		mpstream_encode_uint(stream, query_id);
	} else {
		size_t len;
		const char *query = lua_tolstring(L, idx, &len);
		mpstream_encode_uint(stream, IPROTO_SQL_TEXT);
		mpstream_encode_strn(stream, query, len);
	}

	mpstream_encode_uint(stream, IPROTO_SQL_BIND);
	luamp_encode_tuple(L, cfg, stream, idx + 1);

	mpstream_encode_uint(stream, IPROTO_OPTIONS);
	luamp_encode_tuple(L, cfg, stream, idx + 2);

	netbox_encode_request(stream, svp);
}

static void
netbox_encode_prepare(lua_State *L, int idx, struct mpstream *stream,
		      uint64_t sync)
{
	/* Lua stack at idx: query */
	size_t svp = netbox_prepare_request(stream, sync, IPROTO_PREPARE);

	mpstream_encode_map(stream, 1);

	if (lua_type(L, idx) == LUA_TNUMBER) {
		uint32_t query_id = lua_tointeger(L, idx);
		mpstream_encode_uint(stream, IPROTO_STMT_ID);
		mpstream_encode_uint(stream, query_id);
	} else {
		size_t len;
		const char *query = lua_tolstring(L, idx, &len);
		mpstream_encode_uint(stream, IPROTO_SQL_TEXT);
		mpstream_encode_strn(stream, query, len);
	};

	netbox_encode_request(stream, svp);
}

static void
netbox_encode_unprepare(lua_State *L, int idx, struct mpstream *stream,
			uint64_t sync)
{
	/* Lua stack at idx: query, parameters, options */
	netbox_encode_prepare(L, idx, stream, sync);
}

static void
netbox_encode_inject(struct lua_State *L, int idx, struct mpstream *stream,
		     uint64_t sync)
{
	/* Lua stack at idx: bytes */
	(void)sync;
	size_t len;
	const char *data = lua_tolstring(L, idx, &len);
	mpstream_memcpy(stream, data, len);
	mpstream_flush(stream);
}

/*
 * Encodes a request for the specified method.
 *
 * Takes three mandatory arguments:
 *  - method: a value from the netbox_method enumeration
 *  - ibuf: buffer to write the result to
 *  - sync: value of the IPROTO_SYNC key
 *
 * Other arguments are method-specific.
 */
static int
netbox_encode_method(struct lua_State *L)
{
	typedef void (*method_encoder_f)(struct lua_State *L, int idx,
					 struct mpstream *stream,
					 uint64_t sync);
	static method_encoder_f method_encoder[] = {
		[NETBOX_PING]		= netbox_encode_ping,
		[NETBOX_CALL_16]	= netbox_encode_call_16,
		[NETBOX_CALL_17]	= netbox_encode_call,
		[NETBOX_EVAL]		= netbox_encode_eval,
		[NETBOX_INSERT]		= netbox_encode_insert,
		[NETBOX_REPLACE]	= netbox_encode_replace,
		[NETBOX_DELETE]		= netbox_encode_delete,
		[NETBOX_UPDATE]		= netbox_encode_update,
		[NETBOX_UPSERT]		= netbox_encode_upsert,
		[NETBOX_SELECT]		= netbox_encode_select,
		[NETBOX_EXECUTE]	= netbox_encode_execute,
		[NETBOX_PREPARE]	= netbox_encode_prepare,
		[NETBOX_UNPREPARE]	= netbox_encode_unprepare,
		[NETBOX_GET]		= netbox_encode_select,
		[NETBOX_MIN]		= netbox_encode_select,
		[NETBOX_MAX]		= netbox_encode_select,
		[NETBOX_COUNT]		= netbox_encode_call,
		[NETBOX_INJECT]		= netbox_encode_inject,
	};
	enum netbox_method method = lua_tointeger(L, 1);
	assert(method < netbox_method_MAX);
	struct ibuf *ibuf = (struct ibuf *)lua_topointer(L, 2);
	uint64_t sync = luaL_touint64(L, 3);
	struct mpstream stream;
	mpstream_init(&stream, ibuf, ibuf_reserve_cb, ibuf_alloc_cb,
		      luamp_error, L);
	method_encoder[method](L, 4, &stream, sync);
	return 0;
}

/**
 * This function handles a response that is supposed to have an empty body
 * (e.g. IPROTO_PING result). It doesn't decode anything per se. Instead it
 * simply pushes nil to Lua stack and advances the data ptr to data_end.
 */
static void
netbox_decode_nil(struct lua_State *L, const char **data,
		  const char *data_end, struct tuple_format *format)
{
	(void)format;
	*data = data_end;
	lua_pushnil(L);
}

/**
 * This helper skips a MessagePack map header and IPROTO_DATA key so that
 * *data points to the actual response content.
 */
static void
netbox_skip_to_data(const char **data)
{
	assert(mp_typeof(**data) == MP_MAP);
	uint32_t map_size = mp_decode_map(data);
	/* Until 2.0 body has no keys except DATA. */
	assert(map_size == 1);
	(void)map_size;
	uint32_t key = mp_decode_uint(data);
	assert(key == IPROTO_DATA);
	(void)key;
}

/**
 * Decodes Tarantool response body consisting of single IPROTO_DATA key into
 * a Lua table and pushes the table to Lua stack.
 */
static void
netbox_decode_table(struct lua_State *L, const char **data,
		    const char *data_end, struct tuple_format *format)
{
	(void)data_end;
	(void)format;
	netbox_skip_to_data(data);
	luamp_decode(L, cfg, data);
}

/**
 * Same as netbox_decode_table, but only decodes the first element of the
 * table, skipping the rest.
 */
static void
netbox_decode_value(struct lua_State *L, const char **data,
		    const char *data_end, struct tuple_format *format)
{
	(void)data_end;
	(void)format;
	netbox_skip_to_data(data);
	uint32_t count = mp_decode_array(data);
	if (count == 0)
		return lua_pushnil(L);
	luamp_decode(L, cfg, data);
	for (uint32_t i = 1; i < count; ++i)
		mp_next(data);
}

/**
 * Decodes IPROTO_DATA into a tuple array and pushes the array to Lua stack.
 */
static void
netbox_decode_data(struct lua_State *L, const char **data,
		   struct tuple_format *format)
{
	uint32_t count = mp_decode_array(data);
	lua_createtable(L, count, 0);
	for (uint32_t j = 0; j < count; ++j) {
		const char *begin = *data;
		mp_next(data);
		struct tuple *tuple =
			box_tuple_new(format, begin, *data);
		if (tuple == NULL)
			luaT_error(L);
		luaT_pushtuple(L, tuple);
		lua_rawseti(L, -2, j + 1);
	}
}

/**
 * Decodes Tarantool response body consisting of single IPROTO_DATA key into
 * tuple array and pushes the array to Lua stack.
 */
static void
netbox_decode_select(struct lua_State *L, const char **data,
		     const char *data_end, struct tuple_format *format)
{
	(void)data_end;
	netbox_skip_to_data(data);
	netbox_decode_data(L, data, format);
}

/**
 * Same as netbox_decode_select, but only decodes the first tuple of the array,
 * skipping the rest.
 */
static void
netbox_decode_tuple(struct lua_State *L, const char **data,
		    const char *data_end, struct tuple_format *format)
{
	(void)data_end;
	netbox_skip_to_data(data);
	uint32_t count = mp_decode_array(data);
	if (count == 0)
		return lua_pushnil(L);
	const char *begin = *data;
	mp_next(data);
	struct tuple *tuple = box_tuple_new(format, begin, *data);
	if (tuple == NULL)
		luaT_error(L);
	luaT_pushtuple(L, tuple);
	for (uint32_t i = 1; i < count; ++i)
		mp_next(data);
}

/** Decode optional (i.e. may be present in response) metadata fields. */
static void
decode_metadata_optional(struct lua_State *L, const char **data,
			 uint32_t map_size, const char *name, uint32_t name_len)
{
	/* 2 is default metadata map size (field name + field size). */
	while (map_size-- > 2) {
		uint32_t key = mp_decode_uint(data);
		uint32_t len;
		if (key == IPROTO_FIELD_COLL) {
			const char *coll = mp_decode_str(data, &len);
			lua_pushlstring(L, coll, len);
			lua_setfield(L, -2, "collation");
		} else if (key == IPROTO_FIELD_IS_NULLABLE) {
			bool is_nullable = mp_decode_bool(data);
			lua_pushboolean(L, is_nullable);
			lua_setfield(L, -2, "is_nullable");
		} else if (key == IPROTO_FIELD_SPAN) {
			/*
			 * There's an agreement: if span is not
			 * presented in metadata (encoded as NIL),
			 * then it is the same as name. It allows
			 * avoid sending the same string twice.
			 */
			const char *span = NULL;
			if (mp_typeof(**data) == MP_STR) {
				span = mp_decode_str(data, &len);
			} else {
				assert(mp_typeof(**data) == MP_NIL);
				mp_decode_nil(data);
				span = name;
				len = name_len;
			}
			lua_pushlstring(L, span, len);
			lua_setfield(L, -2, "span");
		} else {
			assert(key == IPROTO_FIELD_IS_AUTOINCREMENT);
			bool is_autoincrement = mp_decode_bool(data);
			lua_pushboolean(L, is_autoincrement);
			lua_setfield(L, -2, "is_autoincrement");
		}
	}
}

/**
 * Decode IPROTO_METADATA into array of maps.
 * @param L Lua stack to push result on.
 * @param data MessagePack.
 */
static void
netbox_decode_metadata(struct lua_State *L, const char **data)
{
	uint32_t count = mp_decode_array(data);
	lua_createtable(L, count, 0);
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t map_size = mp_decode_map(data);
		assert(map_size >= 2 && map_size <= 6);
		uint32_t key = mp_decode_uint(data);
		assert(key == IPROTO_FIELD_NAME);
		(void) key;
		lua_createtable(L, 0, map_size);
		uint32_t name_len, type_len;
		const char *str = mp_decode_str(data, &name_len);
		lua_pushlstring(L, str, name_len);
		lua_setfield(L, -2, "name");
		key = mp_decode_uint(data);
		assert(key == IPROTO_FIELD_TYPE);
		const char *type = mp_decode_str(data, &type_len);
		lua_pushlstring(L, type, type_len);
		lua_setfield(L, -2, "type");
		decode_metadata_optional(L, data, map_size, str, name_len);
		lua_rawseti(L, -2, i + 1);
	}
}

/**
 * Decode IPROTO_SQL_INFO into map.
 * @param L Lua stack to push result on.
 * @param data MessagePack.
 */
static void
netbox_decode_sql_info(struct lua_State *L, const char **data)
{
	uint32_t map_size = mp_decode_map(data);
	assert(map_size == 1 || map_size == 2);
	lua_newtable(L);
	/*
	 * First element in data is SQL_INFO_ROW_COUNT.
	 */
	uint32_t key = mp_decode_uint(data);
	assert(key == SQL_INFO_ROW_COUNT);
	uint32_t row_count = mp_decode_uint(data);
	lua_pushinteger(L, row_count);
	lua_setfield(L, -2, sql_info_key_strs[SQL_INFO_ROW_COUNT]);
	/*
	 * If data have two elements then second is
	 * SQL_INFO_AUTOINCREMENT_IDS.
	 */
	if (map_size == 2) {
		key = mp_decode_uint(data);
		assert(key == SQL_INFO_AUTOINCREMENT_IDS);
		(void)key;
		uint64_t count = mp_decode_array(data);
		assert(count > 0);
		lua_createtable(L, 0, count);
		for (uint32_t j = 0; j < count; ++j) {
			int64_t id = INT64_MIN;
			mp_read_int64(data, &id);
			luaL_pushint64(L, id);
			lua_rawseti(L, -2, j + 1);
		}
		lua_setfield(L, -2,
			     sql_info_key_strs[SQL_INFO_AUTOINCREMENT_IDS]);
	}
}

static void
netbox_decode_execute(struct lua_State *L, const char **data,
		      const char *data_end, struct tuple_format *format)
{
	(void)data_end;
	(void)format;
	assert(mp_typeof(**data) == MP_MAP);
	uint32_t map_size = mp_decode_map(data);
	int rows_index = 0, meta_index = 0, info_index = 0;
	for (uint32_t i = 0; i < map_size; ++i) {
		uint32_t key = mp_decode_uint(data);
		switch(key) {
		case IPROTO_DATA:
			netbox_decode_data(L, data, tuple_format_runtime);
			rows_index = i - map_size;
			break;
		case IPROTO_METADATA:
			netbox_decode_metadata(L, data);
			meta_index = i - map_size;
			break;
		default:
			assert(key == IPROTO_SQL_INFO);
			netbox_decode_sql_info(L, data);
			info_index = i - map_size;
			break;
		}
	}
	if (info_index == 0) {
		assert(meta_index != 0);
		assert(rows_index != 0);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, meta_index - 1);
		lua_setfield(L, -2, "metadata");
		lua_pushvalue(L, rows_index - 1);
		lua_setfield(L, -2, "rows");
	} else {
		assert(meta_index == 0);
		assert(rows_index == 0);
	}
}

static void
netbox_decode_prepare(struct lua_State *L, const char **data,
		      const char *data_end, struct tuple_format *format)
{
	(void)data_end;
	(void)format;
	assert(mp_typeof(**data) == MP_MAP);
	uint32_t map_size = mp_decode_map(data);
	int stmt_id_idx = 0, meta_idx = 0, bind_meta_idx = 0,
	    bind_count_idx = 0;
	uint32_t stmt_id = 0;
	for (uint32_t i = 0; i < map_size; ++i) {
		uint32_t key = mp_decode_uint(data);
		switch(key) {
		case IPROTO_STMT_ID: {
			stmt_id = mp_decode_uint(data);
			luaL_pushuint64(L, stmt_id);
			stmt_id_idx = i - map_size;
			break;
		}
		case IPROTO_METADATA: {
			netbox_decode_metadata(L, data);
			meta_idx = i - map_size;
			break;
		}
		case IPROTO_BIND_METADATA: {
			netbox_decode_metadata(L, data);
			bind_meta_idx = i - map_size;
			break;
		}
		default: {
			assert(key == IPROTO_BIND_COUNT);
			uint32_t bind_count = mp_decode_uint(data);
			luaL_pushuint64(L, bind_count);
			bind_count_idx = i - map_size;
			break;
		}}
	}
	/* These fields must be present in response. */
	assert(stmt_id_idx * bind_meta_idx * bind_count_idx != 0);
	/* General meta is presented only in DQL responses. */
	lua_createtable(L, 0, meta_idx != 0 ? 4 : 3);
	lua_pushvalue(L, stmt_id_idx - 1);
	lua_setfield(L, -2, "stmt_id");
	lua_pushvalue(L, bind_count_idx - 1);
	lua_setfield(L, -2, "param_count");
	lua_pushvalue(L, bind_meta_idx - 1);
	lua_setfield(L, -2, "params");
	if (meta_idx != 0) {
		lua_pushvalue(L, meta_idx - 1);
		lua_setfield(L, -2, "metadata");
	}
}

/**
 * Decodes a response body for the specified method. Pushes the result and the
 * end of the decoded data to Lua stack.
 *
 * Takes the following arguments:
 *  - method: a value from the netbox_method enumeration
 *  - data: pointer to the data to decode (char ptr)
 *  - data_end: pointer to the end of the data (char ptr)
 *  - format: tuple format to use for decoding the body or nil
 */
static int
netbox_decode_method(struct lua_State *L)
{
	typedef void (*method_decoder_f)(struct lua_State *L, const char **data,
					 const char *data_end,
					 struct tuple_format *format);
	static method_decoder_f method_decoder[] = {
		[NETBOX_PING]		= netbox_decode_nil,
		[NETBOX_CALL_16]	= netbox_decode_select,
		[NETBOX_CALL_17]	= netbox_decode_table,
		[NETBOX_EVAL]		= netbox_decode_table,
		[NETBOX_INSERT]		= netbox_decode_tuple,
		[NETBOX_REPLACE]	= netbox_decode_tuple,
		[NETBOX_DELETE]		= netbox_decode_tuple,
		[NETBOX_UPDATE]		= netbox_decode_tuple,
		[NETBOX_UPSERT]		= netbox_decode_nil,
		[NETBOX_SELECT]		= netbox_decode_select,
		[NETBOX_EXECUTE]	= netbox_decode_execute,
		[NETBOX_PREPARE]	= netbox_decode_prepare,
		[NETBOX_UNPREPARE]	= netbox_decode_nil,
		[NETBOX_GET]		= netbox_decode_tuple,
		[NETBOX_MIN]		= netbox_decode_tuple,
		[NETBOX_MAX]		= netbox_decode_tuple,
		[NETBOX_COUNT]		= netbox_decode_value,
		[NETBOX_INJECT]		= netbox_decode_table,
	};
	enum netbox_method method = lua_tointeger(L, 1);
	assert(method < netbox_method_MAX);
	uint32_t ctypeid;
	const char *data = *(const char **)luaL_checkcdata(L, 2, &ctypeid);
	assert(ctypeid == CTID_CHAR_PTR || ctypeid == CTID_CONST_CHAR_PTR);
	const char *data_end = *(const char **)luaL_checkcdata(L, 3, &ctypeid);
	assert(ctypeid == CTID_CHAR_PTR || ctypeid == CTID_CONST_CHAR_PTR);
	struct tuple_format *format;
	if (!lua_isnil(L, 4))
		format = lbox_check_tuple_format(L, 4);
	else
		format = tuple_format_runtime;
	method_decoder[method](L, &data, data_end, format);
	*(const char **)luaL_pushcdata(L, CTID_CONST_CHAR_PTR) = data;
	return 2;
}

int
luaopen_net_box(struct lua_State *L)
{
	static const luaL_Reg net_box_lib[] = {
		{ "encode_auth",    netbox_encode_auth },
		{ "encode_method",  netbox_encode_method },
		{ "decode_greeting",netbox_decode_greeting },
		{ "decode_method",  netbox_decode_method },
		{ "communicate",    netbox_communicate },
		{ NULL, NULL}
	};
	/* luaL_register_module polutes _G */
	lua_newtable(L);
	luaL_openlib(L, NULL, net_box_lib, 0);
	lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
	lua_pushvalue(L, -2);
	lua_setfield(L, -2, "net.box.lib");
	lua_remove(L, -1);
	return 1;
}
