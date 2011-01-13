/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef TARANTOOL_TBUF_H
#define TARANTOOL_TBUF_H

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <util.h>

struct tbuf {
	u32 len;
	u32 size;
	void *data;
	struct palloc_pool *pool;
};

struct tbuf *tbuf_alloc(struct palloc_pool *pool);

void tbuf_ensure_resize(struct tbuf *e, size_t bytes_required);
static inline void tbuf_ensure(struct tbuf *e, size_t required)
{
	assert(e->len <= e->size);
	if (unlikely(e->size - e->len < required))
		tbuf_ensure_resize(e, required);
}

static inline void tbuf_append(struct tbuf *b, const void *data, size_t len)
{
	tbuf_ensure(b, len + 1);
	memcpy(b->data + b->len, data, len);
	b->len += len;
	*(((char *)b->data) + b->len) = '\0';
}

struct tbuf *tbuf_clone(struct palloc_pool *pool, const struct tbuf *orig);
struct tbuf *tbuf_split(struct tbuf *e, size_t at);
size_t tbuf_reserve(struct tbuf *b, size_t count);
void tbuf_reset(struct tbuf *b);
void *tbuf_peek(struct tbuf *b, size_t count);

void tbuf_append_field(struct tbuf *b, void *f);
void tbuf_vprintf(struct tbuf *b, const char *format, va_list ap)
	__attribute__ ((format(FORMAT_PRINTF, 2, 0)));
void tbuf_printf(struct tbuf *b, const char *format, ...)
	__attribute__ ((format(FORMAT_PRINTF, 2, 3)));

char *tbuf_to_hex(const struct tbuf *x);
#endif
