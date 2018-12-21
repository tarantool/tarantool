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
#include <port.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;

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

/** Port for storing the result of a Lua CALL/EVAL. */
struct port_lua {
	const struct port_vtab *vtab;
	/** Lua state that stores the result. */
	struct lua_State *L;
	/** Reference to L in tarantool_L. */
	int ref;
	/** The argument of port_dump */
	struct obuf *out;
	/** Number of entries dumped to the port. */
	int size;
};

static_assert(sizeof(struct port_lua) <= sizeof(struct port),
	      "sizeof(struct port_lua) must be <= sizeof(struct port)");

/** Initialize a port to dump from Lua. */
void
port_lua_create(struct port *port, struct lua_State *L);

void
port_init(void);

void
port_free(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined __cplusplus */

#endif /* INCLUDES_TARANTOOL_BOX_PORT_H */
