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
#include "exception.h"
#include <string.h>

#include <errcode.h>
#include <fiber.h>
#include <say.h>
#include "coio_buf.h"
#include "tbuf.h"
#include "box/box.h"
#include "box/port.h"
#include "box/tuple.h"
#include "box/request.h"

#include <stdint.h>
#include <stdarg.h>

struct tbuf;
struct obuf;

enum {
	/** Maximal iproto package body length (2GiB) */
	IPROTO_BODY_LEN_MAX = 2147483648,
};

/*
 * struct iproto_header and struct iproto_header_retcode
 * share common prefix {msg_code, len, sync}
 */

struct iproto_header {
	uint32_t msg_code;
	uint32_t len;
	uint32_t sync;
	uint8_t data[];
} __attribute__((packed));

struct iproto_header_retcode {
	uint32_t msg_code;
	uint32_t len;
	uint32_t sync;
	uint32_t ret_code;
} __attribute__((packed));


const uint32_t msg_ping = 0xff00;

static inline struct iproto_header *
iproto(const void *pos)
{
	return (struct iproto_header *) pos;
}

struct port_iproto
{
	struct port_vtab *vtab;
	struct obuf *buf;
	/** Number of found tuples. */
	u32 found;
	void *pfound;
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
	if (port->found == 0)
		obuf_dup(port->buf, &port->found, sizeof(port->found));
	else
		memcpy(port->pfound, &port->found, sizeof(port->found));
}

static void
port_iproto_add_tuple(struct port *ptr, struct tuple *tuple, u32 flags)
{
	struct port_iproto *port = port_iproto(ptr);
	if (++port->found == 1) {
		/* Found the first tuple, add tuple count. */
		port->pfound = obuf_book(port->buf, sizeof(port->found));
	}
	if (flags & BOX_RETURN_TUPLE) {
		obuf_dup(port->buf, &tuple->bsize, tuple_len(tuple));
	}
}

static struct port_vtab port_iproto_vtab = {
	port_iproto_add_tuple,
	port_iproto_eof,
};

struct port *
port_iproto_create(struct obuf *buf)
{
	struct port_iproto *port = palloc(fiber->gc_pool, sizeof(struct port_iproto));
	port->vtab = &port_iproto_vtab;
	port->buf = buf;
	port->found = 0;
	port->pfound = NULL;
	return (struct port *) port;
}

static void
iproto_reply(mod_process_func callback, struct obuf *out, void *req);

static void
iproto_validate_header(struct iproto_header *header);

inline static void
iproto_flush(struct coio *coio, struct iobuf *iobuf, ssize_t to_read)
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
	struct coio coio = va_arg(ap, struct coio);
	struct iobuf *iobuf = va_arg(ap, struct iobuf *);
	mod_process_func *callback = va_arg(ap, mod_process_func *);
	struct ibuf *in = &iobuf->in;
	ssize_t to_read = sizeof(struct iproto_header);
	@try {
		for (;;) {
			if (to_read > 0 && coio_bread(&coio, in, to_read) <= 0)
				break;

			/* validating iproto package header */
			iproto_validate_header(iproto(in->pos));

			ssize_t request_len = sizeof(struct iproto_header)
				+ iproto(in->pos)->len;
			to_read = request_len - ibuf_size(in);

			iproto_flush(&coio, iobuf, to_read);

			if (to_read > 0 && coio_bread(&coio, in, to_read) <= 0)
				break;

			iproto_reply(*callback, &iobuf->out, in->pos);
			in->pos += request_len;

			to_read = sizeof(struct iproto_header) - ibuf_size(in);
			iproto_flush(&coio, iobuf, to_read);
		}
	} @finally {
		coio_close(&coio);
		iobuf_destroy(iobuf);
	}
}

/** Stack a reply to a single request to the fiber's io vector. */

static void
iproto_reply(mod_process_func callback, struct obuf *out, void *req)
{
	struct iproto_header_retcode reply;

	reply.msg_code = iproto(req)->msg_code;
	reply.sync = iproto(req)->sync;

	if (unlikely(reply.msg_code == msg_ping)) {
		reply.len = 0;
		obuf_dup(out, &reply, sizeof(struct iproto_header));
		return;
	}
	reply.len = sizeof(uint32_t); /* ret_code */

	void *p_reply = obuf_book(out, sizeof(struct iproto_header_retcode));
	struct obuf_svp svp = obuf_create_svp(out);

	/* make request point to iproto data */
	struct tbuf request = {
		.size = iproto(req)->len, .capacity = iproto(req)->len,
		.data = iproto(req)->data, .pool = fiber->gc_pool
	};

	@try {
		callback(port_iproto_create(out), reply.msg_code, &request);
		reply.ret_code = 0;
	}
	@catch (ClientError *e) {
		obuf_rollback_to_svp(out, &svp);
		reply.ret_code = tnt_errcode_val(e->errcode);
		obuf_dup(out, e->errmsg, strlen(e->errmsg)+1);
	}
	reply.len += obuf_size(out) - svp.size;
	memcpy(p_reply, &reply, sizeof(struct iproto_header_retcode));
}

static void
iproto_validate_header(struct iproto_header *header)
{
	if (header->len > IPROTO_BODY_LEN_MAX) {
		/*
		 * The package is too big, just close connection for now to
		 * avoid DoS.
		 */
		say_error("received package is too big: %llu",
			  (unsigned long long)header->len);
		tnt_raise(FiberCancelException);
	}
}

void
iproto_init(const char *bind_ipaddr, int primary_port,
	    int secondary_port)
{
	/* run primary server */
	if (primary_port != 0) {
		static struct coio_service primary;
		coio_service_init(&primary, "primary",
				  bind_ipaddr, primary_port,
				  iproto_interact, &mod_process);
		evio_service_on_bind(&primary.evio_service,
				     mod_leave_local_standby_mode, NULL);
		evio_service_start(&primary.evio_service);
	}

	/* run secondary server */
	if (secondary_port != 0) {
		static struct coio_service secondary;
		coio_service_init(&secondary, "secondary",
				  bind_ipaddr, secondary_port,
				  iproto_interact, &mod_process_ro);
		evio_service_start(&secondary.evio_service);
	}
}

