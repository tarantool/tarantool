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
#include "mp_tuple.h"
#include "tweaks.h"
#include "box/mp_box_ctx.h"

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
		e = xmempool_alloc(&port_entry_pool);
		port->last->next = e;
		port->last = e;
	}
	e->next = NULL;
	e->mp_format = NULL;
	++port->size;
	return e;
}

void
port_c_add_tuple(struct port *base, struct tuple *tuple)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe = port_c_new_entry(port);
	assert(pe != NULL);
	/* 0 mp_size means the entry stores a tuple. */
	pe->mp_size = 0;
	pe->tuple = tuple;
	tuple_ref(tuple);
}

/**
 * Helper function of port_c_add_mp etc.
 * Allocate a buffer of given size and add it to new entry of given port.
 * Never fails.
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
		dst = xmempool_alloc(&port_entry_pool);
	} else {
		dst = xmalloc(size);
	}
	pe = port_c_new_entry(port);
	assert(pe != NULL);
	pe->mp = dst;
	pe->mp_size = size;
	return dst;
}

void
port_c_add_mp(struct port *base, const char *mp, const char *mp_end)
{
	assert(mp_end > mp);
	uint32_t mp_size = mp_end - mp;
	char *dst = port_c_prepare_mp(base, mp_size);
	memcpy(dst, mp, mp_end - mp);
}

void
port_c_add_formatted_mp(struct port *base, const char *mp, const char *mp_end,
			struct tuple_format *format)
{
	port_c_add_mp(base, mp, mp_end);
	struct port_c *port = (struct port_c *)base;
	port->last->mp_format = format;
	tuple_format_ref(format);
}

void
port_c_add_str(struct port *base, const char *str, uint32_t len)
{
	uint32_t mp_size = mp_sizeof_str(len);
	char *dst = port_c_prepare_mp(base, mp_size);
	mp_encode_str(dst, str, len);
}

/**
 * Dumps port contents as a sequence of MsgPack object to mpstream (without
 * array header), mpstream is flushed.
 * If ctx is passed, it must be instance of mp_box_ctx, and all tuples are
 * dumped as MP_EXT and their formats are added to the ctx. Otherwise, all
 * tuples are dumped as MP_ARRAY.
 */
static void
port_c_dump_msgpack_impl(struct port *base, struct mpstream *stream,
			 struct mp_ctx *ctx)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe;
	for (pe = port->first; pe != NULL; pe = pe->next) {
		uint32_t size = pe->mp_size;
		if (size == 0) {
			if (ctx != NULL) {
				tuple_to_mpstream_as_ext(pe->tuple, stream);
				struct mp_box_ctx *box_ctx =
					mp_box_ctx_check(ctx);
				tuple_format_map_add_format(
					&box_ctx->tuple_format_map,
					pe->tuple->format_id);
			} else {
				uint32_t size;
				const char *data =
					tuple_data_range(pe->tuple, &size);
				mpstream_memcpy(stream, data, size);
			}
		} else {
			mpstream_memcpy(stream, pe->mp, size);
		}
	}
	mpstream_flush(stream);
}

static int
port_c_dump_msgpack(struct port *base, struct obuf *out, struct mp_ctx *ctx)
{
	struct port_c *port = (struct port_c *)base;
	struct mpstream stream;
	mpstream_init(&stream, out, obuf_reserve_cb, obuf_alloc_cb,
		      mpstream_panic_cb, NULL);
	port_c_dump_msgpack_impl(base, &stream, ctx);
	return port->size;
}

/**
 * If set, don't wrap results into an additional array on encode.
 */
static bool c_func_iproto_multireturn = true;
TWEAK_BOOL(c_func_iproto_multireturn);

void
port_c_dump_msgpack_wrapped(struct port *base, struct obuf *out,
			    struct mp_ctx *ctx)
{
	struct port_c *port = (struct port_c *)base;
	struct mpstream stream;
	mpstream_init(&stream, out, obuf_reserve_cb, obuf_alloc_cb,
		      mpstream_panic_cb, NULL);
	mpstream_encode_array(&stream, port->size);
	port_c_dump_msgpack_impl(base, &stream, ctx);
}

/**
 * Encode the C port results to the msgpack.
 * If c_func_iproto_multireturn is disabled, encode them without
 * wrapping them in the additional msgpack array.
 */
static int
port_c_dump_msgpack_compatible(struct port *base, struct obuf *out,
			       struct mp_ctx *ctx)
{
	if (c_func_iproto_multireturn) {
		return port_c_dump_msgpack(base, out, ctx);
	} else {
		port_c_dump_msgpack_wrapped(base, out, ctx);
		/* One element (the array) was dumped. */
		return 1;
	}
}

const char *
port_c_get_msgpack(struct port *base, uint32_t *size)
{
	struct port_c *port = (struct port_c *)base;
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	struct mpstream stream;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      mpstream_panic_cb, NULL);
	mpstream_encode_array(&stream, port->size);
	port_c_dump_msgpack_impl(base, &stream, NULL);
	*size = region_used(region) - used;
	const char *res = xregion_join(region, *size);
	mp_tuple_assert(res, res + *size);
	return res;
}

extern void
port_c_dump_lua(struct port *port, struct lua_State *L,
		enum port_dump_lua_mode mode);

extern struct Mem *
port_c_get_vdbemem(struct port *base, uint32_t *size);

const struct port_vtab port_c_vtab = {
	.dump_msgpack = port_c_dump_msgpack_compatible,
	.dump_msgpack_16 = port_c_dump_msgpack,
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
