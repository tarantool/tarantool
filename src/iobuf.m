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
#include "coio_buf.h"
#include "palloc.h"

/* {{{ struct ibuf */

/** Initialize an input buffer. */
static void
ibuf_create(struct ibuf *ibuf, struct palloc_pool *pool)
{
	ibuf->pool = pool;
	ibuf->capacity = 0;
	ibuf->buf = ibuf->pos = ibuf->end = NULL;
	/* Don't allocate the buffer yet. */
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
		/* Use cfg_readahead as allocation factor. */
		size_t new_capacity = MAX(ibuf->capacity * 2, cfg_readahead);
		while (new_capacity < current_size + size)
			new_capacity *= 2;

		ibuf->buf = palloc(ibuf->pool, new_capacity);
		memcpy(ibuf->buf, ibuf->pos, current_size);
		ibuf->capacity = new_capacity;
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
		tnt_raise(LoggedError, :ER_MEMORY_ISSUE, buf->pos,
			  "obuf_init_pos", "iovec");
	}
	buf->iov[pos].iov_base = NULL;
	buf->iov[pos].iov_len = 0;
	buf->capacity[pos] = 0;
}

/** Allocate memory for a single iovec buffer. */
static inline void
obuf_alloc_pos(struct obuf *buf, size_t pos, size_t size)
{
	size_t capacity = pos > 0 ?  buf->capacity[pos-1] * 2 : cfg_readahead;
	while (capacity < size) {
		capacity *=2;
	}

	buf->iov[pos].iov_base = palloc(buf->pool, capacity);
	buf->capacity[buf->pos] = capacity;
	assert(buf->iov[pos].iov_len == 0);
}

/** Initialize an output buffer instance. Don't allocate memory
 * yet -- it may never be needed.
 */
void
obuf_create(struct obuf *buf, struct palloc_pool *pool)
{
	buf->pool = pool;
	buf->pos = 0;
	buf->size = 0;
	obuf_init_pos(buf, buf->pos);
}

/** Mark an output buffer as empty. */
static void
obuf_reset(struct obuf *buf)
{
	buf->pos = 0;
	buf->size = 0;
	for (struct iovec *iov = buf->iov; iov->iov_len != 0; iov++) {
		assert(iov < buf->iov + IOBUF_IOV_MAX);
		iov->iov_len = 0;
	}
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
			memcpy(iov->iov_base + iov->iov_len, data, fill);

			iov->iov_len += fill;
			buf->size += fill;
			data += fill;
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
		iov = &buf->iov[buf->pos];
		capacity = buf->capacity[buf->pos];
	}
	memcpy(iov->iov_base + iov->iov_len, data, size);
	iov->iov_len += size;
	buf->size += size;
	assert(iov->iov_len <= buf->capacity[buf->pos]);
}

/** Book a few bytes in the output buffer. */
void *
obuf_book(struct obuf *buf, size_t size)
{
	struct iovec *iov = &buf->iov[buf->pos];
	size_t capacity = buf->capacity[buf->pos];
	if (iov->iov_len + size > capacity) {
		if (iov->iov_len > 0) {
			/* Move to the next buffer. */
			buf->pos++;
			iov = &buf->iov[buf->pos];
			capacity = buf->capacity[buf->pos];
		}
		/* Make sure the next buffer can store size.  */
		if (capacity == 0) {
			obuf_init_pos(buf, buf->pos + 1);
			obuf_alloc_pos(buf, buf->pos, size);
		} else if (size > capacity) {
			/* Simply realloc. */
			obuf_alloc_pos(buf, buf->pos, size);
		}
	}
	void *booking = iov->iov_base + iov->iov_len;
	iov->iov_len += size;
	buf->size += size;
	assert(iov->iov_len <= buf->capacity[buf->pos]);
	return booking;
}

/** Forget about data in the output buffer beyond the savepoint. */
void
obuf_rollback_to_svp(struct obuf *buf, struct obuf_svp *svp)
{
	bool is_last_pos = buf->pos == svp->pos;

	buf->pos = svp->pos;
	buf->iov[buf->pos].iov_len = svp->iov_len;
	buf->size = svp->size;
	/**
	 * We need this check to ensure the following
	 * loop doesn't run away.
	 */
	if (is_last_pos)
		return;
	for (struct iovec *iov = buf->iov + buf->pos + 1; iov->iov_len != 0; iov++) {
		assert(iov < buf->iov + IOBUF_IOV_MAX);
		iov->iov_len = 0;
	}
}

/* struct obuf }}} */

/* {{{ struct iobuf */

/**
 * How big is a buffer which needs to be shrunk before it is put
 * back into buffer cache.
 */
static int iobuf_max_pool_size()
{
	return 18 * cfg_readahead;
}

SLIST_HEAD(iobuf_cache, iobuf) iobuf_cache;

/** Create an instance of input/output buffer or take one from cache. */
struct iobuf *
iobuf_new(const char *name)
{
	struct iobuf *iobuf;
	if (SLIST_EMPTY(&iobuf_cache)) {
		iobuf = palloc(eter_pool, sizeof(struct iobuf));
		struct palloc_pool *pool = palloc_create_pool("");
		/* Note: do not allocate memory upfront. */
		ibuf_create(&iobuf->in, pool);
		obuf_create(&iobuf->out, pool);
	} else {
		iobuf = SLIST_FIRST(&iobuf_cache);
		SLIST_REMOVE_HEAD(&iobuf_cache, next);
	}
	/* When releasing the buffer, we trim it to iobuf_max_pool_size. */
	assert(palloc_allocated(iobuf->in.pool) <= iobuf_max_pool_size());
	palloc_set_name(iobuf->in.pool, name);
	return iobuf;
}

/** Put an instance back to the iobuf_cache. */
void
iobuf_delete(struct iobuf *iobuf)
{
	struct palloc_pool *pool = iobuf->in.pool;
	if (palloc_allocated(pool) < iobuf_max_pool_size()) {
		ibuf_reset(&iobuf->in);
		obuf_reset(&iobuf->out);
	} else {
		prelease(pool);
		ibuf_create(&iobuf->in, pool);
		obuf_create(&iobuf->out, pool);
	}
	palloc_set_name(pool, "iobuf_cache");
	SLIST_INSERT_HEAD(&iobuf_cache, iobuf, next);
}

/** Send all data in the output buffer and garbage collect. */
ssize_t
iobuf_flush(struct iobuf *iobuf, struct ev_io *coio)
{
	ssize_t total = coio_writev(coio, iobuf->out.iov,
				    obuf_iovcnt(&iobuf->out),
				    obuf_size(&iobuf->out));
	iobuf_gc(iobuf);
	/*
	 * If there is some residue in the input buffer, move it
	 * but only in case if we don't have cfg_readahead
	 * bytes available for the next round: it's more efficient
	 * to move any residue now, when it's likely to be small,
	 * rather than when we have read a bunch more data, and only
	 * then discovered we don't have enough space to read a
	 * full request.
	 */
	ibuf_reserve(&iobuf->in, cfg_readahead);
	return total;
}

void
iobuf_gc(struct iobuf *iobuf)
{
	/*
	 * If we happen to have fully processed the input,
	 * move the pos to the start of the input buffer.
	 */
	if (ibuf_size(&iobuf->in) == 0)
		ibuf_reset(&iobuf->in);
	/* Cheap to do even if already done. */
	obuf_reset(&iobuf->out);
}

int cfg_readahead;

/* struct iobuf }}} */
