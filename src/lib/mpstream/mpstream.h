#ifndef TARANTOOL_LIB_CORE_MPSTREAM_H_INCLUDED
#define TARANTOOL_LIB_CORE_MPSTREAM_H_INCLUDED
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

#include "diag.h"
#include "decimal.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tt_uuid;

/**
* Ask the allocator to reserve at least size bytes. It can reserve
* more, and update *size with the new size.
*/
typedef void *(*mpstream_reserve_f)(void *ctx, size_t *size);

/** Actually use the bytes. */
typedef void *(*mpstream_alloc_f)(void *ctx, size_t size);

/** Actually use the bytes. */
typedef void (*mpstream_error_f)(void *error_ctx);

struct mpstream {
    /**
     * When pos >= end, or required size doesn't fit in
     * pos..end range alloc() is called to advance the stream
     * and reserve() to get a new chunk.
     */
    char *buf, *pos, *end;
    void *ctx;
    mpstream_reserve_f reserve;
    mpstream_alloc_f alloc;
    mpstream_error_f error;
    void *error_ctx;
};

void
mpstream_reserve_slow(struct mpstream *stream, size_t size);

void
mpstream_reset(struct mpstream *stream);

/**
 * A streaming API so that it's possible to encode to any output
 * stream.
 */
void
mpstream_init(struct mpstream *stream, void *ctx,
	      mpstream_reserve_f reserve, mpstream_alloc_f alloc,
	      mpstream_error_f error, void *error_ctx);

static inline void
mpstream_flush(struct mpstream *stream)
{
	stream->alloc(stream->ctx, stream->pos - stream->buf);
	stream->buf = stream->pos;
}

static inline char *
mpstream_reserve(struct mpstream *stream, size_t size)
{
	if (stream->pos + size > stream->end)
		mpstream_reserve_slow(stream, size);
	return stream->pos;
}

static inline void
mpstream_advance(struct mpstream *stream, size_t size)
{
	assert(stream->pos + size <= stream->end);
	stream->pos += size;
}

void
mpstream_encode_array(struct mpstream *stream, uint32_t size);

void
mpstream_encode_map(struct mpstream *stream, uint32_t size);

void
mpstream_encode_uint(struct mpstream *stream, uint64_t num);

void
mpstream_encode_int(struct mpstream *stream, int64_t num);

void
mpstream_encode_float(struct mpstream *stream, float num);

void
mpstream_encode_double(struct mpstream *stream, double num);

void
mpstream_encode_strn(struct mpstream *stream, const char *str, uint32_t len);

static inline void
mpstream_encode_str(struct mpstream *stream, const char *str)
{
	mpstream_encode_strn(stream, str, strlen(str));
}

void
mpstream_encode_nil(struct mpstream *stream);

void
mpstream_encode_bool(struct mpstream *stream, bool val);

void
mpstream_encode_binl(struct mpstream *stream, uint32_t len);

void
mpstream_encode_decimal(struct mpstream *stream, const decimal_t *val);

void
mpstream_encode_uuid(struct mpstream *stream, const struct tt_uuid *uuid);

/** Copies n bytes from memory area src to stream. */
void
mpstream_memcpy(struct mpstream *stream, const void *src, uint32_t n);

/** Fills n stream bytes with the constant byte c. */
void
mpstream_memset(struct mpstream *stream, int c, uint32_t n);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_MPSTREAM_H_INCLUDED */
