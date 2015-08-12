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
#include "obuf.h"
#include <string.h>

#include "slab_cache.h"

/** Allocate memory for a single iovec buffer. */
static inline void *
obuf_alloc_pos(struct obuf *buf, size_t size)
{
	int pos = buf->pos;
	assert(buf->capacity[pos] == 0 && buf->iov[pos].iov_len == 0);
	assert(pos < SMALL_OBUF_IOV_MAX);
	assert(buf->n_iov == pos);
	/** Initialize the next pos. */
	buf->iov[pos+1] = buf->iov[pos];
	buf->capacity[pos+1] = buf->capacity[pos];
	size_t capacity = buf->start_capacity << pos;
	while (capacity < size) {
		capacity = capacity == 0 ? buf->start_capacity: capacity * 2;
	}
	struct slab *slab = slab_get(buf->slabc, capacity);
	if (slab == NULL)
		return NULL;
	buf->iov[pos].iov_base = slab_data(slab);
	buf->capacity[pos] = slab_capacity(slab);
	buf->n_iov++;
	return buf->iov[pos].iov_base;
}

/**
 * Initialize an output buffer instance. Don't allocate memory
 * yet -- it may never be needed.
 */
void
obuf_create(struct obuf *buf, struct slab_cache *slabc, size_t start_capacity)
{
	buf->slabc = slabc;
	buf->n_iov = 0;
	buf->pos = 0;
	buf->used = 0;
	buf->start_capacity= start_capacity;
	buf->iov[0].iov_base = NULL;
	buf->iov[0].iov_len = 0;
	buf->capacity[0] = 0;
	buf->wend = buf->wpos = obuf_create_svp(buf);
}


/** Mark an output buffer as empty. */
void
obuf_reset(struct obuf *buf)
{
	int iovcnt = obuf_iovcnt(buf);
	for (int i = 0; i < iovcnt; i++)
		buf->iov[i].iov_len = 0;
	buf->pos = 0;
	buf->used = 0;
	buf->wend = buf->wpos = obuf_create_svp(buf);
}

void
obuf_destroy(struct obuf *buf)
{
	for (int i = 0; i < buf->n_iov; i++) {
		struct slab *slab = slab_from_data(buf->iov[i].iov_base);
		slab_put(buf->slabc, slab);
	}
#ifndef NDEBUG
	obuf_create(buf, buf->slabc, buf->start_capacity);
#endif
}

/** Add data to the output buffer. Copies the data. */
size_t
obuf_dup_nothrow(struct obuf *buf, const void *data, size_t size)
{
	struct iovec *iov = &buf->iov[buf->pos];
	size_t capacity = buf->capacity[buf->pos];
	size_t to_copy = size;
	/**
	 * @pre buf->pos points at an array of allocated buffers.
	 * The array ends with a zero-initialized buffer.
	 */
	while (iov->iov_len + to_copy > capacity) {
		/*
		 * The data doesn't fit into this buffer.
		 * It could be because the buffer is not
		 * allocated, is partially or completely full.
		 * Copy as much as possible into already
		 * allocated buffers.
		 */
		if (iov->iov_len < capacity) {
			/*
			 * This buffer is allocated, but can't
			 * fit all the data. Copy as much data as
			 * possible.
			 */
			size_t fill = capacity - iov->iov_len;
			assert(fill < to_copy);
			memcpy((char *) iov->iov_base + iov->iov_len,
			       data, fill);

			iov->iov_len += fill;
			buf->used += fill;
			data = (char *) data + fill;
			to_copy -= fill;
			/*
			 * Check if the remainder can fit
			 * without allocations.
			 */
		} else if (capacity == 0) {
			/**
			 * Still some data to copy. We have to get
			 * a new buffer. Before we allocate
			 * a buffer for this position, ensure
			 * there is an unallocated buffer in the
			 * next one, since it works as an end
			 * marker for the loop above.
			 */
			if (obuf_alloc_pos(buf, to_copy) == NULL)
				return size - to_copy;
			break;
		}
		assert(capacity == iov->iov_len);
		if (buf->pos + 1 >= SMALL_OBUF_IOV_MAX)
			return size - to_copy;
		buf->pos++;
		iov = &buf->iov[buf->pos];
		capacity = buf->capacity[buf->pos];
	}
	memcpy((char *) iov->iov_base + iov->iov_len, data, to_copy);
	iov->iov_len += to_copy;
	buf->used += to_copy;
	assert(iov->iov_len <= buf->capacity[buf->pos]);
	return size;
}

void *
obuf_reserve_slow_nothrow(struct obuf *buf, size_t size)
{
	struct iovec *iov = &buf->iov[buf->pos];
	size_t capacity = buf->capacity[buf->pos];
	if (iov->iov_len > 0) {
		/* Move to the next buffer. */
		if (buf->pos + 1 >= SMALL_OBUF_IOV_MAX)
			return NULL;
		buf->pos++;
		iov = &buf->iov[buf->pos];
		capacity = buf->capacity[buf->pos];
	}
	assert(iov->iov_len == 0);
	/* Make sure the next buffer can store size. */
	if (size > capacity) {
		if (capacity > 0) {
			/* Simply realloc. */
			while (capacity < size)
				capacity = capacity * 2;
			struct slab *slab = slab_get(buf->slabc, capacity);
			if (slab == NULL)
				return NULL;
			struct slab *old =
				slab_from_data(buf->iov[buf->pos].iov_base);
			slab_put(buf->slabc, old);
			buf->iov[buf->pos].iov_base = slab_data(slab);
			buf->capacity[buf->pos] = slab_capacity(slab);
		} else if (obuf_alloc_pos(buf, size) == NULL) {
			return NULL;
		}
	}
	assert(buf->iov[buf->pos].iov_len + size <= buf->capacity[buf->pos]);
	return (char*) buf->iov[buf->pos].iov_base + buf->iov[buf->pos].iov_len;
}

/** Forget about data in the output buffer beyond the savepoint. */
void
obuf_rollback_to_svp(struct obuf *buf, struct obuf_svp *svp)
{
	int iovcnt = obuf_iovcnt(buf);

	buf->pos = svp->pos;
	buf->iov[buf->pos].iov_len = svp->iov_len;
	buf->used = svp->used;
	for (int i = buf->pos + 1; i < iovcnt; i++)
		buf->iov[i].iov_len = 0;
}
