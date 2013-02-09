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

#include "tarantool.h"
#include "exception.h"
#include "errcode.h"
#include "fiber.h"
#include "say.h"
#include "tbuf.h"
#include "box/box.h"
#include "box/port.h"
#include "box/tuple.h"
#include "box/request.h"
#include "iobuf.h"
#include "evio.h"
#include "session.h"

enum {
	/** Maximal iproto package body length (2GiB) */
	IPROTO_BODY_LEN_MAX = 2147483648,
};

/*
 * struct iproto_header and struct iproto_reply_header
 * share common prefix {msg_code, len, sync}
 */

struct iproto_header {
	uint32_t msg_code;
	uint32_t len;
	uint32_t sync;
} __attribute__((packed));

static struct iproto_header dummy_header = { 0, 0, 0 };

struct iproto_reply_header {
	struct iproto_header hdr;
	uint32_t ret_code;
	uint32_t found;
}  __attribute__((packed));

const uint32_t msg_ping = 0xff00;

static inline struct iproto_header *
iproto(const void *pos)
{
	return (struct iproto_header *) pos;
}

/* {{{ port_iproto */

/**
 * struct port_iproto users need to be careful to:
 * - not unwind output of other fibers when
 *   rolling back to a savepoint (provided that
 *   multiple fibers work on the same session),
 * - not increment write position before there is a complete
 *   response, i.e. a response which will not be rolled back
 *   and which has a complete header.
 * - never increment write position without having
 *   a complete response. Otherwise a situation can occur
 *   when many requests started processing, but completed
 *   in a different order, and thus incomplete output is
 *   sent to the client.
 *
 * To ensure this, port_iproto must be used only in
 * atomic manner, i.e. once first port_add_tuple() is done,
 * there can be no yields until port_eof().
 */
struct port_iproto
{
	struct port_vtab *vtab;
	/** Output buffer. */
	struct obuf *buf;
	/** Reply header. */
	struct iproto_reply_header reply;
	/** A pointer in the reply buffer where the reply starts. */
	struct obuf_svp svp;
};

static inline struct port_iproto *
port_iproto(struct port *port)
{
	return (struct port_iproto *) port;
}

static void
port_iproto_eof(struct port *ptr)
{
	struct port_iproto *port = port_iproto(ptr);
	/* found == 0 means add_tuple wasn't called at all. */
	if (port->reply.found == 0) {
		port->reply.hdr.len = sizeof(port->reply) -
			sizeof(port->reply.hdr);
		obuf_dup(port->buf, &port->reply, sizeof(port->reply));
	} else {
		port->reply.hdr.len = obuf_size(port->buf) - port->svp.size -
			sizeof(port->reply.hdr);
		memcpy(obuf_svp_to_ptr(port->buf, &port->svp),
		       &port->reply, sizeof(port->reply));
	}
}

static void
port_iproto_add_tuple(struct port *ptr, struct tuple *tuple, u32 flags)
{
	struct port_iproto *port = port_iproto(ptr);
	if (++port->reply.found == 1) {
		/* Found the first tuple, add header. */
		port->svp = obuf_book(port->buf, sizeof(port->reply));
	}
	if (flags & BOX_RETURN_TUPLE) {
		obuf_dup(port->buf, &tuple->bsize, tuple_len(tuple));
	}
}

static struct port_vtab port_iproto_vtab = {
	port_iproto_add_tuple,
	port_iproto_eof,
};

static void
port_iproto_init(struct port_iproto *port, struct obuf *buf,
		 struct iproto_header *req)
{
	port->vtab = &port_iproto_vtab;
	port->buf = buf;
	port->reply.hdr = *req;
	port->reply.found = 0;
	port->reply.ret_code = 0;
}

/* }}} */

/* {{{ iproto_queue */

struct iproto_request;

/**
 * Implementation of an input queue of the box request processor.
 *
 * Socket event handlers read data, determine request boundaries
 * and enqueue requests. Once all input/output events are
 * processed, an own event handler is invoked to deal with the
 * requests in the queue: it's important that each request is
 * processed in a fiber environment.
 *
 * @sa iproto_queue_schedule, iproto_handler, iproto_handshake
 */

struct iproto_queue
{
	/**
	 * Ring buffer of fixed size, preallocated
	 * during initialization.
	 */
	struct iproto_request *queue;
	/**
	 * Main function of the fiber invoked to handle
	 * all outstanding tasks in this queue.
	 */
	void (*handler)(va_list);
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

enum {
	IPROTO_REQUEST_QUEUE_SIZE = 2048,
	IPROTO_CONNECT_QUEUE_SIZE = 256
};

struct iproto_session;

typedef void (*iproto_request_f)(struct iproto_request *);

/**
 * A single request from the client. All requests
 * from all clients are queued into a single queue
 * and processed in FIFO order.
 */
struct iproto_request
{
	struct iproto_session *session;
	struct iobuf *iobuf;
	/* Position of the request in the input buffer. */
	struct iproto_header *header;
	iproto_request_f process;
};

/**
 * A single global queue for all requests in all connections. All
 * requests are processed concurrently.
 * Is also used as a queue for just established connections and to
 * execute disconnect triggers. A few notes about these triggers:
 * - they need to be run in a fiber
 * - unlike an ordinary request failure, on_connect trigger
 *   failure must lead to connection shutdown.
 * - as long as on_connect trigger can be used for client
 *   authentication, it must be processed before any other request
 *   on this connection.
 */
static struct iproto_queue request_queue;

static inline bool
iproto_queue_is_empty(struct iproto_queue *i_queue)
{
	return i_queue->begin == i_queue->end;
}

static inline void
iproto_enqueue_request(struct iproto_queue *i_queue,
		       struct iproto_session *session,
		       struct iobuf *iobuf,
		       struct iproto_header *header,
		       iproto_request_f process)
{
	/* If the queue is full, invoke the handler to work it off. */
	if (i_queue->end == i_queue->size)
		ev_invoke(&i_queue->watcher, EV_CUSTOM);
	assert(i_queue->end < i_queue->size);
	bool was_empty = iproto_queue_is_empty(i_queue);
	struct iproto_request *request = i_queue->queue + i_queue->end++;

	request->session = session;
	request->iobuf = iobuf;
	request->header = header;
	request->process = process;
	/*
	 * There were some queued requests, ensure they are
	 * handled.
	 */
	if (was_empty)
		ev_feed_event(&request_queue.watcher, EV_CUSTOM);
}

static inline bool
iproto_dequeue_request(struct iproto_queue *i_queue,
		       struct iproto_request *out)
{
	if (i_queue->begin == i_queue->end)
		return false;
	struct iproto_request *request = i_queue->queue + i_queue->begin++;
	if (i_queue->begin == i_queue->end)
		i_queue->begin = i_queue->end = 0;
	*out = *request;
	return true;
}

/** Put the current fiber into a queue fiber cache. */
static inline void
iproto_cache_fiber(struct iproto_queue *i_queue)
{
	fiber_gc();
	rlist_add_entry(&i_queue->fiber_cache, fiber, state);
	fiber_yield();
}

/** Create fibers to handle all outstanding tasks. */
static void
iproto_queue_schedule(struct ev_async *watcher,
		      int events __attribute__((unused)))
{
	struct iproto_queue *i_queue = watcher->data;
	while (! iproto_queue_is_empty(i_queue)) {

		struct fiber *f = rlist_shift_entry(&i_queue->fiber_cache,
						    struct fiber, state);
		if (f == NULL)
			f = fiber_new("iproto", i_queue->handler);
		fiber_call(f, i_queue);
	}
}

static inline void
iproto_queue_init(struct iproto_queue *i_queue,
		  int size, void (*handler)(va_list))
{
	i_queue->size = size;
	i_queue->begin = i_queue->end = 0;
	i_queue->queue = palloc(eter_pool, size *
				sizeof (struct iproto_request));
	/**
	 * Initialize an ev_async event which would start
	 * workers for all outstanding tasks.
	 */
	ev_async_init(&i_queue->watcher, iproto_queue_schedule);
	i_queue->watcher.data = i_queue;
	i_queue->handler = handler;
	rlist_create(&i_queue->fiber_cache);
}

/** A handler to process all queued requests. */
static void
iproto_queue_handler(va_list ap)
{
	struct iproto_queue *i_queue = va_arg(ap, struct iproto_queue *);
	struct iproto_request request;
restart:
	while (iproto_dequeue_request(i_queue, &request)) {

		request.process(&request);
	}
	iproto_cache_fiber(&request_queue);
	goto restart;
}

/* }}} */

/* {{{ iproto_session */

/** Context of a single client connection. */
struct iproto_session
{
	/* Cache of iproto_session objects. */
	SLIST_ENTRY(iproto_session) next_in_cache;
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
	box_process_func *handler;
	struct ev_io input;
	struct ev_io output;
	/** Session id. */
	uint32_t sid;
};

SLIST_HEAD(, iproto_session) iproto_session_cache =
	SLIST_HEAD_INITIALIZER(iproto_session_cache);

/**
 * A session is idle when the client is gone
 * and there are no outstanding requests in the request queue.
 * An idle session can be safely garbage collected.
 */
static inline bool
iproto_session_is_idle(struct iproto_session *session)
{
	return !evio_is_active(&session->input) &&
		ibuf_size(&session->iobuf[0]->in) == 0 &&
		ibuf_size(&session->iobuf[1]->in) == 0;
}

static void
iproto_session_on_input(struct ev_io *watcher,
			int revents __attribute__((unused)));
static void
iproto_session_on_output(struct ev_io *watcher,
			 int revents __attribute__((unused)));

static void
iproto_process_request(struct iproto_request *request);

static void
iproto_process_connect(struct iproto_request *request);

static void
iproto_process_disconnect(struct iproto_request *request);

static struct iproto_session *
iproto_session_create(const char *name, int fd, box_process_func *param)
{
	struct iproto_session *session;
	if (SLIST_EMPTY(&iproto_session_cache)) {
		session = palloc(eter_pool, sizeof(*session));
		session->input.data = session->output.data = session;
	} else {
		session = SLIST_FIRST(&iproto_session_cache);
		SLIST_REMOVE_HEAD(&iproto_session_cache, next_in_cache);
	}
	session->handler = param;
	ev_io_init(&session->input, iproto_session_on_input, fd, EV_READ);
	ev_io_init(&session->output, iproto_session_on_output, fd, EV_WRITE);
	session->iobuf[0] = iobuf_new(name);
	session->iobuf[1] = iobuf_new(name);
	session->parse_size = 0;
	session->write_pos = obuf_create_svp(&session->iobuf[0]->out);
	session->sid = 0;
	return session;
}

/** Recycle a session. Never throws. */
static inline void
iproto_session_destroy(struct iproto_session *session)
{
	assert(iproto_session_is_idle(session));
	session_destroy(session->sid); /* Never throws. No-op if sid is 0. */
	iobuf_delete(session->iobuf[0]);
	iobuf_delete(session->iobuf[1]);
	SLIST_INSERT_HEAD(&iproto_session_cache, session, next_in_cache);
}

static inline void
iproto_session_gc(struct iproto_session *session)
{
	if (iproto_session_is_idle(session))
		iproto_session_destroy(session);
}

static inline void
iproto_session_shutdown(struct iproto_session *session)
{
	ev_io_stop(&session->input);
	ev_io_stop(&session->output);
	close(session->input.fd);
	session->input.fd = session->output.fd = -1;
	/*
	 * Discard unparsed data, to recycle the session
	 * as soon as all parsed data is processed.
	 */
	session->iobuf[0]->in.end -= session->parse_size;
	/*
	 * If the session is not idle it will
	 * be destroyed only after all its requests
	 * are processed (and dropped).
         */
	iproto_enqueue_request(&request_queue, session,
			       session->iobuf[0], &dummy_header,
			       iproto_process_disconnect);
}

static inline void
iproto_validate_header(struct iproto_header *header, int fd)
{
	if (header->len > IPROTO_BODY_LEN_MAX) {
		/*
		 * The package is too big, just close connection for now to
		 * avoid DoS.
		 */
		tnt_raise(SocketError, :fd in:
			  "received package is too big: %llu",
			  (unsigned long long) header->len);
	}
}

/**
 * If there is no space for reading input, we can do one of the
 * following:
 * - try to get a new iobuf, so that it can fit the request.
 *   Always getting a new input buffer when there is no space
 *   makes the server susceptible to input-flood attacks.
 *   Therefore, at most 2 iobufs are used in a single session,
 *   one is "open", receiving input, and the  other is closed,
 *   flushing output.
 * - stop input and wait until the client reads piled up output,
 *   so the input buffer can be reused. This complements
 *   the previous strategy. It is only safe to stop input if it
 *   is known that there is output. In this case input event
 *   flow will be resumed when all replies to previous requests
 *   are sent, in iproto_session_gc_iobuf(). Since there are two
 *   buffers, the input is only stopped when both of them
 *   are fully used up.
 *
 * To make this strategy work, each iobuf in use must fit at
 * least one request. Otherwise, iobuf[1] may end
 * up having no data to flush, while iobuf[0] is too small to
 * fit a big incoming request.
 */
static struct iobuf *
iproto_session_input_iobuf(struct iproto_session *session)
{
	struct iobuf *old = session->iobuf[0];

	ssize_t to_read = sizeof(struct iproto_header) +
		(session->parse_size >= sizeof(struct iproto_header) ?
		iproto(old->in.end - session->parse_size)->len : 0) -
		session->parse_size;

	if (ibuf_unused(&old->in) >= to_read)
		return old;

	/** All requests are procssed, reuse the buffer. */
	if (ibuf_size(&old->in) == session->parse_size) {
		ibuf_reserve(&old->in, to_read);
		return old;
	}

	if (! iobuf_is_idle(session->iobuf[1])) {
		/*
		 * Wait until the second buffer is flushed
		 * and becomes available for reuse.
		 */
		return NULL;
	}
	struct iobuf *new = session->iobuf[1];

	ibuf_reserve(&new->in, to_read + session->parse_size);
	/*
	 * Discard unparsed data in the old buffer, otherwise it
	 * won't be recycled when all parsed requests are processed.
	 */
	old->in.end -= session->parse_size;
	/* Move the cached request prefix to the new buffer. */
	memcpy(new->in.pos, old->in.end, session->parse_size);
	new->in.end += session->parse_size;
	/*
	 * Rotate buffers. Not strictly necessary, but
	 * helps preserve response order.
	 */
	session->iobuf[1] = old;
	session->iobuf[0] = new;
	return new;
}

/** Enqueue all requests which were read up. */
static inline void
iproto_enqueue_batch(struct iproto_session *session, struct ibuf *in, int fd)
{
	int batch_size;
	for (batch_size = 0; ; batch_size++) {

		if (session->parse_size < sizeof(struct iproto_header))
			break;

		struct iproto_header *
			header = iproto(in->end - session->parse_size);
		iproto_validate_header(header, fd);

		if (session->parse_size < (sizeof(struct iproto_header) +
					   header->len))
			break;

		iproto_enqueue_request(&request_queue, session,
				       session->iobuf[0], header,
				       iproto_process_request);
		session->parse_size -= sizeof(*header) + header->len;
	}
}

static void
iproto_session_on_input(struct ev_io *watcher,
			int revents __attribute__((unused)))
{
	struct iproto_session *session = watcher->data;
	int fd = session->input.fd;

	@try {
		/* Ensure we have sufficient space for the next round.  */
		struct iobuf *iobuf = iproto_session_input_iobuf(session);
		if (iobuf == NULL) {
			ev_io_stop(&session->input);
			return;
		}

		struct ibuf *in = &iobuf->in;
		/* Read input. */
		int nrd = sio_read(fd, in->end, ibuf_unused(in));
		if (nrd < 0) {                  /* Socket is not ready. */
			ev_io_start(&session->input);
			return;
		}
		if (nrd == 0) {                 /* EOF */
			iproto_session_shutdown(session);
			return;
		}
		/* Update the read position and session state. */
		in->end += nrd;
		session->parse_size += nrd;
		/* Enqueue all requests which are fully read up. */
		iproto_enqueue_batch(session, in, fd);
	} @catch (tnt_Exception *e) {
		[e log];
		iproto_session_shutdown(session);
	}
}

/** Get the iobuf which is currently being flushed. */
static inline struct iobuf *
iproto_session_output_iobuf(struct iproto_session *session)
{
	if (obuf_size(&session->iobuf[1]->out))
		return session->iobuf[1];
	/*
	 * Don't try to write from a newer buffer if an older one
	 * exists: in case of a partial write of a newer buffer,
	 * the client may end up getting a salad of different
	 * pieces of replies from both buffers.
	 */
	if (ibuf_size(&session->iobuf[1]->in) == 0 &&
	    obuf_size(&session->iobuf[0]->out))
		return session->iobuf[0];
	return NULL;
}

/** writev() to the socket and handle the output. */
static inline int
iproto_flush(struct iobuf *iobuf, int fd, struct obuf_svp *svp)
{
	/* Begin writing from the saved position. */
	struct iovec *iov = iobuf->out.iov + svp->pos;
	int iovcnt = obuf_iovcnt(&iobuf->out) - svp->pos;
	assert(iovcnt);
	ssize_t nwr;
	@try {
		sio_add_to_iov(iov, -svp->iov_len);
		nwr = sio_writev(fd, iov, iovcnt);
	} @finally {
		sio_add_to_iov(iov, svp->iov_len);
	}
	if (nwr > 0) {
		if (svp->size + nwr == obuf_size(&iobuf->out)) {
			iobuf_gc(iobuf);
			*svp = obuf_create_svp(&iobuf->out);
			return 0;
		}
		svp->size += nwr;
		svp->pos += sio_move_iov(iov, nwr, &svp->iov_len);
	}
	return -1;
}

static void
iproto_session_on_output(struct ev_io *watcher,
			 int revent __attribute__((unused)))
{
	struct iproto_session *session = watcher->data;
	int fd = session->output.fd;
	struct obuf_svp *svp = &session->write_pos;

	@try {
		struct iobuf *iobuf;
		while ((iobuf = iproto_session_output_iobuf(session))) {
			if (iproto_flush(iobuf, fd, svp) < 0) {
				ev_io_start(&session->output);
				return;
			}
			if (! ev_is_active(&session->input))
				ev_feed_event(&session->input, EV_READ);
		}
		if (ev_is_active(&session->output))
			ev_io_stop(&session->output);
	} @catch (tnt_Exception *e) {
		[e log];
		iproto_session_shutdown(session);
	}
}

/* }}} */

/* {{{ iproto_process_* functions */

/** Stack reply to 'ping' packet. */
static inline void
iproto_reply_ping(struct obuf *out, struct iproto_header *req)
{
	struct iproto_header reply = *req;
	reply.len = 0;
	obuf_dup(out, &reply, sizeof(reply));
}

/** Send an error packet back. */
static inline void
iproto_reply_error(struct obuf *out, struct iproto_header *req,
		   ClientError *e)
{
	struct iproto_header reply = *req;
	int errmsg_len = strlen(e->errmsg) + 1;
	uint32_t ret_code = tnt_errcode_val(e->errcode);
	reply.len = sizeof(ret_code) + errmsg_len;;
	obuf_dup(out, &reply, sizeof(reply));
	obuf_dup(out, &ret_code, sizeof(ret_code));
	obuf_dup(out, e->errmsg, errmsg_len);
}

/** Stack a reply to a single request to the fiber's io vector. */
static inline void
iproto_reply(struct port_iproto *port, box_process_func callback,
	     struct obuf *out, struct iproto_header *header)
{
	if (header->msg_code == msg_ping)
		return iproto_reply_ping(out, header);

	/* Make request body point to iproto data */
	struct tbuf body = {
		.size = header->len, .capacity = header->len,
		.data = (char *) &header[1], .pool = fiber->gc_pool
	};
	port_iproto_init(port, out, header);
	@try {
		callback((struct port *) port, header->msg_code, &body);
	} @catch (ClientError *e) {
		if (port->reply.found)
			obuf_rollback_to_svp(out, &port->svp);
		iproto_reply_error(out, header, e);
	}
}

static void
iproto_process_request(struct iproto_request *request)
{
	struct iproto_session *session = request->session;
	struct iproto_header *header = request->header;
	struct iobuf *iobuf = request->iobuf;
	struct port_iproto port;
	@try {
		if (unlikely(! evio_is_active(&session->output)))
			return;

		fiber_set_sid(fiber, session->sid);
		iproto_reply(&port, *session->handler,
			     &iobuf->out, header);

		if (unlikely(! evio_is_active(&session->output)))
			return;
		if (! ev_is_active(&session->output))
			ev_feed_event(&session->output, EV_WRITE);
	} @finally {
		iobuf->in.pos += sizeof(*header) + header->len;
		iproto_session_gc(session);
	}
}

/**
 * Handshake a connection: invoke the on-connect trigger
 * and possibly authenticate. Try to send the client an error
 * upon a failure.
 */
static void
iproto_process_connect(struct iproto_request *request)
{
	struct iproto_session *session = request->session;
	struct iobuf *iobuf = request->iobuf;
	int fd = session->input.fd;
	@try {              /* connect. */
		session->sid = session_create(fd);
	} @catch (ClientError *e) {
		iproto_reply_error(&iobuf->out, request->header, e);
		iproto_flush(iobuf, fd, &session->write_pos);
		iproto_session_shutdown(session);
		return;
	} @catch (tnt_Exception *e) {
		[e log];
		assert(session->sid == 0);
		iproto_session_shutdown(session);
		return;
	}
	/* Handshake OK, start reading input. */
	ev_feed_event(&session->input, EV_READ);
}

static void
iproto_process_disconnect(struct iproto_request *request)
{
	fiber_set_sid(fiber, request->session->sid);
	/* Runs the trigger, which may yield. */
	iproto_session_gc(request->session);
}

/** }}} */


/**
 * Create a session context and start input.
 */
static void
iproto_on_accept(struct evio_service *service, int fd,
		 struct sockaddr_in *addr)
{
	char name[SERVICE_NAME_MAXLEN];
	snprintf(name, sizeof(name), "%s/%s", "iobuf", sio_strfaddr(addr));

	struct iproto_session *session;
	session = iproto_session_create(name, fd, service->on_accept_param);
	iproto_enqueue_request(&request_queue, session,
			       session->iobuf[0], &dummy_header,
			       iproto_process_connect);
}

/**
 * Initialize read-write and read-only ports
 * with binary protocol handlers.
 */
void
iproto_init(const char *bind_ipaddr, int primary_port,
	    int secondary_port)
{
	/* Run a primary server. */
	if (primary_port != 0) {
		static struct evio_service primary;
		evio_service_init(&primary, "primary",
				  bind_ipaddr, primary_port,
				  iproto_on_accept, &box_process);
		evio_service_on_bind(&primary,
				     box_leave_local_standby_mode, NULL);
		evio_service_start(&primary);
	}

	/* Run a secondary server. */
	if (secondary_port != 0) {
		static struct evio_service secondary;
		evio_service_init(&secondary, "secondary",
				  bind_ipaddr, secondary_port,
				  iproto_on_accept, &box_process_ro);
		evio_service_start(&secondary);
	}
	iproto_queue_init(&request_queue, IPROTO_REQUEST_QUEUE_SIZE,
			  iproto_queue_handler);
}

