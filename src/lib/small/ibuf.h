#ifndef TARANTOOL_SMALL_IBUF_H_INCLUDED
#define TARANTOOL_SMALL_IBUF_H_INCLUDED
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
#include <stddef.h>
#include <assert.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** @module Input buffer. */

struct slab_cache;

/*
 * Continuous piece of memory to store input.
 * Allocated in factors of 'start_capacity'.
 * Maintains position of the data "to be processed".
 *
 * Typical use case:
 *
 * struct ibuf *in;
 * coio_bread(coio, in, request_len);
 * if (ibuf_size(in) >= request_len) {
 *	process_request(in->rpos, request_len);
 *	in->rpos += request_len;
 * }
 */
struct ibuf
{
	struct slab_cache *slabc;
	char *buf;
	/** Start of input. */
	char *rpos;
	/** End of useful input */
	char *wpos;
	/** End of buffer. */
	char *end;
	size_t start_capacity;
};

void
ibuf_create(struct ibuf *ibuf, struct slab_cache *slabc, size_t start_capacity);

void
ibuf_destroy(struct ibuf *ibuf);

void
ibuf_reinit(struct ibuf *ibuf);

/** How much data is read and is not parsed yet. */
static inline size_t
ibuf_used(struct ibuf *ibuf)
{
	assert(ibuf->wpos >= ibuf->rpos);
	return ibuf->wpos - ibuf->rpos;
}

/** How much data can we fit beyond buf->wpos */
static inline size_t
ibuf_unused(struct ibuf *ibuf)
{
	assert(ibuf->wpos <= ibuf->end);
	return ibuf->end - ibuf->wpos;
}

/** How much memory is allocated */
static inline size_t
ibuf_capacity(struct ibuf *ibuf)
{
	return ibuf->end - ibuf->buf;
}

/**
 * Integer value of the position in the buffer - stable
 * in case of realloc.
 */
static inline size_t
ibuf_pos(struct ibuf *ibuf)
{
	assert(ibuf->buf <= ibuf->rpos);
	return ibuf->rpos - ibuf->buf;
}

/** Forget all cached input. */
static inline void
ibuf_reset(struct ibuf *ibuf)
{
	ibuf->rpos = ibuf->wpos = ibuf->buf;
}

void *
ibuf_reserve_nothrow_slow(struct ibuf *ibuf, size_t size);

static inline void *
ibuf_reserve_nothrow(struct ibuf *ibuf, size_t size)
{
	if (ibuf->wpos + size <= ibuf->end)
		return ibuf->wpos;
	return ibuf_reserve_nothrow_slow(ibuf, size);
}

static inline void *
ibuf_alloc_nothrow(struct ibuf *ibuf, size_t size)
{
	void *ptr;
	if (ibuf->wpos + size <= ibuf->end)
		ptr = ibuf->wpos;
	else {
		ptr = ibuf_reserve_nothrow_slow(ibuf, size);
		if (ptr == NULL)
			return NULL;
	}
	ibuf->wpos += size;
	return ptr;
}

#if defined(__cplusplus)
} /* extern "C" */

#include "exception.h"

/** Reserve space for sz bytes in the input buffer. */
static inline void *
ibuf_reserve(struct ibuf *ibuf, size_t size)
{
	void *ptr = ibuf_reserve_nothrow(ibuf, size);
	if (ptr == NULL)
		tnt_raise(OutOfMemory, size, "ibuf", "reserve");
	return ptr;
}

static inline void *
ibuf_reserve_cb(void *ptr, size_t *size)
{
	struct ibuf *b = (struct ibuf*) ptr;
	size_t s = *size;
	return ibuf_reserve(b, s);
}

static inline void *
ibuf_alloc_cb(void *ptr, size_t size)
{
	struct ibuf *b = (struct ibuf*) ptr;
	return ibuf_alloc_nothrow(b, size);
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_SMALL_IBUF_H_INCLUDED */
