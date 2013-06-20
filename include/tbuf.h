#ifndef TARANTOOL_TBUF_H_INCLUDED
#define TARANTOOL_TBUF_H_INCLUDED
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
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "tarantool/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tbuf {
	/* Used space in the buffer. */
	u32 size;
	/* Total allocated buffer capacity. */
	u32 capacity;
	/* Allocated buffer. */
	char *data;
	struct palloc_pool *pool;
};

struct tbuf *
tbuf_new(struct palloc_pool *pool);

void tbuf_ensure_resize(struct tbuf *e, size_t bytes_required);
static inline void tbuf_ensure(struct tbuf *e, size_t required)
{
	assert(e->size <= e->capacity);
	if (unlikely(e->size + required > e->capacity))
		tbuf_ensure_resize(e, required);
}

static inline void tbuf_append(struct tbuf *b, const void *data, size_t len)
{
	tbuf_ensure(b, len + 1); /* +1 for trailing '\0' */
	memcpy(b->data + b->size, data, len);
	b->size += len;
	*((b->data) + b->size) = '\0';
}

static inline char *
tbuf_str(struct tbuf *tbuf) { return tbuf->data; }

static inline void *
tbuf_end(struct tbuf *tbuf) { return tbuf->data + tbuf->size; }

static inline size_t
tbuf_unused(const struct tbuf *tbuf) { return tbuf->capacity - tbuf->size; }

struct tbuf *tbuf_clone(struct palloc_pool *pool, const struct tbuf *orig);
struct tbuf *tbuf_split(struct tbuf *e, size_t at);
void tbuf_reset(struct tbuf *b);
void *tbuf_peek(struct tbuf *b, size_t count);

/**
 * Remove count bytes from the beginning, and adjust all sizes
 * accordingly.
 *
 * @param    count   the number of bytes to forget about.
 *
 * @pre      0 <= count <= tbuf->len
 */
void tbuf_ltrim(struct tbuf *b, size_t count);

void tbuf_append_field(struct tbuf *b, const void *f);
void tbuf_vprintf(struct tbuf *b, const char *format, va_list ap)
	__attribute__ ((format(FORMAT_PRINTF, 2, 0)));
void tbuf_printf(struct tbuf *b, const char *format, ...)
	__attribute__ ((format(FORMAT_PRINTF, 2, 3)));

char *tbuf_to_hex(const struct tbuf *x);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_TBUF_H_INCLUDED */
