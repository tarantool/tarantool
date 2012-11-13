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

#include "coio_buf.h"
#include "exception.h"
#include "errcode.h"
#include "fiber.h"
#include "say.h"
#include "tbuf.h"
#include "box/box.h"
#include "box/port.h"
#include "box/tuple.h"
#include "box/request.h"

#include <stdint.h>
#include <stdarg.h>

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

/* {{{ struct port_iproto */

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
 *   sent to the client
 *
 * To ensure this, port_iproto must be used only in
 * atomic manner, i.e. once first port_add_tuple() is done,
 * there can be no yields until port_eof().
 */
struct port_iproto
{
	struct port_vtab *vtab;
	struct obuf *buf;
	struct iproto_reply_header reply;
	void *p_reply;
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
		memcpy(port->p_reply, &port->reply, sizeof(port->reply));
	}
}

static void
port_iproto_add_tuple(struct port *ptr, struct tuple *tuple, u32 flags)
{
	struct port_iproto *port = port_iproto(ptr);
	if (++port->reply.found == 1) {
		/* Found the first tuple, add header. */
		port->svp = obuf_create_svp(port->buf);
		port->p_reply = obuf_book(port->buf, sizeof(port->reply));
	}
	if (flags & BOX_RETURN_TUPLE) {
		obuf_dup(port->buf, &tuple->bsize, tuple_len(tuple));
	}
}

static struct port_vtab port_iproto_vtab = {
	port_iproto_add_tuple,
	port_iproto_eof,
};

struct port_iproto *
port_iproto_create(struct obuf *buf, struct iproto_header *req)
{
	struct port_iproto *port = palloc(fiber->gc_pool, sizeof(struct port_iproto));
	port->vtab = &port_iproto_vtab;
	port->buf = buf;
	port->reply.hdr = *req;
	port->reply.found = 0;
	port->reply.ret_code = 0;
	return port;
}

/* }}} */

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

static void
iproto_reply(mod_process_func callback, struct obuf *out,
	     struct iproto_header *header);

inline static void
iproto_flush(struct ev_io *coio, struct iobuf *iobuf, ssize_t to_read)
{
	/*
	 * Flush output and garbage collect before reading
	 * next header.
	 */
	if (to_read > 0) {
		iobuf_flush(iobuf, coio);
		fiber_gc();
	}
}

void
iproto_interact(va_list ap)
{
	struct ev_io coio = va_arg(ap, struct ev_io);
	struct iobuf *iobuf = va_arg(ap, struct iobuf *);
	mod_process_func *callback = va_arg(ap, mod_process_func *);
	struct ibuf *in = &iobuf->in;
	ssize_t to_read = sizeof(struct iproto_header);
	@try {
		for (;;) {
			if (to_read > 0 && coio_bread(&coio, in, to_read) <= 0)
				break;

			/* validating iproto package header */
			iproto_validate_header(iproto(in->pos), coio.fd);

			ssize_t request_len = sizeof(struct iproto_header)
				+ iproto(in->pos)->len;
			to_read = request_len - ibuf_size(in);

			iproto_flush(&coio, iobuf, to_read);

			if (to_read > 0 && coio_bread(&coio, in, to_read) <= 0)
				break;

			iproto_reply(*callback, &iobuf->out, iproto(in->pos));
			in->pos += request_len;

			to_read = sizeof(struct iproto_header) - ibuf_size(in);
			iproto_flush(&coio, iobuf, to_read);
		}
	} @finally {
		evio_close(&coio);
		iobuf_destroy(iobuf);
	}
}


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
iproto_reply(mod_process_func callback, struct obuf *out,
	     struct iproto_header *header)
{

	if (header->msg_code == msg_ping)
		return iproto_reply_ping(out, header);

	/* Make request body point to iproto data */
	struct tbuf body = {
		.size = header->len, .capacity = header->len,
		.data = (char *) &header[1], .pool = fiber->gc_pool
	};
	struct port_iproto *port = port_iproto_create(out, header);
	@try {
		callback((struct port *) port, header->msg_code, &body);
	} @catch (ClientError *e) {
		if (port->reply.found)
			obuf_rollback_to_svp(out, &port->svp);
		iproto_reply_error(out, header, e);
	}
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
		static struct coio_service primary;
		coio_service_init(&primary, "primary",
				  bind_ipaddr, primary_port,
				  iproto_interact, &mod_process);
		evio_service_on_bind(&primary.evio_service,
				     mod_leave_local_standby_mode, NULL);
		evio_service_start(&primary.evio_service);
	}

	/* Run a secondary server. */
	if (secondary_port != 0) {
		static struct coio_service secondary;
		coio_service_init(&secondary, "secondary",
				  bind_ipaddr, secondary_port,
				  iproto_interact, &mod_process_ro);
		evio_service_start(&secondary.evio_service);
	}
}

