#ifndef INCLUDES_TARANTOOL_BOX_PORT_H
#define INCLUDES_TARANTOOL_BOX_PORT_H
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
#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;

/**
 * A single port represents a destination of box_process output.
 * One such destination can be a Lua stack, or the binary
 * protocol.
 * An instance of a port is usually short lived, as it is created
 * for every server request. State of the instance is represented
 * by the tuples added to it. E.g.:
 *
 * struct port_iproto *port = port_iproto_new(...)
 * for (tuple in tuples)
 *	port_add_tuple(tuple);
 *
 * Beginning with Tarantool 1.5, tuple can have different internal
 * structure and port_add_tuple() requires a double
 * dispatch: first, by the type of the port the tuple is being
 * added to, second, by the type of the tuple format, since the
 * format defines the internal structure of the tuple.
 */

struct port_entry {
	struct port_entry *next;
	struct tuple *tuple;
};

struct port {
	size_t size;
	struct port_entry *first;
	struct port_entry *last;
	struct port_entry first_entry;
};

void
port_create(struct port *port);

/**
 * Unref all tuples and free allocated memory
 */
void
port_destroy(struct port *port);

void
port_dump(struct port *port, struct obuf *out);

void
port_add_tuple(struct port *port, struct tuple *tuple);

void
port_init(void);

void
port_free(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_PORT_H */
