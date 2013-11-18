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
#include "tbuf.h"

#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "lib/small/region.h"

#ifdef POISON
#  define TBUF_POISON
#endif

#ifdef TBUF_POISON
#  define poison(ptr, len) memset((ptr), 'A', (len))
#else
#  define poison(ptr, len)
#endif

/** Try to make all region allocations a multiple of  this number. */
enum { TBUF_ALLOC_FACTOR = 128 };

static void
tbuf_assert(const struct tbuf *b)
{
	(void)b;		/* arg used :-) */
	assert(b->size <= b->capacity);
}

struct tbuf *
tbuf_new(struct region *pool)
{
	struct tbuf *e = (struct tbuf *) region_alloc_nothrow(pool, TBUF_ALLOC_FACTOR);
	e->size = 0;
	e->capacity = TBUF_ALLOC_FACTOR - sizeof(struct tbuf);
	e->data = (char *)e + sizeof(struct tbuf);
	e->pool = pool;
	poison(e->data, e->capacity);
	tbuf_assert(e);
	return e;
}

void
tbuf_ensure_resize(struct tbuf *e, size_t required)
{
	tbuf_assert(e);

	/* Make new capacity a multiple of alloc factor. */
	size_t new_capacity = MAX(e->capacity, (uint32_t)TBUF_ALLOC_FACTOR) * 2;

	while (new_capacity < e->size + required)
		new_capacity *= 2;

	char *p = (char *) region_alloc_nothrow(e->pool, new_capacity);

	poison(p, new_capacity);
	memcpy(p, e->data, e->size);
	poison(e->data, e->size);
	e->data = p;
	e->capacity = new_capacity;
	tbuf_assert(e);
}

struct tbuf *
tbuf_clone(struct region *pool, const struct tbuf *orig)
{
	struct tbuf *clone = tbuf_new(pool);
	tbuf_assert(orig);
	tbuf_append(clone, orig->data, orig->size);
	return clone;
}

struct tbuf *
tbuf_split(struct tbuf *orig, size_t at)
{
	struct tbuf *head = (struct tbuf *) region_alloc_nothrow(orig->pool, sizeof(*orig));
	assert(at <= orig->size);
	tbuf_assert(orig);
	head->pool = orig->pool;
	head->data = orig->data;
	head->size = head->capacity = at;
	orig->data += at;
	orig->capacity -= at;
	orig->size -= at;
	return head;
}

void *
tbuf_peek(struct tbuf *b, size_t count)
{
	void *p = b->data;
	tbuf_assert(b);
	if (count <= b->size) {
		b->data += count;
		b->size -= count;
		b->capacity -= count;
		return p;
	}
	return NULL;
}

/** Remove first count bytes from the beginning. */

void
tbuf_ltrim(struct tbuf *b, size_t count)
{
	tbuf_assert(b);
	assert(count <= b->size);

	memmove(b->data, b->data + count, b->size - count);
	b->size -= count;
}

void
tbuf_reset(struct tbuf *b)
{
	tbuf_assert(b);
	poison(b->data, b->size);
	b->size = 0;
}

void
tbuf_vprintf(struct tbuf *b, const char *format, va_list ap)
{
	int printed_len;
	size_t free_len = b->capacity - b->size;
	va_list ap_copy;

	va_copy(ap_copy, ap);

	tbuf_assert(b);
	printed_len = vsnprintf(b->data + b->size, free_len, format, ap);

	/*
	 * if buffer too short, resize buffer and
	 * print it again
	 */
	if (free_len <= printed_len) {
		tbuf_ensure(b, printed_len + 1);
		free_len = b->capacity - b->size - 1;
		printed_len = vsnprintf(b->data + b->size, free_len, format, ap_copy);
	}

	b->size += printed_len;

	va_end(ap_copy);
}

void
tbuf_printf(struct tbuf *b, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	tbuf_vprintf(b, format, args);
	va_end(args);
}

/* for debug printing */
char *
tbuf_to_hex(const struct tbuf *x)
{
	const char *data = x->data;
	size_t size = x->size;
	char *out = (char *) region_alloc_nothrow(x->pool, size * 3 + 1);
	out[size * 3] = 0;

	for (int i = 0; i < size; i++) {
		int c = *(data + i);
		sprintf(out + i * 3, "%02x ", c);
	}

	return out;
}
