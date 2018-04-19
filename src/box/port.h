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
struct obuf;

/**
 * A single port represents a destination of box_process output.
 * One such destination can be a Lua stack, or the binary
 * protocol.
 * An instance of a port is usually short lived, as it is created
 * for every server request. State of the instance is represented
 * by the tuples added to it. E.g.:
 *
 * struct port port;
 * port_tuple_create(&port);
 * for (tuple in tuples)
 *	port_tuple_add(tuple);
 *
 * port_dump(&port, obuf);
 * port_destroy(&port);
 *
 * Beginning with Tarantool 1.5, tuple can have different internal
 * structure and port_tuple_add() requires a double
 * dispatch: first, by the type of the port the tuple is being
 * added to, second, by the type of the tuple format, since the
 * format defines the internal structure of the tuple.
 */

struct port;

struct port_vtab {
	/**
	 * Dump the content of a port to an output buffer.
	 * On success returns number of entries dumped.
	 * On failure sets diag and returns -1.
	 */
	int (*dump_msgpack)(struct port *port, struct obuf *out);
	/**
	 * Same as dump_msgpack(), but use the legacy Tarantool
	 * 1.6 format.
	 */
	int (*dump_msgpack_16)(struct port *port, struct obuf *out);
	/**
	 * Destroy a port and release associated resources.
	 */
	void (*destroy)(struct port *port);
};

/**
 * Abstract port instance. It is supposed to be converted to
 * a concrete port realization, e.g. port_tuple.
 */
struct port {
	/** Virtual method table. */
	const struct port_vtab *vtab;
	/**
	 * Implementation dependent content. Needed to declare
	 * an abstract port instance on stack.
	 */
	char pad[48];
};

struct port_tuple_entry {
	struct port_tuple_entry *next;
	struct tuple *tuple;
};

/**
 * Port implementation used for storing tuples.
 */
struct port_tuple {
	const struct port_vtab *vtab;
	int size;
	struct port_tuple_entry *first;
	struct port_tuple_entry *last;
	struct port_tuple_entry first_entry;
};
static_assert(sizeof(struct port_tuple) <= sizeof(struct port),
	      "sizeof(struct port_tuple) must be <= sizeof(struct port)");

extern const struct port_vtab port_tuple_vtab;

/**
 * Convert an abstract port instance to a tuple port.
 */
static inline struct port_tuple *
port_tuple(struct port *port)
{
	assert(port->vtab == &port_tuple_vtab);
	return (struct port_tuple *)port;
}

/**
 * Create a port for storing tuples.
 */
void
port_tuple_create(struct port *port);

/**
 * Append a tuple to a port.
 */
int
port_tuple_add(struct port *port, struct tuple *tuple);

/**
 * Destroy an abstract port instance.
 */
void
port_destroy(struct port *port);

/**
 * Dump an abstract port instance to an output buffer.
 * Return number of entries dumped on success, -1 on error.
 */
int
port_dump_msgpack(struct port *port, struct obuf *out);

/**
 * Same as port_dump(), but use the legacy Tarantool 1.6
 * format.
 */
int
port_dump_msgpack_16(struct port *port, struct obuf *out);

void
port_init(void);

void
port_free(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined __cplusplus */

#endif /* INCLUDES_TARANTOOL_BOX_PORT_H */
