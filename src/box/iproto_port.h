#ifndef TARANTOOL_IPROTO_PORT_H_INCLUDED
#define TARANTOOL_IPROTO_PORT_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct obuf;
struct obuf_svp;

int
iproto_prepare_select(struct obuf *buf, struct obuf_svp *svp);

void
iproto_reply_select(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		    uint32_t count);
#if defined(__cplusplus)
} /*  extern "C" */

#include "box.h"
#include "request.h"
#include "port.h"
#include "tuple.h"
#include "iobuf.h"
#include "msgpuck/msgpuck.h"

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
	struct port base;
	/** Output buffer. */
	struct obuf *buf;
	/** Reply header. */
	uint64_t sync;
	uint32_t found;
	/** A pointer in the reply buffer where the reply starts. */
	struct obuf_svp svp;
	/** Size of data written after reply starts */
};

extern struct port_vtab iproto_port_vtab;

static inline void
iproto_port_init(struct iproto_port *port, struct obuf *buf,
		 uint64_t sync)
{
	port->base.vtab = &iproto_port_vtab;
	port->buf = buf;
	port->sync = sync;
	port->found = 0;
}

/** Stack a reply to 'ping' packet. */
void
iproto_reply_ok(struct obuf *out, uint64_t sync);

/** Send an error packet back. */
void
iproto_reply_error(struct obuf *out, const struct error *e, uint64_t sync);


#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_IPROTO_PORT_H_INCLUDED */
