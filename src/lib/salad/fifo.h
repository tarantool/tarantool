#ifndef TARANTOOL_LIB_SALAD_FIFO_H_INCLUDED
#define TARANTOOL_LIB_SALAD_FIFO_H_INCLUDED
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
#include <stdlib.h>

#define FIFO_WATERMARK (512 * sizeof(void*))

/** A simple FIFO made using a ring buffer */

struct fifo {
	char *buf;
	size_t bottom; /* advanced by batch free */
	size_t top;
	size_t size;   /* total buffer size */
};

static inline int
fifo_create(struct fifo *q, size_t size)
{
	q->size = size;
	q->bottom = 0;
	q->top = 0;
	q->buf = (char*)malloc(size);
	return (q->buf == NULL ? -1 : 0);
}

static inline void
fifo_destroy(struct fifo *q)
{
	if (q->buf) {
		free(q->buf);
		q->buf = NULL;
	}
}

static inline int
fifo_size(struct fifo *q)
{
	return (q->top - q->bottom) / sizeof(void*);
}

#ifndef unlikely
# define unlikely __builtin_expect(!! (EXPR), 0)
#endif

static inline int
fifo_push(struct fifo *q, void *ptr)
{
	/* reduce memory allocation and memmove
	 * effect by reusing free pointers buffer space only after the
	 * watermark frees reached. */
	if (unlikely(q->bottom >= FIFO_WATERMARK)) {
		memmove(q->buf, q->buf + q->bottom, q->bottom);
		q->top -= q->bottom;
		q->bottom = 0;
	}
	if (unlikely((q->top + sizeof(void*)) > q->size)) {
		size_t newsize = q->size * 2;
		char *ptr = (char*)realloc((void*)q->buf, newsize);
		if (unlikely(ptr == NULL))
			return -1;
		q->buf = ptr;
		q->size = newsize;
	}
	memcpy(q->buf + q->top, (char*)&ptr, sizeof(ptr));
	q->top += sizeof(void*);
	return 0;
}

static inline void *
fifo_pop(struct fifo *q) {
	if (unlikely(q->bottom == q->top))
		return NULL;
	void *ret = *(void**)(q->buf + q->bottom);
	q->bottom += sizeof(void*);
	return ret;
}

#undef FIFO_WATERMARK

#endif /* TARANTOOL_LIB_SALAD_FIFO_H_INCLUDED */
