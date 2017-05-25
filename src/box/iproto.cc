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
#include "third_party/base64.h"

#include "version.h"
#include "fiber.h"
#include "cbus.h"
#include "say.h"
#include "sio.h"
#include "evio.h"
#include "coio.h"
#include "scoped_guard.h"
#include "memory.h"

#include "port.h"
#include "iproto_port.h"
#include "iobuf.h"
#include "box.h"
#include "tuple.h"
#include "session.h"
#include "xrow.h"
#include "schema.h" /* schema_version */
#include "replication.h" /* instance_uuid */
#include "iproto_constants.h"
#include "rmean.h"

/* The number of iproto messages in flight */
enum { IPROTO_MSG_MAX = 768 };

/* {{{ iproto_msg - declaration */

/**
 * A single msg from io thread. All requests
 * from all connections are queued into a single queue
 * and processed in FIFO order.
 */
struct iproto_msg: public cmsg
{
	struct iproto_connection *connection;

	/* --- Box msgs - actual requests for the transaction processor --- */
	/* Request message code and sync. */
	struct xrow_header header;
	/* Box request, if this is a DML */
	struct request request;
	/*
	 * Remember the active iobuf of the connection,
	 * in which the request is stored. The response
	 * must be put into the out buffer of this iobuf.
	 */
	struct iobuf *iobuf;
	/**
	 * How much space the request takes in the
	 * input buffer (len, header and body - all of it)
	 * This also works as a reference counter to
	 * iproto_connection object.
	 */
	size_t len;
	/** End of write position in the output buffer */
	struct obuf_svp write_end;
	/**
	 * Used in "connect" msgs, true if connect trigger failed
	 * and the connection must be closed.
	 */
	bool close_connection;
};

static struct mempool iproto_msg_pool;

static struct iproto_msg *
iproto_msg_new(struct iproto_connection *con)
{
	struct iproto_msg *msg =
		(struct iproto_msg *) mempool_alloc_xc(&iproto_msg_pool);
	msg->connection = con;
	return msg;
}

/**
 * Resume stopped connections, if any.
 */
static void
iproto_resume();


static inline void
iproto_msg_delete(struct cmsg *msg)
{
	mempool_free(&iproto_msg_pool, msg);
	iproto_resume();
}

struct IprotoMsgGuard {
	struct iproto_msg *msg;
	IprotoMsgGuard(struct iproto_msg *msg_arg):msg(msg_arg) {}
	~IprotoMsgGuard()
	{ if (msg) iproto_msg_delete(msg); }
	struct iproto_msg *release()
	{ struct iproto_msg *tmp = msg; msg = NULL; return tmp; }
};

/* }}} */

/* {{{ iproto connection and requests */

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
/* A pointer to the transaction processor cord. */
struct cord *tx_cord;

struct rmean *rmean_net;

enum rmean_net_name {
	IPROTO_SENT,
	IPROTO_RECEIVED,
	IPROTO_LAST,
};

const char *rmean_net_strings[IPROTO_LAST] = { "SENT", "RECEIVED" };

/** Context of a single client connection. */
struct iproto_connection
{
	/**
	 * Two rotating buffers for I/O. Input is always read into
	 * iobuf[0]. As soon as iobuf[0] input buffer becomes full,
	 * iobuf[0] is moved to iobuf[1], for flushing. As soon as
	 * all output in iobuf[1].out is sent to the client, iobuf[1]
	 * and iobuf[0] are moved around again.
	 */
	struct iobuf *iobuf[2];
	/*
	 * Size of readahead which is not parsed yet, i.e.
	 * size of a piece of request which is not fully read.
	 * Is always relative to iobuf[0]->in.wpos. In other words,
	 * iobuf[0]->in.wpos - parse_size gives the start of the
	 * unparsed request. A size rather than a pointer is used
	 * to be safe in case in->buf is reallocated. Being
	 * relative to in->wpos, rather than to in->rpos is helpful to
	 * make sure ibuf_reserve() or iobuf rotation don't make
	 * the value meaningless.
	 */
	size_t parse_size;
	struct ev_io input;
	struct ev_io output;
	/** Logical session. */
	struct session *session;
	ev_loop *loop;
	/* Pre-allocated disconnect msg. */
	struct iproto_msg *disconnect;
	struct rlist in_stop_list;
};

static struct mempool iproto_connection_pool;
static RLIST_HEAD(stopped_connections);

/**
 * Returns true if we have enough spare messages
 * in the message pool. Disconnect messages are
 * discounted: they are mostly reserved and idle.
 */
static inline bool
iproto_stop_input()
{
	size_t connection_count = mempool_count(&iproto_connection_pool);
	size_t request_count = mempool_count(&iproto_msg_pool);
	return request_count > connection_count + IPROTO_MSG_MAX;
}

/**
 * Throttle the queue to the tx thread and ensure the fiber pool
 * in tx thread is not depleted by a flood of incoming requests:
 * resume a stopped connection only if there is a spare message
 * object in the message pool.
 */
static void
iproto_resume()
{
	/*
	 * Most of the time we have nothing to do here: throttling
	 * is not active.
	 */
	if (rlist_empty(&stopped_connections))
		return;
	if (iproto_stop_input())
		return;

	struct iproto_connection *con;
	con = rlist_first_entry(&stopped_connections, struct iproto_connection,
				in_stop_list);
	ev_feed_event(con->loop, &con->input, EV_READ);
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
	return ibuf_used(&con->iobuf[0]->in) == 0 &&
		ibuf_used(&con->iobuf[1]->in) == 0;
}

static inline void
iproto_connection_stop(struct iproto_connection *con)
{
	assert(rlist_empty(&con->in_stop_list));
	ev_io_stop(con->loop, &con->input);
	rlist_add_tail(&stopped_connections, &con->in_stop_list);
}

static void
iproto_connection_on_input(ev_loop * /* loop */, struct ev_io *watcher,
			   int /* revents */);
static void
iproto_connection_on_output(ev_loop * /* loop */, struct ev_io *watcher,
			    int /* revents */);

/** Recycle a connection. Never throws. */
static inline void
iproto_connection_delete(struct iproto_connection *con)
{
	assert(iproto_connection_is_idle(con));
	assert(!evio_has_fd(&con->output));
	assert(!evio_has_fd(&con->input));
	assert(con->session == NULL);
	/*
	 * The output buffers must have been deleted
	 * in tx thread.
	 */
	iobuf_delete_mt(con->iobuf[0]);
	iobuf_delete_mt(con->iobuf[1]);
	if (con->disconnect)
		iproto_msg_delete(con->disconnect);
	mempool_free(&iproto_connection_pool, con);
}

static void
tx_process_misc(struct cmsg *msg);
static void
tx_process1(struct cmsg *msg);
static void
tx_process_select(struct cmsg *msg);
static void
net_send_msg(struct cmsg *msg);

static void
tx_process_join_subscribe(struct cmsg *msg);
static void
net_end_join_subscribe(struct cmsg *msg);

static void
tx_fiber_init(struct session *session, uint64_t sync)
{
	session->sync = sync;
	fiber_set_session(fiber(), session);
}

/**
 * Fire on_disconnect triggers in the tx
 * thread and destroy the session object,
 * as well as output buffers of the connection.
 */
static void
tx_process_disconnect(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;
	if (con->session) {
		tx_fiber_init(con->session, 0);
		if (! rlist_empty(&session_on_disconnect))
			session_run_on_disconnect_triggers(con->session);
		session_destroy(con->session);
		con->session = NULL; /* safety */
	}
	/*
	 * Got to be done in iproto thread since
	 * that's where the memory is allocated.
	 */
	obuf_destroy(&con->iobuf[0]->out);
	obuf_destroy(&con->iobuf[1]->out);
}

/**
 * Cleanup the net thread resources of a connection
 * and close the connection.
 */
static void
net_finish_disconnect(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	/* Runs the trigger, which may yield. */
	iproto_connection_delete(msg->connection);
	iproto_msg_delete(msg);
}

static const struct cmsg_hop disconnect_route[] = {
	{ tx_process_disconnect, &net_pipe },
	{ net_finish_disconnect, NULL },
};

static const struct cmsg_hop misc_route[] = {
	{ tx_process_misc, &net_pipe },
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
	misc_route,                             /* IPROTO_CALL_16 */
	misc_route,                             /* IPROTO_AUTH */
	misc_route,                             /* IPROTO_EVAL */
	process1_route,                         /* IPROTO_UPSERT */
	misc_route                              /* IPROTO_CALL */
};

static const struct cmsg_hop sync_route[] = {
	{ tx_process_join_subscribe, &net_pipe },
	{ net_end_join_subscribe, NULL },
};

static struct iproto_connection *
iproto_connection_new(const char *name, int fd)
{
	(void) name;
	struct iproto_connection *con = (struct iproto_connection *)
		mempool_alloc_xc(&iproto_connection_pool);
	con->input.data = con->output.data = con;
	con->loop = loop();
	ev_io_init(&con->input, iproto_connection_on_input, fd, EV_READ);
	ev_io_init(&con->output, iproto_connection_on_output, fd, EV_WRITE);
	con->iobuf[0] = iobuf_new_mt(&tx_cord->slabc);
	con->iobuf[1] = iobuf_new_mt(&tx_cord->slabc);
	con->parse_size = 0;
	con->session = NULL;
	rlist_create(&con->in_stop_list);
	/* It may be very awkward to allocate at close. */
	con->disconnect = iproto_msg_new(con);
	cmsg_init(con->disconnect, disconnect_route);
	return con;
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
		con->iobuf[0]->in.wpos -= con->parse_size;
	}
	/*
	 * If the connection has no outstanding requests in the
	 * input buffer, then no one (e.g. tx thread) is referring
	 * to it, so it must be destroyed at once. Queue a msg to
	 * run on_disconnect() trigger and destroy the connection.
	 *
	 * Otherwise, it will be destroyed by the last request on
	 * this connection that has finished processing.
	 *
	 * The check is mandatory to not destroy a connection
	 * twice.
	 */
	if (iproto_connection_is_idle(con)) {
		assert(con->disconnect != NULL);
		struct iproto_msg *msg = con->disconnect;
		con->disconnect = NULL;
		cpipe_push(&tx_pipe, msg);
	}
	rlist_del(&con->in_stop_list);
}

/**
 * If there is no space for reading input, we can do one of the
 * following:
 * - try to get a new iobuf, so that it can fit the request.
 *   Always getting a new input buffer when there is no space
 *   makes the instance susceptible to input-flood attacks.
 *   Therefore, at most 2 iobufs are used in a single connection,
 *   one is "open", receiving input, and the  other is closed,
 *   flushing output.
 * - stop input and wait until the client reads piled up output,
 *   so the input buffer can be reused. This complements
 *   the previous strategy. It is only safe to stop input if it
 *   is known that there is output. In this case input event
 *   flow will be resumed when all replies to previous requests
 *   are sent, in iproto_connection_gc_iobuf(). Since there are two
 *   buffers, the input is only stopped when both of them
 *   are fully used up.
 *
 * To make this strategy work, each iobuf in use must fit at
 * least one request. Otherwise, iobuf[1] may end
 * up having no data to flush, while iobuf[0] is too small to
 * fit a big incoming request.
 */
static struct iobuf *
iproto_connection_input_iobuf(struct iproto_connection *con)
{
	struct iobuf *oldbuf = con->iobuf[0];

	size_t to_read = 3; /* Smallest possible valid request. */

	/* The type code is checked in iproto_enqueue_batch() */
	if (con->parse_size) {
		const char *pos = oldbuf->in.wpos - con->parse_size;
		if (mp_check_uint(pos, oldbuf->in.wpos) <= 0)
			to_read = mp_decode_uint(&pos);
	}

	if (ibuf_unused(&oldbuf->in) >= to_read)
		return oldbuf;

	/**
	 * Reuse the buffer if:
	 * - all requests are processed (in only has unparsed
	 *   content, and out is empty, so we will not bloat
	 *   output by reusing input
	 * - we received a large packet, so we need to
	 *   extend input buffer size to store a single large
	 *   packet. In this case we need to realloc the input
	 *   buffer, simply falling through to the subsequent
	 *   branches will not make the buffer larger.
	 */
	if (ibuf_used(&oldbuf->in) == con->parse_size &&
	    (ibuf_pos(&oldbuf->in) == con->parse_size ||
	     obuf_size(&oldbuf->out) == 0)) {
		ibuf_reserve_xc(&oldbuf->in, to_read);
		return oldbuf;
	}

	if (! iobuf_is_idle(con->iobuf[1])) {
		/*
		 * Wait until the second buffer is flushed
		 * and becomes available for reuse.
		 */
		return NULL;
	}
	struct iobuf *newbuf = con->iobuf[1];

	ibuf_reserve_xc(&newbuf->in, to_read + con->parse_size);
	/*
	 * Discard unparsed data in the old buffer, otherwise it
	 * won't be recycled when all parsed requests are processed.
	 */
	oldbuf->in.wpos -= con->parse_size;
	if (con->parse_size != 0) {
		/* Move the cached request prefix to the new buffer. */
		memcpy(newbuf->in.rpos, oldbuf->in.wpos, con->parse_size);
		newbuf->in.wpos += con->parse_size;
		/*
		 * We made ibuf idle. If obuf was already idle it makes the whole
		 * iobuf idle, time to trim buffers.
		 */
		if (iobuf_is_idle(oldbuf))
			iobuf_reset_mt(oldbuf);
	}
	/*
	 * Rotate buffers. Not strictly necessary, but
	 * helps preserve response order.
	 */
	con->iobuf[1] = oldbuf;
	con->iobuf[0] = newbuf;
	return newbuf;
}

static void
iproto_decode_msg(struct iproto_msg *msg, const char **pos, const char *reqend,
		  bool *stop_input)
{
	xrow_header_decode_xc(&msg->header, pos, reqend);
	assert(*pos == reqend);
	request_create(&msg->request, msg->header.type);
	msg->request.header = &msg->header;

	switch (msg->header.type) {
	case IPROTO_SELECT:
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
	case IPROTO_UPDATE:
	case IPROTO_DELETE:
	case IPROTO_CALL_16:
	case IPROTO_CALL:
	case IPROTO_AUTH:
	case IPROTO_EVAL:
	case IPROTO_UPSERT:
		/*
		 * This is a common request which can be parsed with
		 * request_decode(). Parse it before putting it into
		 * the queue to save tx some CPU. More complicated
		 * requests are parsed in tx thread into request type-
		 * specific objects.
		 */
		if (msg->header.bodycnt == 0) {
			tnt_raise(ClientError, ER_INVALID_MSGPACK,
				  "missing request body");
		}
		request_decode_xc(&msg->request,
				 (const char *) msg->header.body[0].iov_base,
				 msg->header.body[0].iov_len,
				 request_key_map(msg->header.type));
		assert(msg->header.type < sizeof(dml_route)/sizeof(*dml_route));
		cmsg_init(msg, dml_route[msg->header.type]);
		break;
	case IPROTO_PING:
		cmsg_init(msg, misc_route);
		break;
	case IPROTO_JOIN:
	case IPROTO_SUBSCRIBE:
		cmsg_init(msg, sync_route);
		*stop_input = true;
		break;
	default:
		tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
			  (uint32_t) msg->header.type);
		break;
	}
}

/** Enqueue all requests which were read up. */
static inline void
iproto_enqueue_batch(struct iproto_connection *con, struct ibuf *in)
{
	int n_requests = 0;
	bool stop_input = false;
	while (con->parse_size && stop_input == false) {
		const char *reqstart = in->wpos - con->parse_size;
		const char *pos = reqstart;
		/* Read request length. */
		if (mp_typeof(*pos) != MP_UINT) {
			tnt_raise(ClientError, ER_INVALID_MSGPACK,
				  "packet length");
		}
		if (mp_check_uint(pos, in->wpos) >= 0)
			break;
		uint32_t len = mp_decode_uint(&pos);
		const char *reqend = pos + len;
		if (reqend > in->wpos)
			break;
		struct iproto_msg *msg = iproto_msg_new(con);
		msg->iobuf = con->iobuf[0];
		IprotoMsgGuard guard(msg);

		msg->len = reqend - reqstart; /* total request length */

		try {
			iproto_decode_msg(msg, &pos, reqend, &stop_input);
			cpipe_push_input(&tx_pipe, guard.release());
			n_requests++;
		} catch (Exception *e) {
			/*
			 * Do not close connection if we failed to
			 * decode a request, as we have enough info
			 * to proceed to the next one.
			 */
			struct obuf *out = &msg->iobuf->out;
			iproto_reply_error(out, e, msg->header.sync);
			out->wend = obuf_create_svp(out);
			if (!ev_is_active(&con->output))
				ev_feed_event(con->loop, &con->output, EV_WRITE);
			e->log();
		}

		/* Request is parsed */
		assert(reqend > reqstart);
		assert(con->parse_size >= (size_t) (reqend - reqstart));
		/*
		 * sic: in case of exception con->parse_size
		 * must not be advanced to stay in sync with
		 * in->rpos.
		 */
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
		assert(rlist_empty(&con->in_stop_list));
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
}

static void
iproto_connection_on_input(ev_loop *loop, struct ev_io *watcher,
			   int /* revents */)
{
	struct iproto_connection *con =
		(struct iproto_connection *) watcher->data;
	int fd = con->input.fd;
	assert(fd >= 0);
	if (! rlist_empty(&con->in_stop_list)) {
		/* Resumed stopped connection. */
		rlist_del(&con->in_stop_list);
		/*
		 * This connection may have no input, so
		 * resume one more connection which might have
		 * input.
		 */
		iproto_resume();
	}
	/*
	 * Throttle if there are too many pending requests,
	 * otherwise we might deplete the fiber pool and
	 * deadlock (e.g. WAL writer needs a fiber to wake
	 * another fiber waiting for write to complete).
	 * Ignore iproto_connection->disconnect messages.
	 */
	if (iproto_stop_input()) {
		iproto_connection_stop(con);
		return;
	}

	try {
		/* Ensure we have sufficient space for the next round.  */
		struct iobuf *iobuf = iproto_connection_input_iobuf(con);
		if (iobuf == NULL) {
			ev_io_stop(loop, &con->input);
			return;
		}

		struct ibuf *in = &iobuf->in;
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
		iproto_enqueue_batch(con, in);
	} catch (Exception *e) {
		/* Best effort at sending the error message to the client. */
		iproto_write_error(fd, e);
		e->log();
		iproto_connection_close(con);
	}
}

/** Get the iobuf which is currently being flushed. */
static inline struct iobuf *
iproto_connection_output_iobuf(struct iproto_connection *con)
{
	if (obuf_used(&con->iobuf[1]->out) > 0)
		return con->iobuf[1];
	/*
	 * Don't try to write from a newer buffer if an older one
	 * exists: in case of a partial write of a newer buffer,
	 * the client may end up getting a salad of different
	 * pieces of replies from both buffers.
	 */
	if (ibuf_used(&con->iobuf[1]->in) == 0 &&
	    obuf_used(&con->iobuf[0]->out) > 0)
		return con->iobuf[0];
	return NULL;
}

/** writev() to the socket and handle the result. */

static int
iproto_flush(struct iobuf *iobuf, struct iproto_connection *con)
{
	int fd = con->output.fd;
	struct obuf_svp *begin = &iobuf->out.wpos;
	struct obuf_svp *end = &iobuf->out.wend;
	assert(begin->used < end->used);
	struct iovec iov[SMALL_OBUF_IOV_MAX+1];
	struct iovec *src = iobuf->out.iov;
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
			if (ibuf_used(&iobuf->in) == 0) {
				/* Quickly recycle the buffer if it's idle. */
				assert(end->used == obuf_size(&iobuf->out));
				/* resets wpos and wpend to zero pos */
				iobuf_reset_mt(iobuf);
			} else { /* Avoid assignment reordering. */
				/* Advance write position. */
				*begin = *end;
			}
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
		struct iobuf *iobuf;
		while ((iobuf = iproto_connection_output_iobuf(con))) {
			if (iproto_flush(iobuf, con) < 0) {
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
tx_process1(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct obuf *out = &msg->iobuf->out;

	tx_fiber_init(msg->connection->session, msg->header.sync);
	if (tx_check_schema(msg->header.schema_version))
		goto error;

	struct tuple *tuple;
	struct obuf_svp svp;
	if (box_process1(&msg->request, &tuple) ||
	    iproto_prepare_select(out, &svp))
		goto error;
	if (tuple && tuple_to_obuf(tuple, out))
		goto error;
	iproto_reply_select(out, &svp, msg->header.sync,
			    tuple != 0);
	msg->write_end = obuf_create_svp(out);
	return;
error:
	iproto_reply_error(out, diag_last_error(&fiber()->diag),
			   msg->header.sync);
	msg->write_end = obuf_create_svp(out);
}

static void
tx_process_select(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct obuf *out = &msg->iobuf->out;
	struct obuf_svp svp;
	struct port port;
	int rc;
	struct request *req = &msg->request;

	tx_fiber_init(msg->connection->session, msg->header.sync);

	if (tx_check_schema(msg->header.schema_version))
		goto error;

	port_create(&port);
	rc = box_select((struct port *) &port,
			req->space_id, req->index_id,
			req->iterator, req->offset, req->limit,
			req->key, req->key_end);
	if (rc < 0 || iproto_prepare_select(out, &svp) != 0) {
		port_destroy(&port);
		goto error;
	}
	port_dump(&port, out);
	iproto_reply_select(out, &svp, msg->header.sync, port.size);
	msg->write_end = obuf_create_svp(out);
	return;
error:
	iproto_reply_error(out, diag_last_error(&fiber()->diag),
			   msg->header.sync);
	msg->write_end = obuf_create_svp(out);
}

static void
tx_process_misc(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct obuf *out = &msg->iobuf->out;

	tx_fiber_init(msg->connection->session, msg->header.sync);

	if (tx_check_schema(msg->header.schema_version))
		goto error;

	try {
		switch (msg->header.type) {
		case IPROTO_CALL:
		case IPROTO_CALL_16:
			assert(msg->request.type == msg->header.type);
			box_process_call(&msg->request, out);
			break;
		case IPROTO_EVAL:
			assert(msg->request.type == msg->header.type);
			box_process_eval(&msg->request, out);
			break;
		case IPROTO_AUTH:
			assert(msg->request.type == msg->header.type);
			box_process_auth(&msg->request, out);
			break;
		case IPROTO_PING:
			iproto_reply_ok(out, msg->header.sync);
			break;
		default:
			unreachable();
		}
	} catch (Exception *e) {
		iproto_reply_error(out, diag_last_error(&fiber()->diag),
				   msg->header.sync);
	}
	msg->write_end = obuf_create_svp(out);
	return;
error:
	iproto_reply_error(out, diag_last_error(&fiber()->diag),
			   msg->header.sync);
	msg->write_end = obuf_create_svp(out);
}

static void
tx_process_join_subscribe(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;

	tx_fiber_init(con->session, msg->header.sync);

	try {
		switch (msg->header.type) {
		case IPROTO_JOIN:
			/*
			 * As soon as box_process_subscribe() returns
			 * the lambda in the beginning of the block
			 * will re-activate the watchers for us.
			 */
			box_process_join(&con->input, &msg->header);
			break;
		case IPROTO_SUBSCRIBE:
			/*
			 * Subscribe never returns - unless there
			 * is an error/exception. In that case
			 * the write watcher will be re-activated
			 * the same way as for JOIN.
			 */
			box_process_subscribe(&con->input, &msg->header);
			break;
		default:
			unreachable();
		}
	} catch (SocketError *e) {
		throw; /* don't write error response to prevent SIGPIPE */
	} catch (Exception *e) {
		iproto_write_error(con->input.fd, e);
	}
}

static void
net_send_msg(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;
	struct iobuf *iobuf = msg->iobuf;
	/* Discard request (see iproto_enqueue_batch()) */
	iobuf->in.rpos += msg->len;
	iobuf->out.wend = msg->write_end;

	if (evio_has_fd(&con->output)) {
		if (! ev_is_active(&con->output))
			ev_feed_event(con->loop, &con->output, EV_WRITE);
	} else if (iproto_connection_is_idle(con)) {
		iproto_connection_close(con);
	}
	iproto_msg_delete(msg);
}

static void
net_end_join_subscribe(struct cmsg *m)
{
	struct iproto_msg *msg = (struct iproto_msg *) m;
	struct iproto_connection *con = msg->connection;
	struct iobuf *iobuf = msg->iobuf;

	iobuf->in.rpos += msg->len;
	iproto_msg_delete(msg);

	assert(! ev_is_active(&con->input));
	/*
	 * Enqueue any messages if they are in the readahead
	 * queue. Will simply start input otherwise.
	 */
	iproto_enqueue_batch(con, &iobuf->in);
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
	struct obuf *out = &msg->iobuf->out;
	try {              /* connect. */
		con->session = session_create(con->input.fd);
		if (con->session == NULL)
			diag_raise();
		tx_fiber_init(con->session, 0);
		static __thread char greeting[IPROTO_GREETING_SIZE];
		/* TODO: dirty read from tx thread */
		struct tt_uuid uuid = INSTANCE_UUID;
		greeting_encode(greeting, tarantool_version_id(),
				&uuid, con->session->salt, SESSION_SEED_SIZE);
		obuf_dup_xc(out, greeting, IPROTO_GREETING_SIZE);
		if (! rlist_empty(&session_on_connect)) {
			if (session_run_on_connect_triggers(con->session) != 0)
				diag_raise();
		}
		msg->write_end = obuf_create_svp(out);
	} catch (Exception *e) {
		iproto_reply_error(out, e, 0 /* zero sync for connect error */);
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
		struct obuf *out = &msg->iobuf->out;
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
	con->iobuf[0]->out.wend = msg->write_end;
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
	char name[SERVICE_NAME_MAXLEN];
	snprintf(name, sizeof(name), "%s/%s", "iobuf",
		sio_strfaddr(addr, addrlen));

	struct iproto_connection *con;

	con = iproto_connection_new(name, fd);
	/*
	 * Ignore msg allocation failure - the queue size is
	 * fixed so there is a limited number of msgs in
	 * use, all stored in just a few blocks of the memory pool.
	 */
	struct iproto_msg *msg = iproto_msg_new(con);
	cmsg_init(msg, connect_route);
	msg->iobuf = con->iobuf[0];
	msg->close_connection = false;
	cpipe_push(&tx_pipe, msg);
}

static struct evio_service binary; /* iproto binary listener */

/**
 * The network io thread main function:
 * begin serving the message bus.
 */
static int
net_cord_f(va_list /* ap */)
{
	/* Got to be called in every thread using iobuf */
	iobuf_init();
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
	cpipe_set_max_input(&tx_pipe, IPROTO_MSG_MAX/2);
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

/** Initialize the iproto subsystem and start network io thread */
void
iproto_init()
{
	tx_cord = cord();

	static struct cord net_cord;
	if (cord_costart(&net_cord, "iproto", net_cord_f, NULL))
		panic("failed to initialize iproto thread");

	/* Create a pipe to "net" thread. */
	cpipe_create(&net_pipe, "net");
	cpipe_set_max_input(&net_pipe, IPROTO_MSG_MAX/2);
}

/**
 * Since there is no way to "synchronously" change the
 * state of the io thread, to change the listen port
 * we need to bounce a couple of messages to and
 * from this thread.
 */
struct iproto_bind_msg: public cbus_call_msg
{
	const char *uri;
};

static int
iproto_do_bind(struct cbus_call_msg *m)
{
	const char *uri  = ((struct iproto_bind_msg *) m)->uri;
	try {
		if (evio_service_is_active(&binary))
			evio_service_stop(&binary);
		if (uri != NULL)
			evio_service_bind(&binary, uri);
	} catch (Exception *e) {
		return -1;
	}
	return 0;
}

static int
iproto_do_listen(struct cbus_call_msg *m)
{
	(void) m;
	try {
		if (evio_service_is_active(&binary))
			evio_service_listen(&binary);
	} catch (Exception *e) {
		return -1;
	}
	return 0;
}

void
iproto_bind(const char *uri)
{
	static struct iproto_bind_msg m;
	m.uri = uri;
	if (cbus_call(&net_pipe, &tx_pipe, &m, iproto_do_bind,
		      NULL, TIMEOUT_INFINITY))
		diag_raise();
}

void
iproto_listen()
{
	/* Declare static to avoid stack corruption on fiber cancel. */
	static struct cbus_call_msg m;
	if (cbus_call(&net_pipe, &tx_pipe, &m, iproto_do_listen,
		      NULL, TIMEOUT_INFINITY))
		diag_raise();
}

/* vim: set foldmethod=marker */
