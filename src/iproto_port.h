#ifndef TARANTOOL_IPROTO_PORT_H_INCLUDED
#define TARANTOOL_IPROTO_PORT_H_INCLUDED
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
#include "box/box.h"
#include "box/request.h"
#include "box/port.h"
#include "box/tuple.h"
#include "iobuf.h"
#include "msgpuck/msgpuck.h"
#include "iproto_constants.h"

/**
 * struct iproto_port users need to be careful to:
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
 * To ensure this, iproto_port must be used only in
 * atomic manner, i.e. once first port_add_tuple() is done,
 * there can be no yields until port_eof().
 */
struct iproto_port
{
	struct port_vtab *vtab;
	/** Output buffer. */
	struct obuf *buf;
	/** Reply header. */
	uint64_t sync;
	uint32_t found;
	/** A pointer in the reply buffer where the reply starts. */
	struct obuf_svp svp;
	/** Size of data written after reply starts */
	uint32_t size;
};

extern struct port_vtab iproto_port_vtab;

static inline void
iproto_port_init(struct iproto_port *port, struct obuf *buf,
		 uint64_t sync)
{
	port->vtab = &iproto_port_vtab;
	port->buf = buf;
	port->sync = sync;
	port->found = 0;
	port->size = 0;
}

/** Stack a reply to 'ping' packet. */
void
iproto_reply_ping(struct obuf *out, uint64_t sync);

/** Send an error packet back. */
void
iproto_reply_error(struct obuf *out, const ClientError *e, uint64_t sync);

#endif /* TARANTOOL_IPROTO_PORT_H_INCLUDED */
