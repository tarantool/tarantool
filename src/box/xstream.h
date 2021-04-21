#ifndef TARANTOOL_XSTREAM_H_INCLUDED
#define TARANTOOL_XSTREAM_H_INCLUDED

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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
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

#include "diag.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct xrow_header;
struct xstream;

/**
 * A type for a callback invoked by recovery after some batch of rows is
 * processed. Is used mostly to unblock the event loop every now and then.
 */
typedef void (*xstream_yield_f)(struct xstream *);

typedef void (*xstream_write_f)(struct xstream *, struct xrow_header *);

struct xstream {
	xstream_write_f write;
	xstream_yield_f yield;
	uint64_t row_count;
};

static inline void
xstream_create(struct xstream *xstream, xstream_write_f write,
	       xstream_yield_f yield)
{
	xstream->write = write;
	xstream->yield = yield;
	xstream->row_count = 0;
}

static inline void
xstream_yield(struct xstream *stream)
{
	stream->yield(stream);
}

static inline void
xstream_reset(struct xstream *stream)
{
	stream->row_count = 0;
	xstream_yield(stream);
}

int
xstream_write(struct xstream *stream, struct xrow_header *row);

#if defined(__cplusplus)
} /* extern C */

static inline void
xstream_write_xc(struct xstream *stream, struct xrow_header *row)
{
	if (xstream_write(stream, row) != 0)
		diag_raise();
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_XSTREAM_H_INCLUDED */
