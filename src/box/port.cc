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
#include "port.h"
#include "tuple.h"
#include <lib/small/slab_cache.h>
#include <lib/small/mempool.h>
#include <fiber.h>

void
null_port_eof(struct port *port __attribute__((unused)))
{
}

static void
null_port_add_tuple(struct port *port __attribute__((unused)),
		    struct tuple *tuple __attribute__((unused)))
{
}

static struct port_vtab null_port_vtab = {
	null_port_add_tuple,
	null_port_eof,
};

struct port null_port = {
	/* .vtab = */ &null_port_vtab,
};

static struct mempool port_buf_entry_pool;

static void
port_buf_add_tuple(struct port *port, struct tuple *tuple)
{
	struct port_buf *port_buf = (struct port_buf *) port;
	struct port_buf_entry *e;
	if (port_buf->size == 0) {
		tuple_ref(tuple); /* throws */
		e = &port_buf->first_entry;
		port_buf->first = port_buf->last = e;
	} else {
		e = (struct port_buf_entry *)
			mempool_alloc_xc(&port_buf_entry_pool); /* throws */
		try {
			tuple_ref(tuple); /* throws */
		} catch (Exception *) {
			mempool_free(&port_buf_entry_pool, e);
			throw;
		}
		port_buf->last->next = e;
		port_buf->last = e;
	}
	e->tuple = tuple;
	e->next = NULL;
	++port_buf->size;
}

static struct port_vtab port_buf_vtab = {
	port_buf_add_tuple,
	null_port_eof,
};

void
port_buf_create(struct port_buf *port_buf)
{
	port_buf->base.vtab = &port_buf_vtab;
	port_buf->size = 0;
	port_buf->first = NULL;
	port_buf->last = NULL;
}

void
port_buf_destroy(struct port_buf *port_buf)
{
	struct port_buf_entry *e = port_buf->first;
	if (e == NULL)
		return;
	tuple_unref(e->tuple);
	e = e->next;
	while (e != NULL) {
		struct port_buf_entry *cur = e;
		e = e->next;
		tuple_unref(cur->tuple);
		mempool_free(&port_buf_entry_pool, cur);
	}
}

void
port_buf_transfer(struct port_buf *port_buf)
{
	struct port_buf_entry *e = port_buf->first;
	if (e == NULL)
		return;
	e = e->next;
	while (e != NULL) {
		struct port_buf_entry *cur = e;
		e = e->next;
		mempool_free(&port_buf_entry_pool, cur);
	}
}

void
port_init(void)
{
	mempool_create(&port_buf_entry_pool, &cord()->slabc,
		       sizeof(struct port_buf_entry));
}

void
port_free(void)
{
	mempool_destroy(&port_buf_entry_pool);
}
