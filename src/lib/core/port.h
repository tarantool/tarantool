#ifndef TARANTOOL_LIB_CORE_PORT_H_INCLUDED
#define TARANTOOL_LIB_CORE_PORT_H_INCLUDED
/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct obuf;
struct lua_State;
struct port;

/**
 * A single port represents a destination of any output. One such
 * destination can be a Lua stack, or the binary protocol. An
 * instance of a port is usually short lived, as it is created
 * per request. Used to virtualize functions which can return
 * directly into Lua or into network.
 */
struct port_vtab {
	/**
	 * Dump the content of a port to an output buffer.
	 * @param port Port to dump.
	 * @param out Buffer to dump to.
	 *
	 * @retval >= 0 Number of entries dumped.
	 * @retval < 0 Error.
	 */
	int (*dump_msgpack)(struct port *port, struct obuf *out);
	/**
	 * Same as dump_msgpack(), but do not add MsgPack array
	 * header. Used by the legacy Tarantool 1.6 format.
	 */
	int (*dump_msgpack_16)(struct port *port, struct obuf *out);
	/**
	 * Dump the content of a port to a given Lua stack.
	 * When is_flat == true is specified, the data is dumped
	 * directly to Lua stack, item-by-item. Otherwise, a
	 * result table is created. The is_flat == true mode
	 * follows Lua functions convention to pass arguments
	 * and return a results, while is_flat == false follows
	 * Tarantool's :select convention when the table of
	 * results is returned.
	 */
	void (*dump_lua)(struct port *port, struct lua_State *L, bool is_flat);
	/**
	 * Dump a port content as a plain text into a buffer,
	 * allocated inside.
	 * @param port Port with data to dump.
	 * @param[out] size Length of a result plain text.
	 *
	 * @retval nil Error.
	 * @retval not nil Plain text.
	 */
	const char *(*dump_plain)(struct port *port, uint32_t *size);
	/**
	 * Get the content of a port as a msgpack data.
	 * By an obsolete convention, C stored routines expect
	 * msgpack data as input format for its arguments.
	 * This API is also usefull to process a function
	 * returned value as msgpack data in memory.
	 * The lifecycle of the returned value is
	 * implementation-specific: it may either be returned
	 * directly from the port, in which case the data will
	 * stay alive as long as the port is alive, or it may be
	 * allocated on the fiber()->gc, in which case the caller
	 * is responsible for cleaning up.
	 **/
	const char *(*get_msgpack)(struct port *port, uint32_t *size);
	/**
	 * Get the content of a port as a sequence of VDBE memory
	 * cells. This API is used to pass VDBE variables to
	 * user-defined Lua functions (see OP_Function in sql/vdbe.c).
	 * The lifecycle of the returned value is the same as for
	 * @get_msgpack method, i.e. it depends on particular
	 * implementation
	 */
	struct sql_value *(*get_vdbemem)(struct port *port, uint32_t *size);
	/** Destroy a port and release associated resources. */
	void (*destroy)(struct port *port);
};

/**
 * Abstract port instance. It is supposed to be converted to
 * a concrete port realization, e.g. port_c.
 */
struct port {
	/** Virtual method table. */
	const struct port_vtab *vtab;
	/**
	 * Implementation dependent content. Needed to declare
	 * an abstract port instance on stack.
	 */
	char pad[60];
};

/** Is not inlined just to be exported. */
void
port_destroy(struct port *port);

static inline int
port_dump_msgpack(struct port *port, struct obuf *out)
{
	return port->vtab->dump_msgpack(port, out);
}

static inline int
port_dump_msgpack_16(struct port *port, struct obuf *out)
{
	return port->vtab->dump_msgpack_16(port, out);
}

static inline void
port_dump_lua(struct port *port, struct lua_State *L, bool is_flat)
{
	port->vtab->dump_lua(port, L, is_flat);
}

static inline const char *
port_dump_plain(struct port *port, uint32_t *size)
{
	return port->vtab->dump_plain(port, size);
}

static inline const char *
port_get_msgpack(struct port *port, uint32_t *size)
{
	return port->vtab->get_msgpack(port, size);
}

static inline struct sql_value *
port_get_vdbemem(struct port *port, uint32_t *size)
{
	return port->vtab->get_vdbemem(port, size);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined __cplusplus */

#endif /* TARANTOOL_LIB_CORE_PORT_H_INCLUDED */
