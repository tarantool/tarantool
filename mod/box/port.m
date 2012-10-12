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
#include "port.h"
#include <pickle.h>
#include <fiber.h>
#include <tarantool_lua.h>
#include "tuple.h"
#include "box_lua.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lj_obj.h"
#include "lj_ctype.h"
#include "lj_cdata.h"
#include "lj_cconv.h"

/*
  For tuples of size below this threshold, when sending a tuple
  to the client, make a deep copy of the tuple for the duration
  of sending rather than increment a reference counter.
  This is necessary to avoid excessive page splits when taking
  a snapshot: many small tuples can be accessed by clients
  immediately after the snapshot process has forked off,
  thus incrementing tuple ref count, and causing the OS to
  create a copy of the memory page for the forked
  child.
*/
const int BOX_REF_THRESHOLD = 8196;

static void
tuple_unref(void *tuple)
{
	tuple_ref((struct tuple *) tuple, -1);
}

void
iov_ref_tuple(struct tuple *tuple)
{
	tuple_ref(tuple, 1);
	fiber_register_cleanup(tuple_unref, tuple);
}

void
port_null_eof(struct port *port __attribute__((unused)))
{
}

static void
port_null_add_tuple(struct port *port __attribute__((unused)),
		    struct tuple *tuple __attribute__((unused)),
		    u32 flags __attribute__((unused)))
{
}

static struct port_vtab port_null_vtab = {
	port_null_add_tuple,
	port_null_eof,
};

struct port port_null = {
	.vtab = &port_null_vtab,
};

struct port_iproto
{
	struct port_vtab *vtab;
	/** Number of found tuples. */
	u32 found;
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
		iov_dup(&port->found, sizeof(port->found));
}

static void
port_iproto_add_tuple(struct port *ptr, struct tuple *tuple, u32 flags)
{
	struct port_iproto *port = port_iproto(ptr);
	if (++port->found == 1) {
		/* Found the first tuple, add tuple count. */
		iov_add(&port->found, sizeof(port->found));
	}
	if (flags & BOX_RETURN_TUPLE) {
		size_t len = tuple_len(tuple);

		if (len > BOX_REF_THRESHOLD) {
			iov_ref_tuple(tuple);
			iov_add(&tuple->bsize, len);
		} else {
			iov_dup(&tuple->bsize, len);
		}
	}
}

static struct port_vtab port_iproto_vtab = {
	port_iproto_add_tuple,
	port_iproto_eof,
};

struct port *
port_iproto_create()
{
	struct port_iproto *port = palloc(fiber->gc_pool, sizeof(struct port_iproto));
	port->vtab = &port_iproto_vtab;
	port->found = 0;
	return (struct port *) port;
}

