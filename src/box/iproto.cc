/*
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

#include "iproto_port.h"
#include "tarantool.h"
#include "exception.h"
#include "errcode.h"
#include "fiber.h"
#include "say.h"
#include "evio.h"
#include "scoped_guard.h"
#include "memory.h"
#include "msgpuck/msgpuck.h"
#include "replication.h"
#include "session.h"
#include "third_party/base64.h"
#include "coio.h"
#include "xrow.h"
#include "iproto_constants.h"

class IprotoConnectionShutdown: public Exception
{
public:
	IprotoConnectionShutdown(const char *file, int line)
		:Exception(file, line) {}
	virtual void log() const;
};

void
IprotoConnectionShutdown::log() const
{}

/* {{{ iproto_request - declaration */

struct iproto_connection;

typedef void (*iproto_request_f)(struct iproto_request *);

/**
 * A single request from the client. All requests
 * from all clients are queued into a single queue
 * and processed in FIFO order.
 */
struct iproto_request
{
	struct iproto_connection *connection;
	struct iobuf *iobuf;
	struct session *session;
	iproto_request_f process;
	/* Request message code and sync. */
	struct iproto_header header;
	/* Box request, if this is a DML */
	struct request request;
	size_t total_len;
};

struct mempool iproto_request_pool;

static struct iproto_request *
iproto_request_new(struct iproto_connection *con,
		   iproto_request_f process);

static void
iproto_process_connect(struct iproto_request *request);

static void
iproto_process_disconnect(struct iproto_request *request);

static void
iproto_process_dml(struct iproto_request *request);

static void
iproto_process_admin(struct iproto_request *request);

struct IprotoRequestGuard {
	struct iproto_request *ireq;
	IprotoRequestGuard(struct iproto_request *ireq_arg):ireq(ireq_arg) {}
	~IprotoRequestGuard()
	{ if (ireq) mempool_free(&iproto_request_pool, ireq); }
	struct iproto_request *release()
	{ struct iproto_request *tmp = ireq; ireq = NULL; return tmp; }
};

/* }}} */

/* {{{ iproto_queue */

struct iproto_request;

enum { IPROTO_REQUEST_QUEUE_SIZE = 2048, };

/**
 * Implementation of an input queue of the box request processor.
 *
 * Event handlers read data, determine request boundaries
 * and enqueue requests. Once all input/output events are
 * processed, an own handler is invoked to deal with the
 * requests in the queue. It leases a fiber from a pool
 * and runs the request in the fiber.
 *
 * @sa iproto_queue_schedule
 */
struct iproto_queue
{
	/** Ring buffer of fixed size */
	struct iproto_request *queue[IPROTO_REQUEST_QUEUE_SIZE];
	/**
	 * Cache of fibers which work on requests
	 * in this queue.
	 */
	struct rlist fiber_cache;
	/**
	 * Used to trigger request processing when
	 * the queue becomes non-empty.
	 */
	struct ev_async watcher;
	/* Ring buffer position. */
	int begin, end;
	/* Ring buffer size. */
	int size;
};

static inline bool
iproto_queue_is_empty(struct iproto_queue *i_queue)
{
	return i_queue->begin == i_queue->end;
}

/**
 * A single global queue for all requests in all connections. All
 * requests are processed concurrently.
 * Is also used as a queue for just established connections and to
 * execute disconnect triggers. A few notes about these triggers:
 * - they need to be run in a fiber
 * - unlike an ordinary request failure, on_connect trigger
 *   failure must lead to connection close.
 * - on_connect trigger must be processed before any other
 *   request on this connection.
 */
static struct iproto_queue request_queue;

static void
iproto_queue_push(struct iproto_queue *i_queue,
		  struct iproto_request *request)
{
	/* If the queue is full, invoke the handler to work it off. */
	if (i_queue->end == i_queue->size)
		ev_invoke(loop(), &i_queue->watcher, EV_CUSTOM);
	assert(i_queue->end < i_queue->size);
	/*
	 * There were some queued requests, ensure they are
	 * handled.
	 */
	if (iproto_queue_is_empty(i_queue))
		ev_feed_event(loop(), &request_queue.watcher, EV_CUSTOM);
	i_queue->queue[i_queue->end++] = request;
}

static struct iproto_request *
iproto_queue_pop(struct iproto_queue *i_queue)
{
	if (i_queue->begin == i_queue->end)
		return NULL;
	struct iproto_request *request = i_queue->queue[i_queue->begin++];
	if (i_queue->begin == i_queue->end)
		i_queue->begin = i_queue->end = 0;
	return request;
}

/**
 * Main function of the fiber invoked to handle all outstanding
 * tasks in a queue.
 */
static void
iproto_queue_handler(va_list ap)
{
	struct iproto_queue *i_queue = va_arg(ap, struct iproto_queue *);
	struct iproto_request *request;
restart:
	while ((request = iproto_queue_pop(i_queue))) {
		IprotoRequestGuard guard(request);
		fiber_set_session(fiber(), request->session);
		request->process(request);
	}
	/** Put the current fiber into a queue fiber cache. */
	rlist_add_entry(&i_queue->fiber_cache, fiber(), state);
	fiber_yield();
	goto restart;
}

/** Create fibers to handle all outstanding tasks. */
static void
iproto_queue_schedule(ev_loop * /* loop */, struct ev_async *watcher,
		      int /* events */)
{
	struct iproto_queue *i_queue = (struct iproto_queue *) watcher->data;
	while (! iproto_queue_is_empty(i_queue)) {

		struct fiber *f;
		if (! rlist_empty(&i_queue->fiber_cache))
			f = rlist_shift_entry(&i_queue->fiber_cache,
					      struct fiber, state);
		else
			f = fiber_new("iproto", iproto_queue_handler);
		fiber_call(f, i_queue);
	}
}

static inline void
iproto_queue_init(struct iproto_queue *i_queue)
{
	i_queue->size = IPROTO_REQUEST_QUEUE_SIZE;
	i_queue->begin = i_queue->end = 0;
	/**
	 * Initialize an ev_async event which would start
	 * workers for all outstanding tasks.
	 */
	ev_async_init(&i_queue->watcher, iproto_queue_schedule);
	i_queue->watcher.data = i_queue;
	rlist_create(&i_queue->fiber_cache);
}

/* }}} */

/* {{{ iproto_connection */

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
	 * Is always relative to iobuf[0]->in.end. In other words,
	 * iobuf[0]->in.end - parse_size gives the start of the
	 * unparsed request. A size rather than a pointer is used
	 * to be safe in case in->buf is reallocated. Being
	 * relative to in->end, rather than to in->pos is helpful to
	 * make sure ibuf_reserve() or iobuf rotation don't make
	 * the value meaningless.
	 */
	ssize_t parse_size;
	/** Current write position in the output buffer */
	struct obuf_svp write_pos;
	/**
	 * Function of the request processor to handle
	 * a single request.
	 */
	struct ev_io input;
	struct ev_io output;
	/** Logical session. */
	struct session *session;
	uint64_t cookie;
	ev_loop *loop;
	/* Pre-allocated disconnect request. */
	struct iproto_request *disconnect;
};

static struct mempool iproto_connection_pool;

/**
 * A connection is idle when the client is gone
 * and there are no outstanding requests in the request queue.
 * An idle connection can be safely garbage collected.
 * Note: a connection only becomes idle after iproto_connection_close(),
 * which closes the fd.  This is why here the check is for
 * evio_is_active() (false if fd is closed), not ev_is_active()
 * (false if event is not started).
 */
static inline bool
iproto_connection_is_idle(struct iproto_connection *con)
{
	return !evio_is_active(&con->input) &&
		ibuf_size(&con->iobuf[0]->in) == 0 &&
		ibuf_size(&con->iobuf[1]->in) == 0;
}

static void
iproto_connection_on_input(ev_loop * /* loop */, struct ev_io *watcher,
			   int /* revents */);
static void
iproto_connection_on_output(ev_loop * /* loop */, struct ev_io *watcher,
			    int /* revents */);

static struct iproto_connection *
iproto_connection_new(const char *name, int fd, struct sockaddr *addr)
{
	struct iproto_connection *con = (struct iproto_connection *)
		mempool_alloc(&iproto_connection_pool);
	con->input.data = con->output.data = con;
	con->loop = loop();
	ev_io_init(&con->input, iproto_connection_on_input, fd, EV_READ);
	ev_io_init(&con->output, iproto_connection_on_output, fd, EV_WRITE);
	con->iobuf[0] = iobuf_new(name);
	con->iobuf[1] = iobuf_new(name);
	con->parse_size = 0;
	con->write_pos = obuf_create_svp(&con->iobuf[0]->out);
	con->session = NULL;
	con->cookie = *(uint64_t *) addr;
	/* It may be very awkward to allocate at close. */
	con->disconnect = iproto_request_new(con, iproto_process_disconnect);
	return con;
}

/** Recycle a connection. Never throws. */
static inline void
iproto_connection_delete(struct iproto_connection *con)
{
	assert(iproto_connection_is_idle(con));
	assert(!evio_is_active(&con->output));
	if (con->session) {
		fiber_set_session(fiber(), con->session);
		session_run_on_disconnect_triggers(con->session);
		session_destroy(con->session);
	}
	iobuf_delete(con->iobuf[0]);
	iobuf_delete(con->iobuf[1]);
	if (con->disconnect)
		mempool_free(&iproto_request_pool, con->disconnect);
	mempool_free(&iproto_connection_pool, con);
}

static inline void
iproto_connection_shutdown(struct iproto_connection *con)
{
	ev_io_stop(con->loop, &con->input);
	ev_io_stop(con->loop, &con->output);
	con->input.fd = con->output.fd = -1;
	/*
	 * Discard unparsed data, to recycle the con
	 * as soon as all parsed data is processed.
	 */
	con->iobuf[0]->in.end -= con->parse_size;
	/*
	 * If the con is not idle, it is destroyed
	 * after the last request is handled. Otherwise,
	 * queue a separate request to run on_disconnect()
	 * trigger and destroy the connection.
	 * Sic: the check is mandatory to not destroy a connection
	 * twice.
	 */
	if (iproto_connection_is_idle(con)) {
		struct iproto_request *ireq = con->disconnect;
		con->disconnect = NULL;
		iproto_queue_push(&request_queue, ireq);
	}
}

static inline void
iproto_connection_close(struct iproto_connection *con)
{
	int fd = con->input.fd;
	iproto_connection_shutdown(con);
	close(fd);
}

/**
 * If there is no space for reading input, we can do one of the
 * following:
 * - try to get a new iobuf, so that it can fit the request.
 *   Always getting a new input buffer when there is no space
 *   makes the server susceptible to input-flood attacks.
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

	ssize_t to_read = 3; /* Smallest possible valid request. */

	/* The type code is checked in iproto_enqueue_batch() */
	if (con->parse_size) {
		const char *pos = oldbuf->in.end - con->parse_size;
		if (mp_check_uint(pos, oldbuf->in.end) <= 0)
			to_read = mp_decode_uint(&pos);
	}

	if (ibuf_unused(&oldbuf->in) >= to_read)
		return oldbuf;

	/** All requests are processed, reuse the buffer. */
	if (ibuf_size(&oldbuf->in) == con->parse_size) {
		ibuf_reserve(&oldbuf->in, to_read);
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

	ibuf_reserve(&newbuf->in, to_read + con->parse_size);
	/*
	 * Discard unparsed data in the old buffer, otherwise it
	 * won't be recycled when all parsed requests are processed.
	 */
	oldbuf->in.end -= con->parse_size;
	/* Move the cached request prefix to the new buffer. */
	memcpy(newbuf->in.pos, oldbuf->in.end, con->parse_size);
	newbuf->in.end += con->parse_size;
	/*
	 * Rotate buffers. Not strictly necessary, but
	 * helps preserve response order.
	 */
	con->iobuf[1] = oldbuf;
	con->iobuf[0] = newbuf;
	return newbuf;
}

/** Enqueue all requests which were read up. */
static inline void
iproto_enqueue_batch(struct iproto_connection *con, struct ibuf *in)
{
	while (true) {
		const char *reqstart = in->end - con->parse_size;
		const char *pos = reqstart;
		/* Read request length. */
		if (mp_typeof(*pos) != MP_UINT) {
			tnt_raise(ClientError, ER_INVALID_MSGPACK,
				  "packet length");
		}
		if (mp_check_uint(pos, in->end) >= 0)
			break;
		uint32_t len = mp_decode_uint(&pos);
		const char *reqend = pos + len;
		if (reqend > in->end)
			break;
		struct iproto_request *ireq =
			iproto_request_new(con, iproto_process_dml);
		IprotoRequestGuard guard(ireq);

		iproto_header_decode(&ireq->header, &pos, reqend);
		ireq->total_len = pos - reqstart; /* total request length */


		/*
		 * sic: in case of exception con->parse_size
		 * as well as in->pos must not be advanced, to
		 * stay in sync.
		 */
		if (iproto_type_is_dml(ireq->header.type)) {
			if (ireq->header.bodycnt == 0) {
				tnt_raise(ClientError, ER_INVALID_MSGPACK,
					  "request type");
			}
			request_create(&ireq->request, ireq->header.type);
			pos = (const char *) ireq->header.body[0].iov_base;
			request_decode(&ireq->request, pos,
				       ireq->header.body[0].iov_len);
		} else {
			ireq->process = iproto_process_admin;
		}
		ireq->request.header = &ireq->header;
		iproto_queue_push(&request_queue, guard.release());
		/* Request will be discarded in iproto_process_XXX */

		/* Request is parsed */
		con->parse_size -= reqend - reqstart;
		if (con->parse_size == 0)
			break;
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

	try {
		/* Ensure we have sufficient space for the next round.  */
		struct iobuf *iobuf = iproto_connection_input_iobuf(con);
		if (iobuf == NULL) {
			ev_io_stop(loop, &con->input);
			return;
		}

		struct ibuf *in = &iobuf->in;
		/* Read input. */
		int nrd = sio_read(fd, in->end, ibuf_unused(in));
		if (nrd < 0) {                  /* Socket is not ready. */
			ev_io_start(loop, &con->input);
			return;
		}
		if (nrd == 0) {                 /* EOF */
			iproto_connection_close(con);
			return;
		}
		/* Update the read position and connection state. */
		in->end += nrd;
		con->parse_size += nrd;
		/* Enqueue all requests which are fully read up. */
		iproto_enqueue_batch(con, in);
		/*
		 * Keep reading input, as long as the socket
		 * supplies data.
		 */
		if (!ev_is_active(&con->input))
			ev_feed_event(loop, &con->input, EV_READ);
	} catch (IprotoConnectionShutdown *e) {
		iproto_connection_shutdown(con);
	} catch (Exception *e) {
		e->log();
		iproto_connection_close(con);
	}
}

/** Get the iobuf which is currently being flushed. */
static inline struct iobuf *
iproto_connection_output_iobuf(struct iproto_connection *con)
{
	if (obuf_size(&con->iobuf[1]->out))
		return con->iobuf[1];
	/*
	 * Don't try to write from a newer buffer if an older one
	 * exists: in case of a partial write of a newer buffer,
	 * the client may end up getting a salad of different
	 * pieces of replies from both buffers.
	 */
	if (ibuf_size(&con->iobuf[1]->in) == 0 &&
	    obuf_size(&con->iobuf[0]->out))
		return con->iobuf[0];
	return NULL;
}

/** writev() to the socket and handle the output. */
static int
iproto_flush(struct iobuf *iobuf, int fd, struct obuf_svp *svp)
{
	/* Begin writing from the saved position. */
	struct iovec *iov = iobuf->out.iov + svp->pos;
	int iovcnt = obuf_iovcnt(&iobuf->out) - svp->pos;
	assert(iovcnt);
	ssize_t nwr;
	try {
		sio_add_to_iov(iov, -svp->iov_len);
		nwr = sio_writev(fd, iov, iovcnt);

		sio_add_to_iov(iov, svp->iov_len);
	} catch (Exception *) {
		sio_add_to_iov(iov, svp->iov_len);
		throw;
	}

	if (nwr > 0) {
		if (svp->size + nwr == obuf_size(&iobuf->out)) {
			iobuf_reset(iobuf);
			*svp = obuf_create_svp(&iobuf->out);
			return 0;
		}
		svp->size += nwr;
		svp->pos += sio_move_iov(iov, nwr, &svp->iov_len);
	}
	return -1;
}

static void
iproto_connection_on_output(ev_loop *loop, struct ev_io *watcher,
			    int /* revents */)
{
	struct iproto_connection *con = (struct iproto_connection *) watcher->data;
	int fd = con->output.fd;
	struct obuf_svp *svp = &con->write_pos;

	try {
		struct iobuf *iobuf;
		while ((iobuf = iproto_connection_output_iobuf(con))) {
			if (iproto_flush(iobuf, fd, svp) < 0) {
				ev_io_start(loop, &con->output);
				return;
			}
			if (! ev_is_active(&con->input))
				ev_feed_event(loop, &con->input, EV_READ);
		}
		if (ev_is_active(&con->output))
			ev_io_stop(loop, &con->output);
	} catch (Exception *e) {
		e->log();
		iproto_connection_close(con);
	}
}

/* }}} */

/* {{{ iproto_process_* functions */

static void
iproto_process_dml(struct iproto_request *ireq)
{
	struct iobuf *iobuf = ireq->iobuf;
	struct iproto_connection *con = ireq->connection;

	auto scope_guard = make_scoped_guard([=]{
		/* Discard request (see iproto_enqueue_batch()) */
		iobuf->in.pos += ireq->total_len;

		if (evio_is_active(&con->output)) {
			if (! ev_is_active(&con->output))
				ev_feed_event(con->loop,
					      &con->output,
					      EV_WRITE);
		} else if (iproto_connection_is_idle(con)) {
			iproto_connection_delete(con);
		}
	});

	if (unlikely(! evio_is_active(&con->output)))
		return;

	struct obuf *out = &iobuf->out;

	struct iproto_port port;
	iproto_port_init(&port, out, ireq->header.sync);
	try {
		box_process((struct port *) &port, &ireq->request);
	} catch (ClientError *e) {
		if (port.found)
			obuf_rollback_to_svp(out, &port.svp);
		iproto_reply_error(out, e, ireq->header.sync);
	}
}

static void
iproto_process_admin(struct iproto_request *ireq)
{
	struct iobuf *iobuf = ireq->iobuf;
	struct iproto_connection *con = ireq->connection;

	auto scope_guard = make_scoped_guard([=]{
		/* Discard request (see iproto_enqueue_batch()) */
		iobuf->in.pos += ireq->total_len;

		if (evio_is_active(&con->output)) {
			if (! ev_is_active(&con->output))
				ev_feed_event(con->loop,
					      &con->output,
					      EV_WRITE);
		} else if (iproto_connection_is_idle(con)) {
			iproto_connection_delete(con);
		}
	});

	if (unlikely(! evio_is_active(&con->output)))
		return;

	try {
		switch (ireq->header.type) {
		case IPROTO_PING:
			iproto_reply_ping(&ireq->iobuf->out,
					  ireq->header.sync);
			break;
		case IPROTO_JOIN:
			replication_join(con->input.fd, &ireq->header);
			/* TODO: check requests in `con; queue */
			iproto_connection_shutdown(con);
			return;
		case IPROTO_SUBSCRIBE:
			replication_subscribe(con->input.fd, &ireq->header);
			/* TODO: check requests in `con; queue */
			iproto_connection_shutdown(con);
			return;
		default:
			tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
				   (uint32_t) ireq->header.type);
		}
	} catch (ClientError *e) {
		say_error("admin command error: %s", e->errmsg());
		iproto_reply_error(&iobuf->out, e, ireq->header.sync);
	}
}

static struct iproto_request *
iproto_request_new(struct iproto_connection *con,
		   iproto_request_f process)
{
	struct iproto_request *ireq =
		(struct iproto_request *) mempool_alloc(&iproto_request_pool);
	ireq->connection = con;
	ireq->iobuf = con->iobuf[0];
	ireq->session = con->session;
	ireq->process = process;
	return ireq;
}

const char *
iproto_greeting(const char *salt)
{
	static __thread char greeting[IPROTO_GREETING_SIZE + 1];
	char base64buf[SESSION_SEED_SIZE * 4 / 3 + 5];

	base64_encode(salt, SESSION_SEED_SIZE, base64buf, sizeof(base64buf));
	snprintf(greeting, sizeof(greeting),
		 "Tarantool %-20s %-32s\n%-63s\n",
		 tarantool_version(), custom_proc_title, base64buf);
	return greeting;
}

/**
 * Handshake a connection: invoke the on-connect trigger
 * and possibly authenticate. Try to send the client an error
 * upon a failure.
 */
static void
iproto_process_connect(struct iproto_request *request)
{
	struct iproto_connection *con = request->connection;
	struct iobuf *iobuf = request->iobuf;
	int fd = con->input.fd;
	try {              /* connect. */
		con->session = session_create(fd, con->cookie);
		coio_write(&con->input, iproto_greeting(con->session->salt),
			   IPROTO_GREETING_SIZE);
		fiber_set_session(fiber(), con->session);
		session_run_on_connect_triggers(con->session);
		/* Set session user to guest, until it is authenticated. */
		session_set_user(con->session, GUEST, GUEST);
	} catch (ClientError *e) {
		iproto_reply_error(&iobuf->out, e, request->header.type);
		try {
			iproto_flush(iobuf, fd, &con->write_pos);
		} catch (Exception *e) {
			e->log();
		}
		iproto_connection_close(con);
		return;
	} catch (Exception *e) {
		e->log();
		assert(con->session == NULL);
		iproto_connection_close(con);
		return;
	}
	/*
	 * Connect is synchronous, so no one could have been
	 * messing up with the connection while it was in
	 * progress.
	 */
	assert(evio_is_active(&con->input));
	/* Handshake OK, start reading input. */
	ev_feed_event(con->loop, &con->input, EV_READ);
}

static void
iproto_process_disconnect(struct iproto_request *request)
{
	/* Runs the trigger, which may yield. */
	iproto_connection_delete(request->connection);
}

/** }}} */

/**
 * Create a connection context and start input.
 */
static void
iproto_on_accept(struct evio_service * /* service */, int fd,
		 struct sockaddr *addr, socklen_t addrlen)
{
	char name[SERVICE_NAME_MAXLEN];
	snprintf(name, sizeof(name), "%s/%s", "iobuf",
		sio_strfaddr(addr, addrlen));

	struct iproto_connection *con;

	con = iproto_connection_new(name, fd, addr);
	/*
	 * Ignore request allocation failure - the queue size is
	 * fixed so there is a limited number of requests in
	 * use, all stored in just a few blocks of the memory pool.
	 */
	struct iproto_request *ireq =
		iproto_request_new(con, iproto_process_connect);
	iproto_queue_push(&request_queue, ireq);
}

static void on_bind(void *arg __attribute__((unused)))
{
	fiber_call(fiber_new("leave_local_hot_standby",
			     (fiber_func) box_leave_local_standby_mode));
}

/** Initialize a read-write port. */
void
iproto_init(const char *uri)
{
	/* Run a primary server. */
	if (!uri)
		return;

	static struct evio_service primary;
	evio_service_init(loop(), &primary, "primary",
			  uri,
			  iproto_on_accept, NULL);
	evio_service_on_bind(&primary, on_bind, NULL);
	evio_service_start(&primary);

	mempool_create(&iproto_request_pool, &cord()->slabc,
		       sizeof(struct iproto_request));
	iproto_queue_init(&request_queue);
	mempool_create(&iproto_connection_pool, &cord()->slabc,
		       sizeof(struct iproto_connection));
}

/* vim: set foldmethod=marker */
