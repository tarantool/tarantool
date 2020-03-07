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
#include "port.h"
#include "tuple.h"
#include "tuple_convert.h"
#include <small/obuf.h>
#include <small/slab_cache.h>
#include <small/mempool.h>
#include <fiber.h>
#include "errinj.h"

/**
 * The pool is used both by port_c and port_tuple, since their
 * entires are almost of the same size. Also port_c can use
 * objects from the pool to store result data in their memory,
 * when it fits.
 */
static struct mempool port_entry_pool;

enum {
	PORT_ENTRY_SIZE = MAX(sizeof(struct port_c_entry),
			      sizeof(struct port_tuple_entry)),
};

int
port_tuple_add(struct port *base, struct tuple *tuple)
{
	struct port_tuple *port = port_tuple(base);
	struct port_tuple_entry *e;
	if (port->size == 0) {
		tuple_ref(tuple);
		e = &port->first_entry;
		port->first = port->last = e;
	} else {
		e = mempool_alloc(&port_entry_pool);
		if (e == NULL) {
			diag_set(OutOfMemory, sizeof(*e), "mempool_alloc", "e");
			return -1;
		}
		tuple_ref(tuple);
		port->last->next = e;
		port->last = e;
	}
	e->tuple = tuple;
	e->next = NULL;
	++port->size;
	return 0;
}

void
port_tuple_create(struct port *base)
{
	struct port_tuple *port = (struct port_tuple *)base;
	port->vtab = &port_tuple_vtab;
	port->size = 0;
	port->first = NULL;
	port->last = NULL;
}

static void
port_tuple_destroy(struct port *base)
{
	struct port_tuple *port = port_tuple(base);
	struct port_tuple_entry *e = port->first;
	if (e == NULL)
		return;
	tuple_unref(e->tuple);
	e = e->next;
	while (e != NULL) {
		struct port_tuple_entry *cur = e;
		e = e->next;
		tuple_unref(cur->tuple);
		mempool_free(&port_entry_pool, cur);
	}
}

static int
port_tuple_dump_msgpack_16(struct port *base, struct obuf *out)
{
	struct port_tuple *port = port_tuple(base);
	struct port_tuple_entry *pe;
	for (pe = port->first; pe != NULL; pe = pe->next) {
		if (tuple_to_obuf(pe->tuple, out) != 0)
			return -1;
		ERROR_INJECT(ERRINJ_PORT_DUMP, {
			diag_set(OutOfMemory, tuple_size(pe->tuple), "obuf_dup",
				 "data");
			return -1;
		});
	}
	return port->size;
}

static int
port_tuple_dump_msgpack(struct port *base, struct obuf *out)
{
	struct port_tuple *port = port_tuple(base);
	char *size_buf = obuf_alloc(out, mp_sizeof_array(port->size));
	if (size_buf == NULL) {
		diag_set(OutOfMemory, mp_sizeof_array(port->size), "obuf_alloc",
			 "size_buf");
		return -1;
	}
	mp_encode_array(size_buf, port->size);
	if (port_tuple_dump_msgpack_16(base, out) < 0)
		return -1;
	return 1;
}

extern void
port_tuple_dump_lua(struct port *base, struct lua_State *L, bool is_flat);

static inline void
port_c_destroy_entry(struct port_c_entry *pe)
{
	/*
	 * See port_c_add_*() for algorithm of how and where to
	 * store data, to understand why it is freed differently.
	 */
	if (pe->mp_size == 0)
		tuple_unref(pe->tuple);
	else if (pe->mp_size <= PORT_ENTRY_SIZE)
		mempool_free(&port_entry_pool, pe->mp);
	else
		free(pe->mp);
}

static void
port_c_destroy(struct port *base)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe = port->first;
	if (pe == NULL)
		return;
	port_c_destroy_entry(pe);
	/*
	 * Port->first is skipped, it is pointing at
	 * port_c.first_entry, and is freed together with the
	 * port.
	 */
	pe = pe->next;
	while (pe != NULL) {
		struct port_c_entry *cur = pe;
		pe = pe->next;
		port_c_destroy_entry(cur);
		mempool_free(&port_entry_pool, cur);
	}
}

static inline struct port_c_entry *
port_c_new_entry(struct port_c *port)
{
	struct port_c_entry *e;
	if (port->size == 0) {
		e = &port->first_entry;
		port->first = e;
		port->last = e;
	} else {
		e = mempool_alloc(&port_entry_pool);
		if (e == NULL) {
			diag_set(OutOfMemory, sizeof(*e), "mempool_alloc", "e");
			return NULL;
		}
		port->last->next = e;
		port->last = e;
	}
	e->next = NULL;
	++port->size;
	return e;
}

int
port_c_add_tuple(struct port *base, struct tuple *tuple)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe = port_c_new_entry(port);
	if (pe == NULL)
		return -1;
	/* 0 mp_size means the entry stores a tuple. */
	pe->mp_size = 0;
	pe->tuple = tuple;
	tuple_ref(tuple);
	return 0;
}

int
port_c_add_mp(struct port *base, const char *mp, const char *mp_end)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe;
	assert(mp_end > mp);
	uint32_t size = mp_end - mp;
	char *dst;
	if (size <= PORT_ENTRY_SIZE) {
		/*
		 * Alloc on a mempool is several times faster than
		 * on the heap. And it perfectly fits any
		 * MessagePack number, a short string, a boolean.
		 */
		dst = mempool_alloc(&port_entry_pool);
		if (dst == NULL) {
			diag_set(OutOfMemory, size, "mempool_alloc", "dst");
			return -1;
		}
	} else {
		dst = malloc(size);
		if (dst == NULL) {
			diag_set(OutOfMemory, size, "malloc", "dst");
			return -1;
		}
	}
	pe = port_c_new_entry(port);
	if (pe != NULL) {
		memcpy(dst, mp, size);
		pe->mp = dst;
		pe->mp_size = size;
		return 0;
	}
	if (size <= PORT_ENTRY_SIZE)
		mempool_free(&port_entry_pool, dst);
	else
		free(dst);
	return -1;
}

static int
port_c_dump_msgpack_16(struct port *base, struct obuf *out)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe;
	for (pe = port->first; pe != NULL; pe = pe->next) {
		uint32_t size = pe->mp_size;
		if (size == 0) {
			if (tuple_to_obuf(pe->tuple, out) != 0)
				return -1;
		} else if (obuf_dup(out, pe->mp, size) != size) {
			diag_set(OutOfMemory, size, "obuf_dup", "data");
			return -1;
		}
		ERROR_INJECT(ERRINJ_PORT_DUMP, {
			diag_set(OutOfMemory,
				 size == 0 ? tuple_size(pe->tuple) : size,
				 "obuf_dup", "data");
			return -1;
		});
	}
	return port->size;
}

static int
port_c_dump_msgpack(struct port *base, struct obuf *out)
{
	struct port_c *port = (struct port_c *)base;
	char *size_buf = obuf_alloc(out, mp_sizeof_array(port->size));
	if (size_buf == NULL) {
		diag_set(OutOfMemory, mp_sizeof_array(port->size), "obuf_alloc",
			 "size_buf");
		return -1;
	}
	mp_encode_array(size_buf, port->size);
	if (port_c_dump_msgpack_16(base, out) < 0)
		return -1;
	return 1;
}

extern void
port_c_dump_lua(struct port *port, struct lua_State *L, bool is_flat);

extern struct sql_value *
port_c_get_vdbemem(struct port *base, uint32_t *size);

static const struct port_vtab port_c_vtab = {
	.dump_msgpack = port_c_dump_msgpack,
	.dump_msgpack_16 = port_c_dump_msgpack_16,
	.dump_lua = port_c_dump_lua,
	.dump_plain = NULL,
	.get_msgpack = NULL,
	.get_vdbemem = port_c_get_vdbemem,
	.destroy = port_c_destroy,
};

void
port_c_create(struct port *base)
{
	struct port_c *port = (struct port_c *)base;
	port->vtab = &port_c_vtab;
	port->first = NULL;
	port->last = NULL;
	port->size = 0;
}

void
port_init(void)
{
	mempool_create(&port_entry_pool, &cord()->slabc, PORT_ENTRY_SIZE);
}

void
port_free(void)
{
	mempool_destroy(&port_entry_pool);
}

const struct port_vtab port_tuple_vtab = {
	.dump_msgpack = port_tuple_dump_msgpack,
	.dump_msgpack_16 = port_tuple_dump_msgpack_16,
	.dump_lua = port_tuple_dump_lua,
	.dump_plain = NULL,
	.get_msgpack = NULL,
	.get_vdbemem = NULL,
	.destroy = port_tuple_destroy,
};
