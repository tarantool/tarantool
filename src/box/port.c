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
#include "mpstream/mpstream.h"

/**
 * The pool is used by port_c to allocate entries and to store
 * result data when it fits into an object from the pool.
 */
static struct mempool port_entry_pool;

enum {
	PORT_ENTRY_SIZE = sizeof(struct port_c_entry),
};

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
	if (pe->mp_format != NULL)
		tuple_format_unref(pe->mp_format);
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
	e->mp_format = NULL;
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

/**
 * Helper function of port_c_add_mp etc.
 * Allocate a buffer of given size and add it to new entry of given port.
 * Return pointer to allocated buffer or NULL in case of error (diag set).
 */
static char *
port_c_prepare_mp(struct port *base, uint32_t size)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe;
	assert(size > 0);
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
			return NULL;
		}
	} else {
		dst = malloc(size);
		if (dst == NULL) {
			diag_set(OutOfMemory, size, "malloc", "dst");
			return NULL;
		}
	}
	pe = port_c_new_entry(port);
	if (pe != NULL) {
		pe->mp = dst;
		pe->mp_size = size;
		return dst;
	}
	if (size <= PORT_ENTRY_SIZE)
		mempool_free(&port_entry_pool, dst);
	else
		free(dst);
	return NULL;
}

int
port_c_add_mp(struct port *base, const char *mp, const char *mp_end)
{
	assert(mp_end > mp);
	uint32_t mp_size = mp_end - mp;
	char *dst = port_c_prepare_mp(base, mp_size);
	if (dst == NULL)
		return -1;
	memcpy(dst, mp, mp_end - mp);
	return 0;
}

int
port_c_add_formatted_mp(struct port *base, const char *mp, const char *mp_end,
			struct tuple_format *format)
{
	int rc = port_c_add_mp(base, mp, mp_end);
	if (rc != 0)
		return rc;
	struct port_c *port = (struct port_c *)base;
	port->last->mp_format = format;
	tuple_format_ref(format);
	return 0;
}

int
port_c_add_str(struct port *base, const char *str, uint32_t len)
{
	uint32_t mp_size = mp_sizeof_str(len);
	char *dst = port_c_prepare_mp(base, mp_size);
	if (dst == NULL)
		return -1;
	mp_encode_str(dst, str, len);
	return 0;
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

/** Callback to forward and error from mpstream methods. */
static inline void
set_encode_error(void *error_ctx)
{
	*(bool *)error_ctx = true;
}

/** Method get_msgpack for struct port_c. */
static const char *
port_c_get_msgpack(struct port *base, uint32_t *size)
{
	struct port_c *port = (struct port_c *)base;
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	bool is_error = false;
	struct mpstream stream;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	mpstream_encode_array(&stream, port->size);
	struct port_c_entry *pe;
	for (pe = port->first; pe != NULL; pe = pe->next) {
		const char *data;
		uint32_t len;
		if (pe->mp_size == 0) {
			data = tuple_data(pe->tuple);
			len = tuple_bsize(pe->tuple);
		} else {
			data = pe->mp;
			len = pe->mp_size;
		}
		mpstream_memcpy(&stream, data, len);
	}
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			 "mpstream_flush", "stream");
		return NULL;
	}
	*size = region_used(region) - used;
	const char *res = region_join(region, *size);
	if (res == NULL) {
		region_truncate(region, used);
		diag_set(OutOfMemory, *size, "region_join", "res");
		return NULL;
	}
	mp_tuple_assert(res, res + *size);
	return res;
}

extern void
port_c_dump_lua(struct port *port, struct lua_State *L, bool is_flat);

extern struct Mem *
port_c_get_vdbemem(struct port *base, uint32_t *size);

const struct port_vtab port_c_vtab = {
	.dump_msgpack = port_c_dump_msgpack,
	.dump_msgpack_16 = port_c_dump_msgpack_16,
	.dump_lua = port_c_dump_lua,
	.dump_plain = NULL,
	.get_msgpack = port_c_get_msgpack,
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
