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
#include "iproto_port.h"

static inline struct iproto_port *
iproto_port(struct port *port)
{
	return (struct iproto_port *) port;
}

static inline void
iproto_port_eof(struct port *ptr)
{
	struct iproto_port *port = iproto_port(ptr);
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

static inline void
iproto_port_add_tuple(struct port *ptr, struct tuple *tuple, u32 flags)
{
	struct iproto_port *port = iproto_port(ptr);
	if (++port->reply.found == 1) {
		/* Found the first tuple, add header. */
		port->svp = obuf_book(port->buf, sizeof(port->reply));
	}
	if (flags & BOX_RETURN_TUPLE)
		tuple_to_obuf(tuple, port->buf);
}

struct port_vtab iproto_port_vtab = {
	iproto_port_add_tuple,
	iproto_port_eof,
};
