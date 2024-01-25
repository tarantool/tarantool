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
#include "iproto.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>

#include <msgpuck.h>
#include <small/ibuf.h>
#include <small/obuf.h>
#include <base64.h>

#include "version.h"
#include "event.h"
#include "core/func_adapter.h"
#include "fiber.h"
#include "fiber_cond.h"
#include "cbus.h"
#include "say.h"
#include "sio.h"
#include "evio.h"
#include "iostream.h"
#include "scoped_guard.h"
#include "memory.h"
#include "random.h"

#include "bind.h"
#include "port.h"
#include "box.h"
#include "call.h"
#include "tuple_convert.h"
#include "session.h"
#include "xrow.h"
#include "schema.h" /* schema_version */
#include "replication.h" /* instance_uuid */
#include "iproto_constants.h"
#include "iproto_features.h"
#include "rmean.h"
#include "execute.h"
#include "errinj.h"
#include "tt_static.h"
#include "trivia/util.h"
#include "salad/stailq.h"
#include "txn.h"
#include "on_shutdown.h"
#include "flightrec.h"
#include "security.h"
#include "watcher.h"
#include "box/mp_box_ctx.h"
#include "box/tuple.h"
#include "mpstream/mpstream.h"

enum {
	IPROTO_PACKET_SIZE_MAX = 2UL * 1024 * 1024 * 1024,
};

enum {
	 ENDPOINT_NAME_MAX = 10
};

struct iproto_connection;
struct iproto_msg;

struct iproto_stream {
	/** Currently active stream transaction or NULL */
	struct txn *txn;
	/**
	 * Queue of pending requests (iproto messages) for this stream,
	 * processed sequentially. This field is accesable only from
	 * iproto thread. Queue items has iproto_msg type.
	 */
	struct stailq pending_requests;
	/** Id of this stream, used as a key in streams hash table */
	uint64_t id;
	/** This stream connection */
	struct iproto_connection *connection;
	/**
	 * Pre-allocated disconnect msg to gracefully rollback stream
	 * transaction and destroy stream object.
	 */
	struct cmsg on_disconnect;
	/**
	 * Message currently being processed in the tx thread.
	 * This field is accesable only from iproto thread.
	 */
	struct iproto_msg *current;
};

/**
 * A position in connection output buffer.
 * Since we use rotating buffers to recycle memory,
 * it includes not only a position in obuf, but also
 * a pointer to obuf the position is for.
 */
struct iproto_wpos {
	struct obuf *obuf;
	struct obuf_svp svp;
};

static void
iproto_wpos_create(struct iproto_wpos *wpos, struct obuf *out)
{
	wpos->obuf = out;
	wpos->svp = obuf_create_svp(out);
}

/**
 * Message sent when iproto thread dropped all connections that requested
 * to be dropped.
 */
struct iproto_drop_finished {
	/** Base structure. */
	struct cmsg base;
	/**
	 * Generation a is a sequence number of iproto_drop_connections()
	 * invocation.
	 *
	 * Generation is used to handle racy situation when previous invocation
	 * of iproto_drop_connections() was failed and there is new invocation.
	 * Message from previous invocation may be delivired and account
	 * iproto thread as finished dropping connection which is not true.
	 */
	unsigned generation;
};

struct iproto_thread {
	/**
	 * Slab cache used for allocating memory for output network buffers
	 * in the tx thread.
	 */
	struct slab_cache net_slabc;
	/**
	 * Network thread execution unit.
	 */
	struct cord net_cord;
	/**
	 * A single global queue for all requests in all connections. All
	 * requests from all connections are processed concurrently.
	 * Is also used as a queue for just established connections and to
	 * execute disconnect triggers. A few notes about these triggers:
	 * - they need to be run in a fiber
	 * - unlike an ordinary request failure, on_connect trigger
	 *   failure must lead to connection close.
	 * - on_connect trigger must be processed before any other
	 *   request on this connection.
	 */
	struct cpipe tx_pipe;
	struct cpipe net_pipe;
	/**
	 * Static routes for this iproto thread
	 */
	struct cmsg_hop begin_route[2];
	struct cmsg_hop commit_route[2];
	struct cmsg_hop rollback_route[2];
	struct cmsg_hop rollback_on_disconnect_route[2];
	struct cmsg_hop destroy_route[2];
	struct cmsg_hop disconnect_route[2];
	struct cmsg_hop misc_route[2];
	struct cmsg_hop call_route[2];
	struct cmsg_hop select_route[2];
	struct cmsg_hop process1_route[2];
	struct cmsg_hop sql_route[2];
	struct cmsg_hop join_route[2];
	struct cmsg_hop subscribe_route[2];
	struct cmsg_hop error_route[2];
	struct cmsg_hop push_route[2];
	struct cmsg_hop *dml_route[IPROTO_TYPE_STAT_MAX];
	struct cmsg_hop connect_route[2];
	struct cmsg_hop override_route[2];
	/*
	 * Set of overridden request handlers. Used by IPROTO thread to skip
	 * request preprocessing and use the 'override' route.
	 */
	mh_i32_t *req_handlers;
	/*
	 * Iproto thread memory pools
	 */
	struct mempool iproto_msg_pool;
	struct mempool iproto_connection_pool;
	struct mempool iproto_stream_pool;
	/*
	 * List of stopped connections
	 */
	struct rlist stopped_connections;
	/*
	 * Iproto thread stat
	 */
	struct rmean *rmean;
	/*
	 * Iproto thread id
	 */
	uint32_t id;
	/** Array of iproto binary listeners */
	struct evio_service binary;
	/** Requests count currently pending in stream queue. */
	size_t requests_in_stream_queue;
	/** List of all connections. */
	struct rlist connections;
	/** Number of connections that pending drop. */
	size_t drop_pending_connection_count;
	/**
	 * Message used to notify TX thread that all connections marked
	 * to de dropped are dropped.
	 */
	struct iproto_drop_finished drop_finished_msg;
	/**
	 * If set then iproto thread shutdown is started and we should not
	 * accept new connections.
	 */
	bool is_shutting_down;
	/**
	 * The following fields are used exclusively by the tx thread.
	 * Align them to prevent false-sharing.
	 */
	struct {
		alignas(CACHELINE_SIZE)
		/** Request count currently processed by tx thread. */
		size_t requests_in_progress;
		/** Iproto thread stat collected in tx thread. */
		struct rmean *rmean;
	} tx;
};

/** Condition for drop finished. */
static struct fiber_cond drop_finished_cond;
/** Count of iproto threads that are not finished connections drop yet. */
static size_t drop_pending_thread_count;
/**
 * Generation is a sequence number of dropping connection invocation.
 *
 * See also `struct iproto_drop_finished`.
 */
static unsigned drop_generation;

/**
 * IPROTO listen URIs. Set by box.cfg.listen.
 */
static struct uri_set iproto_uris;

static struct iproto_thread *iproto_threads;
int iproto_threads_count;
/**
 * This binary contains all bind socket properties, like
 * address the iproto listens for. Is kept in TX to be
 * shown in box.info. It should be global, because it contains
 * properties, and should be accessible from differnent functions
 * in tx thread.
 */
static struct evio_service tx_binary;

/**
 * In Greek mythology, Kharon is the ferryman who carries souls
 * of the newly deceased across the river Styx that divided the
 * world of the living from the world of the dead. Here Kharon is
 * a cbus message and does similar work. It notifies the iproto
 * thread about new data in a connection output buffer and carries
 * back to tx thread the position in the output buffer which has
 * been successfully flushed to the socket. Styx here is cpipe,
 * and the boat is cbus message.
 */
struct iproto_kharon {
	struct cmsg base;
	/**
	 * Tx thread sets wpos to the current position in the
	 * output buffer and sends the message to iproto thread.
	 * Iproto returns the message to tx after setting wpos
	 * to the last flushed position (similarly to
	 * iproto_msg.wpos).
	 */
	struct iproto_wpos wpos;
};

/**
 * Network readahead. A signed integer to avoid
 * automatic type coercion to an unsigned type.
 * We assign it without locks in txn thread and
 * use in iproto thread -- it's OK that
 * readahead has a stale value while until the thread
 * caches have synchronized, after all, it's used
 * in new connections only.
 *
 * Notice that the default is not a strict power of two.
 * slab metadata takes some space, and we want
 * allocation steps to be correlated to slab buddy
 * sizes, so when we ask slab cache for 16320 bytes,
 * we get a slab of size 16384, not 32768.
 */
unsigned iproto_readahead = 16320;

/* The maximal number of iproto messages in fly. */
static int iproto_msg_max = IPROTO_MSG_MAX_MIN;

/**
 * Request handlers meta information. The IPROTO request of each type can be
 * overridden by the following types of handlers (listed in priority order):
 *  1. Lua handlers, set in the event registry by request type id;
 *  2. Lua handlers, set in the event registry by request type name;
 *  3. C handler, set by `iproto_override()'.
 */
struct iproto_req_handlers {
	/**
	 * Triggers from the event registry, set by request type id.
	 * NULL if no such triggers.
	 */
	struct event *event_by_id;
	/**
	 * Triggers from the event registry, set by request type name.
	 * NULL if no such triggers.
	 */
	struct event *event_by_name;
	/**
	 * C request handler.
	 */
	struct {
		/** C request handler. NULL if not set. */
		iproto_handler_t cb;
		/** C request handler destructor, can be NULL. */
		iproto_handler_destroy_t destroy;
		/** Context passed to the handler and destructor. */
		void *ctx;
	} c;
};

/**
 * Request handler table used in TX thread for processing requests with
 * overridden handlers.
 */
static mh_i32ptr_t *tx_req_handlers;

/**
 * If set then iproto shutdown is started and we should not accept new
 * connections.
 */
static bool iproto_is_shutting_down;

/** Available iproto configuration changes. */
enum iproto_cfg_op {
	/** Command code to set max input for iproto thread */
	IPROTO_CFG_MSG_MAX,
	/**
	 * Command code to start listen socket contained
	 * in evio_service object
	 */
	IPROTO_CFG_START,
	/**
	 * Command code to stop listen socket contained
	 * in evio_service object. In case when user sets
	 * new parameters for iproto, it is necessary to stop
	 * listen sockets in iproto threads before reconfiguration.
	 */
	IPROTO_CFG_STOP,
	/**
	 * Equivalent to IPROTO_CFG_STOP followed by IPROTO_CFG_START.
	 */
	IPROTO_CFG_RESTART,
	/**
	 * Command code do get statistic from iproto thread
	 */
	IPROTO_CFG_STAT,
	/**
	 * Command code to notify IPROTO threads a new handler has been set or
	 * reset.
	 */
	IPROTO_CFG_OVERRIDE,
	/**
	 * Command code to create a new IPROTO session.
	 */
	IPROTO_CFG_SESSION_NEW,
	/**
	 * Command code to drop all current connections.
	 */
	IPROTO_CFG_DROP_CONNECTIONS,
	IPROTO_CFG_SHUTDOWN,
};

/**
 * Since there is no way to "synchronously" change the
 * state of the io thread, to change the listen port or max
 * message count in flight send a special message to iproto
 * thread.
 */
struct iproto_cfg_msg: public cbus_call_msg
{
	/** Operation to execute in iproto thread. */
	enum iproto_cfg_op op;
	union {
		/** Pointer to the statistic structure. */
		struct iproto_stats *stats;
		/** New iproto max message count. */
		int iproto_msg_max;
		struct {
			/** New connection IO stream. */
			struct iostream io;
			/** New connection session. */
			struct session *session;
		} session_new;
		struct {
			/** Overridden request type. */
			uint32_t req_type;
			/** Whether the request handler is set or reset. */
			bool is_set;
		} override;
		struct {
			/**
			 * Connection that executing iproto_drop_connections.
			 * NULL if the function is called not from connection.
			 */
			struct iproto_connection *owner;
			/**
			 * Generation is sequence number of dropping
			 * connection invocation.
			 *
			 * See also `struct iproto_drop_finished`.
			 */
			unsigned generation;
		} drop_connections;
	};
	struct iproto_thread *iproto_thread;
};

static inline void
iproto_cfg_msg_create(struct iproto_cfg_msg *msg, enum iproto_cfg_op op)
{
	memset(msg, 0, sizeof(*msg));
	msg->op = op;
}

/**
 * Sends a configuration message to an IPROTO thread and waits for completion.
 *
 * The message may be allocated on stack.
 */
static void
iproto_do_cfg(struct iproto_thread *iproto_thread, struct iproto_cfg_msg *msg);

int
iproto_addr_count(void)
{
	return evio_service_count(&tx_binary);
}

const char *
iproto_addr_str(char *buf, int idx)
{
	socklen_t size;
	const struct sockaddr *addr = evio_service_addr(&tx_binary, idx, &size);
	sio_addr_snprintf(buf, SERVICE_NAME_MAXLEN, addr, size);
	return buf;
}

/**
 * How big is a buffer which needs to be shrunk before
 * it is put back into buffer cache.
 */
static inline unsigned
iproto_max_input_size(void)
{
	return 18 * iproto_readahead;
}

void
iproto_reset_input(struct ibuf *ibuf)
{
	/*
	 * If we happen to have fully processed the input,
	 * move the pos to the start of the input buffer.
	 */
	assert(ibuf_used(ibuf) == 0);
	if (ibuf_capacity(ibuf) < iproto_max_input_size()) {
		ibuf_reset(ibuf);
	} else {
		struct slab_cache *slabc = ibuf->slabc;
		ibuf_destroy(ibuf);
		ibuf_create(ibuf, slabc, iproto_readahead);
	}
}

/* {{{ iproto_msg - declaration */

/**
 * A single msg from io thread. All requests
 * from all connections are queued into a single queue
 * and processed in FIFO order.
 */
struct iproto_msg
{
	struct cmsg base;
	struct iproto_connection *connection;

	/* --- Box msgs - actual requests for the transaction processor --- */
	/* Request message code and sync. */
	struct xrow_header header;
	union {
		/** Connect. */
		struct {
			union {
				/** Peer address. */
				struct sockaddr addr;
				/** Peer address storage. */
				struct sockaddr_storage addrstorage;
			};
			/** Peer address size. */
			socklen_t addrlen;
			/**
			 * Session to use for the new connection.
			 * Optional. If omitted, a new session object
			 * will be created in the TX thread.
			 */
			struct session *session;
		} connect;
		/** Box request, if this is a DML */
		struct request dml;
		/** Box request, if this is a call or eval. */
		struct call_request call;
		/** Watch request. */
		struct watch_request watch;
		/** Authentication request. */
		struct auth_request auth;
		/** Features request. */
		struct id_request id;
		/** SQL request, if this is the EXECUTE/PREPARE request. */
		struct sql_request sql;
		/** BEGIN request */
		struct begin_request begin;
		/** COMMIT request */
		struct commit_request commit;
		/** In case of iproto parse error, saved diagnostics. */
		struct diag diag;
	};
	/**
	 * Input buffer which stores the request data. It can be
	 * discarded only when the message returns to iproto thread.
	 */
	struct ibuf *p_ibuf;
	/**
	 * How much space the request takes in the
	 * input buffer (len, header and body - all of it)
	 * This also works as a reference counter to
	 * ibuf object.
	 */
	size_t len;
	/**
	 * Pointer to the start of unparsed request stored in @a p_ibuf.
	 * It is used to dump request to flight recorder (if available) in
	 * TX thread. It is guaranteed that @a reqstart points to the valid
	 * position: rpos of input buffer is moved after processing the message;
	 * meanwhile requests are handled in the order they are stored in
	 * the buffer.
	 */
	const char *reqstart;
	/**
	 * Position in the connection output buffer. When sending a
	 * message to the tx thread, iproto sets it to its current
	 * flush position so that tx can reuse a buffer that has been
	 * flushed. The tx thread, in turn, sets it to the end of the
	 * data it has just written, to let iproto know that there is
	 * more output to flush.
	 */
	struct iproto_wpos wpos;
	/**
	 * Message sent by the tx thread to notify iproto that input has
	 * been processed and can be discarded before request completion.
	 * Used by long (yielding) CALL/EVAL requests.
	 */
	struct cmsg discard_input;
	/**
	 * Used in "connect" msgs, true if connect trigger failed
	 * and the connection must be closed.
	 */
	bool close_connection;
	/**
	 * A stailq_entry to hold message in stream.
	 * All messages processed in stream sequently. Before processing
	 * all messages added to queue of pending requests. If this queue
	 * was empty message begins to be processed, otherwise it waits until
	 * all previous messages are processed.
	 */
	struct stailq_entry in_stream;
	/** Stream that owns this message, or NULL. */
	struct iproto_stream *stream;
	/** Link in connection->tx.inprogress. */
	struct rlist in_inprogress;
	/** TX thread fiber that processing this message. */
	struct fiber *fiber;
};

/**
 * Resume stopped connections, if any.
 */
static void
iproto_resume(struct iproto_thread *iproto_thread);

/**
 * Prepares IPROTO message: decodes the message header, checks the message's
 * stream identifier, and set's the message's cbus route.
 */
static void
iproto_msg_prepare(struct iproto_msg *msg, const char **pos,
		   const char *reqend);

enum rmean_net_name {
	IPROTO_SENT,
	IPROTO_RECEIVED,
	IPROTO_CONNECTIONS,
	IPROTO_REQUESTS,
	IPROTO_STREAMS,
	REQUESTS_IN_STREAM_QUEUE,
	RMEAN_NET_LAST,
};

const char *rmean_net_strings[RMEAN_NET_LAST] = {
	"SENT",
	"RECEIVED",
	"CONNECTIONS",
	"REQUESTS",
	"STREAMS",
	"REQUESTS_IN_STREAM_QUEUE",
};

enum rmean_tx_name {
	REQUESTS_IN_PROGRESS,
	RMEAN_TX_LAST,
};

const char *rmean_tx_strings[RMEAN_TX_LAST] = {
	"REQUESTS_IN_PROGRESS",
};

static void
tx_process_destroy(struct cmsg *m);

static void
net_finish_destroy(struct cmsg *m);

/** Fire on_disconnect triggers in the tx thread. */
static void
tx_process_disconnect(struct cmsg *m);

/** Send destroy message to tx thread. */
static void
net_finish_disconnect(struct cmsg *m);

/**
 * Kharon is in the dead world (iproto). Schedule an event to
 * flush new obuf as reflected in the fresh wpos.
 * @param m Kharon.
 */
static void
iproto_process_push(struct cmsg *m);

/**
 * Kharon returns to the living world (tx) back from the dead one
 * (iproto). Check if a new push is pending and make a new trip
 * to iproto if necessary.
 * @param m Kharon.
 */
static void
tx_end_push(struct cmsg *m);

/* }}} */

/* {{{ iproto_connection - declaration and definition */

/** Connection life cycle stages. */
enum iproto_connection_state {
	/**
	 * A connection is always alive in the beginning because
	 * takes an already active socket in a constructor.
	 */
	IPROTO_CONNECTION_ALIVE,
	/**
	 * Socket was closed, a notification is sent to the TX
	 * thread to close the session.
	 */
	IPROTO_CONNECTION_CLOSED,
	/**
	 * TX thread was notified about close, but some requests
	 * are still not finished. That state may be skipped in
	 * case the connection was already idle (not having
	 * unfinished requests) at the moment of closing.
	 */
	IPROTO_CONNECTION_PENDING_DESTROY,
	/**
	 * All requests are finished, a destroy request is sent to
	 * the TX thread.
	 */
	IPROTO_CONNECTION_DESTROYED,
};

/**
 * Context of a single client connection.
 * Interaction scheme:
 *
 *  Receive from the network.
 *     |
 * +---|---------------------+   +------------+
 * |   |      iproto thread  |   | tx thread  |
 * |   v                     |   |            |
 * | ibuf[0]- - - - - - - - -|- -|- - >+      |
 * |                         |   |     |      |
 * |           ibuf[1]       |   |     |      |
 * |                         |   |     |      |
 * | obuf[0] <- - - - - - - -|- -|- - -+      |
 * |    |                    |   |     |      |
 * |    |      obuf[1] <- - -|- -|- - -+      |
 * +----|-----------|--------+   +------------+
 *      |           v
 *      |        Send to
 *      |        network.
 *      v
 * Send to network after obuf[1], i.e. older responses are sent first.
 *
 * ibuf structure:
 *                   rpos             wpos           end
 * +-------------------|----------------|-------------+
 * \________/\________/ \________/\____/
 *  \  msg       msg /    msg     parse
 *   \______________/             size
 *   response is sent,
 *     messages are
 *      discarded
 */
struct iproto_connection
{
	/**
	 * Two rotating buffers for input. Input is first read into
	 * ibuf[0]. As soon as it buffer becomes full, the buffers are
	 * rotated. When all input buffers are used up, the input
	 * is suspended. The buffer becomes available for use
	 * again when tx thread completes processing the messages
	 * stored in the buffer.
	 */
	struct ibuf ibuf[2];
	/** Pointer to the current buffer. */
	struct ibuf *p_ibuf;
	/**
	 * Number of not yet processed messages in the corresponding
	 * input buffer.
	 */
	size_t input_msg_count[2];
	/**
	 * Two rotating buffers for output. The tx thread switches to
	 * another buffer if it finds it to be empty (flushed out).
	 * This guarantees that memory gets recycled as soon as output
	 * is flushed by the iproto thread.
	 */
	struct obuf obuf[2];
	/**
	 * Position in the output buffer that points to the beginning
	 * of the data awaiting to be flushed. Advanced by the iproto
	 * thread upon successfull flush.
	 */
	struct iproto_wpos wpos;
	/**
	 * Position in the output buffer that points to the end of the
	 * data awaiting to be flushed. Advanced by the iproto thread
	 * upon receiving a message from the tx thread telling that more
	 * output is available (see iproto_msg::wpos).
	 */
	struct iproto_wpos wend;
	/*
	 * Size of readahead which is not parsed yet, i.e. size of
	 * a piece of request which is not fully read. Is always
	 * relative to ibuf.wpos. In other words, ibuf.wpos -
	 * parse_size gives the start of the unparsed request.
	 * A size rather than a pointer is used to be safe in case
	 * ibuf.buf is reallocated. Being relative to ibuf.wpos,
	 * rather than to ibuf.rpos is helpful to make sure
	 * ibuf_reserve() or buffers rotation don't make the value
	 * meaningless.
	 */
	size_t parse_size;
	/**
	 * Nubmer of active long polling requests that have already
	 * discarded their arguments in order not to stall other
	 * connections.
	 */
	int long_poll_count;
	/** I/O stream used for communication with the client. */
	struct iostream io;
	struct ev_io input;
	struct ev_io output;
	/** Logical session. */
	struct session *session;
	ev_loop *loop;
	/**
	 * Pre-allocated disconnect msg. Is sent right after
	 * actual disconnect has happened. Does not destroy the
	 * connection. Used to notify existing requests about the
	 * occasion.
	 */
	struct cmsg disconnect_msg;
	/**
	 * Pre-allocated destroy msg. Is sent after disconnect has
	 * happened and a last request has finished. Firstly
	 * destroys tx-related resources and then deletes the
	 * connection.
	 */
	struct cmsg destroy_msg;
	/**
	 * Connection state. Mainly it is used to determine when
	 * the connection can be destroyed, and for debug purposes
	 * to assert on a double destroy, for example.
	 */
	enum iproto_connection_state state;
	struct rlist in_stop_list;
	/**
	 * Flag indicates, that client sent SHUT_RDWR or connection
	 * is closed from client side. When it is set to false, we
	 * should not write to the socket.
	 */
	bool can_write;
	/**
	 * Hash table that holds all streams for this connection.
	 * This field is accesable only from iproto thread.
	 */
	struct mh_i64ptr_t *streams;
	/**
	 * Kharon is used to implement box.session.push().
	 * When a new push is ready, tx uses kharon to notify
	 * iproto about new data in connection output buffer.
	 *
	 * Kharon can not be in two places at the time. When
	 * kharon leaves tx, is_push_sent is set to true. After
	 * that new pushes can not use it. Instead, they set
	 * is_push_pending flag. When Kharon is back to tx it
	 * clears is_push_sent, checks is_push_pending and departs
	 * immediately back to iproto if it is set.
	 *
	 * This design makes it easy to use a single message per
	 * connection for pushes while new pushes do not wait for
	 * the message to become available.
	 *
	 * iproto                                               tx
	 * -------------------------------------------------------
	 *                                        + [push message]
	 *                 <--- notification ----
	 *                                        + [push message]
	 * [feed event]
	 *             --- kharon travels back ---->
	 * [write to socket]
	 *                                        + [push message]
	 *                                        [new push found]
	 *                 <--- notification ----
	 * [write ends]
	 *                          ...
	 */
	struct iproto_kharon kharon;
	/**
	 * The following fields are used exclusively by the tx thread.
	 * Align them to prevent false-sharing.
	 */
	struct {
		alignas(CACHELINE_SIZE)
		/** Pointer to the current output buffer. */
		struct obuf *p_obuf;
		/** True if Kharon is in use/travelling. */
		bool is_push_sent;
		/**
		 * True if new pushes are waiting for Kharon
		 * return.
		 */
		bool is_push_pending;
		/** List of inprogress messages. */
		struct rlist inprogress;
	} tx;
	/** Authentication salt. */
	char salt[IPROTO_SALT_SIZE];
	/** Iproto connection thread */
	struct iproto_thread *iproto_thread;
	/**
	 * The connection is processing replication command so that
	 * IO is handled by relay code.
	 */
	bool is_in_replication;
	/** Link in iproto_thread->connections. */
	struct rlist in_connections;
	/** Set if connection is being dropped. */
	bool is_drop_pending;
	/**
	 * Generation is sequence number of dropping connection invocation.
	 *
	 * See also `struct iproto_drop_finished`.
	 */
	unsigned drop_generation;
	/**
	 * Messaged sent to TX to cancel all inprogress requests of the
	 * connection.
	 */
	struct cmsg cancel_msg;
	/** Set if connection is accepted in TX. */
	bool is_established;
};

/** Returns a string suitable for logging. */
static inline const char *
iproto_connection_name(const struct iproto_connection *con)
{
	return sio_socketname(con->io.fd);
}

#ifdef NDEBUG
#define iproto_write_error(io, e, schema_version, sync)                         \
	iproto_do_write_error(io, e, schema_version, sync);
#else
#define iproto_write_error(io, e, schema_version, sync) do {                    \
	int fd = (io)->fd;                                                      \
	int flags = fcntl(fd, F_GETFL, 0);                                      \
	if (flags >= 0)                                                         \
		fcntl(fd, F_SETFL, flags & (~O_NONBLOCK));                      \
	iproto_do_write_error(io, e, schema_version, sync);                     \
	if (flags >= 0)                                                         \
		fcntl(fd, F_SETFL, flags);                                      \
} while (0);
#endif

static struct iproto_stream *
iproto_stream_new(struct iproto_connection *connection, uint64_t stream_id)
{
	struct iproto_thread *iproto_thread = connection->iproto_thread;
	struct iproto_stream *stream = (struct iproto_stream *)
		xmempool_alloc(&iproto_thread->iproto_stream_pool);
	rmean_collect(connection->iproto_thread->rmean, IPROTO_STREAMS, 1);
	stream->txn = NULL;
	stream->current = NULL;
	stailq_create(&stream->pending_requests);
	stream->id = stream_id;
	stream->connection = connection;
	return stream;
}

static inline void
iproto_stream_rollback_on_disconnect(struct iproto_stream *stream)
{
	struct iproto_connection *conn = stream->connection;
	struct iproto_thread *iproto_thread = conn->iproto_thread;
	struct cmsg_hop *route =
		iproto_thread->rollback_on_disconnect_route;
	cmsg_init(&stream->on_disconnect, route);
	cpipe_push(&iproto_thread->tx_pipe, &stream->on_disconnect);
}

/**
 * Return true if we have not enough spare messages
 * in the message pool.
 */
static inline bool
iproto_check_msg_max(struct iproto_thread *iproto_thread)
{
	size_t request_count = mempool_count(&iproto_thread->iproto_msg_pool);
	return request_count > (size_t) iproto_msg_max;
}

static inline void
iproto_msg_delete(struct iproto_msg *msg)
{
	struct iproto_thread *iproto_thread = msg->connection->iproto_thread;
	mempool_free(&msg->connection->iproto_thread->iproto_msg_pool, msg);
	iproto_resume(iproto_thread);
}

static void
iproto_stream_delete(struct iproto_stream *stream)
{
	assert(stream->current == NULL);
	assert(stailq_empty(&stream->pending_requests));
	assert(stream->txn == NULL);
	mempool_free(&stream->connection->iproto_thread->iproto_stream_pool, stream);
}

static struct iproto_msg *
iproto_msg_new(struct iproto_connection *con)
{
	struct mempool *iproto_msg_pool = &con->iproto_thread->iproto_msg_pool;
	struct iproto_msg *msg =
		(struct iproto_msg *)xmempool_alloc(iproto_msg_pool);
	msg->close_connection = false;
	msg->connection = con;
	msg->stream = NULL;
	msg->fiber = NULL;
	rmean_collect(con->iproto_thread->rmean, IPROTO_REQUESTS, 1);
	return msg;
}

/**
 * Signal input unless it's blocked on I/O or stopped.
 */
static inline void
iproto_connection_feed_input(struct iproto_connection *con)
{
	assert(con->state == IPROTO_CONNECTION_ALIVE);
	if (!ev_is_active(&con->input) && rlist_empty(&con->in_stop_list))
		ev_feed_event(con->loop, &con->input, EV_CUSTOM);
}

/**
 * Signal output unless it's blocked on I/O.
 */
static inline void
iproto_connection_feed_output(struct iproto_connection *con)
{
	assert(con->state == IPROTO_CONNECTION_ALIVE);
	if (!ev_is_active(&con->output))
		ev_feed_event(con->loop, &con->output, EV_CUSTOM);
}

/**
 * A connection is idle when the client is gone
 * and there are no outstanding msgs in the msg queue.
 * An idle connection can be safely garbage collected.
 *
 * ibuf_size() provides an effective reference counter
 * on connection use in the tx request queue. Any request
 * in the request queue has a non-zero len, and ibuf_size()
 * is therefore non-zero as long as there is at least
 * one request in the tx queue.
 */
static inline bool
iproto_connection_is_idle(struct iproto_connection *con)
{
	/*
	 * The check for 'mh_size (streams) == 0' was added, because it is
	 * possible that when disconnect occurs, there is active transaction
	 * in stream after processing all messages. In this case we send
	 * special message to rollback it, and without this check we would
	 * immediately send special message to destroy connection. This would
	 * not lead to error now, since the messages are processed strictly
	 * sequentially and rollback does not yield, but it is not safely and
	 * if we add some more complex logic, it may lead to difficulty catching
	 * errors in the future.
	 */
	return con->long_poll_count == 0 &&
	       mh_size(con->streams) == 0 &&
	       ibuf_used(&con->ibuf[0]) == 0 &&
	       ibuf_used(&con->ibuf[1]) == 0;
}

/**
 * Stop input when readahead limit is reached. When
 * we process some messages *on this connection*, the input can be
 * resumed.
 */
static inline void
iproto_connection_stop_readahead_limit(struct iproto_connection *con)
{
	say_warn_ratelimited("stopping input on connection %s, "
			     "readahead limit is reached",
			     iproto_connection_name(con));
	assert(rlist_empty(&con->in_stop_list));
	ev_io_stop(con->loop, &con->input);
}

static inline void
iproto_connection_stop_msg_max_limit(struct iproto_connection *con)
{
	assert(rlist_empty(&con->in_stop_list));

	say_warn_ratelimited("stopping input on connection %s, "
			     "net_msg_max limit is reached",
			     iproto_connection_name(con));
	ev_io_stop(con->loop, &con->input);
	/*
	 * Important to add to tail and fetch from head to ensure
	 * strict lifo order (fairness) for stopped connections.
	 */
	rlist_add_tail(&con->iproto_thread->stopped_connections,
		       &con->in_stop_list);
}

/**
 * Send a destroy message to TX thread in case all requests are
 * finished.
 */
static inline void
iproto_connection_try_to_start_destroy(struct iproto_connection *con)
{
	assert(con->state == IPROTO_CONNECTION_CLOSED ||
	       con->state == IPROTO_CONNECTION_PENDING_DESTROY);
	if (!iproto_connection_is_idle(con)) {
		/*
		 * Not all requests are finished. Let the last
		 * finished request destroy the connection.
		 */
		con->state = IPROTO_CONNECTION_PENDING_DESTROY;
		return;
	}
	/*
	 * If the connection has no outstanding requests in the
	 * input buffer, then no one (e.g. tx thread) is referring
	 * to it, so it must be destroyed. Firstly queue a msg to
	 * destroy the session and other resources owned by TX
	 * thread. When it is done, iproto thread will destroy
	 * other parts of the connection.
	 */
	con->state = IPROTO_CONNECTION_DESTROYED;
	cpipe_push(&con->iproto_thread->tx_pipe, &con->destroy_msg);
}

/**
 * Initiate a connection shutdown. This method may
 * be invoked many times, and does the internal
 * bookkeeping to only cleanup resources once.
 */
static inline void
iproto_connection_close(struct iproto_connection *con)
{
	if (con->state == IPROTO_CONNECTION_ALIVE) {
		/* Clears all pending events. */
		ev_io_stop(con->loop, &con->input);
		ev_io_stop(con->loop, &con->output);
		/*
		 * Invalidate fd to prevent undefined behavior in case
		 * we mistakenly try to use it after this point.
		 */
		con->input.fd = con->output.fd = -1;
		iostream_close(&con->io);
		/*
		 * Discard unparsed data, to recycle the
		 * connection in net_send_msg() as soon as all
		 * parsed data is processed.  It's important this
		 * is done only once.
		 */
		ibuf_discard(con->p_ibuf, con->parse_size);
		con->parse_size = 0;
		mh_int_t node;
		mh_foreach(con->streams, node) {
			struct iproto_stream *stream = (struct iproto_stream *)
				mh_i64ptr_node(con->streams, node)->val;
			/**
			 * If stream->current == NULL and stream requests
			 * queue is empty, it means that there is some active
			 * transaction which was not commited yet. We need to
			 * rollback it, since we push on_disconnect message
			 * to tx thread here. Otherwise we destroy stream in
			 * `net_send_msg` after processing all requests.
			 */
			if (stream->current == NULL &&
			    stailq_empty(&stream->pending_requests))
				iproto_stream_rollback_on_disconnect(stream);
		}
		cpipe_push(&con->iproto_thread->tx_pipe, &con->disconnect_msg);
		assert(con->state == IPROTO_CONNECTION_ALIVE);
		con->state = IPROTO_CONNECTION_CLOSED;
	} else if (con->state == IPROTO_CONNECTION_PENDING_DESTROY) {
		iproto_connection_try_to_start_destroy(con);
	} else {
		assert(con->state == IPROTO_CONNECTION_CLOSED);
	}
	rlist_del(&con->in_stop_list);
}

static inline struct ibuf *
iproto_connection_next_input(struct iproto_connection *con)
{
	return &con->ibuf[con->p_ibuf == &con->ibuf[0]];
}

/**
 * If there is no space for reading input, we can do one of the
 * following:
 * - try to get a new ibuf, so that it can fit the request.
 *   Always getting a new input buffer when there is no space
 *   makes the instance susceptible to input-flood attacks.
 *   Therefore, at most 2 ibufs are used in a single connection,
 *   one is "open", receiving input, and the other is closed,
 *   waiting for flushing output from a corresponding obuf.
 * - stop input and wait until the client reads piled up output,
 *   so the input buffer can be reused. This complements
 *   the previous strategy. It is only safe to stop input if it
 *   is known that there is output. In this case input event
 *   flow will be resumed when all replies to previous requests
 *   are sent. Since there are two buffers, the input is only
 *   stopped when both of them are fully used up.
 *
 * To make this strategy work, each ibuf in use must fit at least
 * one request. Otherwise, both obufs may end up having no data to
 * flush, while current ibuf is too small to fit a big incoming
 * request.
 */
static struct ibuf *
iproto_connection_input_buffer(struct iproto_connection *con)
{
	struct ibuf *old_ibuf = con->p_ibuf;

	size_t to_read = 3; /* Smallest possible valid request. */

	/* The type code is checked in iproto_enqueue_batch() */
	if (con->parse_size) {
		const char *pos = old_ibuf->wpos - con->parse_size;
		if (mp_check_uint(pos, old_ibuf->wpos) <= 0)
			to_read = mp_decode_uint(&pos);
	}

	if (ibuf_unused(old_ibuf) >= to_read) {
		/*
		 * If all read data is discarded, move read
		 * position to the start of the buffer, to
		 * reduce chances of unaccounted growth of the
		 * buffer as read position is shifted to the
		 * end of the buffer.
		 */
		if (ibuf_used(old_ibuf) == 0)
			ibuf_reset(old_ibuf);
		return old_ibuf;
	}

	/*
	 * Reuse the buffer if all requests are processed
	 * (in only has unparsed content).
	 */
	if (ibuf_used(old_ibuf) == con->parse_size) {
		xibuf_reserve(old_ibuf, to_read);
		return old_ibuf;
	}

	struct ibuf *new_ibuf = iproto_connection_next_input(con);
	if (ibuf_used(new_ibuf) != 0) {
		/*
		 * Wait until the second buffer is flushed
		 * and becomes available for reuse.
		 */
		return NULL;
	}
	/* Update buffer size if readahead has changed. */
	if (new_ibuf->start_capacity != iproto_readahead) {
		ibuf_destroy(new_ibuf);
		ibuf_create(new_ibuf, cord_slab_cache(), iproto_readahead);
	}

	xibuf_reserve(new_ibuf, to_read + con->parse_size);
	if (con->parse_size != 0) {
		/* Move the cached request prefix to the new buffer. */
		void *wpos = ibuf_alloc(new_ibuf, con->parse_size);
		memcpy(wpos, old_ibuf->wpos - con->parse_size, con->parse_size);
		/*
		 * Discard unparsed data in the old buffer, otherwise it
		 * won't be recycled when all parsed requests are processed.
		 */
		ibuf_discard(old_ibuf, con->parse_size);
		/*
		 * We made ibuf idle. If obuf was already idle it
		 * makes the both ibuf and obuf idle, time to trim
		 * them.
		 */
		if (ibuf_used(old_ibuf) == 0)
			iproto_reset_input(old_ibuf);
	}
	/*
	 * Rotate buffers. Not strictly necessary, but
	 * helps preserve response order.
	 */
	con->p_ibuf = new_ibuf;
	return new_ibuf;
}

/**
 * Check if message belongs to stream (stream_id != 0), and if it
 * is so create new stream or get stream from connection streams
 * hash table. Put message to stream pending messages list.
 * @retval true  - the message is ready to push to TX thread (either if
 *                 stream_id is not set (is zero) or the stream is not
 *                 processing other messages).
 *         false - the message is postponed because its stream is busy
 *                 processing previous message(s).
 */
static bool
iproto_msg_start_processing_in_stream(struct iproto_msg *msg)
{
	uint64_t stream_id = msg->header.stream_id;
	if (stream_id == 0)
		return true;

	struct iproto_connection *con = msg->connection;
	struct iproto_stream *stream = NULL;
	mh_int_t pos = mh_i64ptr_find(con->streams, stream_id, 0);
	if (pos == mh_end(con->streams)) {
		stream = iproto_stream_new(msg->connection, msg->header.stream_id);
		struct mh_i64ptr_node_t node;
		node.key = stream_id;
		node.val = stream;
		pos = mh_i64ptr_put(con->streams, &node, NULL, NULL);
	}
	stream = (struct iproto_stream *)mh_i64ptr_node(con->streams, pos)->val;
	assert(stream != NULL);
	msg->stream = stream;
	if (stream->current == NULL) {
		stream->current = msg;
		return true;
	}
	con->iproto_thread->requests_in_stream_queue++;
	rmean_collect(con->iproto_thread->rmean, REQUESTS_IN_STREAM_QUEUE, 1);
	stailq_add_tail_entry(&stream->pending_requests, msg, in_stream);
	return false;
}

/**
 * Enqueue all requests which were read up. If a request limit is
 * reached - stop the connection input even if not the whole batch
 * is enqueued. Else try to read more feeding read event to the
 * event loop.
 * @param con Connection to enqueue in.
 * @param in Buffer to parse.
 *
 * @retval  0 Success.
 * @retval -1 Invalid MessagePack.
 */
static inline int
iproto_enqueue_batch(struct iproto_connection *con, struct ibuf *in)
{
	assert(rlist_empty(&con->in_stop_list));
	int n_requests = 0;
	const char *errmsg;
	while (con->parse_size != 0 && !con->is_in_replication) {
		if (iproto_check_msg_max(con->iproto_thread)) {
			iproto_connection_stop_msg_max_limit(con);
			cpipe_flush_input(&con->iproto_thread->tx_pipe);
			return 0;
		}
		const char *reqstart = in->wpos - con->parse_size;
		const char *pos = reqstart;
		/* Read request length. */
		if (mp_typeof(*pos) != MP_UINT) {
			errmsg = "packet length";
err_msgpack:
			cpipe_flush_input(&con->iproto_thread->tx_pipe);
			diag_set(ClientError, ER_INVALID_MSGPACK,
				 errmsg);
			return -1;
		}
		if (mp_check_uint(pos, in->wpos) >= 0)
			break;
		uint64_t len = mp_decode_uint(&pos);
		if (len > IPROTO_PACKET_SIZE_MAX) {
			errmsg = tt_sprintf("too big packet size in the "\
					    "header: %llu",
					    (unsigned long long) len);
			goto err_msgpack;
		}
		const char *reqend = pos + len;
		if (reqend > in->wpos)
			break;
		struct iproto_msg *msg = iproto_msg_new(con);
		msg->p_ibuf = con->p_ibuf;
		msg->reqstart = reqstart;
		msg->wpos = con->wpos;
		msg->len = reqend - reqstart; /* total request length */
		con->input_msg_count[msg->p_ibuf == &con->ibuf[1]]++;

		iproto_msg_prepare(msg, &pos, reqend);
		if (iproto_msg_start_processing_in_stream(msg)) {
			cpipe_push_input(&con->iproto_thread->tx_pipe, &msg->base);
			n_requests++;
		}

		/* Request is parsed */
		assert(reqend > reqstart);
		assert(con->parse_size >= (size_t) (reqend - reqstart));
		con->parse_size -= reqend - reqstart;
	}
	if (con->is_in_replication) {
		/**
		 * Don't mess with the file descriptor
		 * while join is running. ev_io_stop()
		 * also clears any pending events, which
		 * is good, since their invocation may
		 * re-start the watcher, ruining our
		 * efforts.
		 */
		ev_io_stop(con->loop, &con->output);
		ev_io_stop(con->loop, &con->input);
	} else if (n_requests != 1 || con->parse_size != 0) {
		/*
		 * Keep reading input, as long as the socket
		 * supplies data, but don't waste CPU on an extra
		 * read() if dealing with a blocking client, it
		 * has nothing in the socket for us.
		 *
		 * We look at the amount of enqueued requests
		 * and presence of a partial request in the
		 * input buffer as hints to distinguish
		 * blocking and non-blocking clients:
		 *
		 * For blocking clients, a request typically
		 * is fully read and enqueued.
		 * If there is unparsed data, or 0 queued
		 * requests, keep reading input, if only to avoid
		 * a deadlock on this connection.
		 */
		iproto_connection_feed_input(con);
	}
	cpipe_flush_input(&con->iproto_thread->tx_pipe);
	return 0;
}

/**
 * Enqueue connection's pending requests. Completely resurrect the
 * connection, if it has no more requests, and the limit still is
 * not reached.
 */
static void
iproto_connection_resume(struct iproto_connection *con)
{
	assert(! iproto_check_msg_max(con->iproto_thread));
	rlist_del(&con->in_stop_list);
	/*
	 * Enqueue_batch() stops the connection again, if the
	 * limit is reached again.
	 */
	if (iproto_enqueue_batch(con, con->p_ibuf) != 0) {
		struct error *e = box_error_last();
		error_log(e);
		iproto_write_error(&con->io, e, ::schema_version, 0);
		iproto_connection_close(con);
	}
}

/**
 * Resume as many connections as possible until a request limit is
 * reached. By design of iproto_enqueue_batch(), a paused
 * connection almost always has a pending request fully read up,
 * so resuming a connection will immediately enqueue the request
 * as an iproto message and exhaust the limit. Thus we aren't
 * really resuming all connections here: only as many as is
 * necessary to use up the limit.
 */
static void
iproto_resume(struct iproto_thread *iproto_thread)
{
	while (!iproto_check_msg_max(iproto_thread) &&
	       !rlist_empty(&iproto_thread->stopped_connections)) {
		/*
		 * Shift from list head to ensure strict FIFO
		 * (fairness) for resumed connections.
		 */
		struct iproto_connection *con =
			rlist_first_entry(&iproto_thread->stopped_connections,
					  struct iproto_connection,
					  in_stop_list);
		iproto_connection_resume(con);
	}
}

static void
iproto_connection_on_input(ev_loop *loop, struct ev_io *watcher,
			   int /* revents */)
{
	struct iproto_connection *con =
		(struct iproto_connection *) watcher->data;
	struct iostream *io = &con->io;
	assert(con->state == IPROTO_CONNECTION_ALIVE);
	assert(rlist_empty(&con->in_stop_list));
	assert(loop == con->loop);
	/*
	 * Throttle if there are too many pending requests,
	 * otherwise we might deplete the fiber pool in tx
	 * thread and deadlock.
	 */
	if (iproto_check_msg_max(con->iproto_thread)) {
		iproto_connection_stop_msg_max_limit(con);
		return;
	}

	/* Ensure we have sufficient space for the next round.  */
	struct ibuf *in = iproto_connection_input_buffer(con);
	if (in == NULL) {
		iproto_connection_stop_readahead_limit(con);
		return;
	}
	/* Read input. */
	ibuf_reserve(in, ibuf_unused(in));
	ssize_t nrd = iostream_read(io, in->wpos, ibuf_unused(in));
	if (nrd < 0) {                  /* Socket is not ready. */
		if (nrd == IOSTREAM_ERROR)
			goto error;
		int events = iostream_status_to_events(nrd);
		if (con->input.events != events) {
			ev_io_stop(loop, &con->input);
			ev_io_set(&con->input, con->io.fd, events);
		}
		ev_io_start(loop, &con->input);
		return;
	}
	if (nrd == 0) {                 /* EOF */
		iproto_connection_close(con);
		return;
	}
	/* Count statistics */
	rmean_collect(con->iproto_thread->rmean, IPROTO_RECEIVED, nrd);

	/* Update the read position and connection state. */
	ibuf_alloc(in, nrd);
	con->parse_size += nrd;
	/* Enqueue all requests which are fully read up. */
	if (iproto_enqueue_batch(con, in) != 0)
		goto error;
	return;
error:;
	struct error *e = diag_last_error(diag_get());
	assert(e != NULL);
	error_log(e);
	/* Best effort at sending the error message to the client. */
	iproto_write_error(io, e, ::schema_version, 0);
	iproto_connection_close(con);
}

/** writev() to the socket and handle the result. */
static int
iproto_flush(struct iproto_connection *con)
{
	struct obuf *obuf = con->wpos.obuf;
	struct obuf_svp obuf_end = obuf_create_svp(obuf);
	struct obuf_svp *begin = &con->wpos.svp;
	struct obuf_svp *end = &con->wend.svp;
	if (con->wend.obuf != obuf) {
		/*
		 * Flush the current buffer before
		 * advancing to the next one.
		 */
		if (begin->used == obuf_end.used) {
			obuf = con->wpos.obuf = con->wend.obuf;
			obuf_svp_reset(begin);
		} else {
			end = &obuf_end;
		}
	}
	if (begin->used == end->used) {
		/* Nothing to do. */
		return 1;
	}
	if (!con->can_write) {
		/* Receiving end was closed. Discard the output. */
		*begin = *end;
		return 0;
	}
	assert(begin->used < end->used);
	struct iovec iov[SMALL_OBUF_IOV_MAX+1];
	struct iovec *src = obuf->iov;
	int iovcnt = end->pos - begin->pos + 1;
	/*
	 * iov[i].iov_len may be concurrently modified in tx thread,
	 * but only for the last position.
	 */
	memcpy(iov, src + begin->pos, iovcnt * sizeof(struct iovec));
	sio_add_to_iov(iov, -begin->iov_len);
	/* *Overwrite* iov_len of the last pos as it may be garbage. */
	iov[iovcnt-1].iov_len = end->iov_len - begin->iov_len * (iovcnt == 1);

	ssize_t nwr = iostream_writev(&con->io, iov, iovcnt);
	if (nwr >= 0) {
		/* Count statistics */
		rmean_collect(con->iproto_thread->rmean, IPROTO_SENT, nwr);
		if (begin->used + nwr == end->used) {
			*begin = *end;
			return 0;
		}
		size_t offset = 0;
		int advance = 0;
		advance = sio_move_iov(iov, nwr, &offset);
		begin->used += nwr;             /* advance write position */
		begin->iov_len = advance == 0 ? begin->iov_len + offset: offset;
		begin->pos += advance;
		assert(begin->pos <= end->pos);
		return IOSTREAM_WANT_WRITE;
	} else if (nwr == IOSTREAM_ERROR) {
		/*
		 * Don't close the connection on write error. Log the error and
		 * don't write to the socket anymore. Continue processing
		 * requests as usual, because the client might have closed the
		 * socket, but still expect pending requests to complete.
		 */
		diag_log();
		con->can_write = false;
		*begin = *end;
		return 0;
	}
	return nwr;
}

static void
iproto_connection_on_output(ev_loop *loop, struct ev_io *watcher,
			    int /* revents */)
{
	struct iproto_connection *con = (struct iproto_connection *) watcher->data;
	assert(con->state == IPROTO_CONNECTION_ALIVE);
	int rc;
	while ((rc = iproto_flush(con)) <= 0) {
		if (rc != 0) {
			int events = iostream_status_to_events(rc);
			if (con->output.events != events) {
				ev_io_stop(loop, &con->output);
				ev_io_set(&con->output, con->io.fd, events);
			}
			ev_io_start(loop, &con->output);
			return;
		}
	}
	if (ev_is_active(&con->output))
		ev_io_stop(con->loop, &con->output);
	/*
	 * If the out channel isn't clogged, we can read more requests.
	 * Note, we trigger input even if we didn't write any responses
	 * (iproto_flush returned 1 right away). This is intentional:
	 * some requests don't have responses (IPROTO_WATCH).
	 */
	iproto_connection_feed_input(con);
}

static struct iproto_connection *
iproto_connection_new(struct iproto_thread *iproto_thread)
{
	struct iproto_connection *con = (struct iproto_connection *)
		xmempool_alloc(&iproto_thread->iproto_connection_pool);
	con->streams = mh_i64ptr_new();
	con->iproto_thread = iproto_thread;
	con->input.data = con->output.data = con;
	con->loop = loop();
	iostream_clear(&con->io);
	ev_io_init(&con->input, iproto_connection_on_input, -1, EV_NONE);
	ev_io_init(&con->output, iproto_connection_on_output, -1, EV_NONE);
	ibuf_create(&con->ibuf[0], cord_slab_cache(), iproto_readahead);
	ibuf_create(&con->ibuf[1], cord_slab_cache(), iproto_readahead);
	con->input_msg_count[0] = 0;
	con->input_msg_count[1] = 0;
	obuf_create(&con->obuf[0], &con->iproto_thread->net_slabc,
		    iproto_readahead);
	obuf_create(&con->obuf[1], &con->iproto_thread->net_slabc,
		    iproto_readahead);
	con->p_ibuf = &con->ibuf[0];
	con->tx.p_obuf = &con->obuf[0];
	iproto_wpos_create(&con->wpos, con->tx.p_obuf);
	iproto_wpos_create(&con->wend, con->tx.p_obuf);
	con->parse_size = 0;
	con->can_write = true;
	con->long_poll_count = 0;
	con->session = NULL;
	con->is_in_replication = false;
	con->is_drop_pending = false;
	con->is_established = false;
	rlist_create(&con->in_stop_list);
	rlist_create(&con->tx.inprogress);
	rlist_add_entry(&iproto_thread->connections, con, in_connections);
	/* It may be very awkward to allocate at close. */
	cmsg_init(&con->destroy_msg, con->iproto_thread->destroy_route);
	cmsg_init(&con->disconnect_msg, con->iproto_thread->disconnect_route);
	con->state = IPROTO_CONNECTION_ALIVE;
	con->tx.is_push_pending = false;
	con->tx.is_push_sent = false;
	rmean_collect(iproto_thread->rmean, IPROTO_CONNECTIONS, 1);
	return con;
}

/** Notify that connections drop is finished. */
static void
tx_process_drop_finished(struct cmsg *m)
{
	struct iproto_drop_finished *drop_finished =
					(struct iproto_drop_finished *)m;
	if (drop_finished->generation == drop_generation &&
	    --drop_pending_thread_count == 0)
		fiber_cond_signal(&drop_finished_cond);
}

/** Send message to TX thread to notify that connections drop is finished. */
static void
iproto_send_drop_finished(struct iproto_thread *iproto_thread,
			  unsigned generation)
{
	static const struct cmsg_hop drop_finished_route[1] =
					{{ tx_process_drop_finished, NULL }};

	cmsg_init(&iproto_thread->drop_finished_msg.base, drop_finished_route);
	iproto_thread->drop_finished_msg.generation = generation;
	cpipe_push(&iproto_thread->tx_pipe,
		   &iproto_thread->drop_finished_msg.base);
}

/** Recycle a connection. */
static inline void
iproto_connection_delete(struct iproto_connection *con)
{
	assert(iproto_connection_is_idle(con));
	assert(!iostream_is_initialized(&con->io));
	assert(con->session == NULL);
	assert(con->state == IPROTO_CONNECTION_DESTROYED);
	/*
	 * The output buffers must have been deleted
	 * in tx thread.
	 */
	ibuf_destroy(&con->ibuf[0]);
	ibuf_destroy(&con->ibuf[1]);
	assert(!obuf_is_initialized(&con->obuf[0]));
	assert(!obuf_is_initialized(&con->obuf[1]));

	assert(mh_size(con->streams) == 0);
	mh_i64ptr_delete(con->streams);
	rlist_del(&con->in_connections);
	if (con->is_drop_pending) {
		struct iproto_thread *iproto_thread = con->iproto_thread;

		assert(iproto_thread->drop_pending_connection_count > 0);
		if (--iproto_thread->drop_pending_connection_count == 0)
			iproto_send_drop_finished(iproto_thread,
						  con->drop_generation);
	}
	mempool_free(&con->iproto_thread->iproto_connection_pool, con);
}

/* }}} iproto_connection */

/* {{{ iproto_msg - methods and routes */

static void
tx_process_misc(struct cmsg *msg);

static void
tx_process_call(struct cmsg *msg);

static void
tx_process1(struct cmsg *msg);

static void
tx_process_select(struct cmsg *msg);

static void
tx_process_sql(struct cmsg *msg);

static void
tx_reply_error(struct iproto_msg *msg);

static void
tx_reply_iproto_error(struct cmsg *m);

static void
net_send_msg(struct cmsg *msg);

static void
net_send_error(struct cmsg *msg);

static void
tx_process_replication(struct cmsg *msg);

static void
net_end_join(struct cmsg *msg);

static void
net_end_subscribe(struct cmsg *msg);

/**
 * Decodes the IPROTO message and returns the route corresponding to the message
 * type.
 * Can be called from both IPROTO and TX threads.
 */
static int
iproto_msg_decode(struct iproto_msg *msg, struct cmsg_hop **route);

static void
iproto_msg_prepare(struct iproto_msg *msg, const char **pos, const char *reqend)
{
	uint64_t stream_id;
	uint32_t type;
	bool request_is_not_for_stream;
	bool request_is_only_for_stream;
	struct iproto_thread *iproto_thread = msg->connection->iproto_thread;
	mh_i32_t *handlers = iproto_thread->req_handlers;
	mh_int_t handler;
	struct cmsg_hop *route;
	int rc;

	if (xrow_header_decode(&msg->header, pos, reqend, true) != 0)
		goto error;
	assert(*pos == reqend);

	type = msg->header.type;
	stream_id = msg->header.stream_id;
	request_is_not_for_stream =
		((type > IPROTO_TYPE_STAT_MAX &&
		 type != IPROTO_PING) || type == IPROTO_AUTH);
	request_is_only_for_stream =
		(type == IPROTO_BEGIN ||
		 type == IPROTO_COMMIT ||
		 type == IPROTO_ROLLBACK);

	if (stream_id != 0 && request_is_not_for_stream) {
		diag_set(ClientError, ER_UNABLE_TO_PROCESS_IN_STREAM,
			 iproto_type_name(type));
		goto error;
	} else if (stream_id == 0 && request_is_only_for_stream) {
		diag_set(ClientError, ER_UNABLE_TO_PROCESS_OUT_OF_STREAM,
			 iproto_type_name(type));
		goto error;
	}

	msg->connection->is_in_replication = type == IPROTO_JOIN ||
					     type == IPROTO_FETCH_SNAPSHOT ||
					     type == IPROTO_REGISTER ||
					     type == IPROTO_SUBSCRIBE;

	handler = mh_i32_find(handlers, type, NULL);
	if (handler != mh_end(handlers)) {
		assert(!msg->connection->is_in_replication);
		cmsg_init(&msg->base, iproto_thread->override_route);
		return;
	}

	rc = iproto_msg_decode(msg, &route);
	if (rc == 0) {
		assert(route != NULL);
		cmsg_init(&msg->base, route);
		return;
	}
	if (route == NULL) {
		handler = mh_i32_find(handlers, IPROTO_UNKNOWN, NULL);
		if (handler != mh_end(handlers)) {
			cmsg_init(&msg->base, iproto_thread->override_route);
			return;
		}
		diag_set(ClientError, ER_UNKNOWN_REQUEST_TYPE, (uint32_t)type);
	}
error:
	/** Log and send the error. */
	diag_log();
	diag_create(&msg->diag);
	diag_move(&fiber()->diag, &msg->diag);
	cmsg_init(&msg->base, iproto_thread->error_route);
}

static int
iproto_msg_decode(struct iproto_msg *msg, struct cmsg_hop **route)
{
	uint32_t type = msg->header.type;
	struct iproto_thread *iproto_thread = msg->connection->iproto_thread;
	switch (type) {
	case IPROTO_SELECT:
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
	case IPROTO_UPDATE:
	case IPROTO_DELETE:
	case IPROTO_UPSERT:
		assert(type < sizeof(iproto_thread->dml_route) /
			      sizeof(*iproto_thread->dml_route));
		*route = iproto_thread->dml_route[type];
		if (xrow_decode_dml_iproto(&msg->header, &msg->dml,
					   dml_request_key_map(type)) != 0)
			return -1;
		/*
		 * In contrast to replication requests, for a client request
		 * the xrow header is set by WAL, which generates LSNs and sets
		 * replica id. Ignore the header received over network.
		 */
		msg->dml.header = NULL;
		return 0;
	case IPROTO_BEGIN:
		*route = iproto_thread->begin_route;
		if (xrow_decode_begin(&msg->header, &msg->begin) != 0)
			return -1;
		return 0;
	case IPROTO_COMMIT:
		*route = iproto_thread->commit_route;
		if (xrow_decode_commit(&msg->header, &msg->commit) != 0)
			return -1;
		return 0;
	case IPROTO_ROLLBACK:
		*route = iproto_thread->rollback_route;
		return 0;
	case IPROTO_CALL_16:
	case IPROTO_CALL:
	case IPROTO_EVAL:
		*route = iproto_thread->call_route;
		if (xrow_decode_call(&msg->header, &msg->call))
			return -1;
		return 0;
	case IPROTO_WATCH:
	case IPROTO_UNWATCH:
	case IPROTO_WATCH_ONCE:
		*route = iproto_thread->misc_route;
		ERROR_INJECT(ERRINJ_IPROTO_DISABLE_WATCH, {
			*route = NULL;
			return -1;
		});
		if (xrow_decode_watch(&msg->header, &msg->watch) != 0)
			return -1;
		return 0;
	case IPROTO_EXECUTE:
	case IPROTO_PREPARE:
		*route = iproto_thread->sql_route;
		if (xrow_decode_sql(&msg->header, &msg->sql) != 0)
			return -1;
		return 0;
	case IPROTO_PING:
		*route = iproto_thread->misc_route;
		return 0;
	case IPROTO_ID:
		*route = iproto_thread->misc_route;
		ERROR_INJECT(ERRINJ_IPROTO_DISABLE_ID, {
			*route = NULL;
			return -1;
		});
		if (xrow_decode_id(&msg->header, &msg->id) != 0)
			return -1;
		return 0;
	case IPROTO_JOIN:
	case IPROTO_FETCH_SNAPSHOT:
	case IPROTO_REGISTER:
		*route = iproto_thread->join_route;
		return 0;
	case IPROTO_SUBSCRIBE:
		*route = iproto_thread->subscribe_route;
		return 0;
	case IPROTO_VOTE_DEPRECATED:
	case IPROTO_VOTE:
		*route = iproto_thread->misc_route;
		return 0;
	case IPROTO_AUTH:
		*route = iproto_thread->misc_route;
		if (xrow_decode_auth(&msg->header, &msg->auth))
			return -1;
		return 0;
	default:
		*route = NULL;
		return -1;
	}
}

static void
tx_fiber_init(struct session *session, uint64_t sync)
{
	struct fiber *f = fiber();
	/*
	 * There should not be any not executed on_stop triggers
	 * from a previous request executed in that fiber.
	 */
	assert(rlist_empty(&f->on_stop));
	f->storage.net.sync = sync;
	/*
	 * We do not cleanup fiber keys at the end of each request.
	 * This does not lead to privilege escalation as long as
	 * fibers used to serve iproto requests never mingle with
	 * fibers used to serve background tasks without going
	 * through the purification of fiber_recycle(), which
	 * resets the fiber local storage. Fibers, used to run
	 * background tasks clean up their session in on_stop
	 * trigger as well.
	 */
	fiber_set_session(f, session);
	fiber_set_user(f, &session->credentials);
}

static void
tx_process_rollback_on_disconnect(struct cmsg *m)
{
	struct iproto_stream *stream =
		container_of(m, struct iproto_stream,
			     on_disconnect);

	if (stream->txn != NULL) {
		tx_fiber_init(stream->connection->session, 0);
		txn_attach(stream->txn);
		if (box_txn_rollback() != 0)
			panic("failed to rollback transaction on disconnect");
		stream->txn = NULL;
	}
}

static void
net_finish_rollback_on_disconnect(struct cmsg *m)
{
	struct iproto_stream *stream =
		container_of(m, struct iproto_stream,
			     on_disconnect);
	struct iproto_connection *con = stream->connection;

	struct mh_i64ptr_node_t node = { stream->id, NULL };
	mh_i64ptr_remove(con->streams, &node, 0);
	iproto_stream_delete(stream);
	assert(con->state != IPROTO_CONNECTION_ALIVE);
	if (con->state == IPROTO_CONNECTION_PENDING_DESTROY)
		iproto_connection_try_to_start_destroy(con);
}

/** Cancel all inprogress requests of the connection. */
static void
tx_process_cancel_inprogress(struct cmsg *m)
{
	struct iproto_connection *con =
		container_of(m, struct iproto_connection, cancel_msg);
	struct iproto_msg *msg;
	rlist_foreach_entry(msg, &con->tx.inprogress, in_inprogress)
		fiber_cancel(msg->fiber);
}

static void
tx_process_disconnect(struct cmsg *m)
{
	struct iproto_connection *con =
		container_of(m, struct iproto_connection, disconnect_msg);
	if (con->session != NULL) {
		session_close(con->session);
		/*
		 * When kharon returns, it should not go back - the socket is
		 * already dead anyway, and soon the connection itself will be
		 * deleted. More pushes can't come, because after the session is
		 * closed, its push() method is replaced with a stub.
		 */
		con->tx.is_push_pending = false;
		tx_fiber_init(con->session, 0);
		session_run_on_disconnect_triggers(con->session);
	}
}

static void
net_finish_disconnect(struct cmsg *m)
{
	struct iproto_connection *con =
		container_of(m, struct iproto_connection, disconnect_msg);
	iproto_connection_try_to_start_destroy(con);
}

/**
 * Destroy the session object, as well as output buffers of the
 * connection.
 */
static void
tx_process_destroy(struct cmsg *m)
{
	struct iproto_connection *con =
		container_of(m, struct iproto_connection, destroy_msg);
	assert(con->state == IPROTO_CONNECTION_DESTROYED);
	if (con->session) {
		session_delete(con->session);
		con->session = NULL; /* safety */
	}
	/*
	 * obuf is being destroyed in tx thread cause it is where
	 * it was allocated.
	 */
	obuf_destroy(&con->obuf[0]);
	obuf_destroy(&con->obuf[1]);
}

/**
 * Cleanup the net thread resources of a connection
 * and close the connection.
 */
static void
net_finish_destroy(struct cmsg *m)
{
	struct iproto_connection *con =
		container_of(m, struct iproto_connection, destroy_msg);
	/* Runs the trigger, which may yield. */
	iproto_connection_delete(con);
}

/** Account msg data in connection input buffer as processed. */
static void
iproto_msg_finish_input(iproto_msg *msg)
{
	struct iproto_connection *con = msg->connection;
	struct ibuf *ibuf = msg->p_ibuf;
	size_t *count = &con->input_msg_count[msg->p_ibuf == &con->ibuf[1]];
	/*
	 * Consume data from input buffer only when data of all messages
	 * is processed because messages process order and order of messages
	 * in the buffer may differ.
	 */
	assert(*count != 0);
	if (--(*count) == 0) {
		size_t processed = ibuf_used(ibuf);
		if (ibuf == con->p_ibuf) {
			assert(processed >= con->parse_size);
			processed -= con->parse_size;
		}
		ibuf_consume(ibuf, processed);
	}
}

static void
net_discard_input(struct cmsg *m)
{
	struct iproto_msg *msg = container_of(m, struct iproto_msg,
					      discard_input);
	struct iproto_connection *con = msg->connection;
	iproto_msg_finish_input(msg);
	msg->len = 0;
	con->long_poll_count++;
	if (con->state == IPROTO_CONNECTION_ALIVE)
		iproto_connection_feed_input(con);
}

static void
tx_discard_input(struct iproto_msg *msg)
{
	struct iproto_thread *iproto_thread = msg->connection->iproto_thread;
	static const struct cmsg_hop discard_input_route[] = {
		{ net_discard_input, NULL },
	};
	cmsg_init(&msg->discard_input, discard_input_route);
	cpipe_push(&iproto_thread->net_pipe, &msg->discard_input);
}

/**
 * The goal of this function is to maintain the state of
 * two rotating connection output buffers in tx thread.
 *
 * The function enforces the following rules:
 * - if both out buffers are empty, any one is selected;
 * - if one of the buffers is empty, and the other is
 *   not, the empty buffer is selected.
 * - if neither of the buffers are empty, the function
 *   does not rotate the buffer.
 *
 * @param con iproto connection.
 * @param wpos Last flushed write position, received from iproto
 *        thread.
 */
static void
tx_accept_wpos(struct iproto_connection *con, const struct iproto_wpos *wpos)
{
	struct obuf *prev = &con->obuf[con->tx.p_obuf == con->obuf];
	if (wpos->obuf == con->tx.p_obuf) {
		/*
		 * We got a message advancing the buffer which
		 * is being appended to. The previous buffer is
		 * guaranteed to have been flushed first, since
		 * buffers are never flushed out of order.
		 */
		if (obuf_size(prev) != 0)
			obuf_reset(prev);
	}
	if (obuf_size(con->tx.p_obuf) != 0 && obuf_size(prev) == 0) {
		/*
		 * If the current buffer is not empty, and the
		 * previous buffer has been flushed, rotate
		 * the current buffer.
		 */
		con->tx.p_obuf = prev;
	}
}

/**
 * Since the processing of requests within a transaction
 * for a stream can occur in different fibers, we store
 * a pointer to transaction in the stream structure.
 * Check if message belongs to stream and there is active
 * transaction for this stream. In case it is so, sets this
 * transaction for current fiber.
 */
static inline void
tx_prepare_transaction_for_request(struct iproto_msg *msg)
{
	if (msg->stream != NULL && msg->stream->txn != NULL) {
		txn_attach(msg->stream->txn);
		msg->stream->txn = NULL;
	}
	assert(!in_txn() || msg->stream != NULL);
}

static inline struct iproto_msg *
tx_accept_msg(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	if (msg->fiber != NULL)
		return msg;
	tx_accept_wpos(msg->connection, &msg->wpos);
	tx_fiber_init(msg->connection->session, msg->header.sync);
	tx_prepare_transaction_for_request(msg);
	msg->connection->iproto_thread->tx.requests_in_progress++;
	rlist_add_entry(&msg->connection->tx.inprogress, msg,
			in_inprogress);
	msg->fiber = fiber();
	rmean_collect(msg->connection->iproto_thread->tx.rmean,
		      REQUESTS_IN_PROGRESS, 1);
	flightrec_write_request(msg->reqstart, msg->len);
	return msg;
}

/**
 * Check if the watch request key is in the white list which doesn't need
 * additional checks.
 * The only allowed subscription is to "internal.ballot" event - the one used by
 * replication instead of IPROTO_VOTE on Tarantool 2.11+.
 */
static bool
check_watch_key(const char *key, uint32_t len)
{
	if (len != strlen(box_ballot_event_key))
		return false;
	return strncmp(key, box_ballot_event_key, len) == 0;
}

/**
 * Check if the tx thread may continue with processing an accepted message.
 * If something's wrong, returns -1 and sets diag, otherwise returns 0.
 */
static int
tx_check_msg(struct iproto_msg *msg)
{
	uint64_t new_schema_version = msg->header.schema_version;
	if (new_schema_version != 0 && new_schema_version != schema_version) {
		diag_set(ClientError, ER_WRONG_SCHEMA_VERSION,
			 new_schema_version, schema_version);
		return -1;
	}
	enum iproto_type type = (enum iproto_type)msg->header.type;
	if (type != IPROTO_AUTH && type != IPROTO_PING && type != IPROTO_ID &&
	    type != IPROTO_VOTE && type != IPROTO_VOTE_DEPRECATED &&
	    (type != IPROTO_WATCH ||
	     !check_watch_key(msg->watch.key, msg->watch.key_len)) &&
	    security_check_session() != 0)
		return -1;
	return 0;
}

static inline void
tx_end_msg(struct iproto_msg *msg, struct obuf_svp *svp)
{
	if (msg->stream != NULL) {
		assert(msg->stream->txn == NULL);
		msg->stream->txn = txn_detach();
	}
	msg->connection->iproto_thread->tx.requests_in_progress--;
	rlist_del(&msg->in_inprogress);
	msg->fiber = NULL;
	struct obuf *out = msg->connection->tx.p_obuf;
	if (msg->connection->tx.p_obuf->used != svp->used)
		/* Log response to the flight recorder. */
		flightrec_write_response(out, svp);
}

/**
 * Write error message to the output buffer and advance write position.
 */
static void
tx_reply_error(struct iproto_msg *msg)
{
	struct obuf *out = msg->connection->tx.p_obuf;
	iproto_reply_error(out, diag_last_error(&fiber()->diag),
			   msg->header.sync, ::schema_version);
	iproto_wpos_create(&msg->wpos, out);
}

/**
 * Write error from iproto thread to the output buffer and advance
 * write position.
 */
static void
tx_reply_iproto_error(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct obuf *out = msg->connection->tx.p_obuf;
	struct obuf_svp header = obuf_create_svp(out);
	iproto_reply_error(out, diag_last_error(&msg->diag),
			   msg->header.sync, ::schema_version);
	iproto_wpos_create(&msg->wpos, out);
	tx_end_msg(msg, &header);
}

/** Inject a short delay on tx request processing for testing. */
static inline void
tx_inject_delay(void)
{
	ERROR_INJECT(ERRINJ_IPROTO_TX_DELAY, {
		if (rand() % 100 < 10)
			fiber_sleep(0.001);
	});
}

static void
tx_process_begin(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct obuf *out;
	struct obuf_svp header;
	uint32_t txn_isolation = msg->begin.txn_isolation;
	bool is_sync = msg->begin.is_sync;

	if (tx_check_msg(msg) != 0)
		goto error;

	if (box_txn_begin() != 0)
		goto error;

	if (msg->begin.timeout != 0 &&
	    box_txn_set_timeout(msg->begin.timeout) != 0) {
		int rc = box_txn_rollback();
		assert(rc == 0);
		(void)rc;
		goto error;
	}
	if (box_txn_set_isolation(txn_isolation) != 0) {
		int rc = box_txn_rollback();
		assert(rc == 0);
		(void)rc;
		goto error;
	}
	if (is_sync)
		box_txn_make_sync();

	out = msg->connection->tx.p_obuf;
	header = obuf_create_svp(out);
	iproto_reply_ok(out, msg->header.sync, ::schema_version);
	iproto_wpos_create(&msg->wpos, out);
	tx_end_msg(msg, &header);
	return;
error:
	out = msg->connection->tx.p_obuf;
	header = obuf_create_svp(out);
	tx_reply_error(msg);
	tx_end_msg(msg, &header);
}

static void
tx_process_commit(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct obuf *out;
	struct obuf_svp header;
	bool is_sync = msg->commit.is_sync;

	if (tx_check_msg(msg) != 0)
		goto error;

	if (is_sync)
		box_txn_make_sync();

	if (box_txn_commit() != 0)
		goto error;

	out = msg->connection->tx.p_obuf;
	header = obuf_create_svp(out);
	iproto_reply_ok(out, msg->header.sync, ::schema_version);
	iproto_wpos_create(&msg->wpos, out);
	tx_end_msg(msg, &header);
	return;
error:
	out = msg->connection->tx.p_obuf;
	header = obuf_create_svp(out);
	tx_reply_error(msg);
	tx_end_msg(msg, &header);
}

static void
tx_process_rollback(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct obuf *out;
	struct obuf_svp header;

	if (tx_check_msg(msg) != 0)
		goto error;

	if (box_txn_rollback() != 0)
		goto error;

	out = msg->connection->tx.p_obuf;
	header = obuf_create_svp(out);
	iproto_reply_ok(out, msg->header.sync, ::schema_version);
	iproto_wpos_create(&msg->wpos, out);
	tx_end_msg(msg, &header);
	return;
error:
	out = msg->connection->tx.p_obuf;
	header = obuf_create_svp(out);
	tx_reply_error(msg);
	tx_end_msg(msg, &header);
}

/*
 * In case the request does not contain a space or identifier but contains a
 * corresponding name, tries to resolve the name.
 */
static int
tx_resolve_space_and_index_name(struct request *dml)
{
	struct space *space = NULL;
	if (dml->space_name != NULL) {
		space = space_by_name(dml->space_name, dml->space_name_len);
		if (space == NULL) {
			diag_set(ClientError, ER_NO_SUCH_SPACE,
				 tt_cstr(dml->space_name, dml->space_name_len));
			return -1;
		}
		dml->space_id = space->def->id;
	}
	if ((dml->type == IPROTO_SELECT || dml->type == IPROTO_UPDATE ||
	     dml->type == IPROTO_DELETE) && dml->index_name != NULL) {
		if (space == NULL)
			space = space_cache_find(dml->space_id);
		if (space == NULL)
			return -1;
		struct index *idx = space_index_by_name(space, dml->index_name,
							dml->index_name_len);
		if (idx == NULL) {
			diag_set(ClientError, ER_NO_SUCH_INDEX_NAME,
				 tt_cstr(dml->index_name, dml->index_name_len),
				 space->def->name);
			return -1;
		}
		dml->index_id = idx->dense_id;
	}
	return 0;
}

static void
tx_process1(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	bool box_tuple_as_ext =
		iproto_features_test(&msg->connection->session->meta.features,
				     IPROTO_FEATURE_DML_TUPLE_EXTENSION);
	struct tuple_format_map format_map;
	tuple_format_map_create_empty(&format_map);
	auto format_map_guard = make_scoped_guard([&format_map] {
		tuple_format_map_destroy(&format_map);
	});
	if (tx_check_msg(msg) != 0)
		goto error;

	struct tuple *tuple;
	struct obuf_svp svp;
	struct obuf *out;
	tx_inject_delay();
	if (tx_resolve_space_and_index_name(&msg->dml) != 0)
		goto error;
	if (box_process1(&msg->dml, &tuple) != 0)
		goto error;
	out = msg->connection->tx.p_obuf;
	iproto_prepare_select(out, &svp);
	if (tuple != NULL) {
		if (box_tuple_as_ext) {
			tuple_format_map_add_format(&format_map,
						    tuple->format_id);
			if (tuple_to_obuf_as_ext(tuple, out) != 0)
				goto error;
		} else if (tuple_to_obuf(tuple, out) != 0) {
			goto error;
		}
	}
	/*
	 * Even if there is no tuple, we still need to send an empty tuple
	 * format map.
	 */
	if (box_tuple_as_ext &&
	    tuple_format_map_to_iproto_obuf(&format_map, out) != 0)
		goto error;
	iproto_reply_select(out, &svp, msg->header.sync, ::schema_version,
			    tuple != 0, box_tuple_as_ext);
	iproto_wpos_create(&msg->wpos, out);
	tx_end_msg(msg, &svp);
	return;
error:
	out = msg->connection->tx.p_obuf;
	svp = obuf_create_svp(out);
	tx_reply_error(msg);
	tx_end_msg(msg, &svp);
}

static void
tx_process_select(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	bool box_tuple_as_ext =
		iproto_features_test(&msg->connection->session->meta.features,
				     IPROTO_FEATURE_DML_TUPLE_EXTENSION);
	struct obuf *out;
	struct obuf_svp svp;
	struct port port;

	struct mp_box_ctx ctx;
	struct mp_ctx *ctx_ref = NULL;
	if (box_tuple_as_ext) {
		mp_box_ctx_create(&ctx, NULL, NULL);
		ctx_ref = (struct mp_ctx *)&ctx;
	}
	auto ctx_guard = make_scoped_guard([ctx_ref] {
		mp_ctx_destroy(ctx_ref);
	});
	ctx_guard.is_active = box_tuple_as_ext;

	int count;
	int rc;
	const char *packed_pos, *packed_pos_end;
	bool reply_position;
	struct request *req = &msg->dml;
	uint32_t region_svp = region_used(&fiber()->gc);
	if (tx_check_msg(msg) != 0)
		goto error;

	tx_inject_delay();
	if (tx_resolve_space_and_index_name(&msg->dml) != 0)
		goto error;
	packed_pos = req->after_position;
	packed_pos_end = req->after_position_end;
	if (packed_pos != NULL) {
		mp_decode_strl(&packed_pos);
	} else if (req->after_tuple != NULL) {
		rc = box_index_tuple_position(req->space_id, req->index_id,
					      req->after_tuple,
					      req->after_tuple_end,
					      &packed_pos, &packed_pos_end);
		if (rc < 0)
			goto error;
	}
	rc = box_select(req->space_id, req->index_id,
			req->iterator, req->offset, req->limit,
			req->key, req->key_end, &packed_pos, &packed_pos_end,
			req->fetch_position, &port);
	if (rc < 0)
		goto error;

	out = msg->connection->tx.p_obuf;
	reply_position = req->fetch_position && packed_pos != NULL;
	if (reply_position)
		iproto_prepare_select_with_position(out, &svp);
	else
		iproto_prepare_select(out, &svp);
	/*
	 * SELECT output format has not changed since Tarantool 1.6
	 */
	count = port_dump_msgpack_16_with_ctx(&port, out, ctx_ref);
	port_destroy(&port);
	if (count < 0 || (box_tuple_as_ext &&
			  tuple_format_map_to_iproto_obuf(&ctx.tuple_format_map,
							  out) != 0)) {
		goto discard;
	}
	if (reply_position) {
		assert(packed_pos != NULL);
		iproto_reply_select_with_position(out, &svp, msg->header.sync,
						  ::schema_version, count,
						  packed_pos, packed_pos_end,
						  box_tuple_as_ext);
	} else {
		iproto_reply_select(out, &svp, msg->header.sync,
				    ::schema_version, count, box_tuple_as_ext);
	}
	region_truncate(&fiber()->gc, region_svp);
	iproto_wpos_create(&msg->wpos, out);
	tx_end_msg(msg, &svp);
	return;
discard:
	/* Discard the prepared select. */
	obuf_rollback_to_svp(out, &svp);
error:
	region_truncate(&fiber()->gc, region_svp);
	out = msg->connection->tx.p_obuf;
	svp = obuf_create_svp(out);
	tx_reply_error(msg);
	tx_end_msg(msg, &svp);
}

static int
tx_process_call_on_yield(struct trigger *trigger, void *event)
{
	(void)event;
	struct iproto_msg *msg = (struct iproto_msg *)trigger->data;
	TRASH(&msg->call);
	tx_discard_input(msg);
	trigger_clear(trigger);
	return 0;
}

static void
tx_process_call(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);

	bool box_tuple_as_ext =
		iproto_features_test(&msg->connection->session->meta.features,
				     IPROTO_FEATURE_CALL_RET_TUPLE_EXTENSION);
	struct mp_box_ctx ctx;
	struct mp_ctx *ctx_ref = NULL;
	if (box_tuple_as_ext) {
		mp_box_ctx_create(&ctx, NULL, NULL);
		ctx_ref = (struct mp_ctx *)&ctx;
	}
	auto ctx_guard = make_scoped_guard([ctx_ref] {
		mp_ctx_destroy(ctx_ref);
	});
	ctx_guard.is_active = box_tuple_as_ext;

	if (tx_check_msg(msg) != 0)
		goto error;

	/*
	 * CALL/EVAL should copy its arguments so we can discard
	 * input on yield to avoid stalling other connections by
	 * a long polling request.
	 */
	struct trigger fiber_on_yield;
	trigger_create(&fiber_on_yield, tx_process_call_on_yield, msg, NULL);
	trigger_add(&fiber()->on_yield, &fiber_on_yield);

	int rc;
	struct port port;

	switch (msg->header.type) {
	case IPROTO_CALL:
	case IPROTO_CALL_16:
		rc = box_process_call(&msg->call, &port);
		break;
	case IPROTO_EVAL:
		rc = box_process_eval(&msg->call, &port);
		break;
	default:
		unreachable();
	}

	trigger_clear(&fiber_on_yield);

	if (rc != 0)
		goto error;

	if (in_txn() != NULL && msg->header.stream_id == 0) {
		diag_set(ClientError, ER_FUNCTION_TX_ACTIVE);
		port_destroy(&port);
		goto error;
	}

	/*
	 * Add all elements returned by the function to iproto.
	 *
	 * To allow clients to understand a complex return from
	 * a procedure, we are compatible with SELECT protocol,
	 * and return the number of return values first, and
	 * then each return value as a tuple.
	 *
	 * (!) Please note that a save point for output buffer
	 * must be taken only after finishing executing of Lua
	 * function because Lua can yield and leave the
	 * buffer in inconsistent state (a parallel request
	 * from the same connection will break the protocol).
	 */

	int count;
	struct obuf *out;
	struct obuf_svp svp;

	out = msg->connection->tx.p_obuf;
	iproto_prepare_select(out, &svp);

	if (msg->header.type == IPROTO_CALL_16)
		count = port_dump_msgpack_16_with_ctx(&port, out, ctx_ref);
	else
		count = port_dump_msgpack_with_ctx(&port, out, ctx_ref);

	port_destroy(&port);
	if (count < 0 || (box_tuple_as_ext &&
			  tuple_format_map_to_iproto_obuf(&ctx.tuple_format_map,
							  out) != 0)) {
		obuf_rollback_to_svp(out, &svp);
		goto error;
	}
	iproto_reply_select(out, &svp, msg->header.sync,
			    ::schema_version, count, box_tuple_as_ext);
	iproto_wpos_create(&msg->wpos, out);
	tx_end_msg(msg, &svp);
	return;
error:
	out = msg->connection->tx.p_obuf;
	svp = obuf_create_svp(out);
	tx_reply_error(msg);
	tx_end_msg(msg, &svp);
}

static void
tx_process_id(struct iproto_connection *con, const struct id_request *id)
{
	extern bool box_tuple_extension;
	con->session->meta.features = id->features;
	if (!box_tuple_extension)
		iproto_features_clear(&con->session->meta.features,
				      IPROTO_FEATURE_CALL_RET_TUPLE_EXTENSION);
}

/** Callback passed to session_watch. */
static void
iproto_session_notify(struct session *session, uint64_t sync,
		      const char *key, size_t key_len,
		      const char *data, const char *data_end);

static void
tx_process_misc(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct iproto_connection *con = msg->connection;
	struct obuf *out = con->tx.p_obuf;
	struct obuf_svp header;
	assert(!(msg->header.type != IPROTO_PING && in_txn()));
	if (tx_check_msg(msg) != 0)
		goto error;

	struct ballot ballot;
	header = obuf_create_svp(out);
	switch (msg->header.type) {
	case IPROTO_AUTH:
		if (box_process_auth(&msg->auth, con->salt,
				     IPROTO_SALT_SIZE) != 0)
			goto error;
		iproto_reply_ok(out, msg->header.sync, ::schema_version);
		break;
	case IPROTO_PING:
		iproto_reply_ok(out, msg->header.sync, ::schema_version);
		break;
	case IPROTO_ID:
		tx_process_id(con, &msg->id);
		iproto_reply_id(out, box_auth_type, msg->header.sync,
				::schema_version);
		break;
	case IPROTO_VOTE_DEPRECATED:
		iproto_reply_vclock(out, &replicaset.vclock, msg->header.sync,
				    ::schema_version);
		break;
	case IPROTO_VOTE:
		box_process_vote(&ballot);
		iproto_reply_vote(out, &ballot, msg->header.sync,
				  ::schema_version);
		break;
	case IPROTO_WATCH:
		session_watch(con->session, msg->header.sync,
			      msg->watch.key, msg->watch.key_len,
			      iproto_session_notify);
		/* Sic: no reply. */
		break;
	case IPROTO_UNWATCH:
		session_unwatch(con->session, msg->watch.key,
				msg->watch.key_len);
		/* Sic: no reply. */
		break;
	case IPROTO_WATCH_ONCE: {
		const char *data, *data_end;
		data = box_watch_once(msg->watch.key, msg->watch.key_len,
				      &data_end);
		iproto_prepare_select(out, &header);
		xobuf_dup(out, data, data_end - data);
		iproto_reply_select(out, &header, msg->header.sync,
				    ::schema_version, data != NULL ? 1 : 0,
				    /*box_tuple_as_ext=*/false);
		break;
	}
	default:
		unreachable();
	}
	iproto_wpos_create(&msg->wpos, out);
	tx_end_msg(msg, &header);
	return;
error:
	header = obuf_create_svp(out);
	tx_reply_error(msg);
	tx_end_msg(msg, &header);
}

static void
tx_process_sql(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct obuf *out;
	struct port port;
	RegionGuard region_guard(&fiber()->gc);

	if (tx_check_msg(msg) != 0)
		goto error;
	assert(msg->header.type == IPROTO_EXECUTE ||
	       msg->header.type == IPROTO_PREPARE);
	tx_inject_delay();
	if (box_process_sql(&msg->sql, &port) != 0)
		goto error;
	/*
	 * Take an obuf only after execute(). Else the buffer can
	 * become out of date during yield.
	 */
	out = msg->connection->tx.p_obuf;
	struct obuf_svp header_svp;
	iproto_prepare_header(out, &header_svp, IPROTO_HEADER_LEN);
	if (port_dump_msgpack(&port, out) != 0) {
		port_destroy(&port);
		obuf_rollback_to_svp(out, &header_svp);
		goto error;
	}
	port_destroy(&port);
	iproto_reply_sql(out, &header_svp, msg->header.sync, schema_version);
	iproto_wpos_create(&msg->wpos, out);
	tx_end_msg(msg, &header_svp);
	return;
error:
	out = msg->connection->tx.p_obuf;
	header_svp = obuf_create_svp(out);
	tx_reply_error(msg);
	tx_end_msg(msg, &header_svp);
}

static void
tx_process_replication(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct iproto_connection *con = msg->connection;
	struct iostream *io = &con->io;
	assert(!in_txn());
	try {
		if (tx_check_msg(msg) != 0)
			diag_raise();
		switch (msg->header.type) {
		case IPROTO_JOIN:
			/*
			 * As soon as box_process_subscribe() returns
			 * the lambda in the beginning of the block
			 * will re-activate the watchers for us.
			 */
			box_process_join(io, &msg->header);
			break;
		case IPROTO_FETCH_SNAPSHOT:
			box_process_fetch_snapshot(io, &msg->header);
			break;
		case IPROTO_REGISTER:
			box_process_register(io, &msg->header);
			break;
		case IPROTO_SUBSCRIBE:
			/*
			 * Subscribe never returns - unless there
			 * is an error/exception. In that case
			 * the write watcher will be re-activated
			 * the same way as for JOIN.
			 */
			box_process_subscribe(io, &msg->header);
			break;
		default:
			unreachable();
		}
	} catch (SocketError *e) {
		/* don't write error response to prevent SIGPIPE */
	} catch (TimedOut *e) {
		 /*
		  * In case of a timeout the error could come after a partially
		  * written row. Do not push it on top.
		  */
	} catch (FiberIsCancelled *e) {
		/* Do not write into connection on connection drop. */
	} catch (Exception *e) {
		iproto_write_error(io, e, ::schema_version, msg->header.sync);
	}
	struct obuf_svp empty = obuf_create_svp(msg->connection->tx.p_obuf);
	tx_end_msg(msg, &empty);
}

/**
 * Allocates a new `iproto_req_handlers'. The memory is set to zero.
 */
static struct iproto_req_handlers *
iproto_req_handlers_new(void)
{
	struct iproto_req_handlers *handlers;
	handlers = (struct iproto_req_handlers *)xmalloc(sizeof(*handlers));
	memset(handlers, 0, sizeof(*handlers));
	return handlers;
}

/**
 * Destroys all handlers and deallocates the `handlers' structure.
 */
static void
iproto_req_handlers_delete(struct iproto_req_handlers *handlers)
{
	if (handlers->event_by_id != NULL)
		event_unref(handlers->event_by_id);
	if (handlers->event_by_name != NULL)
		event_unref(handlers->event_by_name);
	if (handlers->c.destroy != NULL)
		handlers->c.destroy(handlers->c.ctx);
	TRASH(handlers);
	free(handlers);
}

/**
 * Inserts `handlers' for the given `req_type' into the `tx_req_handlers' table.
 * There must be no previous entries in the table for this key.
 */
static void
mh_req_handlers_put(uint32_t req_type, struct iproto_req_handlers *handlers)
{
	struct mh_i32ptr_node_t old;
	struct mh_i32ptr_node_t *replaced = &old;
	struct mh_i32ptr_node_t node = {
		/* .key = */ req_type,
		/* .val = */ handlers,
	};
	mh_i32ptr_put(tx_req_handlers, &node, &replaced, NULL);
	assert(replaced == NULL);
}

/**
 * Returns a pointer to `iproto_req_handlers' for the given IPROTO request
 * `req_type', or NULL if there are no such handlers.
 */
static struct iproto_req_handlers *
mh_req_handlers_get(uint32_t req_type)
{
	mh_int_t k = mh_i32ptr_find(tx_req_handlers, req_type, NULL);
	if (k == mh_end(tx_req_handlers))
		return NULL;
	struct mh_i32ptr_node_t *node = mh_i32ptr_node(tx_req_handlers, k);
	return (struct iproto_req_handlers *)node->val;
}

/**
 * Deletes the handlers of IPROTO request `req_type' from the `tx_req_handlers'
 * hash table. The entry must be present in the table.
 */
static void
mh_req_handlers_del(uint32_t req_type)
{
	mh_int_t k = mh_i32ptr_find(tx_req_handlers, req_type, NULL);
	assert(k != mh_end(tx_req_handlers));
	mh_i32ptr_del(tx_req_handlers, k, NULL);
}

/**
 * Replaces an event in `handlers' by the new `event'. If `is_by_id', the
 * handler is set by request type id, otherwise it is set by request type name.
 */
static void
iproto_req_handlers_set_event(struct iproto_req_handlers *handlers,
			      struct event *event, bool is_by_id)
{
	assert(handlers != NULL);
	assert(event != NULL);

	if (is_by_id) {
		if (handlers->event_by_id == NULL) {
			event_ref(event);
			handlers->event_by_id = event;
		} else {
			assert(handlers->event_by_id == event);
		}
	} else {
		if (handlers->event_by_name == NULL) {
			event_ref(event);
			handlers->event_by_name = event;
		} else {
			assert(handlers->event_by_name == event);
		}
	}
}

/**
 * Deletes an event, which is set in `handlers' by request type id (if
 * `is_by_id'), or by request type name.
 */
static void
iproto_req_handlers_del_event(struct iproto_req_handlers *handlers,
			      bool is_by_id)
{
	assert(handlers != NULL);

	if (is_by_id) {
		event_unref(handlers->event_by_id);
		handlers->event_by_id = NULL;
	} else {
		event_unref(handlers->event_by_name);
		handlers->event_by_name = NULL;
	}
}

/**
 * Returns `true' if there is at least one handler in `handlers'.
 */
static bool
iproto_req_handler_is_set(struct iproto_req_handlers *handlers)
{
	if (handlers == NULL)
		return false;

	return handlers->event_by_id != NULL ||
	       handlers->event_by_name != NULL ||
	       handlers->c.cb != NULL;
}

/**
 * Returns `enum iproto_type' if `name' is a valid IPROTO type name or equals
 * "unknown". Otherwise, iproto_type_MAX is returned. The name is expected to
 * be in lowercase.
 */
static enum iproto_type
get_iproto_type_by_name(const char *name)
{
	for (uint32_t i = 0; i < iproto_type_MAX; i++) {
		const char *type_name = iproto_type_name_lower(i);
		if (type_name != NULL && strcmp(type_name, name) == 0)
			return (enum iproto_type)i;
	}
	if (strcmp(name, "unknown") == 0)
		return IPROTO_UNKNOWN;
	return iproto_type_MAX;
}

/**
 * Runs triggers registered for the `event'.
 * The given header and body the IPROTO packet are passed as trigger args.
 * Returns IPROTO_HANDLER_OK if some trigger successfully handled the request,
 * IPROTO_HANDLER_FALLBACK if no triggers handled the request, or
 * IPROTO_HANDLER_ERROR on failure.
 */
static enum iproto_handler_status
tx_run_override_triggers(struct event *event, const char *header,
			 const char *header_end, const char *body,
			 const char *body_end)
{
	enum iproto_handler_status rc = IPROTO_HANDLER_FALLBACK;
	const char *name = NULL;
	struct func_adapter *trigger = NULL;
	struct port args, ret_port;
	port_c_create(&args);
	port_c_add_mp_object(&args, header, header_end, &iproto_mp_ctx);
	port_c_add_mp_object(&args, body, body_end, &iproto_mp_ctx);

	struct event_trigger_iterator it;
	event_trigger_iterator_create(&it, event);
	while (event_trigger_iterator_next(&it, &trigger, &name)) {
		size_t region_svp = region_used(&fiber()->gc);
		if (func_adapter_call(trigger, &args, &ret_port) == 0) {
			const struct port_c_entry *ret =
				port_get_c_entries(&ret_port);
			if (ret != NULL && ret->type == PORT_C_ENTRY_BOOL) {
				if (ret->boolean)
					rc = IPROTO_HANDLER_OK;
			} else {
				diag_set(ClientError, ER_PROC_LUA,
					 "Invalid Lua IPROTO handler return "
					 "type: expected boolean");
				rc = IPROTO_HANDLER_ERROR;
			}
			port_destroy(&ret_port);
		} else {
			rc = IPROTO_HANDLER_ERROR;
		}
		region_truncate(&fiber()->gc, region_svp);
		if (rc != IPROTO_HANDLER_FALLBACK)
			break;
	}
	event_trigger_iterator_destroy(&it);
	return rc;
}

/**
 * Process a request using overridden handlers (or the unknown request handler
 * as a last resort).
 */
static void
tx_process_override(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	const char *header = msg->reqstart;
	mp_decode_uint(&header);

	const char *header_end = msg->reqstart + msg->len;
	const char *body = "\x80"; /* Empty MsgPack map encoding. */
	const char *body_end = body + 1;
	if (msg->header.bodycnt != 0) {
		assert(msg->header.bodycnt == 1);
		header_end -= msg->header.body[0].iov_len;
		body = (const char *)msg->header.body[0].iov_base;
		body_end = body + msg->header.body[0].iov_len;
	}

	/*
	 * If we took the `override_route', there must exist either request
	 * type-specific or unknown request type handler. Their availability
	 * is checked by the IPROTO thread.
	 */
	struct iproto_req_handlers *handlers;
	handlers = mh_req_handlers_get(msg->header.type);
	if (handlers == NULL)
		handlers = mh_req_handlers_get(IPROTO_UNKNOWN);
	assert(handlers != NULL);
	enum iproto_handler_status rc = IPROTO_HANDLER_FALLBACK;

	/*
	 * Run handlers from the event registry, set by request type id.
	 */
	if (handlers->event_by_id != NULL) {
		rc = tx_run_override_triggers(handlers->event_by_id, header,
					      header_end, body, body_end);
	}
	/*
	 * Run handlers from the event registry, set by request type name.
	 */
	if (rc == IPROTO_HANDLER_FALLBACK && handlers->event_by_name != NULL) {
		rc = tx_run_override_triggers(handlers->event_by_name, header,
					      header_end, body, body_end);
	}
	/*
	 * Run C handlers.
	 */
	if (rc == IPROTO_HANDLER_FALLBACK && handlers->c.cb != NULL) {
		rc = handlers->c.cb(header, header_end, body, body_end,
				    handlers->c.ctx);
	}

	struct cmsg_hop *route = NULL;
	switch (rc) {
	case IPROTO_HANDLER_OK: {
		struct obuf *out = msg->connection->tx.p_obuf;
		iproto_wpos_create(&msg->wpos, out);
		struct obuf_svp empty = obuf_create_svp(out);
		tx_end_msg(msg, &empty);
		return;
	}
	case IPROTO_HANDLER_FALLBACK: {
		int rc = iproto_msg_decode(msg, &route);
		assert(route != NULL);
		if (rc != 0)
			route = NULL;
		FALLTHROUGH;
	}
	case IPROTO_HANDLER_ERROR:
		break;
	default:
		unreachable();
	}
	if (route != NULL) {
		assert(m->hop[1].f == route[1].f);
		route->f(m);
		return;
	}
	struct obuf_svp svp = obuf_create_svp(msg->connection->tx.p_obuf);
	tx_reply_error(msg);
	tx_end_msg(msg, &svp);
}

static void
iproto_msg_finish_processing_in_stream(struct iproto_msg *msg)
{
	struct iproto_connection *con = msg->connection;
	struct iproto_stream *stream = msg->stream;

	if (stream == NULL)
		return;

	assert(stream->current == msg);
	stream->current = NULL;

	if (stailq_empty(&stream->pending_requests)) {
		/*
		 * If no more messages for the current stream
		 * and no transaction started, then delete it.
		 */
		if (stream->txn == NULL) {
			struct mh_i64ptr_node_t node = { stream->id, NULL };
			mh_i64ptr_remove(con->streams, &node, 0);
			iproto_stream_delete(stream);
		} else if (con->state != IPROTO_CONNECTION_ALIVE) {
			/*
			 * Here we are in case when connection was closed,
			 * there is no messages in stream queue, but there
			 * is some active transaction in stream.
			 * Send disconnect message to rollback this
			 * transaction.
			 */
			iproto_stream_rollback_on_disconnect(stream);
		}
	} else {
		/*
		 * If there are new messages for this stream
		 * then schedule their processing.
		 */
		stream->current =
			stailq_shift_entry(&stream->pending_requests,
					   struct iproto_msg,
					   in_stream);
		assert(stream->current != NULL);
		stream->current->wpos = con->wpos;
		con->iproto_thread->requests_in_stream_queue--;
		cpipe_push_input(&con->iproto_thread->tx_pipe,
				 &stream->current->base);
		cpipe_flush_input(&con->iproto_thread->tx_pipe);
	}
}

static void
net_send_msg(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;

	iproto_msg_finish_processing_in_stream(msg);
	if (msg->len != 0) {
		/* Discard request (see iproto_enqueue_batch()). */
		iproto_msg_finish_input(msg);
	} else {
		/* Already discarded by net_discard_input(). */
		assert(con->long_poll_count > 0);
		con->long_poll_count--;
	}
	con->wend = msg->wpos;

	if (con->state == IPROTO_CONNECTION_ALIVE) {
		iproto_connection_feed_output(con);
	} else if (iproto_connection_is_idle(con)) {
		iproto_connection_close(con);
	}
	iproto_msg_delete(msg);
}

/**
 * Complete sending an iproto error:
 * recycle the error object and flush output.
 */
static void
net_send_error(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	/* Recycle the exception. */
	diag_move(&msg->diag, &fiber()->diag);
	net_send_msg(m);
}

static void
net_end_join(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;
	struct ibuf *ibuf = msg->p_ibuf;

	iproto_msg_finish_input(msg);
	iproto_msg_delete(msg);

	assert(! ev_is_active(&con->input));
	con->is_in_replication = false;

	if (con->is_drop_pending) {
		iproto_connection_close(con);
		return;
	}
	/*
	 * Enqueue any messages if they are in the readahead
	 * queue. Will simply start input otherwise.
	 */
	if (iproto_enqueue_batch(con, ibuf) != 0)
		iproto_connection_close(con);
}

static void
net_end_subscribe(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;

	iproto_msg_finish_input(msg);
	iproto_msg_delete(msg);

	assert(! ev_is_active(&con->input));

	iproto_connection_close(con);
}

/**
 * Handshake a connection: invoke the on-connect trigger
 * and possibly authenticate. Try to send the client an error
 * upon a failure.
 */
static void
tx_process_connect(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;
	struct obuf *out = msg->connection->tx.p_obuf;
	if (msg->connect.session != NULL) {
		con->session = msg->connect.session;
		session_set_type(con->session, SESSION_TYPE_BINARY);
	} else {
		con->session = session_new(SESSION_TYPE_BINARY);
	}
	con->session->meta.connection = con;
	session_set_peer_addr(con->session, &msg->connect.addr,
			      msg->connect.addrlen);
	iproto_features_create(&con->session->meta.features);
	tx_fiber_init(con->session, 0);
	char *greeting = (char *)static_alloc(IPROTO_GREETING_SIZE);
	/* TODO: dirty read from tx thread */
	struct tt_uuid uuid = INSTANCE_UUID;
	random_bytes(con->salt, IPROTO_SALT_SIZE);
	greeting_encode(greeting, tarantool_version_id(), &uuid,
			con->salt, IPROTO_SALT_SIZE);
	xobuf_dup(out, greeting, IPROTO_GREETING_SIZE);
	if (session_run_on_connect_triggers(con->session) != 0)
		goto error;
	iproto_wpos_create(&msg->wpos, out);
	return;
error:
	tx_reply_error(msg);
	msg->close_connection = true;
}

/**
 * Send a response to connect to the client or close the
 * connection in case on_connect trigger failed.
 */
static void
net_send_greeting(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;
	if (con->is_drop_pending) {
		iproto_connection_close(con);
		iproto_msg_delete(msg);
		return;
	}
	if (msg->close_connection) {
		struct obuf *out = msg->wpos.obuf;
		int64_t nwr = iostream_writev(&con->io, out->iov,
					      obuf_iovcnt(out));
		if (nwr > 0) {
			/* Count statistics. */
			rmean_collect(con->iproto_thread->rmean,
				      IPROTO_SENT, nwr);
		} else if (nwr == IOSTREAM_ERROR) {
			diag_log();
		}
		assert(iproto_connection_is_idle(con));
		iproto_connection_close(con);
		iproto_msg_delete(msg);
		return;
	}
	con->is_established = true;
	con->wend = msg->wpos;
	/*
	 * Connect is synchronous, so no one could have been
	 * messing up with the connection while it was in
	 * progress.
	 */
	assert(con->state == IPROTO_CONNECTION_ALIVE);
	/* Handshake OK, start reading input. */
	iproto_connection_feed_output(con);
	iproto_msg_delete(msg);
}

/** }}} */

/**
 * Create a connection and start input.
 *
 * If session is NULL, a new session object will be created for the connection
 * in the TX thread.
 *
 * The function takes ownership of the passed IO stream and session.
 */
static void
iproto_thread_accept(struct iproto_thread *iproto_thread, struct iostream *io,
		     struct sockaddr *addr, socklen_t addrlen,
		     struct session *session)
{
	struct iproto_connection *con = iproto_connection_new(iproto_thread);
	struct iproto_msg *msg = iproto_msg_new(con);
	assert(addrlen <= sizeof(msg->connect.addrstorage));
	memcpy(&msg->connect.addrstorage, addr, addrlen);
	msg->connect.addrlen = addrlen;
	msg->connect.session = session;
	iostream_move(&con->io, io);
	cmsg_init(&msg->base, iproto_thread->connect_route);
	msg->p_ibuf = con->p_ibuf;
	msg->wpos = con->wpos;
	cpipe_push(&iproto_thread->tx_pipe, &msg->base);
}

static void
iproto_on_accept_cb(struct evio_service *service, struct iostream *io,
		    struct sockaddr *addr, socklen_t addrlen)
{
	struct iproto_thread *iproto_thread =
		(struct iproto_thread *)service->on_accept_param;
	iproto_thread_accept(iproto_thread, io, addr, addrlen,
			     /*session=*/NULL);
}

/**
 * The network io thread main function:
 * begin serving the message bus.
 */
static int
net_cord_f(va_list  ap)
{
	struct iproto_thread *iproto_thread =
		va_arg(ap, struct iproto_thread *);

	mempool_create(&iproto_thread->iproto_msg_pool, &cord()->slabc,
		       sizeof(struct iproto_msg));
	mempool_create(&iproto_thread->iproto_connection_pool, &cord()->slabc,
		       sizeof(struct iproto_connection));
	mempool_create(&iproto_thread->iproto_stream_pool, &cord()->slabc,
		       sizeof(struct iproto_stream));

	evio_service_create(loop(), &iproto_thread->binary, "binary",
			    iproto_on_accept_cb, iproto_thread);

	char endpoint_name[ENDPOINT_NAME_MAX];
	snprintf(endpoint_name, ENDPOINT_NAME_MAX, "net%u",
		 iproto_thread->id);

	struct cbus_endpoint endpoint;
	/* Create "net" endpoint. */
	cbus_endpoint_create(&endpoint, endpoint_name,
			     fiber_schedule_cb, fiber());
	/* Create a pipe to "tx" thread. */
	cpipe_create(&iproto_thread->tx_pipe, "tx");
	cpipe_set_max_input(&iproto_thread->tx_pipe, iproto_msg_max / 2);

	/* Process incomming messages. */
	cbus_loop(&endpoint);

	cbus_endpoint_destroy(&endpoint, cbus_process);
	cpipe_destroy(&iproto_thread->tx_pipe);
	evio_service_detach(&iproto_thread->binary);

	mempool_destroy(&iproto_thread->iproto_stream_pool);
	mempool_destroy(&iproto_thread->iproto_connection_pool);
	mempool_destroy(&iproto_thread->iproto_msg_pool);
	return 0;
}

int
iproto_session_fd(struct session *session)
{
	struct iproto_connection *con =
		(struct iproto_connection *) session->meta.connection;
	return con->io.fd;
}

int64_t
iproto_session_sync(struct session *session)
{
	(void) session;
	assert(session == fiber()->storage.session);
	return fiber()->storage.net.sync;
}

/** {{{ IPROTO_PUSH implementation. */

static void
iproto_process_push(struct cmsg *m)
{
	struct iproto_kharon *kharon = (struct iproto_kharon *) m;
	struct iproto_connection *con =
		container_of(kharon, struct iproto_connection, kharon);
	con->wend = kharon->wpos;
	kharon->wpos = con->wpos;
	if (con->state == IPROTO_CONNECTION_ALIVE)
		iproto_connection_feed_output(con);
}

/**
 * Send to iproto thread a notification about new pushes.
 * @param con iproto connection.
 */
static void
tx_begin_push(struct iproto_connection *con)
{
	assert(! con->tx.is_push_sent);
	cmsg_init(&con->kharon.base, con->iproto_thread->push_route);
	iproto_wpos_create(&con->kharon.wpos, con->tx.p_obuf);
	con->tx.is_push_pending = false;
	con->tx.is_push_sent = true;
	cpipe_push(&con->iproto_thread->net_pipe,
		   (struct cmsg *) &con->kharon);
}

static void
tx_end_push(struct cmsg *m)
{
	struct iproto_kharon *kharon = (struct iproto_kharon *) m;
	struct iproto_connection *con =
		container_of(kharon, struct iproto_connection, kharon);
	tx_accept_wpos(con, &kharon->wpos);
	con->tx.is_push_sent = false;
	if (con->tx.is_push_pending)
		tx_begin_push(con);
}

/**
 * Asynchronously send response message using Kharon facility.
 */
static void
tx_push(struct iproto_connection *con, struct obuf_svp *svp)
{
	flightrec_write_response(con->tx.p_obuf, svp);
	if (!con->tx.is_push_sent)
		tx_begin_push(con);
	else
		con->tx.is_push_pending = true;
}

/**
 * Push a message from @a port to a remote client.
 * @param session iproto session.
 * @param port Port with data to send.
 *
 * @retval -1 Memory error.
 * @retval  0 Success, a message is written to the output buffer.
 *            We don't wait here that the push has reached the
 *            client: the output buffer is flushed asynchronously.
 */
static int
iproto_session_push(struct session *session, struct port *port)
{
	struct iproto_connection *con =
		(struct iproto_connection *) session->meta.connection;
	struct obuf_svp svp;
	iproto_prepare_select(con->tx.p_obuf, &svp);
	if (port_dump_msgpack(port, con->tx.p_obuf) < 0) {
		obuf_rollback_to_svp(con->tx.p_obuf, &svp);
		return -1;
	}
	iproto_reply_chunk(con->tx.p_obuf, &svp, iproto_session_sync(session),
			   ::schema_version);
	tx_push(con, &svp);
	return 0;
}

/**
 * Sends a notification to a remote watcher when a key is updated.
 * Uses IPROTO_PUSH (kharon) infrastructure to signal the iproto thread
 * about new data.
 */
static void
iproto_session_notify(struct session *session, uint64_t sync,
		      const char *key, size_t key_len,
		      const char *data, const char *data_end)
{
	struct iproto_connection *con =
		(struct iproto_connection *)session->meta.connection;
	struct obuf *out = con->tx.p_obuf;
	struct obuf_svp svp = obuf_create_svp(out);
	iproto_send_event(out, sync, key, key_len, data, data_end);
	tx_push(con, &svp);
}

/** }}} */

/**
 * Stops accepting new connections on shutdown.
 */
static int
iproto_on_shutdown_f(void *arg)
{
	(void)arg;
	fiber_set_name(fiber_self(), "iproto.shutdown");
	iproto_is_shutting_down = true;
	struct iproto_cfg_msg cfg_msg;
	iproto_cfg_msg_create(&cfg_msg, IPROTO_CFG_SHUTDOWN);
	for (int i = 0; i < iproto_threads_count; i++)
		iproto_do_cfg(&iproto_threads[i], &cfg_msg);
	evio_service_stop(&tx_binary);
	return 0;
}

static inline void
iproto_thread_init_routes(struct iproto_thread *iproto_thread)
{
	iproto_thread->begin_route[0] =
		{ tx_process_begin, &iproto_thread->net_pipe };
	iproto_thread->begin_route[1] =
		{ net_send_msg, NULL };
	iproto_thread->commit_route[0] =
		{ tx_process_commit, &iproto_thread->net_pipe };
	iproto_thread->commit_route[1] =
		{ net_send_msg, NULL };
	iproto_thread->rollback_route[0] =
		{ tx_process_rollback, &iproto_thread->net_pipe };
	iproto_thread->rollback_route[1] =
		{ net_send_msg, NULL };
	iproto_thread->rollback_on_disconnect_route[0] =
		{ tx_process_rollback_on_disconnect,
		  &iproto_thread->net_pipe };
	iproto_thread->rollback_on_disconnect_route[1] =
		{ net_finish_rollback_on_disconnect, NULL };
	iproto_thread->destroy_route[0] =
		{ tx_process_destroy, &iproto_thread->net_pipe };
	iproto_thread->destroy_route[1] =
		{ net_finish_destroy, NULL };
	iproto_thread->disconnect_route[0] =
		{ tx_process_disconnect, &iproto_thread->net_pipe };
	iproto_thread->disconnect_route[1] =
		{ net_finish_disconnect, NULL };
	iproto_thread->misc_route[0] =
		{ tx_process_misc, &iproto_thread->net_pipe };
	iproto_thread->misc_route[1] = { net_send_msg, NULL };
	iproto_thread->call_route[0] =
		{ tx_process_call, &iproto_thread->net_pipe };
	iproto_thread->call_route[1] = { net_send_msg, NULL };
	iproto_thread->select_route[0] =
		{ tx_process_select, &iproto_thread->net_pipe };
	iproto_thread->select_route[1] = { net_send_msg, NULL };
	iproto_thread->process1_route[0] =
		{ tx_process1, &iproto_thread->net_pipe };
	iproto_thread->process1_route[1] = { net_send_msg, NULL };
	iproto_thread->sql_route[0] =
		{ tx_process_sql, &iproto_thread->net_pipe };
	iproto_thread->sql_route[1] = { net_send_msg, NULL };
	iproto_thread->join_route[0] =
		{ tx_process_replication, &iproto_thread->net_pipe };
	iproto_thread->join_route[1] = { net_end_join, NULL };
	iproto_thread->subscribe_route[0] =
		{ tx_process_replication, &iproto_thread->net_pipe };
	iproto_thread->subscribe_route[1] = { net_end_subscribe, NULL };
	iproto_thread->error_route[0] =
		{ tx_reply_iproto_error, &iproto_thread->net_pipe };
	iproto_thread->error_route[1] = { net_send_error, NULL };
	iproto_thread->push_route[0] =
		{ iproto_process_push, &iproto_thread->tx_pipe };
	iproto_thread->push_route[1] = { tx_end_push, NULL };
	/* IPROTO_OK */
	iproto_thread->dml_route[0] = NULL;
	/* IPROTO_SELECT */
	iproto_thread->dml_route[1] = iproto_thread->select_route;
	/* IPROTO_INSERT */
	iproto_thread->dml_route[2] = iproto_thread->process1_route;
	/* IPROTO_REPLACE */
	iproto_thread->dml_route[3] = iproto_thread->process1_route;
	/* IPROTO_UPDATE */
	iproto_thread->dml_route[4] = iproto_thread->process1_route;
	/* IPROTO_DELETE */
	iproto_thread->dml_route[5] = iproto_thread->process1_route;
	 /* IPROTO_CALL_16 */
	iproto_thread->dml_route[6] =  iproto_thread->call_route;
	/* IPROTO_AUTH */
	iproto_thread->dml_route[7] = iproto_thread->misc_route;
	/* IPROTO_EVAL */
	iproto_thread->dml_route[8] = iproto_thread->call_route;
	/* IPROTO_UPSERT */
	iproto_thread->dml_route[9] = iproto_thread->process1_route;
	/* IPROTO_CALL */
	iproto_thread->dml_route[10] = iproto_thread->call_route;
	/* IPROTO_EXECUTE */
	iproto_thread->dml_route[11] = iproto_thread->sql_route;
	/* IPROTO_NOP */
	iproto_thread->dml_route[12] = NULL;
	/* IPROTO_PREPARE */
	iproto_thread->dml_route[13] = iproto_thread->sql_route;
	iproto_thread->connect_route[0] =
		{ tx_process_connect, &iproto_thread->net_pipe };
	iproto_thread->connect_route[1] = { net_send_greeting, NULL };
	iproto_thread->override_route[0] =
		{ tx_process_override, &iproto_thread->net_pipe };
	iproto_thread->override_route[1] = { net_send_msg, NULL };
};

static inline void
iproto_thread_init(struct iproto_thread *iproto_thread)
{
	iproto_thread_init_routes(iproto_thread);
	iproto_thread->req_handlers = mh_i32_new();
	slab_cache_create(&iproto_thread->net_slabc, &runtime);
	/* Init statistics counter */
	iproto_thread->rmean = rmean_new(rmean_net_strings, RMEAN_NET_LAST);
	iproto_thread->tx.rmean = rmean_new(rmean_tx_strings, RMEAN_TX_LAST);
	rlist_create(&iproto_thread->stopped_connections);
	iproto_thread->tx.requests_in_progress = 0;
	iproto_thread->requests_in_stream_queue = 0;
	rlist_create(&iproto_thread->connections);
}

/**
 * True for IPROTO request types that can be overridden.
 */
static bool
is_iproto_override_supported(uint32_t req_type)
{
	switch (req_type) {
	case IPROTO_JOIN:
	case IPROTO_SUBSCRIBE:
	case IPROTO_FETCH_SNAPSHOT:
	case IPROTO_REGISTER:
		return false;
	default:
		return true;
	}
}

/**
 * If the `name' contains a valid name of an IPROTO overriding event, sets
 * `req_type' and returns True. If the name contains correct prefix, but
 * the request type is invalid, the error is logged with CRIT log level.
 * `is_by_id' set to True if the request is overridden by id, False if by name.
 */
static bool
get_iproto_type_from_event_name(const char *name, uint32_t *req_type,
				bool *is_by_id)
{
	const char *prefix = "box.iproto.override";
	const size_t prefix_len = strlen(prefix);
	if (strncmp(name, prefix, prefix_len) != 0)
		return false;

	const char *req_name = name + prefix_len;
	const char *req_name_err = req_name;
	if (*req_name == '.') {
		*is_by_id = false;
		/* Skip the dot. */
		req_name++;
		req_name_err = req_name;
		*req_type = get_iproto_type_by_name(req_name);
		if (*req_type == iproto_type_MAX)
			goto err_bad_type;
	} else if (*req_name == '[') {
		*is_by_id = true;
		/* Skip open bracket. */
		req_name++;
		if (!isdigit(*req_name) && *req_name != '-')
			goto err_bad_type;
		char *endptr;
		*req_type = strtol(req_name, &endptr, 10);
		if (endptr == req_name)
			goto err_bad_type;
		/*
		 * At least one digit is parsed.
		 * Check that the rest of the string equals "]".
		 */
		if (*endptr != ']' || endptr[1] != 0)
			goto err_bad_type;
	} else {
		/* Not in IPROTO override namespace. */
		return false;
	}

	if (!is_iproto_override_supported(*req_type)) {
		say_crit("IPROTO request handler overriding does not support "
			 "`%s' request type", iproto_type_name(*req_type));
		return false;
	}
	return true;

err_bad_type:
	say_crit("The event `%s' is in IPROTO override namespace, but `%s' is "
		 "not a valid request type", name, req_name_err);
	return false;
}

/**
 * Gets an arbitrary `event', checks its name, and adds it to `req_handlers' if
 * it is a valid IPROTO overriding event.
 * If the event name contains correct IPROTO overriding prefix, but the request
 * type is invalid, the error is logged with CRIT log level.
 */
static bool
iproto_override_event_init(struct event *event, void *arg)
{
	(void)arg;
	uint32_t type;
	bool is_by_id;
	if (!get_iproto_type_from_event_name(event->name, &type, &is_by_id))
		return true;

	struct iproto_req_handlers *handlers = mh_req_handlers_get(type);
	if (handlers == NULL) {
		handlers = iproto_req_handlers_new();
		mh_req_handlers_put(type, handlers);
	}
	iproto_req_handlers_set_event(handlers, event, is_by_id);

	for (int i = 0; i < iproto_threads_count; i++) {
		struct iproto_thread *iproto_thread = &iproto_threads[i];
		mh_i32_put(iproto_thread->req_handlers, &type, NULL, NULL);
	}
	return true;
}

/**
 * Notifies IPROTO threads that a new request handler has been set.
 */
static void
iproto_cfg_override(uint32_t req_type, bool is_set);

/**
 * Calls iproto_cfg_override() and destroys the handlers when necessary.
 */
static void
iproto_override_finish(struct iproto_req_handlers *handlers, uint32_t req_type,
		       bool old_is_set)
{
	bool new_is_set = iproto_req_handler_is_set(handlers);
	if (new_is_set != old_is_set)
		iproto_cfg_override(req_type, new_is_set);

	if (!new_is_set && handlers != NULL) {
		mh_req_handlers_del(req_type);
		iproto_req_handlers_delete(handlers);
	}
}

/**
 * Trigger which is fired on any change in the event registry.
 */
static int
trigger_on_change_iproto_notify(struct trigger *trigger, void *arg)
{
	(void)trigger;
	uint32_t type;
	bool is_by_id;
	struct event *event = (struct event *)arg;
	if (!get_iproto_type_from_event_name(event->name, &type, &is_by_id))
		return 0;

	struct iproto_req_handlers *handlers;
	handlers = mh_req_handlers_get(type);
	bool is_set = iproto_req_handler_is_set(handlers);

	if (event_has_triggers(event)) {
		if (handlers == NULL) {
			handlers = iproto_req_handlers_new();
			mh_req_handlers_put(type, handlers);
		}
		iproto_req_handlers_set_event(handlers, event, is_by_id);
	} else {
		iproto_req_handlers_del_event(handlers, is_by_id);
	}

	iproto_override_finish(handlers, type, is_set);
	return 0;
}

TRIGGER(trigger_on_change, trigger_on_change_iproto_notify);

/** Initialize the iproto subsystem and start network io thread */
void
iproto_init(int threads_count)
{
	iproto_features_init();

	iproto_threads_count = 0;
	struct session_vtab iproto_session_vtab = {
		/* .push = */ iproto_session_push,
		/* .fd = */ iproto_session_fd,
		/* .sync = */ iproto_session_sync,
	};
	/*
	 * We use this tx_binary only for bind, not for listen, so
	 * we don't need any accept functions.
	 */
	evio_service_create(loop(), &tx_binary, "tx_binary", NULL, NULL);
	iproto_threads = (struct iproto_thread *)
		xcalloc(threads_count, sizeof(struct iproto_thread));
	fiber_cond_create(&drop_finished_cond);

	for (int i = 0; i < threads_count; i++, iproto_threads_count++) {
		struct iproto_thread *iproto_thread = &iproto_threads[i];
		iproto_thread->id = i;
		iproto_thread_init(iproto_thread);
	}

	/*
	 * Go through all events with triggers, and initialize overridden
	 * request handlers that were registered before IPROTO initialization.
	 */
	tx_req_handlers = mh_i32ptr_new();
	event_foreach(iproto_override_event_init, NULL);

	for (int i = 0; i < threads_count; i++) {
		struct iproto_thread *iproto_thread = &iproto_threads[i];
		if (cord_costart(&iproto_thread->net_cord, "iproto",
				 net_cord_f, iproto_thread))
			panic("failed to start iproto thread");
		/* Create a pipe to "net" thread. */
		char endpoint_name[ENDPOINT_NAME_MAX];
		snprintf(endpoint_name, ENDPOINT_NAME_MAX, "net%u",
			 iproto_thread->id);
		cpipe_create(&iproto_thread->net_pipe, endpoint_name);
		cpipe_set_max_input(&iproto_thread->net_pipe,
				    iproto_msg_max / 2);
	}

	session_vtab_registry[SESSION_TYPE_BINARY] = iproto_session_vtab;

	event_on_change(&trigger_on_change);
	if (box_on_shutdown(NULL, iproto_on_shutdown_f, NULL) != 0)
		panic("failed to set iproto shutdown trigger");
}

static void
iproto_fill_stat(struct iproto_thread *iproto_thread,
		 struct iproto_cfg_msg *cfg_msg)
{
	assert(cfg_msg->stats != NULL);
	cfg_msg->stats->mem_used =
		slab_cache_used(&iproto_thread->net_cord.slabc) +
		slab_cache_used(&iproto_thread->net_slabc);
	cfg_msg->stats->connections =
		mempool_count(&iproto_thread->iproto_connection_pool);
	cfg_msg->stats->streams =
		mempool_count(&iproto_thread->iproto_stream_pool);
	cfg_msg->stats->requests =
		mempool_count(&iproto_thread->iproto_msg_pool);
	cfg_msg->stats->requests_in_stream_queue =
		iproto_thread->requests_in_stream_queue;
}

static int
iproto_do_cfg_f(struct cbus_call_msg *m)
{
	struct iproto_cfg_msg *cfg_msg = (struct iproto_cfg_msg *) m;
	struct iproto_thread *iproto_thread = cfg_msg->iproto_thread;
	struct mh_i32_t *req_handlers = iproto_thread->req_handlers;
	struct evio_service *binary = &iproto_thread->binary;
	switch (cfg_msg->op) {
	case IPROTO_CFG_MSG_MAX: {
		cpipe_set_max_input(&iproto_thread->tx_pipe,
				    cfg_msg->iproto_msg_max / 2);
		int old = iproto_msg_max;
		iproto_msg_max = cfg_msg->iproto_msg_max;
		if (old < iproto_msg_max)
			iproto_resume(iproto_thread);
		break;
	}
	case IPROTO_CFG_START:
		if (iproto_thread->is_shutting_down)
			break;
		evio_service_attach(binary, &tx_binary);
		break;
	case IPROTO_CFG_SHUTDOWN:
		iproto_thread->is_shutting_down = true;
		FALLTHROUGH;
	case IPROTO_CFG_STOP:
		evio_service_detach(binary);
		break;
	case IPROTO_CFG_RESTART:
		evio_service_detach(binary);
		evio_service_attach(binary, &tx_binary);
		break;
	case IPROTO_CFG_STAT:
		iproto_fill_stat(iproto_thread, cfg_msg);
		break;
	case IPROTO_CFG_OVERRIDE:
		if (cfg_msg->override.is_set) {
			uint32_t old;
			uint32_t *replaced = &old;
			mh_i32_put(req_handlers, &cfg_msg->override.req_type,
				   &replaced, NULL);
			assert(replaced == NULL);
		} else {
			mh_int_t k = mh_i32_find(req_handlers,
						 cfg_msg->override.req_type,
						 NULL);
			assert(k != mh_end(req_handlers));
			mh_i32_del(req_handlers, k, NULL);
		}
		break;
	case IPROTO_CFG_SESSION_NEW: {
		struct iostream *io = &cfg_msg->session_new.io;
		struct session *session = cfg_msg->session_new.session;
		struct sockaddr_storage addrstorage;
		struct sockaddr *addr = (struct sockaddr *)&addrstorage;
		socklen_t addrlen = sizeof(addrstorage);
		if (sio_getpeername(io->fd, addr, &addrlen) != 0)
			addrlen = 0;
		iproto_thread_accept(iproto_thread, io, addr, addrlen, session);
		break;
	}
	case IPROTO_CFG_DROP_CONNECTIONS: {
		struct iproto_connection *con;
		static const struct cmsg_hop cancel_route[1] =
				{{ tx_process_cancel_inprogress, NULL }};
		iproto_thread->drop_pending_connection_count = 0;
		rlist_foreach_entry(con, &iproto_thread->connections,
				    in_connections) {
			/*
			 * Replication IO is done outside iproto so we
			 * cannot close them as usual. Anyway we cancel
			 * replication fibers as well and close connection
			 * after replication is breaked.
			 *
			 * Do not close connection that is not yet
			 * established. Otherwise session
			 * on_connect/on_disconnect callbacks may be
			 * executed in reverse order in case of yields
			 * in on_connect callbacks.
			 */
			if (!con->is_in_replication &&
			    con->state == IPROTO_CONNECTION_ALIVE &&
			    con->is_established)
				iproto_connection_close(con);
			/*
			 * Do not wait deletion of connection that called
			 * iproto_drop_connections to avoid deadlock.
			 */
			if (con != cfg_msg->drop_connections.owner) {
				con->is_drop_pending = true;
				con->drop_generation =
					cfg_msg->drop_connections.generation;
				iproto_thread->drop_pending_connection_count++;
			}
			if (con->state != IPROTO_CONNECTION_DESTROYED) {
				cmsg_init(&con->cancel_msg, cancel_route);
				cpipe_push(&iproto_thread->tx_pipe,
					   &con->cancel_msg);
			}
		}
		if (iproto_thread->drop_pending_connection_count == 0)
			iproto_send_drop_finished(
				iproto_thread,
				cfg_msg->drop_connections.generation);
		break;
	}
	default:
		unreachable();
	}
	return 0;
}

static void
iproto_do_cfg(struct iproto_thread *iproto_thread, struct iproto_cfg_msg *msg)
{
	msg->iproto_thread = iproto_thread;
	int rc = cbus_call(&iproto_thread->net_pipe, &iproto_thread->tx_pipe,
			   msg, iproto_do_cfg_f);
	assert(rc == 0);
	(void)rc;
}

static int
iproto_do_cfg_async_free_f(struct cbus_call_msg *m)
{
	free(m);
	return 0;
}

/**
 * Sends a configuration message to an IPROTO thread without waiting for
 * completion.
 *
 * The message must be allocated with malloc.
 */
static void
iproto_do_cfg_async(struct iproto_thread *iproto_thread,
		    struct iproto_cfg_msg *msg)
{
	msg->iproto_thread = iproto_thread;
	cbus_call_async(&iproto_thread->net_pipe, &iproto_thread->tx_pipe,
			msg, iproto_do_cfg_f, iproto_do_cfg_async_free_f);
}

/** Send IPROTO_CFG_STOP to all threads. */
static void
iproto_send_stop_msg(void)
{
	struct iproto_cfg_msg cfg_msg;
	iproto_cfg_msg_create(&cfg_msg, IPROTO_CFG_STOP);
	for (int i = 0; i < iproto_threads_count; i++)
		iproto_do_cfg(&iproto_threads[i], &cfg_msg);
}

/** Send IPROTO_CFG_START to all threads. */
static void
iproto_send_start_msg(void)
{
	struct iproto_cfg_msg cfg_msg;
	iproto_cfg_msg_create(&cfg_msg, IPROTO_CFG_START);
	for (int i = 0; i < iproto_threads_count; i++)
		iproto_do_cfg(&iproto_threads[i], &cfg_msg);
}

int
iproto_drop_connections(double timeout)
{
	static struct latch latch = LATCH_INITIALIZER(latch);
	latch_lock(&latch);
	struct iproto_connection *owner = NULL;
	struct session *session = fiber_get_session(fiber());
	if (session != NULL && session->type == SESSION_TYPE_BINARY)
		owner = (struct iproto_connection *)session->meta.connection;
	drop_generation++;
	drop_pending_thread_count = iproto_threads_count;
	for (int i = 0; i < iproto_threads_count; i++) {
		struct iproto_cfg_msg *cfg_msg =
			(struct iproto_cfg_msg *)xmalloc(sizeof(*cfg_msg));
		iproto_cfg_msg_create(cfg_msg, IPROTO_CFG_DROP_CONNECTIONS);
		cfg_msg->drop_connections.owner = owner;
		cfg_msg->drop_connections.generation = drop_generation;
		iproto_do_cfg_async(&iproto_threads[i], cfg_msg);
	}

	double deadline = ev_monotonic_now(loop()) + timeout;
	while (drop_pending_thread_count != 0) {
		if (fiber_cond_wait_deadline(&drop_finished_cond,
					     deadline) != 0)
			break;
	}
	latch_unlock(&latch);
	return drop_pending_thread_count == 0 ? 0 : -1;
}

/** Send IPROTO_CFG_RESTART to all threads. */
static void
iproto_send_restart_msg(void)
{
	struct iproto_cfg_msg cfg_msg;
	iproto_cfg_msg_create(&cfg_msg, IPROTO_CFG_RESTART);
	for (int i = 0; i < iproto_threads_count; i++)
		iproto_do_cfg(&iproto_threads[i], &cfg_msg);
}

int
iproto_listen(const struct uri_set *uri_set)
{
	/*
	 * No need to rebind IPROTO ports in case the configuration is
	 * the same. However, we should still reload the URIs because
	 * a URI parameter may store a path to a file (for example,
	 * an SSL certificate), which could change.
	 */
	if (uri_set_is_equal(uri_set, &iproto_uris)) {
		if (evio_service_reload_uris(&tx_binary) != 0)
			return -1;
		iproto_send_restart_msg();
		return 0;
	}
	/*
	 * Note that we set iproto_uris before trying to bind so even if
	 * we fail, iproto_uris will still contain the new configuration.
	 * It's okay because box.cfg.listen is reverted on failure at
	 * the box.cfg level.
	 */
	uri_set_destroy(&iproto_uris);
	uri_set_copy(&iproto_uris, uri_set);
	iproto_send_stop_msg();
	evio_service_stop(&tx_binary);
	struct errinj *inj = errinj(ERRINJ_IPROTO_CFG_LISTEN, ERRINJ_INT);
	if (inj != NULL && inj->iparam > 0) {
		inj->iparam--;
		diag_set(ClientError, ER_INJECTION, "iproto listen");
		return -1;
	}
	/*
	 * Please note, we bind sockets in main thread, and then
	 * listen these sockets in all iproto threads! With this
	 * implementation, we rely on the Linux kernel to distribute
	 * incoming connections across iproto threads.
	 */
	if (evio_service_start(&tx_binary, uri_set) != 0)
		return -1;
	iproto_send_start_msg();
	return 0;
}

static void
iproto_stats_add(struct iproto_stats *total_stats,
		 struct iproto_stats *thread_stats)
{
	total_stats->mem_used += thread_stats->mem_used;
	total_stats->connections += thread_stats->connections;
	total_stats->streams += thread_stats->streams;
	total_stats->requests += thread_stats->requests;
	total_stats->requests_in_stream_queue +=
		thread_stats->requests_in_stream_queue;
	total_stats->requests_in_progress +=
		thread_stats->requests_in_progress;
}

void
iproto_stats_get(struct iproto_stats *stats)
{
	struct iproto_stats thread_stats;
	memset(stats, 0, sizeof(iproto_stats));
	for (int i = 0; i < iproto_threads_count; i++) {
		iproto_thread_stats_get(&thread_stats, i);
		iproto_stats_add(stats, &thread_stats);
	}
}

void
iproto_thread_stats_get(struct iproto_stats *stats, int thread_id)
{
	memset(stats, 0, sizeof(iproto_stats));
	struct iproto_cfg_msg cfg_msg;
	iproto_cfg_msg_create(&cfg_msg, IPROTO_CFG_STAT);
	assert(thread_id >= 0 && thread_id < iproto_threads_count);
	cfg_msg.stats = stats;
	iproto_do_cfg(&iproto_threads[thread_id], &cfg_msg);
	stats->requests_in_progress =
		iproto_threads[thread_id].tx.requests_in_progress;
}

void
iproto_reset_stat(void)
{
	for (int i = 0; i < iproto_threads_count; i++) {
		rmean_cleanup(iproto_threads[i].rmean);
		rmean_cleanup(iproto_threads[i].tx.rmean);
	}
}

int
iproto_set_msg_max(int new_iproto_msg_max)
{
	if (new_iproto_msg_max < IPROTO_MSG_MAX_MIN) {
		diag_set(ClientError, ER_CFG, "net_msg_max",
			 tt_sprintf("minimal value is %d", IPROTO_MSG_MAX_MIN));
		return -1;
	}
	struct iproto_cfg_msg cfg_msg;
	iproto_cfg_msg_create(&cfg_msg, IPROTO_CFG_MSG_MAX);
	cfg_msg.iproto_msg_max = new_iproto_msg_max;
	for (int i = 0; i < iproto_threads_count; i++) {
		iproto_do_cfg(&iproto_threads[i], &cfg_msg);
		cpipe_set_max_input(&iproto_threads[i].net_pipe,
				    new_iproto_msg_max / 2);
	}
	return 0;
}

int
iproto_session_new(struct iostream *io, struct user *user, uint64_t *sid)
{
	assert(iostream_is_initialized(io));
	if (iproto_is_shutting_down) {
		diag_set(ClientError, ER_SHUTDOWN);
		return -1;
	}
	struct session *session = session_new(SESSION_TYPE_BACKGROUND);
	if (user != NULL)
		credentials_reset(&session->credentials, user);
	struct iproto_cfg_msg *cfg_msg =
		(struct iproto_cfg_msg *)xmalloc(sizeof(*cfg_msg));
	iproto_cfg_msg_create(cfg_msg, IPROTO_CFG_SESSION_NEW);
	iostream_move(&cfg_msg->session_new.io, io);
	cfg_msg->session_new.session = session;
	static int thread = 0;
	thread = (thread + 1) % iproto_threads_count;
	iproto_do_cfg_async(&iproto_threads[thread], cfg_msg);
	*sid = session->id;
	return 0;
}

static void
iproto_cfg_override(uint32_t req_type, bool is_set)
{
	struct iproto_cfg_msg cfg_msg;
	iproto_cfg_msg_create(&cfg_msg, IPROTO_CFG_OVERRIDE);
	cfg_msg.override.req_type = req_type;
	cfg_msg.override.is_set = is_set;
	for (int i = 0; i < iproto_threads_count; ++i)
		iproto_do_cfg(&iproto_threads[i], &cfg_msg);
}

int
iproto_session_send(struct session *session,
		    const char *header, const char *header_end,
		    const char *body, const char *body_end)
{
	assert(session->type == SESSION_TYPE_BINARY);
	struct iproto_connection *con =
		(struct iproto_connection *)session->meta.connection;
	if (con->state != IPROTO_CONNECTION_ALIVE) {
		diag_set(ClientError, ER_SESSION_CLOSED);
		return -1;
	}

	struct obuf *out = con->tx.p_obuf;
	struct obuf_svp svp = obuf_create_svp(out);
	ptrdiff_t header_size = header_end - header;
	ptrdiff_t body_size = body_end - body;
	ptrdiff_t packet_size = 5 + header_size + body_size;
	char *p = (char *)xobuf_alloc(out, packet_size);
	*(p++) = INT8_C(0xce);
	p = mp_store_u32(p, packet_size - 5);
	memcpy(p, header, header_size);
	p += header_size;
	memcpy(p, body, body_size);
	tx_push(con, &svp);
	/*
	 * The control yield is solely for enforcing the fact this function
	 * yields — in the future we may implement back pressure based on this.
	 */
	fiber_sleep(0);
	return 0;
}

int
iproto_shutdown(double timeout)
{
	assert(iproto_is_shutting_down);
	return iproto_drop_connections(timeout);
}

void
iproto_free(void)
{
	for (int i = 0; i < iproto_threads_count; i++) {
		cbus_stop_loop(&iproto_threads[i].net_pipe);
		cpipe_destroy(&iproto_threads[i].net_pipe);
		if (cord_join(&iproto_threads[i].net_cord) != 0)
			panic_syserror("iproto cord join failed");
		mh_i32_delete(iproto_threads[i].req_handlers);
		/*
		 * Close socket descriptor to prevent hot standby instance
		 * failing to bind in case it tries to bind before socket
		 * is closed by OS.
		 */
		evio_service_detach(&iproto_threads[i].binary);
		rmean_delete(iproto_threads[i].rmean);
		rmean_delete(iproto_threads[i].tx.rmean);
		slab_cache_destroy(&iproto_threads[i].net_slabc);
	}
	free(iproto_threads);

	mh_int_t i;
	mh_foreach(tx_req_handlers, i) {
		struct mh_i32ptr_node_t *node =
			mh_i32ptr_node(tx_req_handlers, i);
		struct iproto_req_handlers *handlers =
			(struct iproto_req_handlers *)node->val;
		iproto_req_handlers_delete(handlers);
	}
	mh_i32ptr_delete(tx_req_handlers);
	fiber_cond_destroy(&drop_finished_cond);

	/*
	 * Here we close sockets and unlink all unix socket paths.
	 * in case it's unix sockets.
	 */
	evio_service_stop(&tx_binary);
}

static int
iproto_thread_rmean_foreach_impl(struct rmean *rmean, void *cb, void *cb_ctx)
{
	int rc = 0;
	for (size_t i = 0; i < rmean->stats_n; i++) {
		int64_t mean = rmean_mean(rmean, i);
		int64_t total = rmean_total(rmean, i);
		if (((rmean_cb)cb)(rmean->stats[i].name, mean,
				   total, cb_ctx) != 0)
			rc = 1;
	}
	return rc;
}

/**
 * We use offset of rmean in struct iproto_thread, instead of pointer to
 * rmean, because we should iterate over all same rmeans for all iproto
 * threads.
 */
static int
iproto_rmean_foreach_impl(ptrdiff_t rmean_offset, void *cb, void *cb_ctx)
{
	struct rmean *rmean0 =
		*(struct rmean **)((char *)&iproto_threads[0] + rmean_offset);
	for (size_t i = 0; i < rmean0->stats_n; i++) {
		int64_t mean = 0;
		int64_t total = 0;
		for (int j = 0; j < iproto_threads_count; j++) {
			struct rmean *rmean =
				*(struct rmean **)
				((char *)&iproto_threads[j] + rmean_offset);
			assert(rmean == iproto_threads[j].rmean ||
			       rmean == iproto_threads[j].tx.rmean);
			mean += rmean_mean(rmean, i);
			total += rmean_total(rmean, i);
		}
		int rc = ((rmean_cb)cb)(rmean0->stats[i].name, mean,
					total, cb_ctx);
		if (rc != 0)
			return rc;
	}
	return 0;
}

int
iproto_rmean_foreach(void *cb, void *cb_ctx)
{
	int rc;
	rc = iproto_rmean_foreach_impl(offsetof(struct iproto_thread, rmean),
				       cb, cb_ctx);
	if (rc != 0)
		return rc;
	rc = iproto_rmean_foreach_impl(offsetof(struct iproto_thread, tx.rmean),
				       cb, cb_ctx);
	if (rc != 0)
		return rc;
	return 0;
}

int
iproto_thread_rmean_foreach(int thread_id, void *cb, void *cb_ctx)
{
	assert(thread_id >= 0 && thread_id < iproto_threads_count);
	int rc = 0;
	if (iproto_thread_rmean_foreach_impl(iproto_threads[thread_id].rmean,
					cb, cb_ctx) != 0)
		rc = 1;
	if (iproto_thread_rmean_foreach_impl(iproto_threads[thread_id].tx.rmean,
					cb, cb_ctx) != 0)
		rc = 1;
	return rc;
}

int
iproto_override(uint32_t req_type, iproto_handler_t cb,
		iproto_handler_destroy_t destroy, void *ctx)
{
	if (!is_iproto_override_supported(req_type)) {
		const char *feature = tt_sprintf("%s request type",
						 iproto_type_name(req_type));
		diag_set(ClientError, ER_UNSUPPORTED,
			 "IPROTO request handler overriding", feature);
		return -1;
	}

	struct iproto_req_handlers *handlers;
	handlers = mh_req_handlers_get(req_type);
	bool is_set = iproto_req_handler_is_set(handlers);

	if (handlers != NULL && handlers->c.destroy != NULL)
		handlers->c.destroy(handlers->c.ctx);

	if (cb != NULL) {
		if (handlers == NULL) {
			handlers = iproto_req_handlers_new();
			mh_req_handlers_put(req_type, handlers);
		}
		handlers->c.cb = cb;
		handlers->c.destroy = destroy;
		handlers->c.ctx = ctx;
	} else if (handlers != NULL) {
		handlers->c.cb = NULL;
		handlers->c.destroy = NULL;
		handlers->c.ctx = NULL;
	}

	iproto_override_finish(handlers, req_type, is_set);
	return 0;
}
