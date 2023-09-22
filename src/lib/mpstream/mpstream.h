/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/decimal.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tt_uuid;
struct datetime;
struct interval;

typedef void *(*mpstream_reserve_f)(void *ctx, size_t *size);
typedef void *(*mpstream_alloc_f)(void *ctx, size_t size);
typedef void (*mpstream_error_f)(void *error_ctx);

struct mpstream {
	/**
	 * When pos >= end, or required size doesn't fit in
	 * pos..end range alloc() is called to advance the stream
	 * and reserve() to get a new chunk.
	 */
	char *buf, *pos, *end;
	/** Context passed to the reserve and alloc callbacks. */
	void *ctx;
	/**
	 * Ask the allocator to reserve at least size bytes.
	 * It can reserve more, and update *size with the new size.
	 */
	mpstream_reserve_f reserve;
	/** Actually use the bytes. */
	mpstream_alloc_f alloc;
	/** Called on allocation error. */
	mpstream_error_f error;
	/** Argument passed to the error callback. */
	void *error_ctx;
};

void
mpstream_reserve_slow(struct mpstream *stream, size_t size);

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
	if (stream->pos != stream->buf)
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

void
mpstream_encode_datetime(struct mpstream *stream, const struct datetime *dt);

/** Encode interval value to a stream. */
void
mpstream_encode_interval(struct mpstream *stream, const struct interval *val);

/** Copies n bytes from memory area src to stream. */
void
mpstream_memcpy(struct mpstream *stream, const void *src, uint32_t n);

/** Fills n stream bytes with the constant byte c. */
void
mpstream_memset(struct mpstream *stream, int c, uint32_t n);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
