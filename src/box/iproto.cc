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
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

#include <msgpuck.h>
#include <small/ibuf.h>
#include <small/obuf.h>
#include <base64.h>

#include "version.h"
#include "fiber.h"
#include "cbus.h"
#include "say.h"
#include "sio.h"
#include "evio.h"
#include "coio.h"
#include "scoped_guard.h"
#include "memory.h"
#include "random.h"

#include "port.h"
#include "box.h"
#include "call.h"
#include "tuple_convert.h"
#include "session.h"
#include "xrow.h"
#include "schema.h" /* schema_version */
#include "replication.h" /* instance_uuid */
#include "iproto_constants.h"
#include "rmean.h"
#include "errinj.h"

enum {
	IPROTO_SALT_SIZE = 32,
	IPROTO_PACKET_SIZE_MAX = 2UL * 1024 * 1024 * 1024,
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
		/** Box request, if this is a DML */
		struct request dml;
		/** Box request, if this is a call or eval. */
		struct call_request call;
		/** Authentication request. */
		struct auth_request auth;
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
};

static struct mempool iproto_msg_pool;

static struct iproto_msg *
iproto_msg_new(struct iproto_connection *con);

/**
 * Resume stopped connections, if any.
 */
static void
iproto_resume();

static void
iproto_msg_decode(struct iproto_msg *msg, const char **pos, const char *reqend,
		  bool *stop_input);

static inline void
iproto_msg_delete(struct iproto_msg *msg)
{
	mempool_free(&iproto_msg_pool, msg);
	iproto_resume();
}

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
static struct cpipe tx_pipe;
static struct cpipe net_pipe;

/**
 * Network thread.
 */
static struct cord net_cord;

/**
 * Slab cache used for allocating memory for output network buffers
 * in the tx thread.
 */
static struct slab_cache net_slabc;

struct rmean *rmean_net;

enum rmean_net_name {
	IPROTO_SENT,
	IPROTO_RECEIVED,
	IPROTO_CONNECTIONS,
	IPROTO_REQUESTS,
	IPROTO_LAST,
};

const char *rmean_net_strings[IPROTO_LAST] = {
	"SENT",
	"RECEIVED",
	"CONNECTIONS",
	"REQUESTS",
};

static void
tx_process_destroy(struct cmsg *m);

static void
net_finish_destroy(struct cmsg *m);

static const struct cmsg_hop destroy_route[] = {
	{ tx_process_destroy, &net_pipe },
	{ net_finish_destroy, NULL },
};

/** Fire on_disconnect triggers in the tx thread. */
static void
tx_process_disconnect(struct cmsg *m);

/** Send destroy message to tx thread. */
static void
net_finish_disconnect(struct cmsg *m);

static const struct cmsg_hop disconnect_route[] = {
	{ tx_process_disconnect, &net_pipe },
	{ net_finish_disconnect, NULL }
};

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

static const struct cmsg_hop push_route[] = {
	{ iproto_process_push, &tx_pipe },
	{ tx_end_push, NULL }
};


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
	} tx;
	/** Authentication salt. */
	char salt[IPROTO_SALT_SIZE];
};

static struct mempool iproto_connection_pool;
static RLIST_HEAD(stopped_connections);

/**
 * Return true if we have not enough spare messages
 * in the message pool.
 */
static inline bool
iproto_check_msg_max()
{
	size_t request_count = mempool_count(&iproto_msg_pool);
	return request_count > (size_t) iproto_msg_max;
}

static struct iproto_msg *
iproto_msg_new(struct iproto_connection *con)
{
	struct iproto_msg *msg =
		(struct iproto_msg *) mempool_alloc(&iproto_msg_pool);
	ERROR_INJECT(ERRINJ_TESTING, {
		mempool_free(&iproto_msg_pool, msg);
		msg = NULL;
	});
	if (msg == NULL) {
		diag_set(OutOfMemory, sizeof(*msg), "mempool_alloc", "msg");
		say_warn("can not allocate memory for a new message, "
			 "connection %s", sio_socketname(con->input.fd));
		return NULL;
	}
	msg->close_connection = false;
	msg->connection = con;
	rmean_collect(rmean_net, IPROTO_REQUESTS, 1);
	return msg;
}

/**
 * A connection is idle when the client is gone
 * and there are no outstanding msgs in the msg queue.
 * An idle connection can be safely garbage collected.
 * Note: a connection only becomes idle after iproto_connection_close(),
 * which closes the fd.  This is why here the check is for
 * evio_has_fd(), not ev_is_active()  (false if event is not
 * started).
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
	return con->long_poll_count == 0 &&
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
			     sio_socketname(con->input.fd));
	assert(rlist_empty(&con->in_stop_list));
	ev_io_stop(con->loop, &con->input);
}

static inline void
iproto_connection_stop_msg_max_limit(struct iproto_connection *con)
{
	assert(rlist_empty(&con->in_stop_list));

	say_warn_ratelimited("stopping input on connection %s, "
			     "net_msg_max limit is reached",
			     sio_socketname(con->input.fd));
	ev_io_stop(con->loop, &con->input);
	/*
	 * Important to add to tail and fetch from head to ensure
	 * strict lifo order (fairness) for stopped connections.
	 */
	rlist_add_tail(&stopped_connections, &con->in_stop_list);
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
	cpipe_push(&tx_pipe, &con->destroy_msg);
}

/**
 * Initiate a connection shutdown. This method may
 * be invoked many times, and does the internal
 * bookkeeping to only cleanup resources once.
 */
static inline void
iproto_connection_close(struct iproto_connection *con)
{
	if (evio_has_fd(&con->input)) {
		/* Clears all pending events. */
		ev_io_stop(con->loop, &con->input);
		ev_io_stop(con->loop, &con->output);

		int fd = con->input.fd;
		/* Make evio_has_fd() happy */
		con->input.fd = con->output.fd = -1;
		close(fd);
		/*
		 * Discard unparsed data, to recycle the
		 * connection in net_send_msg() as soon as all
		 * parsed data is processed.  It's important this
		 * is done only once.
		 */
		con->p_ibuf->wpos -= con->parse_size;
		cpipe_push(&tx_pipe, &con->disconnect_msg);
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
		ibuf_reserve_xc(old_ibuf, to_read);
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

	ibuf_reserve_xc(new_ibuf, to_read + con->parse_size);
	/*
	 * Discard unparsed data in the old buffer, otherwise it
	 * won't be recycled when all parsed requests are processed.
	 */
	old_ibuf->wpos -= con->parse_size;
	if (con->parse_size != 0) {
		/* Move the cached request prefix to the new buffer. */
		memcpy(new_ibuf->rpos, old_ibuf->wpos, con->parse_size);
		new_ibuf->wpos += con->parse_size;
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
 * Enqueue all requests which were read up. If a request limit is
 * reached - stop the connection input even if not the whole batch
 * is enqueued. Else try to read more feeding read event to the
 * event loop.
 * @param con Connection to enqueue in.
 * @param in Buffer to parse.
 *
 * @retval  0 Success.
 * @retval -1 Invalid MessagePack error.
 */
static inline int
iproto_enqueue_batch(struct iproto_connection *con, struct ibuf *in)
{
	assert(rlist_empty(&con->in_stop_list));
	int n_requests = 0;
	bool stop_input = false;
	const char *errmsg;
	while (con->parse_size != 0 && !stop_input) {
		if (iproto_check_msg_max()) {
			iproto_connection_stop_msg_max_limit(con);
			cpipe_flush_input(&tx_pipe);
			return 0;
		}
		const char *reqstart = in->wpos - con->parse_size;
		const char *pos = reqstart;
		/* Read request length. */
		if (mp_typeof(*pos) != MP_UINT) {
			errmsg = "packet length";
err_msgpack:
			cpipe_flush_input(&tx_pipe);
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
		if (msg == NULL) {
			/*
			 * Do not treat it as an error - just wait
			 * until some of requests are finished.
			 */
			iproto_connection_stop_msg_max_limit(con);
			return 0;
		}
		msg->p_ibuf = con->p_ibuf;
		msg->wpos = con->wpos;

		msg->len = reqend - reqstart; /* total request length */

		iproto_msg_decode(msg, &pos, reqend, &stop_input);
		/*
		 * This can't throw, but should not be
		 * done in case of exception.
		 */
		cpipe_push_input(&tx_pipe, &msg->base);
		n_requests++;
		/* Request is parsed */
		assert(reqend > reqstart);
		assert(con->parse_size >= (size_t) (reqend - reqstart));
		con->parse_size -= reqend - reqstart;
	}
	if (stop_input) {
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
		ev_feed_event(con->loop, &con->input, EV_READ);
	}
	cpipe_flush_input(&tx_pipe);
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
	assert(! iproto_check_msg_max());
	rlist_del(&con->in_stop_list);
	/*
	 * Enqueue_batch() stops the connection again, if the
	 * limit is reached again.
	 */
	if (iproto_enqueue_batch(con, con->p_ibuf) != 0) {
		struct error *e = box_error_last();
		iproto_write_error(con->input.fd, e, ::schema_version, 0);
		error_log(e);
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
iproto_resume()
{
	while (!iproto_check_msg_max() && !rlist_empty(&stopped_connections)) {
		/*
		 * Shift from list head to ensure strict FIFO
		 * (fairness) for resumed connections.
		 */
		struct iproto_connection *con =
			rlist_first_entry(&stopped_connections,
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
	int fd = con->input.fd;
	assert(fd >= 0);
	assert(rlist_empty(&con->in_stop_list));
	assert(loop == con->loop);
	/*
	 * Throttle if there are too many pending requests,
	 * otherwise we might deplete the fiber pool in tx
	 * thread and deadlock.
	 */
	if (iproto_check_msg_max()) {
		iproto_connection_stop_msg_max_limit(con);
		return;
	}

	try {
		/* Ensure we have sufficient space for the next round.  */
		struct ibuf *in = iproto_connection_input_buffer(con);
		if (in == NULL) {
			iproto_connection_stop_readahead_limit(con);
			return;
		}
		/* Read input. */
		int nrd = sio_read(fd, in->wpos, ibuf_unused(in));
		if (nrd < 0) {                  /* Socket is not ready. */
			ev_io_start(loop, &con->input);
			return;
		}
		if (nrd == 0) {                 /* EOF */
			iproto_connection_close(con);
			return;
		}
		/* Count statistics */
		rmean_collect(rmean_net, IPROTO_RECEIVED, nrd);

		/* Update the read position and connection state. */
		in->wpos += nrd;
		con->parse_size += nrd;
		/* Enqueue all requests which are fully read up. */
		if (iproto_enqueue_batch(con, in) != 0)
			diag_raise();
	} catch (Exception *e) {
		/* Best effort at sending the error message to the client. */
		iproto_write_error(fd, e, ::schema_version, 0);
		e->log();
		iproto_connection_close(con);
	}
}

/** writev() to the socket and handle the result. */

static int
iproto_flush(struct iproto_connection *con)
{
	int fd = con->output.fd;
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

	ssize_t nwr = sio_writev(fd, iov, iovcnt);

	/* Count statistics */
	rmean_collect(rmean_net, IPROTO_SENT, nwr);
	if (nwr > 0) {
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
	}
	return -1;
}

static void
iproto_connection_on_output(ev_loop *loop, struct ev_io *watcher,
			    int /* revents */)
{
	struct iproto_connection *con = (struct iproto_connection *) watcher->data;

	try {
		int rc;
		while ((rc = iproto_flush(con)) <= 0) {
			if (rc != 0) {
				ev_io_start(loop, &con->output);
				return;
			}
			if (! ev_is_active(&con->input) &&
			    rlist_empty(&con->in_stop_list)) {
				ev_feed_event(loop, &con->input, EV_READ);
			}
		}
		if (ev_is_active(&con->output))
			ev_io_stop(con->loop, &con->output);
	} catch (Exception *e) {
		e->log();
		iproto_connection_close(con);
	}
}

static struct iproto_connection *
iproto_connection_new(int fd)
{
	struct iproto_connection *con = (struct iproto_connection *)
		mempool_alloc(&iproto_connection_pool);
	if (con == NULL) {
		diag_set(OutOfMemory, sizeof(*con), "mempool_alloc", "con");
		return NULL;
	}
	con->input.data = con->output.data = con;
	con->loop = loop();
	ev_io_init(&con->input, iproto_connection_on_input, fd, EV_READ);
	ev_io_init(&con->output, iproto_connection_on_output, fd, EV_WRITE);
	ibuf_create(&con->ibuf[0], cord_slab_cache(), iproto_readahead);
	ibuf_create(&con->ibuf[1], cord_slab_cache(), iproto_readahead);
	obuf_create(&con->obuf[0], &net_slabc, iproto_readahead);
	obuf_create(&con->obuf[1], &net_slabc, iproto_readahead);
	con->p_ibuf = &con->ibuf[0];
	con->tx.p_obuf = &con->obuf[0];
	iproto_wpos_create(&con->wpos, con->tx.p_obuf);
	iproto_wpos_create(&con->wend, con->tx.p_obuf);
	con->parse_size = 0;
	con->long_poll_count = 0;
	con->session = NULL;
	rlist_create(&con->in_stop_list);
	/* It may be very awkward to allocate at close. */
	cmsg_init(&con->destroy_msg, destroy_route);
	cmsg_init(&con->disconnect_msg, disconnect_route);
	con->state = IPROTO_CONNECTION_ALIVE;
	con->tx.is_push_pending = false;
	con->tx.is_push_sent = false;
	rmean_collect(rmean_net, IPROTO_CONNECTIONS, 1);
	return con;
}

/** Recycle a connection. Never throws. */
static inline void
iproto_connection_delete(struct iproto_connection *con)
{
	assert(iproto_connection_is_idle(con));
	assert(!evio_has_fd(&con->output));
	assert(!evio_has_fd(&con->input));
	assert(con->session == NULL);
	assert(con->state == IPROTO_CONNECTION_DESTROYED);
	/*
	 * The output buffers must have been deleted
	 * in tx thread.
	 */
	ibuf_destroy(&con->ibuf[0]);
	ibuf_destroy(&con->ibuf[1]);
	assert(con->obuf[0].pos == 0 &&
	       con->obuf[0].iov[0].iov_base == NULL);
	assert(con->obuf[1].pos == 0 &&
	       con->obuf[1].iov[0].iov_base == NULL);
	mempool_free(&iproto_connection_pool, con);
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
tx_reply_error(struct iproto_msg *msg);

static void
tx_reply_iproto_error(struct cmsg *m);

static void
net_send_msg(struct cmsg *msg);

static void
net_send_error(struct cmsg *msg);

static void
tx_process_join_subscribe(struct cmsg *msg);

static void
net_end_join(struct cmsg *msg);

static void
net_end_subscribe(struct cmsg *msg);

static const struct cmsg_hop misc_route[] = {
	{ tx_process_misc, &net_pipe },
	{ net_send_msg, NULL },
};

static const struct cmsg_hop call_route[] = {
	{ tx_process_call, &net_pipe },
	{ net_send_msg, NULL },
};

static const struct cmsg_hop select_route[] = {
	{ tx_process_select, &net_pipe },
	{ net_send_msg, NULL },
};

static const struct cmsg_hop process1_route[] = {
	{ tx_process1, &net_pipe },
	{ net_send_msg, NULL },
};

static const struct cmsg_hop *dml_route[IPROTO_TYPE_STAT_MAX] = {
	NULL,                                   /* IPROTO_OK */
	select_route,                           /* IPROTO_SELECT */
	process1_route,                         /* IPROTO_INSERT */
	process1_route,                         /* IPROTO_REPLACE */
	process1_route,                         /* IPROTO_UPDATE */
	process1_route,                         /* IPROTO_DELETE */
	call_route,                             /* IPROTO_CALL_16 */
	misc_route,                             /* IPROTO_AUTH */
	call_route,                             /* IPROTO_EVAL */
	process1_route,                         /* IPROTO_UPSERT */
	call_route,                             /* IPROTO_CALL */
	NULL,                                   /* reserved */
	NULL,                                   /* IPROTO_NOP */
};

static const struct cmsg_hop join_route[] = {
	{ tx_process_join_subscribe, &net_pipe },
	{ net_end_join, NULL },
};

static const struct cmsg_hop subscribe_route[] = {
	{ tx_process_join_subscribe, &net_pipe },
	{ net_end_subscribe, NULL },
};

static const struct cmsg_hop error_route[] = {
	{ tx_reply_iproto_error, &net_pipe },
	{ net_send_error, NULL },
};

static void
iproto_msg_decode(struct iproto_msg *msg, const char **pos, const char *reqend,
		  bool *stop_input)
{
	uint8_t type;

	/*
	 * nginx_upstream_module may send garbage at the end of
	 * packet, this is a workaround until nginx upstream is
	 * fixed.
	 */
	if (xrow_header_decode(&msg->header, pos, reqend, false))
		goto error;
	/*
	 * Skip trash at the end of msgpack packet, workaround
	 * for the same broken nginx_upstream_module
	 */
	*pos = reqend;

	type = msg->header.type;

	/*
	 * Parse request before putting it into the queue
	 * to save tx some CPU. More complicated requests are
	 * parsed in tx thread into request type-specific objects.
	 */
	switch (type) {
	case IPROTO_SELECT:
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
	case IPROTO_UPDATE:
	case IPROTO_DELETE:
	case IPROTO_UPSERT:
		if (xrow_decode_dml(&msg->header, &msg->dml,
				    dml_request_key_map(type)))
			goto error;
		assert(type < sizeof(dml_route)/sizeof(*dml_route));
		cmsg_init(&msg->base, dml_route[type]);
		break;
	case IPROTO_CALL_16:
	case IPROTO_CALL:
	case IPROTO_EVAL:
		if (xrow_decode_call(&msg->header, &msg->call))
			goto error;
		cmsg_init(&msg->base, call_route);
		break;
	case IPROTO_PING:
		cmsg_init(&msg->base, misc_route);
		break;
	case IPROTO_JOIN:
		cmsg_init(&msg->base, join_route);
		*stop_input = true;
		break;
	case IPROTO_SUBSCRIBE:
		cmsg_init(&msg->base, subscribe_route);
		*stop_input = true;
		break;
	case IPROTO_VOTE_DEPRECATED:
	case IPROTO_VOTE:
		cmsg_init(&msg->base, misc_route);
		break;
	case IPROTO_AUTH:
		if (xrow_decode_auth(&msg->header, &msg->auth))
			goto error;
		cmsg_init(&msg->base, misc_route);
		break;
	default:
		diag_set(ClientError, ER_UNKNOWN_REQUEST_TYPE,
			 (uint32_t) type);
		goto error;
	}
	return;
error:
	/** Log and send the error. */
	diag_log();
	diag_create(&msg->diag);
	diag_move(&fiber()->diag, &msg->diag);
	cmsg_init(&msg->base, error_route);
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
tx_process_disconnect(struct cmsg *m)
{
	struct iproto_connection *con =
		container_of(m, struct iproto_connection, disconnect_msg);
	if (con->session != NULL) {
		session_close(con->session);
		if (! rlist_empty(&session_on_disconnect)) {
			tx_fiber_init(con->session, 0);
			session_run_on_disconnect_triggers(con->session);
		}
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
		session_destroy(con->session);
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


static int
tx_check_schema(uint32_t new_schema_version)
{
	if (new_schema_version && new_schema_version != schema_version) {
		diag_set(ClientError, ER_WRONG_SCHEMA_VERSION,
			 new_schema_version, schema_version);
		return -1;
	}
	return 0;
}

static void
net_discard_input(struct cmsg *m)
{
	struct iproto_msg *msg = container_of(m, struct iproto_msg,
					      discard_input);
	struct iproto_connection *con = msg->connection;
	msg->p_ibuf->rpos += msg->len;
	msg->len = 0;
	con->long_poll_count++;
	if (evio_has_fd(&con->input) && !ev_is_active(&con->input) &&
	    rlist_empty(&con->in_stop_list))
		ev_feed_event(con->loop, &con->input, EV_READ);
}

static void
tx_discard_input(struct iproto_msg *msg)
{
	static const struct cmsg_hop discard_input_route[] = {
		{ net_discard_input, NULL },
	};
	cmsg_init(&msg->discard_input, discard_input_route);
	cpipe_push(&net_pipe, &msg->discard_input);
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

static inline struct iproto_msg *
tx_accept_msg(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	tx_accept_wpos(msg->connection, &msg->wpos);
	tx_fiber_init(msg->connection->session, msg->header.sync);
	return msg;
}

/**
 * Write error message to the output buffer and advance
 * write position. Doesn't throw.
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
 * write position. Doesn't throw.
 */
static void
tx_reply_iproto_error(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct obuf *out = msg->connection->tx.p_obuf;
	iproto_reply_error(out, diag_last_error(&msg->diag),
			   msg->header.sync, ::schema_version);
	iproto_wpos_create(&msg->wpos, out);
}

/** Inject a short delay on tx request processing for testing. */
static inline void
tx_inject_delay()
{
	ERROR_INJECT(ERRINJ_IPROTO_TX_DELAY, {
		if (rand() % 100 < 10)
			fiber_sleep(0.001);
	});
}

static void
tx_process1(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	if (tx_check_schema(msg->header.schema_version))
		goto error;

	struct tuple *tuple;
	struct obuf_svp svp;
	struct obuf *out;
	tx_inject_delay();
	if (box_process1(&msg->dml, &tuple) != 0)
		goto error;
	out = msg->connection->tx.p_obuf;
	if (iproto_prepare_select(out, &svp) != 0)
		goto error;
	if (tuple && tuple_to_obuf(tuple, out))
		goto error;
	iproto_reply_select(out, &svp, msg->header.sync, ::schema_version,
			    tuple != 0);
	iproto_wpos_create(&msg->wpos, out);
	return;
error:
	tx_reply_error(msg);
}

static void
tx_process_select(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct obuf *out;
	struct obuf_svp svp;
	struct port port;
	int count;
	int rc;
	struct request *req = &msg->dml;
	if (tx_check_schema(msg->header.schema_version))
		goto error;

	tx_inject_delay();
	rc = box_select(req->space_id, req->index_id,
			req->iterator, req->offset, req->limit,
			req->key, req->key_end, &port);
	if (rc < 0)
		goto error;

	out = msg->connection->tx.p_obuf;
	if (iproto_prepare_select(out, &svp) != 0) {
		port_destroy(&port);
		goto error;
	}
	/*
	 * SELECT output format has not changed since Tarantool 1.6
	 */
	count = port_dump_msgpack_16(&port, out);
	port_destroy(&port);
	if (count < 0) {
		/* Discard the prepared select. */
		obuf_rollback_to_svp(out, &svp);
		goto error;
	}
	iproto_reply_select(out, &svp, msg->header.sync,
			    ::schema_version, count);
	iproto_wpos_create(&msg->wpos, out);
	return;
error:
	tx_reply_error(msg);
}

static void
tx_process_call_on_yield(struct trigger *trigger, void *event)
{
	(void)event;
	struct iproto_msg *msg = (struct iproto_msg *)trigger->data;
	TRASH(&msg->call);
	tx_discard_input(msg);
	trigger_clear(trigger);
}

static void
tx_process_call(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	if (tx_check_schema(msg->header.schema_version))
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
	if (iproto_prepare_select(out, &svp) != 0) {
		port_destroy(&port);
		goto error;
	}

	if (msg->header.type == IPROTO_CALL_16)
		count = port_dump_msgpack_16(&port, out);
	else
		count = port_dump_msgpack(&port, out);
	port_destroy(&port);
	if (count < 0) {
		obuf_rollback_to_svp(out, &svp);
		goto error;
	}

	iproto_reply_select(out, &svp, msg->header.sync,
			    ::schema_version, count);
	iproto_wpos_create(&msg->wpos, out);
	return;
error:
	tx_reply_error(msg);
}

static void
tx_process_misc(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct iproto_connection *con = msg->connection;
	struct obuf *out = con->tx.p_obuf;
	if (tx_check_schema(msg->header.schema_version))
		goto error;

	try {
		struct ballot ballot;
		switch (msg->header.type) {
		case IPROTO_AUTH:
			box_process_auth(&msg->auth, con->salt);
			iproto_reply_ok_xc(out, msg->header.sync,
					   ::schema_version);
			break;
		case IPROTO_PING:
			iproto_reply_ok_xc(out, msg->header.sync,
					   ::schema_version);
			break;
		case IPROTO_VOTE_DEPRECATED:
			iproto_reply_vclock_xc(out, &replicaset.vclock,
					       msg->header.sync,
					       ::schema_version);
			break;
		case IPROTO_VOTE:
			box_process_vote(&ballot);
			iproto_reply_vote_xc(out, &ballot, msg->header.sync,
					     ::schema_version);
			break;
		default:
			unreachable();
		}
		iproto_wpos_create(&msg->wpos, out);
	} catch (Exception *e) {
		tx_reply_error(msg);
	}
	return;
error:
	tx_reply_error(msg);
}

static void
tx_process_join_subscribe(struct cmsg *m)
{
	struct iproto_msg *msg = tx_accept_msg(m);
	struct iproto_connection *con = msg->connection;
	struct ev_io io;
	coio_create(&io, con->input.fd);
	try {
		switch (msg->header.type) {
		case IPROTO_JOIN:
			/*
			 * As soon as box_process_subscribe() returns
			 * the lambda in the beginning of the block
			 * will re-activate the watchers for us.
			 */
			box_process_join(&io, &msg->header);
			break;
		case IPROTO_SUBSCRIBE:
			/*
			 * Subscribe never returns - unless there
			 * is an error/exception. In that case
			 * the write watcher will be re-activated
			 * the same way as for JOIN.
			 */
			box_process_subscribe(&io, &msg->header);
			break;
		default:
			unreachable();
		}
	} catch (SocketError *e) {
		return; /* don't write error response to prevent SIGPIPE */
	} catch (TimedOut *e) {
		 /*
		  * In case of a timeout the error could come after a partially
		  * written row. Do not push it on top.
		  */
		return;
	} catch (Exception *e) {
		iproto_write_error(con->input.fd, e, ::schema_version,
				   msg->header.sync);
	}
}

static void
net_send_msg(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;

	if (msg->len != 0) {
		/* Discard request (see iproto_enqueue_batch()). */
		msg->p_ibuf->rpos += msg->len;
	} else {
		/* Already discarded by net_discard_input(). */
		assert(con->long_poll_count > 0);
		con->long_poll_count--;
	}
	con->wend = msg->wpos;

	if (evio_has_fd(&con->output)) {
		if (! ev_is_active(&con->output))
			ev_feed_event(con->loop, &con->output, EV_WRITE);
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

	msg->p_ibuf->rpos += msg->len;
	iproto_msg_delete(msg);

	assert(! ev_is_active(&con->input));
	/*
	 * Enqueue any messages if they are in the readahead
	 * queue. Will simply start input otherwise.
	 */
	if (iproto_enqueue_batch(con, msg->p_ibuf) != 0)
		iproto_connection_close(con);
}

static void
net_end_subscribe(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;

	msg->p_ibuf->rpos += msg->len;
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
	try {              /* connect. */
		con->session = session_create(SESSION_TYPE_BINARY);
		if (con->session == NULL)
			diag_raise();
		con->session->meta.connection = con;
		tx_fiber_init(con->session, 0);
		static __thread char greeting[IPROTO_GREETING_SIZE];
		/* TODO: dirty read from tx thread */
		struct tt_uuid uuid = INSTANCE_UUID;
		random_bytes(con->salt, IPROTO_SALT_SIZE);
		greeting_encode(greeting, tarantool_version_id(), &uuid,
				con->salt, IPROTO_SALT_SIZE);
		obuf_dup_xc(out, greeting, IPROTO_GREETING_SIZE);
		if (! rlist_empty(&session_on_connect)) {
			if (session_run_on_connect_triggers(con->session) != 0)
				diag_raise();
		}
		iproto_wpos_create(&msg->wpos, out);
	} catch (Exception *e) {
		tx_reply_error(msg);
		msg->close_connection = true;
	}
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
	if (msg->close_connection) {
		struct obuf *out = msg->wpos.obuf;
		try {
			int64_t nwr = sio_writev(con->output.fd, out->iov,
						 obuf_iovcnt(out));

			/* Count statistics */
			rmean_collect(rmean_net, IPROTO_SENT, nwr);
		} catch (Exception *e) {
			e->log();
		}
		assert(iproto_connection_is_idle(con));
		iproto_connection_close(con);
		iproto_msg_delete(msg);
		return;
	}
	con->wend = msg->wpos;
	/*
	 * Connect is synchronous, so no one could have been
	 * messing up with the connection while it was in
	 * progress.
	 */
	assert(evio_has_fd(&con->output));
	/* Handshake OK, start reading input. */
	ev_feed_event(con->loop, &con->output, EV_WRITE);
	iproto_msg_delete(msg);
}

static const struct cmsg_hop connect_route[] = {
	{ tx_process_connect, &net_pipe },
	{ net_send_greeting, NULL },
};

/** }}} */

/**
 * Create a connection and start input.
 */
static void
iproto_on_accept(struct evio_service * /* service */, int fd,
		 struct sockaddr *addr, socklen_t addrlen)
{
	(void) addr;
	(void) addrlen;
	struct iproto_msg *msg;
	struct iproto_connection *con = iproto_connection_new(fd);
	if (con == NULL)
		goto error_conn;
	/*
	 * Ignore msg allocation failure - the queue size is
	 * fixed so there is a limited number of msgs in
	 * use, all stored in just a few blocks of the memory pool.
	 */
	msg = iproto_msg_new(con);
	if (msg == NULL)
		goto error_msg;
	cmsg_init(&msg->base, connect_route);
	msg->p_ibuf = con->p_ibuf;
	msg->wpos = con->wpos;
	cpipe_push(&tx_pipe, &msg->base);
	return;
error_msg:
	mempool_free(&iproto_connection_pool, con);
error_conn:
	close(fd);
	return;
}

static struct evio_service binary; /* iproto binary listener */

/**
 * The network io thread main function:
 * begin serving the message bus.
 */
static int
net_cord_f(va_list /* ap */)
{
	mempool_create(&iproto_msg_pool, &cord()->slabc,
		       sizeof(struct iproto_msg));
	mempool_create(&iproto_connection_pool, &cord()->slabc,
		       sizeof(struct iproto_connection));

	evio_service_init(loop(), &binary, "binary",
			  iproto_on_accept, NULL);


	/* Init statistics counter */
	rmean_net = rmean_new(rmean_net_strings, IPROTO_LAST);

	if (rmean_net == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct rmean),
			  "rmean", "struct rmean");
	}

	struct cbus_endpoint endpoint;
	/* Create "net" endpoint. */
	cbus_endpoint_create(&endpoint, "net", fiber_schedule_cb, fiber());
	/* Create a pipe to "tx" thread. */
	cpipe_create(&tx_pipe, "tx");
	cpipe_set_max_input(&tx_pipe, iproto_msg_max / 2);
	/* Process incomming messages. */
	cbus_loop(&endpoint);

	cpipe_destroy(&tx_pipe);
	/*
	 * Nothing to do in the fiber so far, the service
	 * will take care of creating events for incoming
	 * connections.
	 */
	if (evio_service_is_active(&binary))
		evio_service_stop(&binary);

	rmean_delete(rmean_net);
	return 0;
}

int
iproto_session_fd(struct session *session)
{
	struct iproto_connection *con =
		(struct iproto_connection *) session->meta.connection;
	return con->output.fd;
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
	if (evio_has_fd(&con->output) && !ev_is_active(&con->output))
		ev_feed_event(con->loop, &con->output, EV_WRITE);
}

/**
 * Send to iproto thread a notification about new pushes.
 * @param con iproto connection.
 */
static void
tx_begin_push(struct iproto_connection *con)
{
	assert(! con->tx.is_push_sent);
	cmsg_init(&con->kharon.base, push_route);
	iproto_wpos_create(&con->kharon.wpos, con->tx.p_obuf);
	con->tx.is_push_pending = false;
	con->tx.is_push_sent = true;
	cpipe_push(&net_pipe, (struct cmsg *) &con->kharon);
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
 * Push a message from @a port to a remote client.
 * @param session iproto session.
 * @param sync Request sync in scope of which to send the push.
 * @param port Port with data to send.
 *
 * @retval -1 Memory error.
 * @retval  0 Success, a message is written to the output buffer.
 *            We don't wait here that the push has reached the
 *            client: the output buffer is flushed asynchronously.
 */
static int
iproto_session_push(struct session *session, uint64_t sync, struct port *port)
{
	struct iproto_connection *con =
		(struct iproto_connection *) session->meta.connection;
	struct obuf_svp svp;
	if (iproto_prepare_select(con->tx.p_obuf, &svp) != 0)
		return -1;
	if (port_dump_msgpack(port, con->tx.p_obuf) < 0) {
		obuf_rollback_to_svp(con->tx.p_obuf, &svp);
		return -1;
	}
	iproto_reply_chunk(con->tx.p_obuf, &svp, sync, ::schema_version);
	if (! con->tx.is_push_sent)
		tx_begin_push(con);
	else
		con->tx.is_push_pending = true;
	return 0;
}

/** }}} */

/** Initialize the iproto subsystem and start network io thread */
void
iproto_init()
{
	slab_cache_create(&net_slabc, &runtime);

	if (cord_costart(&net_cord, "iproto", net_cord_f, NULL))
		panic("failed to initialize iproto thread");

	/* Create a pipe to "net" thread. */
	cpipe_create(&net_pipe, "net");
	cpipe_set_max_input(&net_pipe, iproto_msg_max / 2);
	struct session_vtab iproto_session_vtab = {
		/* .push = */ iproto_session_push,
		/* .fd = */ iproto_session_fd,
		/* .sync = */ iproto_session_sync,
	};
	session_vtab_registry[SESSION_TYPE_BINARY] = iproto_session_vtab;
}

/** Available iproto configuration changes. */
enum iproto_cfg_op {
	IPROTO_CFG_MSG_MAX,
	IPROTO_CFG_LISTEN
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
		/** New URI to bind to. */
		const char *uri;

		/** New iproto max message count. */
		int iproto_msg_max;
	};
};

static inline void
iproto_cfg_msg_create(struct iproto_cfg_msg *msg, enum iproto_cfg_op op)
{
	memset(msg, 0, sizeof(*msg));
	msg->op = op;
}

static int
iproto_do_cfg_f(struct cbus_call_msg *m)
{
	struct iproto_cfg_msg *cfg_msg = (struct iproto_cfg_msg *) m;
	int old;
	try {
		switch (cfg_msg->op) {
		case IPROTO_CFG_MSG_MAX:
			cpipe_set_max_input(&tx_pipe,
					    cfg_msg->iproto_msg_max / 2);
			old = iproto_msg_max;
			iproto_msg_max = cfg_msg->iproto_msg_max;
			if (old < iproto_msg_max)
				iproto_resume();
			break;
		case IPROTO_CFG_LISTEN:
			if (evio_service_is_active(&binary))
				evio_service_stop(&binary);
			if (cfg_msg->uri != NULL) {
				evio_service_bind(&binary, cfg_msg->uri);
				struct errinj *inj =
					errinj(ERRINJ_IPROTO_CFG_LISTEN,
					       ERRINJ_INT);
				if (inj != NULL && inj->iparam > 0) {
					inj->iparam--;
					diag_set(ClientError, ER_INJECTION,
						 "iproto listen");
					diag_raise();
				}
				evio_service_listen(&binary);
			}
			break;
		default:
			unreachable();
		}
	} catch (Exception *e) {
		return -1;
	}
	return 0;
}

static inline int
iproto_do_cfg(struct iproto_cfg_msg *msg)
{
	bool prev = fiber_set_cancellable(false);
	int rc = cbus_call(&net_pipe, &tx_pipe, msg, iproto_do_cfg_f,
			   NULL, TIMEOUT_INFINITY);
	fiber_set_cancellable(prev);
	return rc;
}

void
iproto_listen(const char *uri)
{
	struct iproto_cfg_msg cfg_msg;
	iproto_cfg_msg_create(&cfg_msg, IPROTO_CFG_LISTEN);
	cfg_msg.uri = uri;
	if (iproto_do_cfg(&cfg_msg) != 0)
		diag_raise();
}

size_t
iproto_mem_used(void)
{
	return slab_cache_used(&net_cord.slabc) + slab_cache_used(&net_slabc);
}

size_t
iproto_connection_count(void)
{
	return mempool_count(&iproto_connection_pool);
}

size_t
iproto_request_count(void)
{
	return mempool_count(&iproto_msg_pool);
}

void
iproto_reset_stat(void)
{
	rmean_cleanup(rmean_net);
}

void
iproto_set_msg_max(int new_iproto_msg_max)
{
	if (new_iproto_msg_max < IPROTO_MSG_MAX_MIN) {
		tnt_raise(ClientError, ER_CFG, "net_msg_max",
			  tt_sprintf("minimal value is %d",
				     IPROTO_MSG_MAX_MIN));
	}
	struct iproto_cfg_msg cfg_msg;
	iproto_cfg_msg_create(&cfg_msg, IPROTO_CFG_MSG_MAX);
	cfg_msg.iproto_msg_max = new_iproto_msg_max;
	int rc = iproto_do_cfg(&cfg_msg);
	(void)rc;
	assert(rc == 0);
	cpipe_set_max_input(&net_pipe, new_iproto_msg_max / 2);
}

void
iproto_free()
{
	tt_pthread_cancel(net_cord.id);
	tt_pthread_join(net_cord.id, NULL);
	/*
	* Close socket descriptor to prevent hot standby instance
	* failing to bind in case it tries to bind before socket
	* is closed by OS.
	*/
	if (evio_service_is_active(&binary))
		close(binary.ev.fd);
}
