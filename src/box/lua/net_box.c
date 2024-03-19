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

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "box/authentication.h"
#include "box/errcode.h"
#include "box/iproto_constants.h"
#include "box/iproto_features.h"
#include "box/lua/tuple.h" /* luamp_convert_tuple() / luamp_convert_key() */
#include "box/lua/tuple_format.h"
#include "box/xrow.h"
#include "box/tuple.h"
#include "box/execute.h"
#include "box/error.h"
#include "box/schema_def.h"
#include "box/mp_box_ctx.h"
#include "box/mp_tuple.h"

#include "coio.h"
#include "fiber.h"
#include "fiber_cond.h"
#include "iostream.h"
#include "lua/fiber.h"
#include "lua/fiber_cond.h"
#include "lua/msgpack.h"
#include "lua/uri.h"
#include "lua/utils.h"
#include "msgpuck.h"
#include "small/ibuf.h"
#include "small/region.h"
#include "mpstream/mpstream.h"
#include "uri/uri.h"
#include "version.h"

#define cfg luaL_msgpack_default

enum {
	/**
	 * connect() timeout used by default, in seconds.
	 */
	NETBOX_DEFAULT_CONNECT_TIMEOUT = 10,
	/**
	 * Send and receive buffers readahead.
	 */
	NETBOX_READAHEAD = 16320,
	/**
	 * IPROTO protocol version supported by the netbox connector.
	 */
	NETBOX_IPROTO_VERSION = 7,
};

/**
 * IPROTO protocol features supported by the netbox connector.
 */
static struct iproto_features NETBOX_IPROTO_FEATURES;

#define NETBOX_METHODS(_)						\
	_(PING)								\
	_(CALL)								\
	_(EVAL)								\
	_(INSERT)							\
	_(REPLACE)							\
	_(DELETE)							\
	_(UPDATE)							\
	_(UPSERT)							\
	_(SELECT)							\
	_(SELECT_WITH_POS)						\
	_(EXECUTE)							\
	_(PREPARE)							\
	_(UNPREPARE)							\
	_(GET)								\
	_(MIN)								\
	_(MAX)								\
	_(COUNT)							\
	_(BEGIN)							\
	_(COMMIT)							\
	_(ROLLBACK)							\
	_(WATCH_ONCE)							\
	_(INJECT)							\

#define NETBOX_METHOD_MEMBER(s) \
	NETBOX_ ## s,

enum netbox_method {
	NETBOX_METHODS(NETBOX_METHOD_MEMBER)
	netbox_method_MAX
};

#define NETBOX_METHOD_STRS_MEMBER(s) \
	[NETBOX_ ## s] = #s,

static const char *netbox_method_strs[netbox_method_MAX] = {
	NETBOX_METHODS(NETBOX_METHOD_STRS_MEMBER)
};

enum netbox_state {
	NETBOX_INITIAL           = 0,
	NETBOX_AUTH              = 1,
	NETBOX_FETCH_SCHEMA      = 2,
	NETBOX_ACTIVE            = 3,
	NETBOX_ERROR             = 4,
	NETBOX_ERROR_RECONNECT   = 5,
	NETBOX_CLOSED            = 6,
	NETBOX_GRACEFUL_SHUTDOWN = 7,
	netbox_state_MAX,
};

static const char *netbox_state_str[] = {
	[NETBOX_INITIAL]         = "initial",
	[NETBOX_AUTH]            = "auth",
	[NETBOX_FETCH_SCHEMA]    = "fetch_schema",
	[NETBOX_ACTIVE]          = "active",
	[NETBOX_ERROR]           = "error",
	[NETBOX_ERROR_RECONNECT] = "error_reconnect",
	[NETBOX_CLOSED]          = "closed",
	[NETBOX_GRACEFUL_SHUTDOWN] = "graceful_shutdown",
};

struct netbox_options {
	/** Remote server URI. Nil if this connection was created from fd. */
	struct uri uri;
	/** Connection fd. -1 if this connection was created from URI. */
	int fd;
	/** Authentication method. NULL if unspecified. */
	const struct auth_method *auth_method;
	/** User credentials. */
	char *user;
	char *password;
	/**
	 * Lua reference to the transport callback function
	 * (see the comment to netbox_transport).
	 */
	int callback_ref;
	/** connect() timeout, in seconds. */
	double connect_timeout;
	/**
	 * Timeout to wait after a connection failure before trying to
	 * reconnect, in seconds. Reconnect is disabled if it's 0.
	 */
	double reconnect_after;
	/**
	 * Flag that determines is it required to fetch server schema or not.
	 */
	 bool fetch_schema;
};

/**
 * Basically, *transport* is a TCP connection speaking the Tarantool
 * network protocol (IPROTO). This is a low-level interface.
 * Primary features:
 *  * concurrent perform_request()-s benefit from multiplexing
 *    support in the protocol;
 *  * schema-aware - snoops responses and initiates schema reload
 *    when a response has a new schema version;
 *  * delivers transport events via the callback.
 *
 * Transport state machine:
 *
 * State machine starts in 'initial' state. Start method
 * spawns a worker fiber, which will establish a connection.
 * Stop method sets the state to 'closed' and kills the worker.
 * If the transport is already in 'error' state stop() does
 * nothing.
 *
 * State chart:
 *
 *  initial -> auth -> fetch_schema <-> active
 *
 *  fetch_schema, active -> graceful_shutdown
 *
 *  (any state, on error) -> error_reconnect -> auth -> ...
 *                                           \
 *                                            -> error
 *  (any state, but 'error') -> closed
 *
 * State machine is switched to 'graceful_shutdown' state when it
 * receives a 'box.shutdown' event from the remote host. In this state,
 * no new requests are allowed, and once all in-progress requests
 * have completed, the state machine will be switched to 'error' or
 * 'error_reconnect' state, depending on whether reconnect_after is
 * set.
 *
 * State change events can be delivered to the transport user via
 * the optional 'callback' argument:
 *
 * The callback functions needs to have the following signature:
 *
 *  callback(event_name, ...)
 *
 * The following events are delivered, with arguments:
 *
 *  'state_changed', state, error
 *  'handshake', greeting, version, features
 *  'did_fetch_schema', schema_version, spaces, indices, collations
 *  'event', key, value
 *  'shutdown'
 */
struct netbox_transport {
	/** Connection options. Not modified after initialization. */
	struct netbox_options opts;
	/** Greeting received from the remote host. */
	struct greeting greeting;
	/** Features supported by the server as reported by IPROTO_ID. */
	struct iproto_features features;
	/** Default authentication method reported by IPROTO_ID. */
	const struct auth_method *auth_method_default;
	/** Connection state. */
	enum netbox_state state;
	/**
	 * The connection is closing. No new requests are allowed.
	 * The connection will be closed as soon as all pending requests
	 * have been sent.
	 */
	bool is_closing;
	/** Error that caused the last connection failure or NULL. */
	struct error *last_error;
	/** Fiber doing I/O and dispatching responses. */
	struct fiber *worker;
	/**
	 * Lua reference to the Lua state used by the worker fiber or
	 * LUA_NOREF if the worker fiber isn't running.
	 */
	int coro_ref;
	/**
	 * Lua reference to self or LUA_NOREF. Needed to prevent garbage
	 * collection of this transport object while the worker fiber is
	 * running.
	 */
	int self_ref;
	/** Connection I/O stream context. */
	struct iostream_ctx io_ctx;
	/** Connection I/O stream. */
	struct iostream io;
	/** Connection send buffer. */
	struct ibuf send_buf;
	/** Connection receive buffer. */
	struct ibuf recv_buf;
	/** Size of the last received message. */
	size_t last_msg_size;
	/** Signalled when send_buf becomes empty. */
	struct fiber_cond on_send_buf_empty;
	/** Next request id. */
	uint64_t next_sync;
	/** sync -> netbox_request */
	struct mh_i64ptr_t *requests;
	/**
	 * Number of requests to which the server hasn't responded yet.
	 * Note, it may be greater than the number of entries in the request
	 * map, because a request is removed from the map when it's discarded
	 * by the user.
	 */
	int64_t inprogress_request_count;
};

struct netbox_request {
	enum netbox_method method;
	/**
	 * Unique identifier needed for matching the request with its response.
	 * Used as a key in netbox_transport::requests.
	 */
	uint64_t sync;
	/**
	 * The transport this request belongs to or NULL if the request has
	 * been completed.
	 */
	struct netbox_transport *transport;
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
	/**
	 * If this flag is set, the response data won't be decoded - instead,
	 * a msgpack object will be returned to the caller.
	 */
	bool return_raw;
	/** Lua references to on_push trigger and its context. */
	int on_push_ref;
	int on_push_ctx_ref;
	/**
	 * Lua reference to a table with user-defined fields.
	 * We allow the user to attach extra information to a future object,
	 * e.g. a reference to a connection or the invoked method name/args.
	 * All the information is stored in this table, which is created
	 * lazily, on the first __newindex invocation. Until then, it's
	 * LUA_NOREF.
	 */
	int index_ref;
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

/*
 * Context for encoding a netbox method.
 */
struct netbox_method_encode_ctx {
	/* MsgPack stream to which the method is encoded. */
	struct mpstream *stream;
	/* Current transport's IPROTO sync. */
	uint64_t sync;
	/* Current transport's IPROTO stream identifier. */
	uint64_t stream_id;
	/*
	 * Whether box tuples should be encoded to MsgPack as MP_TUPLE
	 * extension.
	 */
	bool box_tuple_arg_as_ext;
};

static const char netbox_transport_typename[] = "net.box.transport";
static const char netbox_request_typename[] = "net.box.request";

/**
 * We keep a reference to each C function that is frequently called with
 * lua_call so as not to create a new Lua object each time we call it.
 */
static int luaT_netbox_request_iterator_next_ref = LUA_NOREF;

static void
netbox_request_destroy(struct netbox_request *request)
{
	assert(request->transport == NULL);
	if (request->format != NULL)
		tuple_format_unref(request->format);
	fiber_cond_destroy(&request->cond);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, request->buffer_ref);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, request->on_push_ref);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, request->on_push_ctx_ref);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, request->result_ref);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, request->index_ref);
	if (request->error != NULL)
		error_unref(request->error);
}

/**
 * Adds a request to a transport. There must not be a request with the same id
 * (sync) in the transport.
 */
static void
netbox_request_register(struct netbox_request *request,
			struct netbox_transport *transport)
{
	struct mh_i64ptr_t *h = transport->requests;
	struct mh_i64ptr_node_t node = { request->sync, request };
	struct mh_i64ptr_node_t *old_node = NULL;
	mh_i64ptr_put(h, &node, &old_node, NULL);
	assert(old_node == NULL);
	request->transport = transport;
}

/**
 * Unregisters a previously registered request. Does nothing if the request has
 * already been unregistered or has never been registered.
 */
static void
netbox_request_unregister(struct netbox_request *request)
{
	struct netbox_transport *transport = request->transport;
	if (transport == NULL)
		return;
	request->transport = NULL;
	struct mh_i64ptr_t *h = transport->requests;
	mh_int_t k = mh_i64ptr_find(h, request->sync, NULL);
	assert(k != mh_end(h));
	assert(mh_i64ptr_node(h, k)->val == request);
	mh_i64ptr_del(h, k, NULL);
}

static inline bool
netbox_request_is_ready(const struct netbox_request *request)
{
	return request->transport == NULL;
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
	/*
	 * Waiting for a request completion in the net.box worker fiber
	 * would result in a dead lock.
	 */
	assert(request->transport != NULL &&
	       request->transport->worker != fiber());
	if (*timeout == 0)
		return false;
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

static void
netbox_options_create(struct netbox_options *opts)
{
	memset(opts, 0, sizeof(*opts));
	uri_create(&opts->uri, NULL);
	opts->fd = -1;
	opts->auth_method = NULL;
	opts->callback_ref = LUA_NOREF;
	opts->connect_timeout = NETBOX_DEFAULT_CONNECT_TIMEOUT;
	opts->fetch_schema = true;
}

static void
netbox_options_destroy(struct netbox_options *opts)
{
	uri_destroy(&opts->uri);
	free(opts->user);
	free(opts->password);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, opts->callback_ref);
}

static void
netbox_transport_create(struct netbox_transport *transport)
{
	netbox_options_create(&transport->opts);
	memset(&transport->greeting, 0, sizeof(transport->greeting));
	iproto_features_create(&transport->features);
	transport->auth_method_default = AUTH_METHOD_DEFAULT;
	transport->state = NETBOX_INITIAL;
	transport->is_closing = false;
	transport->last_error = NULL;
	transport->worker = NULL;
	transport->coro_ref = LUA_NOREF;
	transport->self_ref = LUA_NOREF;
	iostream_ctx_clear(&transport->io_ctx);
	iostream_clear(&transport->io);
	ibuf_create(&transport->send_buf, &cord()->slabc, NETBOX_READAHEAD);
	ibuf_create(&transport->recv_buf, &cord()->slabc, NETBOX_READAHEAD);
	transport->last_msg_size = 0;
	fiber_cond_create(&transport->on_send_buf_empty);
	transport->next_sync = 1;
	transport->requests = mh_i64ptr_new();
	transport->inprogress_request_count = 0;
}

static void
netbox_transport_destroy(struct netbox_transport *transport)
{
	netbox_options_destroy(&transport->opts);
	if (transport->last_error != NULL)
		error_unref(transport->last_error);
	assert(transport->worker == NULL);
	assert(transport->coro_ref == LUA_NOREF);
	assert(transport->self_ref == LUA_NOREF);
	iostream_ctx_destroy(&transport->io_ctx);
	assert(!iostream_is_initialized(&transport->io));
	assert(ibuf_used(&transport->send_buf) == 0);
	assert(ibuf_used(&transport->recv_buf) == 0);
	fiber_cond_destroy(&transport->on_send_buf_empty);
	struct mh_i64ptr_t *h = transport->requests;
	assert(mh_size(h) == 0);
	mh_i64ptr_delete(h);
	assert(transport->inprogress_request_count == 0);
}

/**
 * Looks up a request by id (sync). Returns NULL if not found.
 */
static inline struct netbox_request *
netbox_transport_lookup_request(struct netbox_transport *transport,
				uint64_t sync)
{
	struct mh_i64ptr_t *h = transport->requests;
	mh_int_t k = mh_i64ptr_find(h, sync, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/**
 * Sets transport->last_error to the last error set in the diagnostics area
 * and aborts all pending requests.
 */
static void
netbox_transport_set_error(struct netbox_transport *transport)
{
	/* Set last error. */
	assert(!diag_is_empty(diag_get()));
	struct error *error = diag_last_error(diag_get());
	if (transport->last_error != NULL)
		error_unref(transport->last_error);
	transport->last_error = error;
	error_ref(error);
	/* Reset buffers. */
	ibuf_reinit(&transport->send_buf);
	ibuf_reinit(&transport->recv_buf);
	transport->last_msg_size = 0;
	fiber_cond_broadcast(&transport->on_send_buf_empty);
	/* Complete requests and clean up the hash. */
	struct mh_i64ptr_t *h = transport->requests;
	mh_int_t k;
	mh_foreach(h, k) {
		struct netbox_request *request = mh_i64ptr_node(h, k)->val;
		request->transport = NULL;
		netbox_request_set_error(request, error);
		netbox_request_signal(request);
	}
	mh_i64ptr_clear(h);
	transport->inprogress_request_count = 0;
}

static inline size_t
netbox_begin_encode(struct mpstream *stream, uint64_t sync,
		    enum iproto_type type, uint64_t stream_id)
{
	/* Remember initial size of ibuf (see netbox_end_encode()) */
	struct ibuf *ibuf = stream->ctx;
	size_t used = ibuf_used(ibuf);

	/* Reserve and skip space for fixheader */
	size_t fixheader_size = mp_sizeof_uint(UINT32_MAX);
	mpstream_reserve(stream, fixheader_size);
	mpstream_advance(stream, fixheader_size);

	/* encode header */
	mpstream_encode_map(stream, 1 + (sync != 0) + (stream_id != 0));

	if (sync != 0) {
		mpstream_encode_uint(stream, IPROTO_SYNC);
		mpstream_encode_uint(stream, sync);
	}

	mpstream_encode_uint(stream, IPROTO_REQUEST_TYPE);
	mpstream_encode_uint(stream, type);

	if (stream_id != 0) {
		mpstream_encode_uint(stream, IPROTO_STREAM_ID);
		mpstream_encode_uint(stream, stream_id);
	}
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

/**
 * Encode an `IPROTO_PING` request and write it to the provided MsgPack stream.
 */
static int
netbox_encode_ping(lua_State *L, int idx, struct netbox_method_encode_ctx *ctx)
{
	(void)L;
	(void)idx;
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_PING,
					 ctx->stream_id);
	netbox_end_encode(ctx->stream, svp);
	return 0;
}

/**
 * Encodes an id request and writes it to the provided buffer.
 * Raises a Lua error on memory allocation failure.
 */
static void
netbox_encode_id(struct lua_State *L, struct ibuf *ibuf, uint64_t sync,
		 bool fetch_schema)
{
	struct iproto_features features = NETBOX_IPROTO_FEATURES;
	if (fetch_schema) {
		iproto_features_clear(&features,
				      IPROTO_FEATURE_DML_TUPLE_EXTENSION);
	}
#ifndef NDEBUG
	struct errinj *errinj = errinj(ERRINJ_NETBOX_FLIP_FEATURE, ERRINJ_INT);
	if (errinj->iparam >= 0 && errinj->iparam < iproto_feature_id_MAX) {
		int feature_id = errinj->iparam;
		if (iproto_features_test(&features, feature_id))
			iproto_features_clear(&features, feature_id);
		else
			iproto_features_set(&features, feature_id);
	}
#endif
	struct mpstream stream;
	mpstream_init(&stream, ibuf, ibuf_reserve_cb, ibuf_alloc_cb,
		      luamp_error, L);
	size_t svp = netbox_begin_encode(&stream, sync, IPROTO_ID, 0);

	mpstream_encode_map(&stream, 2);
	mpstream_encode_uint(&stream, IPROTO_VERSION);
	mpstream_encode_uint(&stream, NETBOX_IPROTO_VERSION);
	mpstream_encode_uint(&stream, IPROTO_FEATURES);
	size_t size = mp_sizeof_iproto_features(&features);
	char *data = mpstream_reserve(&stream, size);
	mp_encode_iproto_features(data, &features);
	mpstream_advance(&stream, size);

	netbox_end_encode(&stream, svp);
}

/**
 * Encodes an authorization request and writes it to the provided buffer.
 * Raises a Lua error on memory allocation failure.
 */
static void
netbox_encode_auth(struct lua_State *L, struct ibuf *ibuf, uint64_t sync,
		   const struct auth_method *method, const char *user,
		   const char *password, const char *salt, uint32_t salt_len)
{
	assert(salt_len >= AUTH_SALT_SIZE);
	(void)salt_len;
	if (password == NULL)
		password = "";
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	const char *auth_request, *auth_request_end;
	auth_request_prepare(method, password, strlen(password), salt,
			     &auth_request, &auth_request_end);
	struct mpstream stream;
	mpstream_init(&stream, ibuf, ibuf_reserve_cb, ibuf_alloc_cb,
		      luamp_error, L);
	size_t svp = netbox_begin_encode(&stream, sync, IPROTO_AUTH, 0);
	mpstream_encode_map(&stream, 2);
	mpstream_encode_uint(&stream, IPROTO_USER_NAME);
	mpstream_encode_strn(&stream, user, strlen(user));
	mpstream_encode_uint(&stream, IPROTO_TUPLE);
	mpstream_encode_array(&stream, 2);
	mpstream_encode_str(&stream, method->name);
	mpstream_memcpy(&stream, auth_request, auth_request_end - auth_request);
	netbox_end_encode(&stream, svp);
	region_truncate(region, region_svp);
}

/**
 * Encodes a SELECT(*) request and writes it to the provided buffer.
 * Raises a Lua error on memory allocation failure.
 */
static void
netbox_encode_select_all(struct lua_State *L, struct ibuf *ibuf, uint64_t sync,
			 uint32_t space_id)
{
	struct mpstream stream;
	mpstream_init(&stream, ibuf, ibuf_reserve_cb, ibuf_alloc_cb,
		      luamp_error, L);
	size_t svp = netbox_begin_encode(&stream, sync, IPROTO_SELECT, 0);
	mpstream_encode_map(&stream, 3);
	mpstream_encode_uint(&stream, IPROTO_SPACE_ID);
	mpstream_encode_uint(&stream, space_id);
	mpstream_encode_uint(&stream, IPROTO_LIMIT);
	mpstream_encode_uint(&stream, UINT32_MAX);
	mpstream_encode_uint(&stream, IPROTO_KEY);
	mpstream_encode_array(&stream, 0);
	netbox_end_encode(&stream, svp);
}

/*
 * Encode the arguments of call or eval netbox methods.
 */
static int
netbox_encode_call_or_eval_args(lua_State *L, int idx, struct mpstream *stream,
				bool box_tuple_arg_as_ext)
{
	mpstream_encode_uint(stream, IPROTO_TUPLE);
	struct mp_box_ctx ctx;
	mp_box_ctx_create(&ctx, NULL, NULL);
	if (luamp_encode_tuple_with_ctx(L, cfg, stream, idx,
					box_tuple_arg_as_ext ?
					(struct mp_ctx *)&ctx : NULL) != 0) {
		mp_ctx_destroy((struct mp_ctx *)&ctx);
		return -1;
	}
	mpstream_encode_uint(stream, IPROTO_TUPLE_FORMATS);
	tuple_format_map_to_mpstream(&ctx.tuple_format_map, stream);
	mp_ctx_destroy((struct mp_ctx *)&ctx);
	return 0;
}

/**
 * Encode an `IPROTO_CALL` request and write it to the provided MsgPack stream.
 */
static int
netbox_encode_call(lua_State *L, int idx, struct netbox_method_encode_ctx *ctx)
{
	/* Lua stack at idx: function_name, args */
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_CALL,
					 ctx->stream_id);

	mpstream_encode_map(ctx->stream, 3);

	/* encode proc name */
	size_t name_len;
	const char *name = lua_tolstring(L, idx, &name_len);
	mpstream_encode_uint(ctx->stream, IPROTO_FUNCTION_NAME);
	mpstream_encode_strn(ctx->stream, name, name_len);

	if (netbox_encode_call_or_eval_args(L, idx + 1, ctx->stream,
					    ctx->box_tuple_arg_as_ext) != 0)
		return -1;

	netbox_end_encode(ctx->stream, svp);
	return 0;
}

/**
 * Encode an `IPROTO_EVAL` request and write it to the provided MsgPack stream.
 */
static int
netbox_encode_eval(lua_State *L, int idx, struct netbox_method_encode_ctx *ctx)
{
	/* Lua stack at idx: expr, args */
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_EVAL,
					 ctx->stream_id);

	mpstream_encode_map(ctx->stream, 3);

	/* encode expr */
	size_t expr_len;
	const char *expr = lua_tolstring(L, idx, &expr_len);
	mpstream_encode_uint(ctx->stream, IPROTO_EXPR);
	mpstream_encode_strn(ctx->stream, expr, expr_len);

	if (netbox_encode_call_or_eval_args(L, idx + 1, ctx->stream,
					    ctx->box_tuple_arg_as_ext) != 0)
		return -1;

	netbox_end_encode(ctx->stream, svp);
	return 0;
}

/*
 * Depending on the type of the argument (see also net.box space metatable)
 * encode either a space identifier or a space name.
 */
static void
netbox_encode_space_id_or_name(lua_State *L, int idx, struct mpstream *stream)
{
	if (lua_type(L, idx) == LUA_TNUMBER) {
		uint32_t space_id = lua_tonumber(L, idx);
		mpstream_encode_uint(stream, IPROTO_SPACE_ID);
		mpstream_encode_uint(stream, space_id);
	} else {
		size_t len;
		const char *space_name = lua_tolstring(L, idx, &len);
		mpstream_encode_uint(stream, IPROTO_SPACE_NAME);
		mpstream_encode_strn(stream, space_name, len);
	}
}

/*
 * Depending on the type of the argument (see also net.box index metatable)
 * encode either a index identifier or an index name.
 */
static void
netbox_encode_index_id_or_name(lua_State *L, int idx, struct mpstream *stream)
{
	if (lua_type(L, idx) == LUA_TNUMBER) {
		uint32_t space_id = lua_tonumber(L, idx);
		mpstream_encode_uint(stream, IPROTO_INDEX_ID);
		mpstream_encode_uint(stream, space_id);
	} else {
		size_t len;
		const char *space_name = lua_tolstring(L, idx, &len);
		mpstream_encode_uint(stream, IPROTO_INDEX_NAME);
		mpstream_encode_strn(stream, space_name, len);
	}
}

/* Encode select request. */
static int
netbox_encode_select(lua_State *L, int idx,
		     struct netbox_method_encode_ctx *ctx)
{
	/*
	 * Lua stack at idx: space_id, index_id, iterator, offset, limit, key,
	 * after, fetch_pos.
	 */
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_SELECT,
					 ctx->stream_id);
	uint32_t map_size = 6;

	bool have_after = !lua_isnil(L, idx + 6);
	if (have_after)
		map_size++;
	bool fetch_pos = lua_toboolean(L, idx + 7);
	if (fetch_pos)
		map_size++;
	mpstream_encode_map(ctx->stream, map_size);
	int iterator = lua_tointeger(L, idx + 2);
	uint32_t offset = lua_tonumber(L, idx + 3);
	uint32_t limit = lua_tonumber(L, idx + 4);

	netbox_encode_space_id_or_name(L, idx, ctx->stream);

	netbox_encode_index_id_or_name(L, idx + 1, ctx->stream);

	/* encode iterator */
	mpstream_encode_uint(ctx->stream, IPROTO_ITERATOR);
	mpstream_encode_uint(ctx->stream, iterator);

	/* encode offset */
	mpstream_encode_uint(ctx->stream, IPROTO_OFFSET);
	mpstream_encode_uint(ctx->stream, offset);

	/* encode limit */
	mpstream_encode_uint(ctx->stream, IPROTO_LIMIT);
	mpstream_encode_uint(ctx->stream, limit);

	/* encode key */
	mpstream_encode_uint(ctx->stream, IPROTO_KEY);
	if (luamp_convert_key(L, cfg, ctx->stream, idx + 5) != 0)
		return -1;

	/* encode after */
	if (have_after) {
		if (lua_isstring(L, idx + 6)) {
			mpstream_encode_uint(ctx->stream,
					     IPROTO_AFTER_POSITION);
			size_t size;
			const char *pos = lua_tolstring(L, idx + 6, &size);
			mpstream_encode_strn(ctx->stream, pos, size);
		} else {
			assert(luaT_istuple(L, idx + 6) != NULL ||
			       lua_istable(L, idx + 6));
			mpstream_encode_uint(ctx->stream, IPROTO_AFTER_TUPLE);
			if (luamp_encode_tuple(L, cfg, ctx->stream, idx + 6) != 0)
				return -1;
		}
	}

	/* encode fetch_pos */
	if (fetch_pos) {
		mpstream_encode_uint(ctx->stream, IPROTO_FETCH_POSITION);
		mpstream_encode_bool(ctx->stream, fetch_pos);
	}

	netbox_end_encode(ctx->stream, svp);
	return 0;
}

static int
netbox_encode_insert_or_replace(lua_State *L, int idx, struct mpstream *stream,
				uint64_t sync, enum iproto_type type,
				uint64_t stream_id)
{
	/* Lua stack at idx: space_id, tuple */
	size_t svp = netbox_begin_encode(stream, sync, type, stream_id);

	mpstream_encode_map(stream, 2);

	netbox_encode_space_id_or_name(L, idx, stream);

	/* encode args */
	mpstream_encode_uint(stream, IPROTO_TUPLE);
	if (luamp_encode_tuple(L, cfg, stream, idx + 1) != 0)
		return -1;

	netbox_end_encode(stream, svp);
	return 0;
}

static int
netbox_encode_insert(lua_State *L, int idx,
		     struct netbox_method_encode_ctx *ctx)
{
	return netbox_encode_insert_or_replace(L, idx, ctx->stream, ctx->sync,
					       IPROTO_INSERT, ctx->stream_id);
}

static int
netbox_encode_replace(lua_State *L, int idx,
		      struct netbox_method_encode_ctx *ctx)
{
	return netbox_encode_insert_or_replace(L, idx, ctx->stream, ctx->sync,
					       IPROTO_REPLACE, ctx->stream_id);
}

static int
netbox_encode_delete(lua_State *L, int idx,
		     struct netbox_method_encode_ctx *ctx)
{
	/* Lua stack at idx: space_id, index_id, key */
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_DELETE,
					 ctx->stream_id);

	mpstream_encode_map(ctx->stream, 3);

	netbox_encode_space_id_or_name(L, idx, ctx->stream);

	netbox_encode_index_id_or_name(L, idx + 1, ctx->stream);

	/* encode key */
	mpstream_encode_uint(ctx->stream, IPROTO_KEY);
	if (luamp_convert_key(L, cfg, ctx->stream, idx + 2) != 0)
		return -1;

	netbox_end_encode(ctx->stream, svp);
	return 0;
}

/**
 * Encode an `IPROTO_UPDATE` request and write it to the provided MsgPack
 * stream.
 */
static int
netbox_encode_update(lua_State *L, int idx,
		     struct netbox_method_encode_ctx *ctx)
{
	/* Lua stack at idx: space_id, index_id, key, ops */
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_UPDATE,
					 ctx->stream_id);

	mpstream_encode_map(ctx->stream, 5);

	netbox_encode_space_id_or_name(L, idx, ctx->stream);

	netbox_encode_index_id_or_name(L, idx + 1, ctx->stream);

	/* encode index_id */
	mpstream_encode_uint(ctx->stream, IPROTO_INDEX_BASE);
	mpstream_encode_uint(ctx->stream, 1);

	/* encode key */
	mpstream_encode_uint(ctx->stream, IPROTO_KEY);
	if (luamp_convert_key(L, cfg, ctx->stream, idx + 2) != 0)
		return -1;

	/* encode ops */
	mpstream_encode_uint(ctx->stream, IPROTO_TUPLE);
	if (luamp_encode_tuple(L, cfg, ctx->stream, idx + 3) != 0)
		return -1;

	netbox_end_encode(ctx->stream, svp);
	return 0;
}

/**
 * Encode an `IPROTO_UPSERT` request and write it to the provided MsgPack
 * stream.
 */
static int
netbox_encode_upsert(lua_State *L, int idx,
		     struct netbox_method_encode_ctx *ctx)
{
	/* Lua stack at idx: space_id, tuple, ops */
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_UPSERT,
					 ctx->stream_id);

	mpstream_encode_map(ctx->stream, 4);

	netbox_encode_space_id_or_name(L, idx, ctx->stream);

	/* encode index_base */
	mpstream_encode_uint(ctx->stream, IPROTO_INDEX_BASE);
	mpstream_encode_uint(ctx->stream, 1);

	/* encode tuple */
	mpstream_encode_uint(ctx->stream, IPROTO_TUPLE);
	if (luamp_encode_tuple(L, cfg, ctx->stream, idx + 1) != 0)
		return -1;

	/* encode ops */
	mpstream_encode_uint(ctx->stream, IPROTO_OPS);
	if (luamp_encode_tuple(L, cfg, ctx->stream, idx + 2) != 0)
		return -1;

	netbox_end_encode(ctx->stream, svp);
	return 0;
}

/**
 * Connects a transport to a remote host and reads a greeting message.
 * Returns 0 on success, -1 on error.
 */
static int
netbox_transport_connect(struct netbox_transport *transport)
{
	struct error *e;
	struct iostream *io = &transport->io;
	assert(!iostream_is_initialized(io));
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, transport->opts.connect_timeout);
	int fd = transport->opts.fd;
	if (fd >= 0) {
		plain_iostream_create(io, fd);
	} else {
		assert(!uri_is_nil(&transport->opts.uri));
		fd = coio_connect_timeout(transport->opts.uri.host,
					  transport->opts.uri.service,
					  transport->opts.uri.host_hint,
					  /*addr=*/NULL, /*addr_len=*/NULL,
					  delay);
		coio_timeout_update(&start, &delay);
		if (fd < 0)
			goto io_error;
		if (iostream_create(io, fd, &transport->io_ctx) != 0) {
			close(fd);
			goto error;
		}
	}
	char greetingbuf[IPROTO_GREETING_SIZE];
	if (coio_readn_timeout(io, greetingbuf, IPROTO_GREETING_SIZE,
			       delay) < 0)
		goto io_error;
	if (greeting_decode(greetingbuf, &transport->greeting) != 0) {
		box_error_raise(ER_NO_CONNECTION, "Invalid greeting");
		goto error;
	}
	if (strcmp(transport->greeting.protocol, "Binary") != 0) {
		box_error_raise(ER_NO_CONNECTION, "Unsupported protocol: %s",
				transport->greeting.protocol);
		goto error;
	}
	return 0;
io_error:
	assert(!diag_is_empty(diag_get()));
	e = diag_last_error(diag_get());
	box_error_raise(ER_NO_CONNECTION, "%s", e->errmsg);
error:
	if (iostream_is_initialized(io))
		iostream_close(io);
	return -1;
}

/**
 * Reads data from the given socket until the limit is reached.
 * Returns 0 on success. On error returns -1 and sets diag.
 *
 * If the connection is closing, then the fiber in which the 'close' function
 * is called waits on the `on_send_buf_empty` conditional variable until all
 * data is sent. When all data is sent this variable is signaled and wakeup
 * appropriate waiting fiber.
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
netbox_transport_communicate(struct netbox_transport *transport, size_t limit)
{
	struct error *e;
	struct iostream *io = &transport->io;
	assert(iostream_is_initialized(io));
	struct ibuf *send_buf = &transport->send_buf;
	struct ibuf *recv_buf = &transport->recv_buf;
	struct fiber_cond *on_send_buf_empty = &transport->on_send_buf_empty;
	while (true) {
		/*
		 * Gracefully shut down the connection if there are no more
		 * in-progress requests and the server requested us to.
		 */
		if (transport->state == NETBOX_GRACEFUL_SHUTDOWN &&
		    transport->inprogress_request_count == 0) {
			box_error_raise(ER_NO_CONNECTION, "Peer closed");
			return -1;
		}
		/* reader serviced first */
		int events = 0;
		while (ibuf_used(recv_buf) < limit) {
			void *p = ibuf_reserve(recv_buf, NETBOX_READAHEAD);
			if (p == NULL) {
				diag_set(OutOfMemory, NETBOX_READAHEAD,
					 "ibuf_reserve", "p");
				return -1;
			}
			ssize_t rc = iostream_read(io, recv_buf->wpos,
						   ibuf_unused(recv_buf));
			if (rc == 0) {
				box_error_raise(ER_NO_CONNECTION,
						"Peer closed");
				return -1;
			} if (rc > 0) {
				VERIFY(ibuf_alloc(recv_buf, rc) != NULL);
			} else if (rc == IOSTREAM_ERROR) {
				goto io_error;
			} else {
				events |= iostream_status_to_events(rc);
				break;
			}
		}
		if (ibuf_used(recv_buf) >= limit)
			return 0;
		while (ibuf_used(send_buf) > 0) {
			ssize_t rc = iostream_write(io, send_buf->rpos,
						    ibuf_used(send_buf));
			if (rc >= 0) {
				ibuf_consume(send_buf, rc);
				if (ibuf_used(send_buf) == 0)
					fiber_cond_broadcast(on_send_buf_empty);
			} else if (rc == IOSTREAM_ERROR) {
				goto io_error;
			} else {
				events |= iostream_status_to_events(rc);
				break;
			}
		}
		coio_wait(io->fd, events, TIMEOUT_INFINITY);
		ERROR_INJECT_YIELD(ERRINJ_NETBOX_IO_DELAY);
		ERROR_INJECT(ERRINJ_NETBOX_IO_ERROR, {
			box_error_raise(ER_NO_CONNECTION, "Error injection");
			return -1;
		});
		if (fiber_is_cancelled()) {
			diag_set(FiberIsCancelled);
			return -1;
		}
	}
io_error:
	assert(!diag_is_empty(diag_get()));
	e = diag_last_error(diag_get());
	box_error_raise(ER_NO_CONNECTION, "%s", e->errmsg);
	return -1;
}

/**
 * Sends and receives data over an iproto connection.
 * Returns 0 and a decoded response header on success.
 * On error returns -1.
 */
static int
netbox_transport_send_and_recv(struct netbox_transport *transport,
			       struct xrow_header *hdr)
{
	ibuf_consume(&transport->recv_buf, transport->last_msg_size);
	while (true) {
		size_t required;
		size_t data_len = ibuf_used(&transport->recv_buf);
		size_t fixheader_size = mp_sizeof_uint(UINT32_MAX);
		if (data_len < fixheader_size) {
			required = fixheader_size;
		} else {
			const char *bufpos = transport->recv_buf.rpos;
			const char *rpos = bufpos;
			uint64_t len = mp_decode_uint(&rpos);
			size_t size = rpos - bufpos;
			if (len > SIZE_MAX - size) {
				box_error_raise(ER_NO_CONNECTION,
						"Response size too large");
				return -1;
			}
			required = size + len;
			if (data_len >= required) {
				const char *body_end = rpos + len;
				int rc = xrow_header_decode(
						hdr, &rpos, body_end,
						/*end_is_exact=*/true);
				transport->last_msg_size = body_end - bufpos;
				return rc;
			}
		}
		if (netbox_transport_communicate(transport, required) != 0)
			return -1;
	}
}

/**
 * Encode an `IPROTO_EXECUTE` request and write it to the provided MsgPack
 * stream.
 */
static int
netbox_encode_execute(lua_State *L, int idx,
		      struct netbox_method_encode_ctx *ctx)
{
	/* Lua stack at idx: query, parameters, options */
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_EXECUTE,
					 ctx->stream_id);

	mpstream_encode_map(ctx->stream, 3);

	if (lua_type(L, idx) == LUA_TNUMBER) {
		uint32_t query_id = lua_tointeger(L, idx);
		mpstream_encode_uint(ctx->stream, IPROTO_STMT_ID);
		mpstream_encode_uint(ctx->stream, query_id);
	} else {
		size_t len;
		const char *query = lua_tolstring(L, idx, &len);
		mpstream_encode_uint(ctx->stream, IPROTO_SQL_TEXT);
		mpstream_encode_strn(ctx->stream, query, len);
	}

	mpstream_encode_uint(ctx->stream, IPROTO_SQL_BIND);
	if (luamp_encode_tuple(L, cfg, ctx->stream, idx + 1) != 0)
		return -1;

	mpstream_encode_uint(ctx->stream, IPROTO_OPTIONS);
	if (luamp_encode_tuple(L, cfg, ctx->stream, idx + 2) != 0)
		return -1;

	netbox_end_encode(ctx->stream, svp);
	return 0;
}

/**
 * Encode an `IPROTO_PREPARE` request and write it to the provided MsgPack
 * stream.
 */
static int
netbox_encode_prepare(lua_State *L, int idx,
		      struct netbox_method_encode_ctx *ctx)
{
	/* Lua stack at idx: query */
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_PREPARE,
					 ctx->stream_id);

	mpstream_encode_map(ctx->stream, 1);

	if (lua_type(L, idx) == LUA_TNUMBER) {
		uint32_t query_id = lua_tointeger(L, idx);
		mpstream_encode_uint(ctx->stream, IPROTO_STMT_ID);
		mpstream_encode_uint(ctx->stream, query_id);
	} else {
		size_t len;
		const char *query = lua_tolstring(L, idx, &len);
		mpstream_encode_uint(ctx->stream, IPROTO_SQL_TEXT);
		mpstream_encode_strn(ctx->stream, query, len);
	};

	netbox_end_encode(ctx->stream, svp);
	return 0;
}

static int
netbox_encode_unprepare(lua_State *L, int idx,
			struct netbox_method_encode_ctx *ctx)
{
	/* Lua stack at idx: query, parameters, options */
	return netbox_encode_prepare(L, idx, ctx);
}

/**
 * Encode an `IPROTO_BEGIN` request and write it to the provided MsgPack stream.
 */
static int
netbox_encode_begin(struct lua_State *L, int idx,
		    struct netbox_method_encode_ctx *ctx)
{
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_BEGIN,
					 ctx->stream_id);
	bool has_timeout = !lua_isnoneornil(L, idx);
	bool has_txn_isolation = !lua_isnoneornil(L, idx + 1);
	bool has_is_sync = !lua_isnoneornil(L, idx + 2);
	if (has_timeout || has_txn_isolation || has_is_sync) {
		uint32_t map_size = (has_timeout ? 1 : 0) +
				    (has_txn_isolation ? 1 : 0) +
				    (has_is_sync ? 1 : 0);
		mpstream_encode_map(ctx->stream, map_size);
	}
	if (has_timeout) {
		assert(lua_type(L, idx) == LUA_TNUMBER);
		double timeout = lua_tonumber(L, idx);
		mpstream_encode_uint(ctx->stream, IPROTO_TIMEOUT);
		mpstream_encode_double(ctx->stream, timeout);
	}
	if (has_txn_isolation) {
		assert(lua_type(L, idx + 1) == LUA_TNUMBER);
		uint32_t txn_isolation = lua_tonumber(L, idx + 1);
		mpstream_encode_uint(ctx->stream, IPROTO_TXN_ISOLATION);
		mpstream_encode_uint(ctx->stream, txn_isolation);
	}
	if (has_is_sync) {
		if (lua_type(L, idx + 2) == LUA_TBOOLEAN) {
			bool is_sync = lua_toboolean(L, idx + 2);
			mpstream_encode_uint(ctx->stream, IPROTO_IS_SYNC);
			mpstream_encode_bool(ctx->stream, is_sync);
		}
	}
	netbox_end_encode(ctx->stream, svp);
	return 0;
}

static int
netbox_encode_commit(struct lua_State *L, int idx,
		     struct netbox_method_encode_ctx *ctx)
{
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync, IPROTO_COMMIT,
					 ctx->stream_id);
	bool has_is_sync = !lua_isnoneornil(L, idx);
	if (has_is_sync) {
		uint32_t map_size = 1;
		mpstream_encode_map(ctx->stream, map_size);
		if (lua_type(L, idx) == LUA_TBOOLEAN) {
			bool is_sync = lua_toboolean(L, idx);
			mpstream_encode_uint(ctx->stream, IPROTO_IS_SYNC);
			mpstream_encode_bool(ctx->stream, is_sync);
		}
	}
	netbox_end_encode(ctx->stream, svp);
	return 0;
}

static int
netbox_encode_rollback(struct lua_State *L, int idx,
		       struct netbox_method_encode_ctx *ctx)
{
	(void)L;
	(void)idx;
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync,
					 IPROTO_ROLLBACK, ctx->stream_id);
	netbox_end_encode(ctx->stream, svp);
	return 0;
}

/**
 * Encodes an IPROTO_WATCH_ONCE request for the notification key stored in
 * the Lua stack at the index idx.
 */
static int
netbox_encode_watch_once(struct lua_State *L, int idx,
			 struct netbox_method_encode_ctx *ctx)
{
	size_t key_len;
	const char *key = lua_tolstring(L, idx, &key_len);
	size_t svp = netbox_begin_encode(ctx->stream, ctx->sync,
					 IPROTO_WATCH_ONCE, ctx->stream_id);
	mpstream_encode_map(ctx->stream, 1);
	mpstream_encode_uint(ctx->stream, IPROTO_EVENT_KEY);
	mpstream_encode_strn(ctx->stream, key, key_len);
	netbox_end_encode(ctx->stream, svp);
	return 0;
}

/**
 * Write an injection to the provided MsgPack stream.
 */
static int
netbox_encode_inject(struct lua_State *L, int idx,
		     struct netbox_method_encode_ctx *ctx)
{
	/* Lua stack at idx: bytes */
	size_t len;
	const char *data = lua_tolstring(L, idx, &len);
	mpstream_memcpy(ctx->stream, data, len);
	mpstream_flush(ctx->stream);
	return 0;
}

/*
 * Encodes a request for the specified method and writes the result to the
 * provided buffer. Values to encode depend on the method and are passed via
 * Lua stack starting at index idx.
 *
 * Return:
 *  0 - on success
 * -1 - on error (diag is set)
 */
static int
netbox_encode_method(struct lua_State *L, int idx, enum netbox_method method,
		     struct ibuf *ibuf, uint64_t sync, uint64_t stream_id,
		     bool box_tuple_arg_as_ext)
{
	typedef int (*method_encoder_f)(struct lua_State *L, int idx,
					struct netbox_method_encode_ctx *ctx);
	static method_encoder_f method_encoder[] = {
		[NETBOX_PING]		= netbox_encode_ping,
		[NETBOX_CALL]		= netbox_encode_call,
		[NETBOX_EVAL]		= netbox_encode_eval,
		[NETBOX_INSERT]		= netbox_encode_insert,
		[NETBOX_REPLACE]	= netbox_encode_replace,
		[NETBOX_DELETE]		= netbox_encode_delete,
		[NETBOX_UPDATE]		= netbox_encode_update,
		[NETBOX_UPSERT]		= netbox_encode_upsert,
		[NETBOX_SELECT]		= netbox_encode_select,
		[NETBOX_SELECT_WITH_POS] = netbox_encode_select,
		[NETBOX_EXECUTE]	= netbox_encode_execute,
		[NETBOX_PREPARE]	= netbox_encode_prepare,
		[NETBOX_UNPREPARE]	= netbox_encode_unprepare,
		[NETBOX_GET]		= netbox_encode_select,
		[NETBOX_MIN]		= netbox_encode_select,
		[NETBOX_MAX]		= netbox_encode_select,
		[NETBOX_COUNT]		= netbox_encode_call,
		[NETBOX_BEGIN]		= netbox_encode_begin,
		[NETBOX_COMMIT]		= netbox_encode_commit,
		[NETBOX_ROLLBACK]	= netbox_encode_rollback,
		[NETBOX_WATCH_ONCE]	= netbox_encode_watch_once,
		[NETBOX_INJECT]		= netbox_encode_inject,
	};
	struct mpstream stream;
	mpstream_init(&stream, ibuf, ibuf_reserve_cb, ibuf_alloc_cb,
		      luamp_error, L);
	struct netbox_method_encode_ctx ctx = {
		.stream = &stream,
		.stream_id = stream_id,
		.sync = sync,
		.box_tuple_arg_as_ext = box_tuple_arg_as_ext,
	};
	return method_encoder[method](L, idx, &ctx);
}

/*
 * Decoded parts of response body.
 */
struct response_body {
	/* IPROTO_DATA */
	const char *data;
	/* IPROTO_DATA end. */
	const char *data_end;
	/* IPROTO_POSITION */
	const char *pos;
	/* IPROTO_POSITION length */
	uint32_t pos_len;
	/* IPROTO_TUPLE_FORMATS */
	const char *tuple_formats;
	/* IPROTO_TUPLE_FORMATS end. */
	const char *tuple_formats_end;
};

/*
 * Decode response body from a given MsgPack map.
 */
static void
response_body_decode(struct response_body *response_body, const char **data,
		     const char *data_end)
{
	(void)data_end;
	memset(response_body, 0, sizeof(*response_body));
	assert(mp_typeof(**data) == MP_MAP);
	uint32_t sz = mp_decode_map(data);
	for (uint32_t i = 0; i < sz; ++i) {
		assert(mp_typeof(**data) == MP_UINT);
		uint32_t key = mp_decode_uint(data);
		const char *value = *data;
		mp_next(data);
		switch (key) {
		case IPROTO_DATA:
			assert(mp_typeof(*value) == MP_ARRAY);
			response_body->data = value;
			response_body->data_end = *data;
			break;
		case IPROTO_POSITION:
			assert(mp_typeof(*value) == MP_STR);
			response_body->pos =
				mp_decode_str(&value, &response_body->pos_len);
			assert(response_body->pos_len != 0);
			break;
		case IPROTO_TUPLE_FORMATS:
			assert(mp_typeof(*value) == MP_MAP);
			response_body->tuple_formats = value;
			response_body->tuple_formats_end = *data;
			break;
		default:
			break;
		}
	}
	assert(*data == data_end);
}

/**
 * This function handles a response that is supposed to have an empty body
 * (e.g. IPROTO_PING result). It doesn't decode anything per se. Instead it
 * simply pushes nil to Lua stack and advances the data ptr to data_end.
 */
static void
netbox_decode_nil(struct lua_State *L, const char **data,
		  const char *data_end, bool return_raw,
		  struct tuple_format *format)
{
	(void)return_raw;
	(void)format;
	*data = data_end;
	lua_pushnil(L);
}

/**
 * Decodes Tarantool response body consisting of single IPROTO_DATA key into
 * a Lua table and pushes the table to Lua stack.
 */
static void
netbox_decode_table(struct lua_State *L, const char **data,
		    const char *data_end, bool return_raw,
		    struct tuple_format *format)
{
	(void)format;
	struct response_body response_body;
	response_body_decode(&response_body, data, data_end);
	if (response_body.data == NULL) {
		lua_pushnil(L);
		return;
	}
	struct mp_box_ctx ctx;
	mp_box_ctx_create(&ctx, NULL, response_body.tuple_formats);
	if (return_raw) {
		luamp_push_with_ctx(L, response_body.data,
				    response_body.data_end,
				    (struct mp_ctx *)&ctx);
	} else {
		luamp_decode_with_ctx(L, cfg, &response_body.data,
				      (struct mp_ctx *)&ctx);
	}
	mp_ctx_destroy((struct mp_ctx *)&ctx);
}

/**
 * Same as netbox_decode_table, but only decodes the first element of the
 * table, skipping the rest.
 */
static void
netbox_decode_value(struct lua_State *L, const char **data,
		    const char *data_end, bool return_raw,
		    struct tuple_format *format)
{
	(void)format;
	struct response_body response_body;
	response_body_decode(&response_body, data, data_end);
	uint32_t count = mp_decode_array(&response_body.data);
	if (count == 0) {
		lua_pushnil(L);
		return;
	}
	struct mp_box_ctx ctx;
		mp_box_ctx_create(&ctx, NULL, response_body.tuple_formats);
	if (return_raw) {
		luamp_push_with_ctx(L, response_body.data,
				    response_body.data_end,
				    (struct mp_ctx *)&ctx);
	} else {
		luamp_decode_with_ctx(L, cfg, &response_body.data,
				      (struct mp_ctx *)&ctx);
	}
	mp_ctx_destroy((struct mp_ctx *)&ctx);
}

/**
 * Used for decoding the index:count() result. It always returns a number so
 * there's no point in wrapping it in a msgpack object.
 */
static void
netbox_decode_count(struct lua_State *L, const char **data,
		    const char *data_end, bool return_raw,
		    struct tuple_format *format)
{
	(void)return_raw;
	netbox_decode_value(L, data, data_end, /*return_raw=*/false, format);
}

/**
 * Decodes IPROTO_DATA into a tuple array and pushes the array to Lua stack.
 */
static void
netbox_decode_data(struct lua_State *L, const char **data,
		   struct tuple_format *format, struct mp_box_ctx *ctx)
{
	uint32_t count = mp_decode_array(data);
	lua_createtable(L, count, 0);
	for (uint32_t j = 0; j < count; ++j) {
		const char *begin = *data;
		mp_next(data);
		struct tuple *tuple;
		if (tuple_format_map_is_empty(&ctx->tuple_format_map))
			tuple = box_tuple_new(format, begin, *data);
		else
			tuple = mp_decode_tuple(&begin, &ctx->tuple_format_map);
		if (tuple == NULL) {
			mp_ctx_destroy((struct mp_ctx *)ctx);
			luaT_error(L);
		}
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
		     const char *data_end, bool return_raw,
		     struct tuple_format *format)
{
	struct response_body response_body;
	response_body_decode(&response_body, data, data_end);
	struct mp_box_ctx ctx;
	mp_box_ctx_create(&ctx, NULL, response_body.tuple_formats);
	if (return_raw) {
		luamp_push_with_ctx(L, response_body.data,
				    response_body.data_end,
				    (struct mp_ctx *)&ctx);
	} else {
		netbox_decode_data(L, &response_body.data, format, &ctx);
	}
	mp_ctx_destroy((struct mp_ctx *)&ctx);
}

/**
 * Decodes Tarantool response body consisting of IPROTO_DATA and probably
 * IPROTO_POSITION keys into array with array of tuple on the first place
 * and position string on the second place, pushes it to Lua stack.
 */
static void
netbox_decode_select_with_pos(struct lua_State *L, const char **data,
			      const char *data_end, bool return_raw,
			      struct tuple_format *format)
{
	struct response_body response_body;
	response_body_decode(&response_body, data, data_end);
	lua_createtable(L, response_body.pos != NULL ? 2 : 1, 0);
	int table_idx = lua_gettop(L);
	struct mp_box_ctx ctx;
	mp_box_ctx_create(&ctx, NULL, response_body.tuple_formats);
	if (return_raw) {
		luamp_push_with_ctx(L, response_body.data,
				    response_body.data_end,
				    (struct mp_ctx *)&ctx);
	} else {
		struct mp_box_ctx ctx;
		mp_box_ctx_create(&ctx, NULL, response_body.tuple_formats);
		netbox_decode_data(L, &response_body.data, format, &ctx);
	}
	mp_ctx_destroy((struct mp_ctx *)&ctx);
	lua_rawseti(L, table_idx, 1);
	if (response_body.pos != NULL) {
		lua_pushlstring(L, response_body.pos, response_body.pos_len);
		lua_rawseti(L, table_idx, 2);
	}
}

/**
 * Same as netbox_decode_select, but only decodes the first tuple of the array,
 * skipping the rest.
 */
static void
netbox_decode_tuple(struct lua_State *L, const char **data,
		    const char *data_end, bool return_raw,
		    struct tuple_format *format)
{
	struct response_body response_body;
	response_body_decode(&response_body, data, data_end);
	uint32_t count = mp_decode_array(&response_body.data);
	if (count == 0) {
		lua_pushnil(L);
		return;
	}
	if (return_raw) {
		struct mp_box_ctx ctx;
		mp_box_ctx_create(&ctx, NULL, response_body.tuple_formats);
		luamp_push_with_ctx(L, response_body.data,
				    response_body.data_end,
				    (struct mp_ctx *)&ctx);
	} else {
		struct tuple *tuple;
		if (response_body.tuple_formats == NULL) {
			tuple = box_tuple_new(format, response_body.data,
					      response_body.data_end);
		} else {
			struct tuple_format_map tuple_format_map;
			const char *tuple_formats = response_body.tuple_formats;
			if (tuple_format_map_create_from_mp(&tuple_format_map,
							    tuple_formats) != 0)
				luaT_error(L);
			tuple = mp_decode_tuple(&response_body.data,
						&tuple_format_map);
			tuple_format_map_destroy(&tuple_format_map);
		}
		if (tuple == NULL) {
			luaT_error(L);
		}
		luaT_pushtuple(L, tuple);
	}
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
		      const char *data_end, bool return_raw,
		      struct tuple_format *format)
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
			if (return_raw) {
				const char *begin = *data;
				mp_next(data);
				luamp_push(L, begin, *data);
			} else {
				struct mp_box_ctx ctx;
				mp_box_ctx_create(&ctx, NULL, NULL);
				netbox_decode_data(L, data,
						   tuple_format_runtime, &ctx);
				mp_ctx_destroy((struct mp_ctx *)&ctx);
			}
			rows_index = lua_gettop(L);
			break;
		case IPROTO_METADATA:
			netbox_decode_metadata(L, data);
			meta_index = lua_gettop(L);
			break;
		default:
			assert(key == IPROTO_SQL_INFO);
			netbox_decode_sql_info(L, data);
			info_index = lua_gettop(L);
			break;
		}
	}
	if (info_index == 0) {
		assert(meta_index != 0);
		assert(rows_index != 0);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, meta_index);
		lua_setfield(L, -2, "metadata");
		lua_pushvalue(L, rows_index);
		lua_setfield(L, -2, "rows");
	} else {
		assert(meta_index == 0);
		assert(rows_index == 0);
	}
}

static void
netbox_decode_prepare(struct lua_State *L, const char **data,
		      const char *data_end, bool return_raw,
		      struct tuple_format *format)
{
	(void)data_end;
	(void)return_raw;
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
			stmt_id_idx = lua_gettop(L);
			break;
		}
		case IPROTO_METADATA: {
			netbox_decode_metadata(L, data);
			meta_idx = lua_gettop(L);
			break;
		}
		case IPROTO_BIND_METADATA: {
			netbox_decode_metadata(L, data);
			bind_meta_idx = lua_gettop(L);
			break;
		}
		default: {
			assert(key == IPROTO_BIND_COUNT);
			uint32_t bind_count = mp_decode_uint(data);
			luaL_pushuint64(L, bind_count);
			bind_count_idx = lua_gettop(L);
			break;
		}}
	}
	/* These fields must be present in response. */
	assert(stmt_id_idx * bind_meta_idx * bind_count_idx != 0);
	/* General meta is presented only in DQL responses. */
	lua_createtable(L, 0, meta_idx != 0 ? 4 : 3);
	lua_pushvalue(L, stmt_id_idx);
	lua_setfield(L, -2, "stmt_id");
	lua_pushvalue(L, bind_count_idx);
	lua_setfield(L, -2, "param_count");
	lua_pushvalue(L, bind_meta_idx);
	lua_setfield(L, -2, "params");
	if (meta_idx != 0) {
		lua_pushvalue(L, meta_idx);
		lua_setfield(L, -2, "metadata");
	}
}

/**
 * Decodes a response body for the specified method and pushes the result to
 * Lua stack. If the return_raw flag is set, pushes a msgpack object instead of
 * decoding data.
 */
static void
netbox_decode_method(struct lua_State *L, enum netbox_method method,
		     const char **data, const char *data_end,
		     bool return_raw, struct tuple_format *format)
{
	typedef void (*method_decoder_f)(struct lua_State *L, const char **data,
					 const char *data_end, bool return_raw,
					 struct tuple_format *format);
	static method_decoder_f method_decoder[] = {
		[NETBOX_PING]		= netbox_decode_nil,
		[NETBOX_CALL]		= netbox_decode_table,
		[NETBOX_EVAL]		= netbox_decode_table,
		[NETBOX_INSERT]		= netbox_decode_tuple,
		[NETBOX_REPLACE]	= netbox_decode_tuple,
		[NETBOX_DELETE]		= netbox_decode_tuple,
		[NETBOX_UPDATE]		= netbox_decode_tuple,
		[NETBOX_UPSERT]		= netbox_decode_nil,
		[NETBOX_SELECT]		= netbox_decode_select,
		[NETBOX_SELECT_WITH_POS] = netbox_decode_select_with_pos,
		[NETBOX_EXECUTE]	= netbox_decode_execute,
		[NETBOX_PREPARE]	= netbox_decode_prepare,
		[NETBOX_UNPREPARE]	= netbox_decode_nil,
		[NETBOX_GET]		= netbox_decode_tuple,
		[NETBOX_MIN]		= netbox_decode_tuple,
		[NETBOX_MAX]		= netbox_decode_tuple,
		[NETBOX_COUNT]		= netbox_decode_count,
		[NETBOX_BEGIN]		= netbox_decode_nil,
		[NETBOX_COMMIT]		= netbox_decode_nil,
		[NETBOX_ROLLBACK]	= netbox_decode_nil,
		[NETBOX_WATCH_ONCE]	= netbox_decode_value,
		[NETBOX_INJECT]		= netbox_decode_table,
	};
	method_decoder[method](L, data, data_end, return_raw, format);
}

static inline struct netbox_transport *
luaT_check_netbox_transport(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, netbox_transport_typename);
}

static int
luaT_netbox_transport_gc(struct lua_State *L)
{
	struct netbox_transport *transport = luaT_check_netbox_transport(L, 1);
	netbox_transport_destroy(transport);
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

/*
 * Autocomplete goes over the index of the object first, using
 * the __autocomplete method.
 * Then it needs a metatable of the type.
 */
static int
luaT_netbox_request_autocomplete(struct lua_State *L)
{
	luaL_getmetatable(L, netbox_request_typename);
	return 1;
}

/*
 * Every new netbox_request object can store some user data.
 * To support the autocompletion of these data a metatable is cerated.
 */
static void
luaT_netbox_request_create_index_table(struct lua_State *L)
{
	lua_newtable(L);
	lua_newtable(L);
	lua_pushstring(L, "__autocomplete");
	lua_pushcfunction(L, luaT_netbox_request_autocomplete);
	lua_settable(L, -3);
	lua_setmetatable(L, -2);
}

static int
luaT_netbox_request_tostring(struct lua_State *L)
{
	lua_pushstring(L, netbox_request_typename);
	return 1;
}

static int
luaT_netbox_request_serialize(struct lua_State *L)
{
	struct netbox_request *request = luaT_check_netbox_request(L, 1);
	if (request->index_ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, request->index_ref);
	} else {
		luaT_netbox_request_create_index_table(L);
	}
	return 1;
}

static int
luaT_netbox_request_index(struct lua_State *L)
{
	struct netbox_request *request = luaT_check_netbox_request(L, 1);
	if (request->index_ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, request->index_ref);
		/*
		 * Copy the key (2nd argument) to the top. Note, we don't move
		 * it with lua_insert, like we do in __newindex, because we want
		 * to save it for the fallback path below.
		 */
		lua_pushvalue(L, 2);
		lua_rawget(L, -2);
		if (lua_type(L, -1) != LUA_TNIL)
			return 1;
		/* Pop nil and the index table. */
		lua_pop(L, 2);
	}
	/* Fall back on metatable methods. */
	lua_getmetatable(L, 1);
	/* Move the metatable before the key (2nd argument). */
	lua_insert(L, 2);
	lua_rawget(L, 2);
	return 1;
}

static int
luaT_netbox_request_newindex(struct lua_State *L)
{
	struct netbox_request *request = luaT_check_netbox_request(L, 1);
	if (request->index_ref == LUA_NOREF) {
		/* Lazily create the index table on the first invocation. */
		luaT_netbox_request_create_index_table(L);
		request->index_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, request->index_ref);
	/* Move the index table before the key (2nd argument). */
	lua_insert(L, 2);
	lua_rawset(L, 2);
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
	if (request->transport != NULL &&
	    request->transport->worker == fiber()) {
		luaL_error(L, "Synchronous requests are not allowed in "
			   "net.box trigger");
	}
	while (!netbox_request_is_ready(request)) {
		if (!netbox_request_wait(request, &timeout)) {
			luaL_testcancel(L);
			diag_set(TimedOut);
			return luaT_push_nil_and_error(L);
		}
	}
	return netbox_request_push_result(request, L);
}

/**
 * Makes the connection forget about the given request. When the response is
 * received, it will be ignored. It reduces the size of the requests hash table
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
	if (request->transport != NULL &&
	    request->transport->worker == fiber()) {
		luaL_error(L, "Synchronous requests are not allowed in "
			   "net.box trigger");
	}
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
			diag_set(TimedOut);
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
 * Creates a netbox transport object (userdata) and pushes it to Lua stack.
 * Takes the following arguments: uri (string or table) or fd (number),
 * user (string or nil), password (string or nil), callback (function),
 * connect_timeout (number or nil), reconnect_after (number or nil),
 * fetch_schema (boolean or nil), auth_type (string or nil).
 */
static int
luaT_netbox_new_transport(struct lua_State *L)
{
	assert(lua_gettop(L) == 8);
	/* Create a transport object. */
	struct netbox_transport *transport;
	transport = lua_newuserdata(L, sizeof(*transport));
	netbox_transport_create(transport);
	luaL_getmetatable(L, netbox_transport_typename);
	lua_setmetatable(L, -2);
	/* Initialize options from Lua arguments. */
	struct netbox_options *opts = &transport->opts;
	if (lua_type(L, 1) == LUA_TNUMBER) {
		if (!luaL_tointeger_strict(L, 1, &opts->fd) || opts->fd < 0) {
			diag_set(IllegalParams,
				 "Invalid fd: expected nonnegative integer");
			return luaT_error(L);
		}
	} else {
		if (luaT_uri_create(L, 1, &opts->uri) != 0)
			return luaT_error(L);
		if (iostream_ctx_create(&transport->io_ctx, IOSTREAM_CLIENT,
					&opts->uri) != 0) {
			return luaT_error(L);
		}
	}
	if (!lua_isnil(L, 2))
		opts->user = xstrdup(luaL_checkstring(L, 2));
	if (!lua_isnil(L, 3))
		opts->password = xstrdup(luaL_checkstring(L, 3));
	assert(lua_isfunction(L, 4));
	lua_pushvalue(L, 4);
	opts->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	if (!lua_isnil(L, 5))
		opts->connect_timeout = luaL_checknumber(L, 5);
	if (!lua_isnil(L, 6))
		opts->reconnect_after = luaL_checknumber(L, 6);
	if (!lua_isnil(L, 7))
		opts->fetch_schema = lua_toboolean(L, 7);
	if (!lua_isnil(L, 8)) {
		size_t len;
		const char *s = luaL_checklstring(L, 8, &len);
		opts->auth_method = auth_method_by_name(s, len);
		if (opts->auth_method == NULL) {
			diag_set(ClientError, ER_UNKNOWN_AUTH_METHOD,
				 tt_cstr(s, len));
			return luaT_error(L);
		}
	}
	if (opts->user == NULL && opts->password != NULL) {
		diag_set(ClientError, ER_PROC_LUA,
			 "net.box: user is not defined");
		return luaT_error(L);
	}
	return 1;
}

/**
 * Writes a request to the send buffer and registers the request object
 * ('future') that can be used for waiting for a response.
 *
 * Takes the following values from Lua stack starting at index idx:
 *  - buffer: buffer (ibuf) to write the result to or nil
 *  - skip_header: whether to skip header when writing the result to the buffer
 *  - return_raw: if set, return msgpack object instead of decoding the result
 *  - on_push: on_push trigger function
 *  - on_push_ctx: on_push trigger function argument
 *  - format: tuple format to use for decoding the body or nil
 *  - stream_id: determines whether or not the request belongs to stream
 *  - method: a value from the netbox_method enumeration
 *  - ...: method-specific arguments passed to the encoder
 *
 * If the request cannot be performed, sets diag and returns -1,
 * otherwise returns 0.
 */
static int
luaT_netbox_transport_make_request(struct lua_State *L, int idx,
				   struct netbox_transport *transport,
				   struct netbox_request *request)
{
	if (transport->state != NETBOX_ACTIVE &&
	    transport->state != NETBOX_FETCH_SCHEMA) {
		struct error *e = transport->last_error;
		if (e != NULL) {
			box_error_raise(ER_NO_CONNECTION, "%s", e->errmsg);
		} else {
			const char *state = netbox_state_str[transport->state];
			box_error_raise(ER_NO_CONNECTION,
					"Connection is not established, "
					"state is \"%s\"", state);
		}
		return -1;
	}
	if (transport->is_closing) {
		box_error_raise(ER_NO_CONNECTION, "Connection is closing");
		return -1;
	}

	/* Encode and write the request to the send buffer. */
	int arg = idx + 6;
	uint64_t sync = transport->next_sync++;
	uint64_t stream_id = luaL_touint64(L, arg++);
	enum netbox_method method = lua_tointeger(L, arg++);
	assert(method < netbox_method_MAX);
	size_t svp = ibuf_used(&transport->send_buf);
	bool box_tuple_arg_as_ext =
		iproto_features_test(&transport->features,
				     IPROTO_FEATURE_CALL_ARG_TUPLE_EXTENSION);
	if (netbox_encode_method(L, arg++, method, &transport->send_buf, sync,
				 stream_id, box_tuple_arg_as_ext) != 0) {
		ibuf_truncate(&transport->send_buf, svp);
		return -1;
	}
	/* Alert worker to notify it of the queued outgoing data. */
	if (svp == 0)
		fiber_wakeup(transport->worker);
	transport->inprogress_request_count++;

	/* Initialize and register the request object. */
	arg = idx;
	request->method = method;
	request->sync = sync;
	request->buffer = (struct ibuf *)lua_topointer(L, arg);
	lua_pushvalue(L, arg++);
	request->buffer_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	request->skip_header = lua_toboolean(L, arg++);
	request->return_raw = lua_toboolean(L, arg++);
	lua_pushvalue(L, arg++);
	request->on_push_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushvalue(L, arg++);
	request->on_push_ctx_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	if (!lua_isnil(L, arg))
		request->format = luaT_check_tuple_format(L, arg++);
	else
		request->format = tuple_format_runtime;
	tuple_format_ref(request->format);
	fiber_cond_create(&request->cond);
	request->index_ref = LUA_NOREF;
	request->result_ref = LUA_NOREF;
	request->error = NULL;
	netbox_request_register(request, transport);
	return 0;
}

static int
luaT_netbox_transport_perform_async_request(struct lua_State *L)
{
	struct netbox_transport *transport = luaT_check_netbox_transport(L, 1);
	struct netbox_request *request = lua_newuserdata(L, sizeof(*request));
	if (luaT_netbox_transport_make_request(L, 2, transport, request) != 0)
		return luaT_push_nil_and_error(L);
	luaL_getmetatable(L, netbox_request_typename);
	lua_setmetatable(L, -2);
	return 1;
}

static int
luaT_netbox_transport_perform_request(struct lua_State *L)
{
	struct netbox_transport *transport = luaT_check_netbox_transport(L, 1);
	double timeout = (!lua_isnil(L, 2) ?
			  lua_tonumber(L, 2) : TIMEOUT_INFINITY);
	struct netbox_request request;
	if (luaT_netbox_transport_make_request(L, 3, transport, &request) != 0)
		return luaT_push_nil_and_error(L);
	while (!netbox_request_is_ready(&request)) {
		if (!netbox_request_wait(&request, &timeout)) {
			netbox_request_unregister(&request);
			netbox_request_destroy(&request);
			luaL_testcancel(L);
			diag_set(TimedOut);
			return luaT_push_nil_and_error(L);
		}
	}
	int ret = netbox_request_push_result(&request, L);
	netbox_request_destroy(&request);
	return ret;
}

/**
 * Encodes a WATCH/UNWATCH request and writes it to the send buffer.
 * Takes the name of the notification key to acknowledge.
 * No-op if the connection is inactive or closing.
 */
static void
luaT_netbox_transport_watch_or_unwatch(struct lua_State *L,
				       enum iproto_type type)
{
	assert(type == IPROTO_WATCH || type == IPROTO_UNWATCH);
	struct netbox_transport *transport = luaT_check_netbox_transport(L, 1);
	size_t key_len;
	const char *key = lua_tolstring(L, 2, &key_len);

	if (!iproto_features_test(&transport->features,
				  IPROTO_FEATURE_WATCHERS) ||
	    transport->is_closing || (transport->state != NETBOX_ACTIVE &&
				      transport->state != NETBOX_FETCH_SCHEMA))
		return;

        /* Alert worker to notify it of the queued outgoing data. */
	if (ibuf_used(&transport->send_buf) == 0)
		fiber_wakeup(transport->worker);

	/* Encode and write the request to the send buffer. */
	struct mpstream stream;
	mpstream_init(&stream, &transport->send_buf, ibuf_reserve_cb,
		      ibuf_alloc_cb, luamp_error, L);
	size_t svp = netbox_begin_encode(&stream, 0, type, 0);
	mpstream_encode_map(&stream, 1);
	mpstream_encode_uint(&stream, IPROTO_EVENT_KEY);
	mpstream_encode_strn(&stream, key, key_len);
	netbox_end_encode(&stream, svp);
}

static int
luaT_netbox_transport_watch(struct lua_State *L)
{
	luaT_netbox_transport_watch_or_unwatch(L, IPROTO_WATCH);
	return 0;
}

static int
luaT_netbox_transport_unwatch(struct lua_State *L)
{
	luaT_netbox_transport_watch_or_unwatch(L, IPROTO_UNWATCH);
	return 0;
}

/**
 * Invokes the 'state_changed' callback.
 */
static void
netbox_transport_on_state_change(struct netbox_transport *transport,
				 struct lua_State *L)
{
	enum netbox_state state = transport->state;
	struct error *error = (state == NETBOX_CLOSED ||
			       state == NETBOX_ERROR ||
			       state == NETBOX_ERROR_RECONNECT ?
			       transport->last_error : NULL);
	lua_rawgeti(L, LUA_REGISTRYINDEX, transport->opts.callback_ref);
	lua_pushliteral(L, "state_changed");
	lua_pushstring(L, netbox_state_str[state]);
	if (error != NULL)
		lua_pushstring(L, error->errmsg);
	lua_call(L, error != NULL ? 3 : 2, 0);
}

static int
netbox_transport_on_state_change_f(struct lua_State *L)
{
	struct netbox_transport *transport = (void *)lua_topointer(L, 1);
	netbox_transport_on_state_change(transport, L);
	return 0;
}

/**
 * Invokes the 'state_changed' callback with pcall.
 *
 * The callback shouldn't fail so this is just a precaution against a run-away
 * Lua exception in C code.
 */
static void
netbox_transport_on_state_change_pcall(struct netbox_transport *transport,
				       struct lua_State *L)
{
	if (luaT_cpcall(L, netbox_transport_on_state_change_f, transport) != 0)
		diag_log();
}

/**
 * Handles an IPROTO_EVENT packet received from the remote host.
 *
 * Note, decoding msgpack may throw a Lua error. This is fine: it will be
 * passed through and handled at the top level, which wraps the whole state
 * machine in pcall.
 */
static void
netbox_transport_on_event(struct netbox_transport *transport,
			  struct lua_State *L, struct xrow_header *hdr)
{
	assert(hdr->type == IPROTO_EVENT);
	struct watch_request watch;
	if (xrow_decode_watch(hdr, &watch) != 0)
		luaT_error(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, transport->opts.callback_ref);
	lua_pushliteral(L, "event");
	lua_pushlstring(L, watch.key, watch.key_len);
	if (watch.data != NULL) {
		const char *data = watch.data;
		luamp_decode(L, luaL_msgpack_default, &data);
		assert(data == watch.data_end);
	}
	lua_call(L, watch.data != NULL ? 3 : 2, 0);
}

/**
 * Argument data is the body of response, it must be an MP_MAP. Only three keys
 * are expected to appear: IPROTO_DATA (necessarily, must be the first one),
 * IPROTO_TUPLE_FORMATS (optionally), and
 * IPROTO_POSITION (optionally). The function writes response to
 * passed ibuf. If skip_header flag is set, data is written without IPROTO_DATA
 * header. If skip_header is true and response contains IPROTO_POSITION,
 * position is not written to a buffer - the function returns a table with
 * number of written bytes on the first place and position on the second one.
 * Otherwise, number of bytes written is returned.
 * An error is raised on failure.
 */
static inline void
netbox_write_response_to_buffer(const char *data, const char *data_end,
				struct lua_State *L, struct ibuf *buffer,
				bool skip_header)
{
	/* Copy xrow.body to user-provided buffer. */
	size_t data_len = data_end - data;
	bool return_table = false;
	struct response_body response_body;
	const char *data_ptr = data;
	response_body_decode(&response_body, &data_ptr, data_end);
	if (skip_header) {
		data = response_body.data;
		data_len = response_body.data_end - response_body.data;
		if (response_body.pos != NULL) {
			/* Create table to return 2 values. */
			return_table = true;
			lua_createtable(L, 2, 0);
			/* Check position length */
			if (response_body.pos_len != 0) {
				/* Set position to the 2nd place in table. */
				lua_pushlstring(L, response_body.pos,
						response_body.pos_len);
				lua_rawseti(L, -2, 2);
			}
		}
	}
	void *wpos = ibuf_alloc(buffer, data_len);
	if (wpos == NULL)
		luaL_error(L, "out of memory");
	memcpy(wpos, data, data_len);
	lua_pushinteger(L, data_len);
	if (return_table)
		lua_rawseti(L, -2, 1);
}

/**
 * Given a netbox transport and a response header, decodes the response and
 * either completes the request or invokes the on-push trigger, depending on
 * the status.
 *
 * Lua stack is used for temporarily storing the response table before getting
 * a reference to it and executing the on-push trigger.
 */
static void
netbox_transport_dispatch_response(struct netbox_transport *transport,
				   struct lua_State *L, struct xrow_header *hdr)
{
	enum iproto_type status = hdr->type;
	if (status == IPROTO_EVENT) {
		return netbox_transport_on_event(transport, L, hdr);
	}
	/*
	 * Account a response even if the request was discarded, but ignore
	 * packets with sync = 0. We use sync = 0 for IPROTO_WATCH, which
	 * isn't accounted, because the server isn't supposed to reply to it.
	 * However, the server may actually reply to it with an error if it
	 * doesn't support the request type.
	 */
	if (hdr->sync > 0 && (status == IPROTO_OK ||
			      iproto_type_is_error(status))) {
		assert(transport->inprogress_request_count > 0);
		transport->inprogress_request_count--;
	}
	struct netbox_request *request =
		netbox_transport_lookup_request(transport, hdr->sync);
	if (request == NULL) {
		/* Nobody is waiting for the response. */
		return;
	}
	if (iproto_type_is_error(status)) {
		/* Handle errors. */
		xrow_decode_error(hdr);
		struct error *error = box_error_last();
		netbox_request_set_error(request, error);
		netbox_request_complete(request);
		return;
	}
	const char *data = hdr->body[0].iov_base;
	const char *data_end = data + hdr->body[0].iov_len;
	if (request->buffer != NULL) {
		netbox_write_response_to_buffer(data, data_end, L,
						request->buffer,
						request->skip_header);
	} else {
		/* Decode xrow.body[DATA] to Lua objects. */
		if (status == IPROTO_OK) {
			netbox_decode_method(L, request->method, &data,
					     data_end, request->return_raw,
					     request->format);
		} else {
			netbox_decode_value(L, &data, data_end,
					    request->return_raw,
					    request->format);
		}
		assert(data == data_end);
	}
	if (status == IPROTO_OK) {
		/*
		 * We received the final response and pushed it to Lua stack.
		 * Store a reference to it in the request, remove the request
		 * from the hash table, and wake up waiters.
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
}

/**
 * Performs a features request for an iproto connection.
 * If the server doesn't support the IPROTO_ID command, assumes the protocol
 * version to be 0 and the feature set to be empty.
 * On success invokes the 'handshake' callback. On failure raises a Lua error.
 */
static void
netbox_transport_do_id(struct netbox_transport *transport, struct lua_State *L)
{
	struct greeting *greeting = &transport->greeting;
	uint32_t peer_version_id = greeting->version_id;
	struct id_request id;
	id.version = 0;
	iproto_features_create(&id.features);
	id.auth_type = NULL;
	id.auth_type_len = 0;
	ERROR_INJECT(ERRINJ_NETBOX_DISABLE_ID, goto out);
	if (peer_version_id < version_id(2, 10, 0))
		goto unsupported;
	netbox_encode_id(L, &transport->send_buf, transport->next_sync++,
			 transport->opts.fetch_schema);
	struct xrow_header hdr;
	if (netbox_transport_send_and_recv(transport, &hdr) != 0)
		luaT_error(L);
	if (hdr.type != IPROTO_OK) {
		uint32_t errcode = hdr.type & (IPROTO_TYPE_ERROR - 1);
		if (errcode == ER_UNKNOWN_REQUEST_TYPE)
			goto unsupported;
		xrow_decode_error(&hdr);
		luaT_error(L);
	}
	if (xrow_decode_id(&hdr, &id) != 0)
		luaT_error(L);
out:
	transport->features = id.features;
	if (id.auth_type != NULL) {
		transport->auth_method_default = auth_method_by_name(
					id.auth_type, id.auth_type_len);
		if (transport->auth_method_default == NULL)
			transport->auth_method_default = AUTH_METHOD_DEFAULT;
	}
	/* Invoke the 'handshake' callback. */
	lua_rawgeti(L, LUA_REGISTRYINDEX, transport->opts.callback_ref);
	lua_pushliteral(L, "handshake");
	/* Push the greeting. */
	lua_newtable(L);
	lua_pushinteger(L, greeting->version_id);
	lua_setfield(L, -2, "version_id");
	lua_pushstring(L, greeting->protocol);
	lua_setfield(L, -2, "protocol");
	luaT_pushuuidstr(L, &greeting->uuid);
	lua_setfield(L, -2, "uuid");
	/* Push the protocol version and features. */
	lua_pushinteger(L, id.version);
	lua_newtable(L);
	int i = 1;
	iproto_features_foreach(&id.features, feature_id) {
		lua_pushinteger(L, feature_id);
		lua_rawseti(L, -2, i++);
	}
	lua_call(L, 4, 0);
	return;
unsupported:
	say_verbose("IPROTO_ID command is not supported");
	goto out;
}

/**
 * Performs an authorization request for an iproto connection.
 * On failure raises a Lua error.
 */
static void
netbox_transport_do_auth(struct netbox_transport *transport,
			 struct lua_State *L)
{
	assert(transport->state == NETBOX_INITIAL ||
	       transport->state == NETBOX_ERROR_RECONNECT);
	transport->state = NETBOX_AUTH;
	netbox_transport_on_state_change(transport, L);
	struct netbox_options *opts = &transport->opts;
	if (opts->user == NULL)
		return;
	const struct auth_method *method = opts->auth_method != NULL ?
			opts->auth_method : transport->auth_method_default;
	if (auth_method_check_io(method, &transport->io) != 0)
		luaT_error(L);
	netbox_encode_auth(L, &transport->send_buf, transport->next_sync++,
			   method, opts->user, opts->password,
			   transport->greeting.salt,
			   transport->greeting.salt_len);
	struct xrow_header hdr;
	if (netbox_transport_send_and_recv(transport, &hdr) != 0)
		luaT_error(L);
	if (hdr.type != IPROTO_OK) {
		xrow_decode_error(&hdr);
		luaT_error(L);
	}
}

/**
 * Fetches schema over an iproto connection. While waiting for the schema,
 * processes other requests in a loop, like netbox_transport_process_requests().
 * On success invokes the 'did_fetch_schema' callback and returns the actual
 * schema version. On failure raises a Lua error.
 */
static uint64_t
netbox_transport_fetch_schema(struct netbox_transport *transport,
			      struct lua_State *L, uint64_t schema_version)
{
	if (!transport->opts.fetch_schema) {
		return schema_version;
	}
	if (transport->state == NETBOX_GRACEFUL_SHUTDOWN) {
		/*
		 * If a connection is in the 'graceful_shutdown', it can't
		 * issue new requests so there's no need to fetch schema.
		 */
		return schema_version;
	}
	assert(transport->state == NETBOX_AUTH ||
	       transport->state == NETBOX_ACTIVE);
	transport->state = NETBOX_FETCH_SCHEMA;
	netbox_transport_on_state_change(transport, L);
	uint32_t peer_version_id = transport->greeting.version_id;
	bool peer_has_vcollation = peer_version_id >= version_id(2, 2, 1);
	bool peer_has_vspace_sequence = peer_version_id >= version_id(2, 10, 5);
restart:
	lua_newtable(L);
	int schema_table_idx = lua_gettop(L);
	uint64_t vspace_sync = transport->next_sync++;
	netbox_encode_select_all(L, &transport->send_buf, vspace_sync,
				 BOX_VSPACE_ID);
	uint64_t vindex_sync = transport->next_sync++;
	netbox_encode_select_all(L, &transport->send_buf, vindex_sync,
				 BOX_VINDEX_ID);
	uint64_t vcollation_sync = transport->next_sync++;
	if (peer_has_vcollation)
		netbox_encode_select_all(L, &transport->send_buf,
					 vcollation_sync, BOX_VCOLLATION_ID);
	uint64_t vspace_sequence_sync = transport->next_sync++;
	if (peer_has_vspace_sequence)
		netbox_encode_select_all(L, &transport->send_buf,
					 vspace_sequence_sync,
					 BOX_VSPACE_SEQUENCE_ID);
	bool got_vspace = false;
	bool got_vindex = false;
	bool got_vcollation = false;
	bool got_vspace_sequence = false;
	schema_version = 0;
	do {
		struct xrow_header hdr;
		if (netbox_transport_send_and_recv(transport, &hdr) != 0)
			luaT_error(L);
		if (hdr.sync != vspace_sync &&
		    hdr.sync != vindex_sync &&
		    hdr.sync != vcollation_sync &&
		    hdr.sync != vspace_sequence_sync) {
			netbox_transport_dispatch_response(transport, L, &hdr);
			continue;
		}
		if (iproto_type_is_error(hdr.type)) {
			uint32_t errcode = hdr.type & (IPROTO_TYPE_ERROR - 1);
			if (errcode == ER_NO_SUCH_SPACE) {
				/* Server may have old dd version. */
				if (hdr.sync == vcollation_sync) {
					peer_has_vcollation = false;
					continue;
				} else if (hdr.sync == vspace_sequence_sync) {
					peer_has_vspace_sequence = false;
					continue;
				}
			}
			xrow_decode_error(&hdr);
			luaT_error(L);
		}
		if (schema_version == 0) {
			schema_version = hdr.schema_version;
		} else if (schema_version != hdr.schema_version) {
			/*
			 * Schema changed while fetching schema.
			 * Restart loader.
			 */
			lua_pop(L, 1);
			goto restart;
		}
		const char *data = hdr.body[0].iov_base;
		const char *data_end = data + hdr.body[0].iov_len;
		int key;
		if (hdr.sync == vspace_sync) {
			key = BOX_VSPACE_ID;
			got_vspace = true;
		} else if (hdr.sync == vindex_sync) {
			key = BOX_VINDEX_ID;
			got_vindex = true;
		} else if (hdr.sync == vcollation_sync) {
			key = BOX_VCOLLATION_ID;
			got_vcollation = true;
		} else if (hdr.sync == vspace_sequence_sync) {
			key = BOX_VSPACE_SEQUENCE_ID;
			got_vspace_sequence = true;
		} else {
			unreachable();
		}
		netbox_decode_table(L, &data, data_end, /*return_raw=*/false,
				    tuple_format_runtime);
		lua_rawseti(L, schema_table_idx, key);
	} while (!(got_vspace && got_vindex &&
		   (got_vcollation || !peer_has_vcollation) &&
		   (got_vspace_sequence || !peer_has_vspace_sequence)));
	/* Invoke the 'did_fetch_schema' callback. */
	lua_rawgeti(L, LUA_REGISTRYINDEX, transport->opts.callback_ref);
	lua_pushliteral(L, "did_fetch_schema");
	lua_pushinteger(L, schema_version);
	lua_rawgeti(L, schema_table_idx, BOX_VSPACE_ID);
	lua_rawgeti(L, schema_table_idx, BOX_VINDEX_ID);
	lua_rawgeti(L, schema_table_idx, BOX_VCOLLATION_ID);
	lua_rawgeti(L, schema_table_idx, BOX_VSPACE_SEQUENCE_ID);
	lua_call(L, 6, 0);
	/* Pop the schema table. */
	lua_pop(L, 1);
	return schema_version;
}

/**
 * Processes iproto requests in a loop until an error or a schema change.
 * Returns the current schema version on schema change. On failure raises
 * a Lua error.
 */
static uint64_t
netbox_transport_process_requests(struct netbox_transport *transport,
				  struct lua_State *L, uint64_t schema_version)
{
	if (transport->state != NETBOX_ACTIVE &&
	    transport->state != NETBOX_GRACEFUL_SHUTDOWN) {
		assert(transport->state == NETBOX_AUTH ||
		       transport->state == NETBOX_FETCH_SCHEMA);
		transport->state = NETBOX_ACTIVE;
		netbox_transport_on_state_change(transport, L);
	}
	while (true) {
		fiber_check_gc();
		struct xrow_header hdr;
		if (netbox_transport_send_and_recv(transport, &hdr) != 0)
			luaT_error(L);
		netbox_transport_dispatch_response(transport, L, &hdr);
		if (hdr.schema_version > 0 &&
		    hdr.schema_version != schema_version) {
			return hdr.schema_version;
		}
	}
}

/**
 * Connection handler. Raises a Lua error on termination.
 */
static int
netbox_connection_handler_f(struct lua_State *L)
{
	struct netbox_transport *transport = (void *)lua_topointer(L, 1);
	netbox_transport_do_id(transport, L);
	netbox_transport_do_auth(transport, L);
	uint64_t schema_version = 0;
	while (true) {
		schema_version = netbox_transport_fetch_schema(
			transport, L, schema_version);
		schema_version = netbox_transport_process_requests(
			transport, L, schema_version);
	}
	return 0;
}
/**
 * Worker fiber routine.
 */
static int
netbox_worker_f(va_list ap)
{
	(void)ap;
	struct netbox_transport *transport = fiber()->f_arg;
	struct lua_State *L = fiber()->storage.lua.stack;
	assert(transport->worker == fiber());
	assert(transport->coro_ref != LUA_NOREF);
	assert(transport->self_ref != LUA_NOREF);
	const double reconnect_after = !uri_is_nil(&transport->opts.uri) ?
				       transport->opts.reconnect_after : 0;
	while (!fiber_is_cancelled()) {
		if (netbox_transport_connect(transport) == 0) {
			int rc = luaT_cpcall(L, netbox_connection_handler_f,
					     transport);
			/* The worker loop can only be broken by an error. */
			assert(rc != 0);
			(void)rc;
			iostream_close(&transport->io);
		}
		if (transport->state == NETBOX_CLOSED)
			break;
		netbox_transport_set_error(transport);
		transport->state = (reconnect_after > 0 ?
				    NETBOX_ERROR_RECONNECT : NETBOX_ERROR);
		netbox_transport_on_state_change_pcall(transport, L);
		if (reconnect_after > 0) {
			fiber_sleep(reconnect_after);
		} else {
			break;
		}
	}
	transport->worker = NULL;
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, transport->coro_ref);
	transport->coro_ref = LUA_NOREF;
	/* Careful: luaL_unref may delete this transport object. */
	int ref = transport->self_ref;
	transport->self_ref = LUA_NOREF;
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, ref);
	fiber()->storage.lua.stack = NULL;
	return 0;
}

/**
 * Starts a worker fiber.
 */
static int
luaT_netbox_transport_start(struct lua_State *L)
{
	struct netbox_transport *transport = luaT_check_netbox_transport(L, 1);
	assert(transport->worker == NULL);
	assert(transport->coro_ref == LUA_NOREF);
	assert(transport->self_ref = LUA_NOREF);
	struct lua_State *fiber_L = lua_newthread(L);
	transport->coro_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	transport->self_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	const char *name;
	if (!uri_is_nil(&transport->opts.uri)) {
		name = tt_sprintf("%s:%s (net.box)",
				  transport->opts.uri.host ?: "",
				  transport->opts.uri.service ?: "");
	} else {
		assert(transport->opts.fd >= 0);
		name = tt_sprintf("fd=%d (net.box)", transport->opts.fd);
	}
	transport->worker = fiber_new_system(name, netbox_worker_f);
	if (transport->worker == NULL) {
		luaL_unref(L, LUA_REGISTRYINDEX, transport->coro_ref);
		transport->coro_ref = LUA_NOREF;
		luaL_unref(L, LUA_REGISTRYINDEX, transport->self_ref);
		transport->self_ref = LUA_NOREF;
		return luaT_error(L);
	}
	transport->worker->f_arg = transport;
	/*
	 * Code that needs a temporary fiber-local Lua state may save some time
	 * and resources for creating a new state and use this one.
	 */
	assert(transport->worker->storage.lua.stack == NULL);
	transport->worker->storage.lua.stack = fiber_L;
	fiber_wakeup(transport->worker);
	return 0;
}

/**
 * Stops a worker fiber.
 *
 * Takes an optional boolean argument 'wait': if set the function will wait
 * for all pending requests to be sent.
 */
static int
luaT_netbox_transport_stop(struct lua_State *L)
{
	struct netbox_transport *transport = luaT_check_netbox_transport(L, 1);
	bool wait = lua_toboolean(L, 2);
	if (wait && fiber() != transport->worker &&
	    transport->state != NETBOX_CLOSED &&
	    transport->state != NETBOX_ERROR) {
		transport->is_closing = true;
		/*
		 * Here we are waiting until send buf became empty:
		 * it is necessary to ensure that all requests are
		 * sent before the connection is closed.
		 */
		while (ibuf_used(&transport->send_buf) > 0)
			fiber_cond_wait(&transport->on_send_buf_empty);
		transport->is_closing = false;
	}
	/*
	 * While we were waiting for the send buffer to be empty,
	 * the state could change.
	 */
	if (transport->state != NETBOX_CLOSED &&
	    transport->state != NETBOX_ERROR) {
		box_error_raise(ER_NO_CONNECTION, "Connection closed");
		netbox_transport_set_error(transport);
		transport->state = NETBOX_CLOSED;
		netbox_transport_on_state_change(transport, L);
	}
	/* Cancel the worker fiber. */
	if (transport->worker != NULL) {
		fiber_cancel(transport->worker);
		/* Check if we cancelled ourselves. */
		luaL_testcancel(L);
	}
	return 0;
}

static int
luaT_netbox_transport_next_sync(struct lua_State *L)
{
	struct netbox_transport *transport = luaT_check_netbox_transport(L, 1);
	luaL_pushuint64(L, transport->next_sync);
	return 1;
}

/**
 * Puts an active connection to 'graceful_shutdown' state, in which no new
 * requests are allowed. The connection will be switched to the error state
 * (or error_reconnect if reconnect_after is configured) as soon as all pending
 * requests have been completed.
 */
static int
luaT_netbox_transport_graceful_shutdown(struct lua_State *L)
{
	struct netbox_transport *transport = luaT_check_netbox_transport(L, 1);
	if (transport->state == NETBOX_ACTIVE ||
	    transport->state == NETBOX_FETCH_SCHEMA) {
		transport->state = NETBOX_GRACEFUL_SHUTDOWN;
		netbox_transport_on_state_change(transport, L);
		/*
		 * If there's no in-progress requests, the worker fiber would
		 * never wake up by itself.
		 */
		if (transport->inprogress_request_count == 0)
			fiber_wakeup(transport->worker);
	}
	return 0;
}

int
luaopen_net_box(struct lua_State *L)
{
	iproto_features_create(&NETBOX_IPROTO_FEATURES);
	iproto_features_set(&NETBOX_IPROTO_FEATURES,
			    IPROTO_FEATURE_STREAMS);
	iproto_features_set(&NETBOX_IPROTO_FEATURES,
			    IPROTO_FEATURE_TRANSACTIONS);
	iproto_features_set(&NETBOX_IPROTO_FEATURES,
			    IPROTO_FEATURE_ERROR_EXTENSION);
	iproto_features_set(&NETBOX_IPROTO_FEATURES,
			    IPROTO_FEATURE_WATCHERS);
	iproto_features_set(&NETBOX_IPROTO_FEATURES,
			    IPROTO_FEATURE_PAGINATION);
	iproto_features_set(&NETBOX_IPROTO_FEATURES,
			    IPROTO_FEATURE_SPACE_AND_INDEX_NAMES);
	iproto_features_set(&NETBOX_IPROTO_FEATURES,
			    IPROTO_FEATURE_WATCH_ONCE);
	iproto_features_set(&NETBOX_IPROTO_FEATURES,
			    IPROTO_FEATURE_DML_TUPLE_EXTENSION);
	iproto_features_set(&NETBOX_IPROTO_FEATURES,
			    IPROTO_FEATURE_CALL_RET_TUPLE_EXTENSION);
	iproto_features_set(&NETBOX_IPROTO_FEATURES,
			    IPROTO_FEATURE_CALL_ARG_TUPLE_EXTENSION);

	lua_pushcfunction(L, luaT_netbox_request_iterator_next);
	luaT_netbox_request_iterator_next_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	static const struct luaL_Reg netbox_transport_meta[] = {
		{ "__gc",           luaT_netbox_transport_gc },
		{ "start",          luaT_netbox_transport_start },
		{ "stop",           luaT_netbox_transport_stop },
		{ "next_sync",	    luaT_netbox_transport_next_sync },
		{ "graceful_shutdown",
			luaT_netbox_transport_graceful_shutdown },
		{ "perform_request",
			luaT_netbox_transport_perform_request },
		{ "perform_async_request",
			luaT_netbox_transport_perform_async_request },
		{ "watch",          luaT_netbox_transport_watch },
		{ "unwatch",        luaT_netbox_transport_unwatch },
		{ NULL, NULL }
	};
	luaL_register_type(L, netbox_transport_typename, netbox_transport_meta);

	static const struct luaL_Reg netbox_request_meta[] = {
		{ "__autocomplete", luaT_netbox_request_serialize },
		{ "__gc",           luaT_netbox_request_gc },
		{ "__tostring",     luaT_netbox_request_tostring },
		{ "__serialize",    luaT_netbox_request_serialize },
		{ "__index",        luaT_netbox_request_index },
		{ "__newindex",     luaT_netbox_request_newindex },
		{ "is_ready",       luaT_netbox_request_is_ready },
		{ "result",         luaT_netbox_request_result },
		{ "wait_result",    luaT_netbox_request_wait_result },
		{ "discard",        luaT_netbox_request_discard },
		{ "pairs",          luaT_netbox_request_pairs },
		{ NULL, NULL }
	};
	luaL_register_type(L, netbox_request_typename, netbox_request_meta);

	static const luaL_Reg net_box_lib[] = {
		{ "new_transport",  luaT_netbox_new_transport },
		{ NULL, NULL}
	};
	luaT_newmodule(L, "net.box.lib", net_box_lib);

	lua_newtable(L);
	for (int i = 0; i < netbox_method_MAX; i++) {
		lua_pushinteger(L, i);
		lua_setfield(L, -2, netbox_method_strs[i]);
	}
	lua_setfield(L, -2, "method");

	return 1;
}
