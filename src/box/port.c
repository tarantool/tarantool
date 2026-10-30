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
 * result data when it fits into an object from the pool. Also,
 * it is used to store `struct port_c_iterator`.
 */
static struct mempool port_entry_pool;

/**
 * The pool is used by port_c to allocate mp_ctx objects because
 * they are too large to store them right in entries.
 */
static struct mempool port_mp_ctx_pool;

enum {
	PORT_ENTRY_SIZE = sizeof(struct port_c_entry),
	PORT_MP_CTX_SIZE = sizeof(struct mp_ctx),
};

static_assert(PORT_MP_CTX_SIZE > PORT_ENTRY_SIZE,
	      "There would be no reason to create a separate mempool "
	      "if mp_ctx fitted into port_c_entry");

static void *
port_c_data_xalloc(size_t size)
{
	assert(size > 0);
	void *ptr = NULL;
	/*
	 * Alloc on a mempool is several times faster than
	 * on the heap. And it perfectly fits small
	 * MessagePack packets and strings.
	 */
	if (size <= PORT_ENTRY_SIZE)
		ptr = xmempool_alloc(&port_entry_pool);
	else if (size <= PORT_MP_CTX_SIZE)
		ptr = xmempool_alloc(&port_mp_ctx_pool);
	else
		ptr = xmalloc(size);
	return ptr;
}

static void
port_c_data_free(void *ptr, size_t size)
{
	assert(size > 0);
	if (size <= PORT_ENTRY_SIZE)
		mempool_free(&port_entry_pool, ptr);
	else if (size <= PORT_MP_CTX_SIZE)
		mempool_free(&port_mp_ctx_pool, ptr);
	else
		free(ptr);
}

static inline void
port_c_destroy_entry(struct port_c_entry *pe)
{
	/*
	 * See port_c_add_*() for algorithm of how and where to store data,
	 * to understand why it is freed differently.
	 */
	switch (pe->type) {
	case PORT_C_ENTRY_TUPLE:
		tuple_unref(pe->tuple);
		break;
	case PORT_C_ENTRY_STR: {
		size_t size = pe->str.size;
		if (size > 0) {
			void *data = (void *)pe->str.data;
			port_c_data_free(data, size);
		}
		break;
	}
	case PORT_C_ENTRY_MP_OBJECT:
	case PORT_C_ENTRY_MP: {
		size_t size = pe->mp.size;
		assert(size > 0);
		void *data = (void *)pe->mp.data;
		port_c_data_free(data, size);
		if (pe->type == PORT_C_ENTRY_MP_OBJECT &&
		    pe->mp.ctx != NULL) {
			mp_ctx_destroy(pe->mp.ctx);
			mempool_free(&port_mp_ctx_pool,
				     pe->mp.ctx);
		}
		if (pe->type == PORT_C_ENTRY_MP &&
		    pe->mp.format != NULL) {
			tuple_format_unref(pe->mp.format);
		}
		break;
	}
	default:
		break;
	}
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
	++port->size;
	return e;
}

void
port_c_add_tuple(struct port *base, struct tuple *tuple)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe = port_c_new_entry(port);
	assert(pe != NULL);
	pe->type = PORT_C_ENTRY_TUPLE;
	pe->tuple = tuple;
	tuple_ref(tuple);
}

/**
 * Appends raw MsgPack to the port, it is copied.
 * Returns the new entry containing MsgPack, never fails.
 */
static struct port_c_entry *
port_c_add_mp_impl(struct port *base, const char *mp, const char *mp_end)
{
	assert(mp_end > mp);
	struct port_c *port = (struct port_c *)base;
	size_t size = mp_end - mp;
	struct port_c_entry *pe = port_c_new_entry(port);
	assert(pe != NULL);
	char *data = port_c_data_xalloc(size);
	memcpy(data, mp, size);
	pe->mp.data = data;
	pe->mp.size = size;
	pe->mp.format = NULL;
	pe->type = PORT_C_ENTRY_MP;
	return pe;
}

void
port_c_add_mp(struct port *base, const char *mp, const char *mp_end)
{
	port_c_add_mp_impl(base, mp, mp_end);
}

void
port_c_add_formatted_mp(struct port *base, const char *mp, const char *mp_end,
			struct tuple_format *format)
{
	struct port_c_entry *pe = port_c_add_mp_impl(base, mp, mp_end);
	pe->mp.format = format;
	tuple_format_ref(format);
}

void
port_c_add_mp_object(struct port *base, const char *mp, const char *mp_end,
		     struct mp_ctx *ctx)
{
	struct port_c_entry *pe = port_c_add_mp_impl(base, mp, mp_end);
	pe->type = PORT_C_ENTRY_MP_OBJECT;
	struct mp_ctx *mp_ctx = NULL;
	if (ctx != NULL) {
		mp_ctx = xmempool_alloc(&port_mp_ctx_pool);
		mp_ctx_copy(mp_ctx, ctx);
	}
	pe->mp.ctx = mp_ctx;
}

void
port_c_add_str(struct port *base, const char *data, size_t len)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe;
	pe = port_c_new_entry(port);
	assert(pe != NULL);
	pe->type = PORT_C_ENTRY_STR;
	char *str = NULL;
	/*
	 * Don't copy an empty string. Anyway, it is handy to have
	 * a valid address as a "beginning" of the string - let's use
	 * address of the entry for this purpose.
	 */
	if (len == 0) {
		str = (char *)pe;
	} else {
		void *ptr = port_c_data_xalloc(len);
		memcpy(ptr, data, len);
		str = ptr;
	}
	pe->str.data = str;
	pe->str.size = len;
}

void
port_c_add_null(struct port *base)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe = port_c_new_entry(port);
	pe->type = PORT_C_ENTRY_NULL;
}

void
port_c_add_bool(struct port *base, bool val)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe = port_c_new_entry(port);
	pe->type = PORT_C_ENTRY_BOOL;
	pe->boolean = val;
}

void
port_c_add_number(struct port *base, double val)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe = port_c_new_entry(port);
	pe->type = PORT_C_ENTRY_NUMBER;
	pe->number = val;
}

void
port_c_add_iterable(struct port *base, void *data,
		    port_c_iterator_create_f create)
{
	struct port_c *port = (struct port_c *)base;
	struct port_c_entry *pe = port_c_new_entry(port);
	pe->type = PORT_C_ENTRY_ITERABLE;
	pe->iterable.data = data;
	pe->iterable.iterator_create = create;
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
		switch (pe->type) {
		/** Encode unsupported types as nil. */
		case PORT_C_ENTRY_ITERABLE:
		case PORT_C_ENTRY_NULL:
			mpstream_encode_nil(stream);
			break;
		case PORT_C_ENTRY_NUMBER:
			mpstream_encode_double(stream, pe->number);
			break;
		case PORT_C_ENTRY_BOOL:
			mpstream_encode_bool(stream, pe->boolean);
			break;
		case PORT_C_ENTRY_STR:
			mpstream_encode_strn(stream, pe->str.data,
					     pe->str.size);
			break;
		case PORT_C_ENTRY_TUPLE: {
			struct tuple *tuple = pe->tuple;
			if (ctx != NULL) {
				tuple_to_mpstream_as_ext(tuple, stream);
				struct mp_box_ctx *box_ctx =
					mp_box_ctx_check(ctx);
				tuple_format_map_add_format(
					&box_ctx->tuple_format_map,
					tuple->format_id);
			} else {
				uint32_t size;
				const char *data =
					tuple_data_range(tuple, &size);
				mpstream_memcpy(stream, data, size);
				break;
			}
			break;
		}
		case PORT_C_ENTRY_MP_OBJECT:
		case PORT_C_ENTRY_MP: {
			size_t size = pe->mp.size;
			mpstream_memcpy(stream, pe->mp.data, size);
			break;
		}
		default:
			unreachable();
		};
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

const struct port_c_entry *
port_c_get_c_entries(struct port *base)
{
	struct port_c *port = (struct port_c *)base;
	return port->first;
}

const struct port_vtab port_c_vtab = {
	.dump_msgpack = port_c_dump_msgpack_compatible,
	.dump_msgpack_16 = port_c_dump_msgpack,
	.dump_lua = port_c_dump_lua,
	.dump_plain = NULL,
	.get_msgpack = port_c_get_msgpack,
	.get_vdbemem = port_c_get_vdbemem,
	.get_c_entries = port_c_get_c_entries,
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
	mempool_create(&port_mp_ctx_pool, &cord()->slabc, PORT_MP_CTX_SIZE);
}

void
port_free(void)
{
	mempool_destroy(&port_entry_pool);
	mempool_destroy(&port_mp_ctx_pool);
}
