#ifndef TARANTOOL_SMALL_OBUF_H_INCLUDED
#define TARANTOOL_SMALL_OBUF_H_INCLUDED
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
#include <sys/uio.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum { SMALL_OBUF_IOV_MAX = 31 };

struct slab_cache;

/**
 * Output buffer savepoint. It's possible to
 * save the current buffer state in a savepoint
 * and roll back to the saved state at any time
 * before obuf_reset()
 */
struct obuf_svp
{
	size_t pos;
	size_t iov_len;
	size_t used;
};

/**
 * An output buffer is a vector of struct iovec
 * for writev().
 * Each iovec buffer is allocated using slab allocator.
 * Buffer size grows by a factor of 2. With this growth factor,
 * the number of used buffers is unlikely to ever exceed the
 * hard limit of SMALL_OBUF_IOV_MAX. If it does, an exception is
 * raised.
 */
struct obuf
{
	struct slab_cache *slabc;
	/** Position of the "current" iovec. */
	int pos;
	/* The number of allocated iov instances. */
	int n_iov;
	/* How many bytes are in the buffer. */
	size_t used;
	/**
	 * iov[0] size (allocations are normally a multiple of this number),
	 * but can be larger if a large chunk is requested by
	 * obuf_reserve().
	 */
	size_t start_capacity;
	/** How many bytes are actually allocated for each iovec. */
	size_t capacity[SMALL_OBUF_IOV_MAX + 1];
	/**
	 * List of iovec vectors, each vector is at least twice
	 * as big as the previous one. The vector following the
	 * last allocated one is always zero-initialized
	 * (iov_base = NULL, iov_len = 0).
	 * Make it the last member to reduce friction around
	 * wpos/wend in iproto thread - last elements
	 * of iov are unlikely to be updated often.
	 */
	struct iovec iov[SMALL_OBUF_IOV_MAX + 1];
	/**
	 * The below two members are used by iproto thread,
	 * avoid false sharing by cache aligning them.
	 */
	struct {
		/** Current write position in the output buffer */
		struct obuf_svp wpos;
		/** End of write position in the output buffer */
		struct obuf_svp wend;
	} __attribute__((aligned(64)));
};

void
obuf_create(struct obuf *buf, struct slab_cache *slabc, size_t start_capacity);

void
obuf_destroy(struct obuf *buf);

void
obuf_reset(struct obuf *buf);

/** How many bytes are in the output buffer. */
static inline size_t
obuf_size(struct obuf *obuf)
{
	return obuf->used;
}

static inline size_t
obuf_used(struct obuf *obuf)
{
	return obuf->wend.used - obuf->wpos.used;
}

/** The size of iov vector in the buffer. */
static inline int
obuf_iovcnt(struct obuf *buf)
{
	return buf->iov[buf->pos].iov_len > 0 ? buf->pos + 1 : buf->pos;
}

/**
 * Slow path of obuf_reserve(), which actually reallocates
 * memory and moves data if necessary.
 */
void *
obuf_reserve_slow_nothrow(struct obuf *buf, size_t size);

/**
 * \brief Ensure \a buf to have at least \a size bytes of contiguous memory
 * for write and return a point to this chunk.
 * After write please call obuf_advance(wsize) where wsize <= size to advance
 * a write position.
 * \param buf
 * \param size
 * \return a pointer to contiguous chunk of memory
 */
static inline void *
obuf_reserve_nothrow(struct obuf *buf, size_t size)
{
	if (buf->iov[buf->pos].iov_len + size > buf->capacity[buf->pos])
		return obuf_reserve_slow_nothrow(buf, size);
	struct iovec *iov = &buf->iov[buf->pos];
	return (char *) iov->iov_base + iov->iov_len;
}

/**
 * \brief Advance write position after using obuf_reserve()
 * \param buf
 * \param size
 * \sa obuf_reserve
 */
static inline void *
obuf_alloc_nothrow(struct obuf *buf, size_t size)
{
	struct iovec *iov = &buf->iov[buf->pos];
	void *ptr;
	if (iov->iov_len + size <= buf->capacity[buf->pos]) {
		ptr = (char *) iov->iov_base + iov->iov_len;
	} else {
		ptr = obuf_reserve_slow_nothrow(buf, size);
		if (ptr == NULL)
			return NULL;
		iov = &buf->iov[buf->pos];
		assert(iov->iov_len <= buf->capacity[buf->pos]);
	}
	iov->iov_len += size;
	buf->used += size;
	return ptr;
}

/** Append data to the output buffer. */
size_t
obuf_dup_nothrow(struct obuf *buf, const void *data, size_t size);

static inline size_t
obuf_capacity(struct obuf *buf)
{
	/** This is an approximation, see obuf_alloc_pos() */
	return buf->capacity[buf->n_iov] * 2;
}

static inline struct obuf_svp
obuf_create_svp(struct obuf *buf)
{
	struct obuf_svp svp;
	svp.pos = buf->pos;
	svp.iov_len = buf->iov[buf->pos].iov_len;
	svp.used = buf->used;
	return svp;
}

/** Forget anything added to output buffer after the savepoint. */
void
obuf_rollback_to_svp(struct obuf *buf, struct obuf_svp *svp);

/** Convert a savepoint position to a pointer in the buffer. */
static inline void *
obuf_svp_to_ptr(struct obuf *buf, struct obuf_svp *svp)
{
	return (char *) buf->iov[svp->pos].iov_base + svp->iov_len;
}

#if defined(__cplusplus)
} /* extern "C" */

#include "exception.h"

static inline void *
obuf_reserve(struct obuf *buf, size_t size)
{
	void *ptr = obuf_reserve_nothrow(buf, size);
	if (ptr == NULL)
		tnt_raise(OutOfMemory, size, "obuf", "reserve");
	return ptr;
}

static inline void *
obuf_alloc(struct obuf *buf, size_t size)
{
	void *ptr = obuf_alloc_nothrow(buf, size);
	if (ptr == NULL)
		tnt_raise(OutOfMemory, size, "obuf", "alloc");
	return ptr;
}

static inline void
obuf_dup(struct obuf *buf, const void *data, size_t size)
{
	if (obuf_dup_nothrow(buf, data, size) != size)
		tnt_raise(OutOfMemory, size, "obuf", "dup");
}

extern "C" {

static inline void *
obuf_reserve_cb(void *ctx, size_t *size)
{
	struct obuf *buf = (struct obuf *) ctx;
	void *ptr = obuf_reserve(buf, *size);
	*size = buf->capacity[buf->pos] - buf->iov[buf->pos].iov_len;
	return ptr;
}

static inline void *
obuf_alloc_cb(void *ctx, size_t size)
{
	return obuf_alloc((struct obuf *) ctx, size);
}

} /* extern "C" */

/**
 * Reserve size bytes in the output buffer
 * and return a pointer to the reserved
 * data. Returns a pointer to a continuous piece of
 * memory.
 * Typical use case:
 * struct obuf_svp svp = obuf_book(buf, sizeof(uint32_t));
 * for (...)
 *	obuf_dup(buf, ...);
 * uint32_t total = obuf_used(buf);
 * memcpy(obuf_svp_to_ptr(&svp), &total, sizeof(total);
 */
static inline struct obuf_svp
obuf_book(struct obuf *buf, size_t size)
{
	obuf_reserve(buf, size);
	struct obuf_svp svp = obuf_create_svp(buf);
	obuf_alloc(buf, size);
	return svp;
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_SMALL_OBUF_H_INCLUDED */
