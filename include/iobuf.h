#ifndef TARANTOOL_IOBUF_H_INCLUDED
#define TARANTOOL_IOBUF_H_INCLUDED
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
#include <sys/uio.h>
#include <stdbool.h>
#include "tarantool/util.h"
#include "third_party/queue.h"
#include "lib/small/region.h"

struct ev_io;

/** {{{ Input buffer.
 *
 * Continuous piece of memory to store input.
 * Allocated in factors of cfg.readahead.
 * Maintains position of the data "to be processed".
 *
 * Typical use case:
 *
 * struct ibuf *in;
 * coio_bread(coio, in, request_len);
 * if (ibuf_size(in) >= request_len) {
 *	process_request(in->pos, request_len);
 *	in->pos += request_len;
 * }
 */
struct ibuf
{
	struct region *pool;
	char *buf;
	/** Start of input. */
	char *pos;
	/** End of useful input */
	char *end;
	/** Buffer size. */
	size_t capacity;
};

/** Reserve space for sz bytes in the input buffer. */
void
ibuf_reserve(struct ibuf *ibuf, size_t sz);

/** How much data is read and is not parsed yet. */
static inline size_t
ibuf_size(struct ibuf *ibuf)
{
	return ibuf->end - ibuf->pos;
}

/** How much data can we fit beyond buf->end */
static inline size_t
ibuf_unused(struct ibuf *ibuf)
{
	return ibuf->buf + ibuf->capacity - ibuf->end;
}

/* Integer value of the position in the buffer - stable
 * in case of realloc.
 */
static inline size_t
ibuf_pos(struct ibuf *ibuf)
{
	return ibuf->pos - ibuf->buf;
}

/* }}} */

/* {{{ Output buffer. */

enum { IOBUF_IOV_MAX = 32 };

/**
 * An output buffer is an array of struct iovec vectors
 * for writev().
 * Each buffer is allocated on region allocator.
 * Buffer size grows by a factor of 2. With this growth factor,
 * the number of used buffers is unlikely to ever exceed the
 * hard limit of IOBUF_IOV_MAX. If it does, an exception is
 * raised.
 */
struct obuf
{
	struct region *pool;
	/* How many bytes are in the buffer. */
	size_t size;
	/** Position of the "current" iovec. */
	size_t pos;
	/** How many bytes are actually allocated for each iovec. */
	size_t capacity[IOBUF_IOV_MAX];
	/**
	 * List of iovec vectors, each vector is at least twice
	 * as big as the previous one. The vector following the
	 * last allocated one is always zero-initialized
	 * (iov_base = NULL, iov_len = 0).
	 */
	struct iovec iov[IOBUF_IOV_MAX];
};

/** How many bytes are in the output buffer. */
static inline size_t
obuf_size(struct obuf *obuf)
{
	return obuf->size;
}

/** The size of iov vector in the buffer. */
static inline int
obuf_iovcnt(struct obuf *buf)
{
	return buf->iov[buf->pos].iov_len > 0 ? buf->pos + 1 : buf->pos;
}

/**
 * Output buffer savepoint. It's possible to
 * save the current buffer state in a savepoint
 * and roll back to the saved state at any time
 * before iobuf_flush()
 */
struct obuf_svp
{
	size_t pos;
	size_t iov_len;
	size_t size;
};

/**
 * Reserve size bytes in the output buffer
 * and return a pointer to the reserved
 * data. Returns a pointer to a continuous piece of
 * memory.
 * Typical use case:
 * struct obuf_svp svp = obuf_book(buf, sizeof(uint32_t));
 * for (...)
 *	obuf_dup(buf, ...);
 * uint32_t total = obuf_size(buf);
 * memcpy(obuf_svp_to_ptr(&svp), &total, sizeof(total);
 * iobuf_flush();
 */
struct obuf_svp
obuf_book(struct obuf *obuf, size_t size);

/** Append data to the output buffer. */
void
obuf_dup(struct obuf *obuf, const void *data, size_t size);

static inline struct obuf_svp
obuf_create_svp(struct obuf *buf)
{
	struct obuf_svp svp;
	svp.pos = buf->pos;
	svp.iov_len = buf->iov[buf->pos].iov_len;
	svp.size = buf->size;
	return svp;
}

/** Convert a savepoint position to a pointer in the buffer. */
static inline void *
obuf_svp_to_ptr(struct obuf *buf, struct obuf_svp *svp)
{
	return (char *) buf->iov[svp->pos].iov_base + svp->iov_len;
}

/** Forget anything added to output buffer after the savepoint. */
void
obuf_rollback_to_svp(struct obuf *buf, struct obuf_svp *svp);

/* }}} */

/** {{{  Input/output pair. */
struct iobuf
{
	/** Used for iobuf cache. */
	SLIST_ENTRY(iobuf) next;
	/** Input buffer. */
	struct ibuf in;
	/** Output buffer. */
	struct obuf out;
	struct region pool;
};

/** Create an instance of input/output buffer. */
struct iobuf *
iobuf_new(const char *name);

/** Destroy an input/output buffer. */
void
iobuf_delete(struct iobuf *iobuf);

/** Flush output using cooperative I/O and garbage collect.
 * @return number of bytes written
 */
ssize_t
iobuf_flush(struct iobuf *iobuf, struct ev_io *coio);

/**
 * Must be called when we are done sending all output,
 * and there is likely no cached input.
 * Is automatically called by iobuf_flush().
 */
void
iobuf_gc(struct iobuf *iobuf);

/** Return true if there is no input and no output. */
static inline bool
iobuf_is_idle(struct iobuf *iobuf)
{
	return ibuf_size(&iobuf->in) == 0 && obuf_size(&iobuf->out) == 0;
}

/**
 * Network readahead. A signed integer to avoid
 * automatic type coercion to an unsigned type.
 */
extern int cfg_readahead;

static inline void
iobuf_init_readahead(int readahead)
{
	cfg_readahead =  readahead;
}

/* }}} */

#endif /* TARANTOOL_IOBUF_H_INCLUDED */
