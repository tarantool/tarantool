#ifndef TARANTOOL_XROW_IO_H_INCLUDED
#define TARANTOOL_XROW_IO_H_INCLUDED
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

#include "small/lsregion.h"
#include "memory.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct ibuf;
struct iostream;
struct xrow_header;

void
coio_read_xrow(struct iostream *io, struct ibuf *in, struct xrow_header *row);

void
coio_read_xrow_timeout_xc(struct iostream *io, struct ibuf *in,
			  struct xrow_header *row, double timeout);

void
coio_write_xrow(struct iostream *io, const struct xrow_header *row);


/** Written data size after which the stream should be flushed. */
extern uint64_t xrow_stream_flush_size;

/**
 * A structure encapsulating writes made by relay. Collects the rows into a
 * buffer and flushes it to the network as soon as its size reaches a specific
 * threshold.
 */
struct xrow_stream {
	/** A region storing rows buffered for dispatch. */
	struct lsregion lsregion;
	/** A growing identifier for lsregion allocations. */
	int64_t lsr_id;
	/** A savepoint used between flushes. */
	struct lsregion_svp flush_pos;
#ifndef NDEBUG
	/** A fiber which's currently using the stream. */
	struct fiber *owner;
#endif
};

/** Initialize the stream. */
static inline void
xrow_stream_create(struct xrow_stream *stream)
{
	lsregion_create(&stream->lsregion, &runtime);
	stream->lsr_id = 0;
	lsregion_svp_create(&stream->flush_pos);
}

static inline void
xrow_stream_destroy(struct xrow_stream *stream)
{
	assert(stream->owner == NULL);
	lsregion_destroy(&stream->lsregion);
}

/** Write a row to the stream. */
void
xrow_stream_write(struct xrow_stream *stream, const struct xrow_header *row);

/** Flush the stream contents to the given iostream. */
int
xrow_stream_flush(struct xrow_stream *stream, struct iostream *io);

/** Check whether the stream is full and flush it, if necessary. */
static inline int
xrow_stream_check_flush(struct xrow_stream *stream, struct iostream *io)
{
	if (lsregion_used(&stream->lsregion) > xrow_stream_flush_size)
		return xrow_stream_flush(stream, io);
	return 0;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_XROW_IO_H_INCLUDED */
