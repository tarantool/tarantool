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
#include "iobuf.h"
#include "fiber.h"

__thread struct mempool iobuf_pool;

/**
 * Network readahead. A signed integer to avoid
 * automatic type coercion to an unsigned type.
 * We assign it without locks in txn thread and
 * use in iproto thread -- it's OK that
 * readahead has a stale value while until the thread
 * caches have synchronized, after all, it's used
 * in new connections only.
 *
 * Notice that the default is not a strict power of two.
 * slab metadata takes some space, and we want
 * allocation steps to be correlated to slab buddy
 * sizes, so when we ask slab cache for 16320 bytes,
 * we get a slab of size 16384, not 32768.
 */
static int iobuf_readahead = 16320;

/* {{{ struct ibuf */

/** Initialize an input buffer. */
static void
ibuf_create(struct ibuf *ibuf, struct slab_cache *slabc)
{
	ibuf->slabc = slabc;
	ibuf->capacity = 0;
	ibuf->buf = ibuf->pos = ibuf->end = NULL;
	/* Don't allocate the buffer yet. */
}

static void
ibuf_destroy(struct ibuf *ibuf)
{
	if (ibuf->buf) {
		struct slab *slab = slab_from_data(ibuf->buf);
		slab_put(ibuf->slabc, slab);
	 }
}

/** Forget all cached input. */
static void
ibuf_reset(struct ibuf *ibuf)
{
	ibuf->pos = ibuf->end = ibuf->buf;
}

/**
 * Ensure the buffer has sufficient capacity
 * to store size bytes.
 */
void
ibuf_reserve(struct ibuf *ibuf, size_t size)
{
	if (size <= ibuf_unused(ibuf))
		return;
	size_t current_size = ibuf_size(ibuf);
	/*
	 * Check if we have enough space in the
	 * current buffer. In this case de-fragment it
	 * by moving existing data to the beginning.
	 * Otherwise, get a bigger buffer.
	 */
	if (size + current_size <= ibuf->capacity) {
		memmove(ibuf->buf, ibuf->pos, current_size);
	} else {
		/* Use iobuf_readahead as allocation factor. */
		size_t new_capacity = MAX(ibuf->capacity * 2, iobuf_readahead);
		while (new_capacity < current_size + size)
			new_capacity *= 2;

		struct slab *slab = slab_get(ibuf->slabc, new_capacity);
		if (slab == NULL) {
			tnt_raise(OutOfMemory, new_capacity,
				  "ibuf_reserve", "slab cache");
		}
		char *ptr = (char *) slab_data(slab);
		memcpy(ptr, ibuf->pos, current_size);
		ibuf->capacity = slab_capacity(slab);
		if (ibuf->buf)
			slab_put(ibuf->slabc, slab_from_data(ibuf->buf));
		ibuf->buf = ptr;
	}
	ibuf->pos = ibuf->buf;
	ibuf->end = ibuf->pos + current_size;
}

/* }}} */

/* {{{ struct obuf */

/**
 * Initialize the next slot in iovec array. The buffer
 * always has at least one empty slot.
 */
static inline void
obuf_init_pos(struct obuf *buf, size_t pos)
{
	if (pos >= IOBUF_IOV_MAX) {
		tnt_raise(OutOfMemory, buf->pos, "obuf_init_pos", "iovec");
	}
	buf->iov[pos].iov_base = NULL;
	buf->iov[pos].iov_len = 0;
	buf->capacity[pos] = 0;
}

/** Allocate memory for a single iovec buffer. */
static inline void
obuf_alloc_pos(struct obuf *buf, size_t pos, size_t size)
{
	assert(buf->capacity[pos] == 0);
	size_t capacity = pos > 0 ?  buf->capacity[pos-1] * 2 :
		buf->start_size;
	while (capacity < size)
		capacity *= 2;

	struct slab *slab = slab_get(buf->slabc, capacity);
	if (slab == NULL)
		tnt_raise(OutOfMemory, capacity, "obuf_alloc_pos", "slab_get");
	buf->iov[pos].iov_base = slab_data(slab);
	buf->capacity[buf->pos] = slab_capacity(slab);
	assert(buf->iov[pos].iov_len == 0);
}

/**
 * Initialize an output buffer instance. Don't allocate memory
 * yet -- it may never be needed.
 */
void
obuf_create(struct obuf *buf, struct slab_cache *slabc, size_t start_size)
{
	buf->slabc = slabc;
	buf->pos = 0;
	buf->size = 0;
	buf->start_size = start_size;
	obuf_init_pos(buf, buf->pos);
}

/** Mark an output buffer as empty. */
static void
obuf_reset(struct obuf *buf)
{
	int iovcnt = obuf_iovcnt(buf);
	for (int i = 0; i < iovcnt; i++)
		buf->iov[i].iov_len = 0;
	buf->pos = 0;
	buf->size = 0;
}

void
obuf_destroy(struct obuf *buf)
{
	int iovcnt = obuf_iovcnt(buf);
	for (int i = 0; i < iovcnt; i++) {
		struct slab *slab = slab_from_data(buf->iov[i].iov_base);
		slab_put(buf->slabc, slab);
	}
#ifndef NDEBUG
	obuf_create(buf, buf->slabc, buf->start_size);
#endif
}

/** Add data to the output buffer. Copies the data. */
void
obuf_dup(struct obuf *buf, const void *data, size_t size)
{
	struct iovec *iov = &buf->iov[buf->pos];
	size_t capacity = buf->capacity[buf->pos];
	/**
	 * @pre buf->pos points at an array of allocated buffers.
	 * The array ends with a zero-initialized buffer.
	 */
	while (iov->iov_len + size > capacity) {
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
			assert(fill < size);
			memcpy((char *) iov->iov_base + iov->iov_len,
			       data, fill);

			iov->iov_len += fill;
			buf->size += fill;
			data = (char *) data + fill;
			size -= fill;
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
			obuf_init_pos(buf, buf->pos + 1);
			obuf_alloc_pos(buf, buf->pos, size);
			break;
		}
		assert(capacity == iov->iov_len);
		buf->pos++;
		assert(buf->pos < IOBUF_IOV_MAX);
		iov = &buf->iov[buf->pos];
		capacity = buf->capacity[buf->pos];
	}
	memcpy((char *) iov->iov_base + iov->iov_len, data, size);
	iov->iov_len += size;
	buf->size += size;
	assert(iov->iov_len <= buf->capacity[buf->pos]);
}

void *
obuf_reserve_slow(struct obuf *buf, size_t size)
{
	struct iovec *iov = &buf->iov[buf->pos];
	size_t capacity = buf->capacity[buf->pos];
	if (iov->iov_len > 0) {
		/* Move to the next buffer. */
		buf->pos++;
		assert(buf->pos < IOBUF_IOV_MAX);
		iov = &buf->iov[buf->pos];
		capacity = buf->capacity[buf->pos];
	}
	/* Make sure the next buffer can store size. */
	assert(iov->iov_len == 0);
	if (size > capacity) {
		if (capacity > 0) {
			/* Simply realloc. */
			struct slab *slab =
				slab_from_data(buf->iov[buf->pos].iov_base);
			slab_put(buf->slabc, slab);
			obuf_init_pos(buf, buf->pos);
		} else {
			/* Initialize the next pos, the end marker. */
			obuf_init_pos(buf, buf->pos + 1);
		}
		obuf_alloc_pos(buf, buf->pos, size);
	}
	assert(buf->iov[buf->pos].iov_len + size <= buf->capacity[buf->pos]);
	return (char *) buf->iov[buf->pos].iov_base + buf->iov[buf->pos].iov_len;
}

/** Book a few bytes in the output buffer. */
struct obuf_svp
obuf_book(struct obuf *buf, size_t size)
{
	obuf_reserve(buf, size);

	struct obuf_svp svp;
	svp.pos = buf->pos;
	svp.iov_len = buf->iov[buf->pos].iov_len;
	svp.size = buf->size;

	obuf_alloc(buf, size);
	return svp;
}

/** Forget about data in the output buffer beyond the savepoint. */
void
obuf_rollback_to_svp(struct obuf *buf, struct obuf_svp *svp)
{
	int iovcnt = obuf_iovcnt(buf);

	buf->pos = svp->pos;
	buf->iov[buf->pos].iov_len = svp->iov_len;
	buf->size = svp->size;
	for (int i = buf->pos + 1; i < iovcnt; i++)
		buf->iov[i].iov_len = 0;
}

static inline size_t
obuf_capacity(struct obuf *buf)
{
	/** This is an approximation, see obuf_alloc_pos() */
	return buf->capacity[buf->pos] * 2;
}

/* struct obuf }}} */

/* {{{ struct iobuf */

/**
 * How big is a buffer which needs to be shrunk before it is put
 * back into buffer cache.
 */
static int iobuf_max_size()
{
	return 18 * iobuf_readahead;
}

/** Create an instance of input/output buffer or take one from cache. */
struct iobuf *
iobuf_new()
{
	return iobuf_new_mt(&cord()->slabc);
}

struct iobuf *
iobuf_new_mt(struct slab_cache *slabc_out)
{
	struct iobuf *iobuf;
	iobuf = (struct iobuf *) mempool_alloc(&iobuf_pool);
	iobuf->pins = 0;
	/* Note: do not allocate memory upfront. */
	ibuf_create(&iobuf->in, &cord()->slabc);
	obuf_create(&iobuf->out, slabc_out, iobuf_readahead);
	return iobuf;
}

/** Destroy an instance and delete it. */
void
iobuf_delete(struct iobuf *iobuf)
{
	ibuf_destroy(&iobuf->in);
	obuf_destroy(&iobuf->out);
	mempool_free(&iobuf_pool, iobuf);
}

/** Second part of multi-threaded destroy. */
void
iobuf_delete_mt(struct iobuf *iobuf)
{
	assert(iobuf->pins == 0);
	ibuf_destroy(&iobuf->in);
	/* Destroyed by the caller. */
	assert(&iobuf->out.pos == 0 && iobuf->out.iov[0].iov_base == NULL);
	mempool_free(&iobuf_pool, iobuf);
}

void
iobuf_reset(struct iobuf *iobuf)
{
	/*
	 * If we happen to have fully processed the input,
	 * move the pos to the start of the input buffer.
	 */
	if (ibuf_size(&iobuf->in) == 0) {
		if (ibuf_capacity(&iobuf->in) < iobuf_max_size()) {
			ibuf_reset(&iobuf->in);
		} else {
			struct slab_cache *slabc = iobuf->in.slabc;
			ibuf_destroy(&iobuf->in);
			ibuf_create(&iobuf->in, slabc);
		}
	}
	if (obuf_capacity(&iobuf->out) < iobuf_max_size()) {
		/* Cheap to do even if already done. */
		obuf_reset(&iobuf->out);
	} else {
		struct slab_cache *slabc = iobuf->out.slabc;
		obuf_destroy(&iobuf->out);
		obuf_create(&iobuf->out, slabc, iobuf_readahead);
	}
}

void
iobuf_init()
{
	mempool_create(&iobuf_pool, &cord()->slabc, sizeof(struct iobuf));
}

void
iobuf_set_readahead(int readahead)
{
	iobuf_readahead =  readahead;
}

/* struct iobuf }}} */
