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
#include "box/error.h"
#include "box/mp_error.h"

#include "lua/msgpack.h"
#include <base64.h>

#include "assoc.h"
#include "coio.h"
#include "fiber_cond.h"
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

struct netbox_registry {
	/** Next request id. */
	uint64_t next_sync;
	/** sync -> netbox_request */
	struct mh_i64ptr_t *requests;
};

struct netbox_request {
	enum netbox_method method;
	/**
	 * Unique identifier needed for matching the request with its response.
	 * Used as a key in the registry.
	 */
	uint64_t sync;
	/**
	 * The registry this request belongs to or NULL if the request has been
	 * completed.
	 */
	struct netbox_registry *registry;
	/** Format used for decoding the response (ref incremented). */
	struct tuple_format *format;
	/** Signaled when the response is received. */
	struct fiber_cond cond;
	/**
	 * A user-provided buffer to which the response body should be copied.
	 * If NULL, the response will be decoded to Lua stack.
	 */
	struct ibuf *buffer;
	/**
	 * Lua reference to the buffer. Used to prevent garbage collection in
	 * case the user discards the request.
	 */
	int buffer_ref;
	/**
	 * Whether to skip MessagePack map header and IPROTO_DATA key when
	 * copying the response body to a user-provided buffer. Ignored if
	 * buffer is not set.
	 */
	bool skip_header;
	/** Lua references to on_push trigger and its context. */
	int on_push_ref;
	int on_push_ctx_ref;
	/**
	 * Lua reference to the request result or LUA_NOREF if the response
	 * hasn't been received yet. If the response was decoded to a
	 * user-provided buffer (see buffer_ref), result_ref stores a Lua
	 * reference to an integer value that contains the length of the
	 * decoded data.
	 */
	int result_ref;
	/**
	 * Error if the request failed (ref incremented). NULL on success or if
	 * the response hasn't been received yet.
	 */
	struct error *error;
};

static const char netbox_registry_typename[] = "net.box.registry";
static const char netbox_request_typename[] = "net.box.request";

/**
 * Instead of pushing luaT_netbox_request_iterator_next with lua_pushcclosure
 * in luaT_netbox_request_pairs, we get it by reference, because it works
 * faster.
 */
static int luaT_netbox_request_iterator_next_ref;

static void
netbox_request_destroy(struct netbox_request *request)
{
	assert(request->registry == NULL);
	if (request->format != NULL)
		tuple_format_unref(request->format);
	fiber_cond_destroy(&request->cond);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, request->buffer_ref);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, request->on_push_ref);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, request->on_push_ctx_ref);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, request->result_ref);
	if (request->error != NULL)
		error_unref(request->error);
}

/**
 * Adds a request to a registry. There must not be a request with the same id
 * (sync) in the registry. Returns -1 if out of memory.
 */
static int
netbox_request_register(struct netbox_request *request,
			struct netbox_registry *registry)
{
	struct mh_i64ptr_t *h = registry->requests;
	struct mh_i64ptr_node_t node = { request->sync, request };
	struct mh_i64ptr_node_t *old_node = NULL;
	if (mh_i64ptr_put(h, &node, &old_node, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mhash", "netbox_registry");
		return -1;
	}
	assert(old_node == NULL);
	request->registry = registry;
	return 0;
}

/**
 * Unregisters a previously registered request. Does nothing if the request has
 * already been unregistered or has never been registered.
 */
static void
netbox_request_unregister(struct netbox_request *request)
{
	struct netbox_registry *registry = request->registry;
	if (registry == NULL)
		return;
	request->registry = NULL;
	struct mh_i64ptr_t *h = registry->requests;
	mh_int_t k = mh_i64ptr_find(h, request->sync, NULL);
	assert(k != mh_end(h));
	assert(mh_i64ptr_node(h, k)->val == request);
	mh_i64ptr_del(h, k, NULL);
}

static inline bool
netbox_request_is_ready(const struct netbox_request *request)
{
	return request->registry == NULL;
}

static inline void
netbox_request_signal(struct netbox_request *request)
{
	fiber_cond_broadcast(&request->cond);
}

static inline void
netbox_request_complete(struct netbox_request *request)
{
	netbox_request_unregister(request);
	netbox_request_signal(request);
}

/**
 * Waits on netbox_request::cond. Subtracts the wait time from the timeout.
 * Returns false on timeout or if the fiber was cancelled.
 */
static inline bool
netbox_request_wait(struct netbox_request *request, double *timeout)
{
	double ts = ev_monotonic_now(loop());
	int rc = fiber_cond_wait_timeout(&request->cond, *timeout);
	*timeout -= ev_monotonic_now(loop()) - ts;
	return rc == 0;
}

static inline void
netbox_request_set_result(struct netbox_request *request, int result_ref)
{
	assert(request->result_ref == LUA_NOREF);
	request->result_ref = result_ref;
}

static inline void
netbox_request_set_error(struct netbox_request *request, struct error *error)
{
	assert(request->error == NULL);
	request->error = error;
	error_ref(error);
}

/**
 * Pushes the result or error to Lua stack. See the comment to request.result()
 * for more information about the values pushed.
 */
static int
netbox_request_push_result(struct netbox_request *request, struct lua_State *L)
{
	if (!netbox_request_is_ready(request)) {
		diag_set(ClientError, ER_PROC_LUA, "Response is not ready");
		return luaT_push_nil_and_error(L);
	}
	if (request->error != NULL) {
		assert(request->result_ref == LUA_NOREF);
		diag_set_error(diag_get(), request->error);
		return luaT_push_nil_and_error(L);
	} else {
		assert(request->result_ref != LUA_NOREF);
		lua_rawgeti(L, LUA_REGISTRYINDEX, request->result_ref);
	}
	return 1;
}

static int
netbox_registry_create(struct netbox_registry *registry)
{
	registry->next_sync = 1;
	registry->requests = mh_i64ptr_new();
	if (registry->requests == NULL) {
		diag_set(OutOfMemory, 0, "mhash", "netbox_registry");
		return -1;
	}
	return 0;
}

static void
netbox_registry_destroy(struct netbox_registry *registry)
{
	struct mh_i64ptr_t *h = registry->requests;
	assert(mh_size(h) == 0);
	mh_i64ptr_delete(h);
}

/**
 * Looks up a request by id (sync). Returns NULL if not found.
 */
static inline struct netbox_request *
netbox_registry_lookup(struct netbox_registry *registry, uint64_t sync)
{
	struct mh_i64ptr_t *h = registry->requests;
	mh_int_t k = mh_i64ptr_find(h, sync, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/**
 * Completes all requests in the registry with the given error and cleans up
 * the hash. Called when the associated connection is closed.
 */
static void
netbox_registry_reset(struct netbox_registry *registry, struct error *error)
{
	struct mh_i64ptr_t *h = registry->requests;
	mh_int_t k;
	mh_foreach(h, k) {
		struct netbox_request *request = mh_i64ptr_node(h, k)->val;
		request->registry = NULL;
		netbox_request_set_error(request, error);
		netbox_request_signal(request);
	}
	mh_i64ptr_clear(h);
}

static inline size_t
netbox_begin_encode(struct mpstream *stream, uint64_t sync,
		    enum iproto_type type)
{
	/* Remember initial size of ibuf (see netbox_end_encode()) */
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
netbox_end_encode(struct mpstream *stream, size_t initial_size)
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
	size_t svp = netbox_begin_encode(stream, sync, IPROTO_PING);
	netbox_end_encode(stream, svp);
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
	size_t svp = netbox_begin_encode(&stream, sync, IPROTO_AUTH);

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

	netbox_end_encode(&stream, svp);
	return 0;
}

static void
netbox_encode_call_impl(lua_State *L, int idx, struct mpstream *stream,
			uint64_t sync, enum iproto_type type)
{
	/* Lua stack at idx: function_name, args */
	size_t svp = netbox_begin_encode(stream, sync, type);

	mpstream_encode_map(stream, 2);

	/* encode proc name */
	size_t name_len;
	const char *name = lua_tolstring(L, idx, &name_len);
	mpstream_encode_uint(stream, IPROTO_FUNCTION_NAME);
	mpstream_encode_strn(stream, name, name_len);

	/* encode args */
	mpstream_encode_uint(stream, IPROTO_TUPLE);
	luamp_encode_tuple(L, cfg, stream, idx + 1);

	netbox_end_encode(stream, svp);
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
	size_t svp = netbox_begin_encode(stream, sync, IPROTO_EVAL);

	mpstream_encode_map(stream, 2);

	/* encode expr */
	size_t expr_len;
	const char *expr = lua_tolstring(L, idx, &expr_len);
	mpstream_encode_uint(stream, IPROTO_EXPR);
	mpstream_encode_strn(stream, expr, expr_len);

	/* encode args */
	mpstream_encode_uint(stream, IPROTO_TUPLE);
	luamp_encode_tuple(L, cfg, stream, idx + 1);

	netbox_end_encode(stream, svp);
}

static void
netbox_encode_select(lua_State *L, int idx, struct mpstream *stream,
		     uint64_t sync)
{
	/* Lua stack at idx: space_id, index_id, iterator, offset, limit, key */
	size_t svp = netbox_begin_encode(stream, sync, IPROTO_SELECT);

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

	netbox_end_encode(stream, svp);
}

static void
netbox_encode_insert_or_replace(lua_State *L, int idx, struct mpstream *stream,
				uint64_t sync, enum iproto_type type)
{
	/* Lua stack at idx: space_id, tuple */
	size_t svp = netbox_begin_encode(stream, sync, type);

	mpstream_encode_map(stream, 2);

	/* encode space_id */
	uint32_t space_id = lua_tonumber(L, idx);
	mpstream_encode_uint(stream, IPROTO_SPACE_ID);
	mpstream_encode_uint(stream, space_id);

	/* encode args */
	mpstream_encode_uint(stream, IPROTO_TUPLE);
	luamp_encode_tuple(L, cfg, stream, idx + 1);

	netbox_end_encode(stream, svp);
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
	size_t svp = netbox_begin_encode(stream, sync, IPROTO_DELETE);

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

	netbox_end_encode(stream, svp);
}

static void
netbox_encode_update(lua_State *L, int idx, struct mpstream *stream,
		     uint64_t sync)
{
	/* Lua stack at idx: space_id, index_id, key, ops */
	size_t svp = netbox_begin_encode(stream, sync, IPROTO_UPDATE);

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

	netbox_end_encode(stream, svp);
}

static void
netbox_encode_upsert(lua_State *L, int idx, struct mpstream *stream,
		     uint64_t sync)
{
	/* Lua stack at idx: space_id, tuple, ops */
	size_t svp = netbox_begin_encode(stream, sync, IPROTO_UPSERT);

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

	netbox_end_encode(stream, svp);
}

static int
netbox_decode_greeting(lua_State *L)
{
	struct greeting greeting;
	size_t len;
	const char *buf = NULL;

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

	luaL_pushuuidstr(L, &greeting.uuid);
	lua_setfield(L, -2, "uuid");

	return 1;
}

/**
 * Reads data from the given socket until the limit or boundary is reached.
 * Returns 0 and sets *size to limit or boundary position on success.
 * On error returns -1 and sets diag.
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
netbox_communicate(int fd, struct ibuf *send_buf, struct ibuf *recv_buf,
		   size_t limit, const void *boundary, size_t boundary_len,
		   size_t *size)
{
	const int NETBOX_READAHEAD = 16320;
	int revents = COIO_READ;
	while (true) {
		/* reader serviced first */
check_limit:
		if (ibuf_used(recv_buf) >= limit) {
			*size = limit;
			return 0;
		}
		const char *p;
		if (boundary != NULL && (p = memmem(
					recv_buf->rpos,
					ibuf_used(recv_buf),
					boundary, boundary_len)) != NULL) {
			*size = p - recv_buf->rpos;
			return 0;
		}

		while (revents & COIO_READ) {
			void *p = ibuf_reserve(recv_buf, NETBOX_READAHEAD);
			if (p == NULL) {
				diag_set(OutOfMemory, NETBOX_READAHEAD,
					 "ibuf_reserve", "p");
				return -1;
			}
			ssize_t rc = recv(
				fd, recv_buf->wpos, ibuf_unused(recv_buf), 0);
			if (rc == 0) {
				box_error_raise(ER_NO_CONNECTION,
						"Peer closed");
				return -1;
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

		ERROR_INJECT_YIELD(ERRINJ_NETBOX_IO_DELAY);
		revents = coio_wait(fd, EV_READ | (ibuf_used(send_buf) != 0 ?
				EV_WRITE : 0), TIMEOUT_INFINITY);
		if (fiber_is_cancelled()) {
			diag_set(FiberIsCancelled);
			return -1;
		}
	}
handle_error:
	box_error_raise(ER_NO_CONNECTION, "%s", strerror(errno));
	return -1;
}

/**
 * Sends and receives data over an iproto connection.
 * Takes socket fd, send_buf (ibuf), recv_buf (ibuf).
 * On success returns header (table), body_rpos (char *), body_end (char *).
 * On error returns nil, error.
 */
static int
netbox_send_and_recv_iproto(lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	struct ibuf *send_buf = (struct ibuf *)lua_topointer(L, 2);
	struct ibuf *recv_buf = (struct ibuf *)lua_topointer(L, 3);
	while (true) {
		size_t required;
		size_t data_len = ibuf_used(recv_buf);
		size_t fixheader_size = mp_sizeof_uint(UINT32_MAX);
		if (data_len < fixheader_size) {
			required = fixheader_size;
		} else {
			/* PWN! insufficient input validation */
			const char *bufpos = recv_buf->rpos;
			const char *rpos = bufpos;
			size_t len = mp_decode_uint(&rpos);
			required = (rpos - bufpos) + len;
			if (data_len >= required) {
				const char *body_end = rpos + len;
				const char *body_rpos = rpos;
				luamp_decode(L, cfg, &body_rpos);
				*(const char **)luaL_pushcdata(
					L, CTID_CONST_CHAR_PTR) = body_rpos;
				*(const char **)luaL_pushcdata(
					L, CTID_CONST_CHAR_PTR) = body_end;
				recv_buf->rpos = (char *)body_end;
				return 3;
			}
		}
		size_t unused;
		if (netbox_communicate(fd, send_buf, recv_buf,
				       /*limit=*/required, /*boundary=*/NULL,
				       /*boundary_len=*/0, &unused) != 0) {
			luaL_testcancel(L);
			return luaT_push_nil_and_error(L);
		}
	}
}

/**
 * Sends and receives data over a console connection.
 * Returns a pointer to a response string and its len.
 * On error returns NULL.
 */
static const char *
netbox_send_and_recv_console(int fd, struct ibuf *send_buf,
			     struct ibuf *recv_buf, size_t *response_len)
{
	const char delim[] = "\n...\n";
	size_t delim_len = sizeof(delim) - 1;
	size_t delim_pos;
	if (netbox_communicate(fd, send_buf, recv_buf, /*limit=*/SIZE_MAX,
			       delim, delim_len, &delim_pos) != 0) {
		return NULL;
	}
	const char *response = recv_buf->rpos;
	recv_buf->rpos += delim_pos + delim_len;
	*response_len = delim_pos + delim_len;
	return response;
}

static void
netbox_encode_execute(lua_State *L, int idx, struct mpstream *stream,
		      uint64_t sync)
{
	/* Lua stack at idx: query, parameters, options */
	size_t svp = netbox_begin_encode(stream, sync, IPROTO_EXECUTE);

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

	netbox_end_encode(stream, svp);
}

static void
netbox_encode_prepare(lua_State *L, int idx, struct mpstream *stream,
		      uint64_t sync)
{
	/* Lua stack at idx: query */
	size_t svp = netbox_begin_encode(stream, sync, IPROTO_PREPARE);

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

	netbox_end_encode(stream, svp);
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
 * Decodes a response body for the specified method and pushes the result to
 * Lua stack.
 */
static void
netbox_decode_method(struct lua_State *L, enum netbox_method method,
		     const char **data, const char *data_end,
		     struct tuple_format *format)
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
	method_decoder[method](L, data, data_end, format);
}

/**
 * Decodes an error from raw data. On success returns the decoded error object
 * with ref counter incremented. On failure returns NULL.
 */
static struct error *
netbox_decode_error(const char **data, uint32_t errcode)
{
	struct error *error = NULL;
	assert(mp_typeof(**data) == MP_MAP);
	uint32_t map_size = mp_decode_map(data);
	for (uint32_t i = 0; i < map_size; ++i) {
		uint32_t key = mp_decode_uint(data);
		if (key == IPROTO_ERROR) {
			if (error != NULL)
				error_unref(error);
			error = error_unpack_unsafe(data);
			if (error == NULL)
				return NULL;
			error_ref(error);
			/*
			 * IPROTO_ERROR comprises error encoded with
			 * IPROTO_ERROR_24, so we may ignore content
			 * of that key.
			 */
			break;
		} else if (key == IPROTO_ERROR_24) {
			if (error != NULL)
				error_unref(error);
			const char *reason = "";
			uint32_t reason_len = 0;
			if (mp_typeof(**data) == MP_STR)
				reason = mp_decode_str(data, &reason_len);
			box_error_raise(errcode, "%.*s", reason_len, reason);
			error = box_error_last();
			error_ref(error);
			continue;
		}
		/* Skip value. */
		mp_next(data);
	}
	if (error == NULL) {
		/*
		 * Error body is missing in the response.
		 * Set the error code without a 'reason' message
		 */
		box_error_raise(errcode, "");
		error = box_error_last();
		error_ref(error);
	}
	return error;
}

static inline struct netbox_registry *
luaT_check_netbox_registry(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, netbox_registry_typename);
}

static int
luaT_netbox_registry_gc(struct lua_State *L)
{
	struct netbox_registry *registry = luaT_check_netbox_registry(L, 1);
	netbox_registry_destroy(registry);
	return 0;
}

/** Allocates a new id (sync). */
static int
luaT_netbox_registry_new_id(struct lua_State *L)
{
	struct netbox_registry *registry = luaT_check_netbox_registry(L, 1);
	luaL_pushuint64(L, registry->next_sync++);
	return 1;
}

static int
luaT_netbox_registry_reset(struct lua_State *L)
{
	struct netbox_registry *registry = luaT_check_netbox_registry(L, 1);
	struct error *error = luaL_checkerror(L, 2);
	netbox_registry_reset(registry, error);
	return 0;
}

static inline struct netbox_request *
luaT_check_netbox_request(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, netbox_request_typename);
}

static int
luaT_netbox_request_gc(struct lua_State *L)
{
	struct netbox_request *request = luaT_check_netbox_request(L, 1);
	netbox_request_unregister(request);
	netbox_request_destroy(request);
	return 0;
}

/**
 * Returns true if the response was received for the given request.
 */
static int
luaT_netbox_request_is_ready(struct lua_State *L)
{
	struct netbox_request *request = luaT_check_netbox_request(L, 1);
	lua_pushboolean(L, netbox_request_is_ready(request));
	return 1;
}

/**
 * Obtains the result of the given request.
 *
 * Returns:
 *  - nil, error             if the response failed or not ready
 *  - response body (table)  if the response is ready and buffer is nil
 *  - body length in bytes   if the response was written to the buffer
 */
static int
luaT_netbox_request_result(struct lua_State *L)
{
	struct netbox_request *request = luaT_check_netbox_request(L, 1);
	return netbox_request_push_result(request, L);
}

/**
 * Waits until the response is received for the given request and obtains the
 * result. Takes an optional timeout argument.
 *
 * See the comment to request.result() for the return value format.
 */
static int
luaT_netbox_request_wait_result(struct lua_State *L)
{
	struct netbox_request *request = luaT_check_netbox_request(L, 1);
	double timeout = TIMEOUT_INFINITY;
	if (!lua_isnoneornil(L, 2)) {
		if (lua_type(L, 2) != LUA_TNUMBER ||
		    (timeout = lua_tonumber(L, 2)) < 0)
			luaL_error(L, "Usage: future:wait_result(timeout)");
	}
	while (!netbox_request_is_ready(request)) {
		if (!netbox_request_wait(request, &timeout)) {
			luaL_testcancel(L);
			diag_set(ClientError, ER_TIMEOUT);
			return luaT_push_nil_and_error(L);
		}
	}
	return netbox_request_push_result(request, L);
}

/**
 * Makes the connection forget about the given request. When the response is
 * received, it will be ignored. It reduces the size of the request registry
 * speeding up other requests.
 */
static int
luaT_netbox_request_discard(struct lua_State *L)
{
	struct netbox_request *request = luaT_check_netbox_request(L, 1);
	if (!netbox_request_is_ready(request)) {
		diag_set(ClientError, ER_PROC_LUA, "Response is discarded");
		netbox_request_set_error(request, diag_last_error(diag_get()));
		netbox_request_complete(request);
	}
	return 0;
}

/**
 * Gets the next message or the final result. Takes the index of the last
 * returned message as a second argument. The request and timeout are passed in
 * the first argument as a table (see request.pairs()).
 *
 * On success returns the index of the current message (used by the iterator
 * implementation to continue iteration) and an object, which is either the
 * message pushed with box.session.push() or the final response. If there's no
 * more messages left for the request, returns nil, nil.
 *
 * On error returns box.NULL, error. We return box.NULL instead of nil to
 * distinguish end of iteration from error when this function is called in
 * `for k, v in future:pairs()`, because nil is treated by Lua as end of
 * iteration marker.
 */
static int
luaT_netbox_request_iterator_next(struct lua_State *L)
{
	/* The first argument is a table: {request, timeout}. */
	lua_rawgeti(L, 1, 1);
	struct netbox_request *request = luaT_check_netbox_request(L, -1);
	lua_rawgeti(L, 1, 2);
	double timeout = lua_tonumber(L, -1);
	/* The second argument is the index of the last returned message. */
	if (luaL_isnull(L, 2)) {
		/* The previous call returned an error. */
		goto stop;
	}
	int i = lua_tointeger(L, 2) + 1;
	/*
	 * In the async mode (and this is the async mode, because 'future'
	 * objects are not available to the user in the sync mode), on_push_ctx
	 * refers to a table that contains received messages. We iterate over
	 * the content of the table.
	 */
	lua_rawgeti(L, LUA_REGISTRYINDEX, request->on_push_ctx_ref);
	int messages_idx = lua_gettop(L);
	assert(lua_istable(L, messages_idx));
	int message_count = lua_objlen(L, messages_idx);
retry:
	if (i <= message_count) {
		lua_pushinteger(L, i);
		lua_rawgeti(L, messages_idx, i);
		return 2;
	}
	if (netbox_request_is_ready(request)) {
		/*
		 * After all the messages are iterated, `i` is equal to
		 * #messages + 1. After we return the response, `i` becomes
		 * #messages + 2. It is the trigger to finish the iteration.
		 */
		if (i > message_count + 1)
			goto stop;
		int n = netbox_request_push_result(request, L);
		if (n == 2)
			goto error;
		/* Success. Return i, response. */
		assert(n == 1);
		lua_pushinteger(L, i);
		lua_insert(L, -2);
		return 2;
	}
	int old_message_count = message_count;
	do {
		if (!netbox_request_wait(request, &timeout)) {
			luaL_testcancel(L);
			diag_set(ClientError, ER_TIMEOUT);
			luaT_push_nil_and_error(L);
			goto error;
		}
		message_count = lua_objlen(L, messages_idx);
	} while (!netbox_request_is_ready(request) &&
		 message_count == old_message_count);
	goto retry;
stop:
	lua_pushnil(L);
	lua_pushnil(L);
	return 2;
error:
	/*
	 * When we get here, the top two elements on the stack are nil, error.
	 * We need to replace nil with box.NULL.
	 */
	luaL_pushnull(L);
	lua_replace(L, -3);
	return 2;
}

static int
luaT_netbox_request_pairs(struct lua_State *L)
{
	if (!lua_isnoneornil(L, 2)) {
		if (lua_type(L, 2) != LUA_TNUMBER || lua_tonumber(L, 2) < 0)
			luaL_error(L, "Usage: future:pairs(timeout)");
	} else {
		if (lua_isnil(L, 2))
			lua_pop(L, 1);
		lua_pushnumber(L, TIMEOUT_INFINITY);
	}
	lua_settop(L, 2);
	/* Create a table passed to next(): {request, timeout}. */
	lua_createtable(L, 2, 0);
	lua_insert(L, 1);
	lua_rawseti(L, 1, 2); /* timeout */
	lua_rawseti(L, 1, 1); /* request */
	/* Push the next() function. It must go first. */
	lua_rawgeti(L, LUA_REGISTRYINDEX,
		    luaT_netbox_request_iterator_next_ref);
	lua_insert(L, 1);
	/* Push the iterator index. */
	lua_pushinteger(L, 0);
	return 3;
}

/**
 * Creates a request registry object (userdata) and pushes it to Lua stack.
 */
static int
netbox_new_registry(struct lua_State *L)
{
	struct netbox_registry *registry = lua_newuserdata(
		L, sizeof(*registry));
	if (netbox_registry_create(registry) != 0)
		luaT_error(L);
	luaL_getmetatable(L, netbox_registry_typename);
	lua_setmetatable(L, -2);
	return 1;
}

/**
 * Creates a request object (userdata) and pushes it to Lua stack.
 *
 * Takes the following arguments:
 *  - requests: registry to register the new request with
 *  - id: id (sync) to assign to the new request
 *  - buffer: buffer (ibuf) to write the result to or nil
 *  - skip_header: whether to skip header when writing the result to the buffer
 *  - method: a value from the netbox_method enumeration
 *  - on_push: on_push trigger function
 *  - on_push_ctx: on_push trigger function argument
 *  - format: tuple format to use for decoding the body or nil
 */
static int
netbox_new_request(struct lua_State *L)
{
	struct netbox_request *request = lua_newuserdata(L, sizeof(*request));
	struct netbox_registry *registry = luaT_check_netbox_registry(L, 1);
	request->sync = luaL_touint64(L, 2);
	request->buffer = (struct ibuf *)lua_topointer(L, 3);
	lua_pushvalue(L, 3);
	request->buffer_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	request->skip_header = lua_toboolean(L, 4);
	request->method = lua_tointeger(L, 5);
	assert(request->method < netbox_method_MAX);
	lua_pushvalue(L, 6);
	request->on_push_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushvalue(L, 7);
	request->on_push_ctx_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	if (!lua_isnil(L, 8))
		request->format = lbox_check_tuple_format(L, 8);
	else
		request->format = tuple_format_runtime;
	tuple_format_ref(request->format);
	fiber_cond_create(&request->cond);
	request->result_ref = LUA_NOREF;
	request->error = NULL;
	if (netbox_request_register(request, registry) != 0) {
		netbox_request_destroy(request);
		luaT_error(L);
	}
	luaL_getmetatable(L, netbox_request_typename);
	lua_setmetatable(L, -2);
	return 1;
}

/**
 * Given a request registry, request id (sync), status, and a pointer to a
 * response body, decodes the response and either completes the request or
 * invokes the on-push trigger, depending on the status.
 */
static int
netbox_dispatch_response_iproto(struct lua_State *L)
{
	struct netbox_registry *registry = luaT_check_netbox_registry(L, 1);
	uint64_t sync = luaL_touint64(L, 2);
	enum iproto_type status = lua_tointeger(L, 3);
	uint32_t ctypeid;
	const char *data = *(const char **)luaL_checkcdata(L, 4, &ctypeid);
	assert(ctypeid == CTID_CHAR_PTR || ctypeid == CTID_CONST_CHAR_PTR);
	const char *data_end = *(const char **)luaL_checkcdata(L, 5, &ctypeid);
	assert(ctypeid == CTID_CHAR_PTR || ctypeid == CTID_CONST_CHAR_PTR);
	struct netbox_request *request = netbox_registry_lookup(registry, sync);
	if (request == NULL) {
		/* Nobody is waiting for the response. */
		return 0;
	}
	if (status > IPROTO_CHUNK) {
		/* Handle errors. */
		struct error *error = netbox_decode_error(
			&data, status & (IPROTO_TYPE_ERROR - 1));
		if (error == NULL)
			return luaT_error(L);
		netbox_request_set_error(request, error);
		error_unref(error);
		netbox_request_complete(request);
		return 0;
	}
	if (request->buffer != NULL) {
		/* Copy xrow.body to user-provided buffer. */
		if (request->skip_header)
			netbox_skip_to_data(&data);
		size_t data_len = data_end - data;
		void *wpos = ibuf_alloc(request->buffer, data_len);
		if (wpos == NULL)
			luaL_error(L, "out of memory");
		memcpy(wpos, data, data_len);
		lua_pushinteger(L, data_len);
	} else {
		/* Decode xrow.body[DATA] to Lua objects. */
		if (status == IPROTO_OK) {
			netbox_decode_method(L, request->method, &data,
					     data_end, request->format);
		} else {
			netbox_decode_value(L, &data, data_end,
					    request->format);
		}
		assert(data == data_end);
	}
	if (status == IPROTO_OK) {
		/*
		 * We received the final response and pushed it to Lua stack.
		 * Store a reference to it in the request, remove the request
		 * from the registry, and wake up waiters.
		 */
		netbox_request_set_result(request,
					  luaL_ref(L, LUA_REGISTRYINDEX));
		netbox_request_complete(request);
	} else {
		/* We received a push. Invoke on_push trigger. */
		lua_rawgeti(L, LUA_REGISTRYINDEX, request->on_push_ref);
		lua_rawgeti(L, LUA_REGISTRYINDEX, request->on_push_ctx_ref);
		/* Push the received message as the second argument. */
		lua_pushvalue(L, -3);
		lua_call(L, 2, 0);
		netbox_request_signal(request);
	}
	return 0;
}

/**
 * Given a request registry, request id (sync), and a response string, assigns
 * the response to the request and completes it.
 *
 * Lua stack is used for temporarily storing the response string before getting
 * a reference to it.
 */
static void
netbox_dispatch_response_console(struct lua_State *L,
				 struct netbox_registry *registry,
				 uint64_t sync, const char *response,
				 size_t response_len)
{
	struct netbox_request *request = netbox_registry_lookup(registry, sync);
	if (request == NULL) {
		/* Nobody is waiting for the response. */
		return;
	}
	lua_pushlstring(L, response, response_len);
	netbox_request_set_result(request, luaL_ref(L, LUA_REGISTRYINDEX));
	netbox_request_complete(request);
}

/**
 * Sets up console delimiter. Should be called before serving any requests.
 * Takes socket fd, send_buf (ibuf), recv_buf (ibuf).
 * Returns none on success, error on failure.
 */
static int
netbox_console_setup(struct lua_State *L)
{
	const char *setup_delimiter_cmd =
		"require('console').delimiter('$EOF$')\n";
	const size_t setup_delimiter_cmd_len = strlen(setup_delimiter_cmd);
	const char *ok_response = "---\n...\n";
	const size_t ok_response_len = strlen(ok_response);
	int fd = lua_tointeger(L, 1);
	struct ibuf *send_buf = (struct ibuf *)lua_topointer(L, 2);
	struct ibuf *recv_buf = (struct ibuf *)lua_topointer(L, 3);
	void *wpos = ibuf_alloc(send_buf, setup_delimiter_cmd_len);
	if (wpos == NULL)
		return luaL_error(L, "out of memory");
	memcpy(wpos, setup_delimiter_cmd, setup_delimiter_cmd_len);
	size_t response_len;
	const char *response = netbox_send_and_recv_console(
		fd, send_buf, recv_buf, &response_len);
	if (response == NULL) {
		luaL_testcancel(L);
		goto error;
	}
	if (response_len != ok_response_len ||
	    strncmp(response, ok_response, ok_response_len) != 0) {
		box_error_raise(ER_NO_CONNECTION, "Unexpected response");
		goto error;
	}
	return 0;
error:
	luaT_pusherror(L, box_error_last());
	return 1;
}

/**
 * Processes console requests in a loop until an error.
 * Takes request registry, socket fd, send_buf (ibuf), recv_buf (ibuf).
 * Returns the error that broke the loop.
 */
static int
netbox_console_loop(struct lua_State *L)
{
	struct netbox_registry *registry = luaT_check_netbox_registry(L, 1);
	int fd = lua_tointeger(L, 2);
	struct ibuf *send_buf = (struct ibuf *)lua_topointer(L, 3);
	struct ibuf *recv_buf = (struct ibuf *)lua_topointer(L, 4);
	uint64_t sync = registry->next_sync;
	while (true) {
		size_t response_len;
		const char *response = netbox_send_and_recv_console(
			fd, send_buf, recv_buf, &response_len);
		if (response == NULL) {
			luaL_testcancel(L);
			luaT_pusherror(L, box_error_last());
			return 1;
		}
		netbox_dispatch_response_console(L, registry, sync++,
						 response, response_len);
	}
}

int
luaopen_net_box(struct lua_State *L)
{
	lua_pushcfunction(L, luaT_netbox_request_iterator_next);
	luaT_netbox_request_iterator_next_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	static const struct luaL_Reg netbox_registry_meta[] = {
		{ "__gc",           luaT_netbox_registry_gc },
		{ "new_id",         luaT_netbox_registry_new_id },
		{ "reset",          luaT_netbox_registry_reset },
		{ NULL, NULL }
	};
	luaL_register_type(L, netbox_registry_typename, netbox_registry_meta);

	static const struct luaL_Reg netbox_request_meta[] = {
		{ "__gc",           luaT_netbox_request_gc },
		{ "is_ready",       luaT_netbox_request_is_ready },
		{ "result",         luaT_netbox_request_result },
		{ "wait_result",    luaT_netbox_request_wait_result },
		{ "discard",        luaT_netbox_request_discard },
		{ "pairs",          luaT_netbox_request_pairs },
		{ NULL, NULL }
	};
	luaL_register_type(L, netbox_request_typename, netbox_request_meta);

	static const luaL_Reg net_box_lib[] = {
		{ "encode_auth",    netbox_encode_auth },
		{ "encode_method",  netbox_encode_method },
		{ "decode_greeting",netbox_decode_greeting },
		{ "send_and_recv_iproto", netbox_send_and_recv_iproto },
		{ "new_registry",   netbox_new_registry },
		{ "new_request",    netbox_new_request },
		{ "dispatch_response_iproto", netbox_dispatch_response_iproto },
		{ "console_setup",  netbox_console_setup },
		{ "console_loop",   netbox_console_loop },
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
