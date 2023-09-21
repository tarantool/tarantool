/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "mpstream.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "diag.h"
#include "msgpuck.h"
#include "mp_decimal.h"
#include "mp_uuid.h"
#include "mp_datetime.h"
#include "mp_interval.h"

void
mpstream_panic_cb(void *error_ctx)
{
	(void)error_ctx;
	diag_log();
	panic("Out of memory");
}

void
mpstream_reserve_slow(struct mpstream *stream, size_t size)
{
	if (stream->pos != stream->buf)
		stream->alloc(stream->ctx, stream->pos - stream->buf);
	stream->buf = stream->reserve(stream->ctx, &size);
	if (stream->buf == NULL) {
		diag_set(OutOfMemory, size, "mpstream", "reserve");
		stream->error(stream->error_ctx);
	}
	stream->pos = stream->buf;
	stream->end = stream->pos + size;
}

void
mpstream_reset(struct mpstream *stream)
{
	size_t size = 0;
	stream->buf = stream->reserve(stream->ctx, &size);
	if (stream->buf == NULL) {
		diag_set(OutOfMemory, size, "mpstream", "reset");
		stream->error(stream->error_ctx);
	}
	stream->pos = stream->buf;
	stream->end = stream->pos + size;
}

/**
 * A streaming API so that it's possible to encode to any output
 * stream.
 */
void
mpstream_init(struct mpstream *stream, void *ctx,
              mpstream_reserve_f reserve, mpstream_alloc_f alloc,
              mpstream_error_f error, void *error_ctx)
{
	stream->ctx = ctx;
	stream->reserve = reserve;
	stream->alloc = alloc;
	stream->error = error;
	stream->error_ctx = error_ctx;
	mpstream_reset(stream);
}

void
mpstream_encode_array(struct mpstream *stream, uint32_t size)
{
	assert(mp_sizeof_array(size) <= 5);
	char *data = mpstream_reserve(stream, 5);
	if (data == NULL)
		return;
	char *pos = mp_encode_array(data, size);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_map(struct mpstream *stream, uint32_t size)
{
	assert(mp_sizeof_map(size) <= 5);
	char *data = mpstream_reserve(stream, 5);
	if (data == NULL)
		return;
	char *pos = mp_encode_map(data, size);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_uint(struct mpstream *stream, uint64_t num)
{
	assert(mp_sizeof_uint(num) <= 9);
	char *data = mpstream_reserve(stream, 9);
	if (data == NULL)
		return;
	char *pos = mp_encode_uint(data, num);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_int(struct mpstream *stream, int64_t num)
{
	assert(mp_sizeof_int(num) <= 9);
	char *data = mpstream_reserve(stream, 9);
	if (data == NULL)
		return;
	char *pos = mp_encode_int(data, num);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_float(struct mpstream *stream, float num)
{
	assert(mp_sizeof_float(num) <= 5);
	char *data = mpstream_reserve(stream, 5);
	if (data == NULL)
		return;
	char *pos = mp_encode_float(data, num);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_double(struct mpstream *stream, double num)
{
	assert(mp_sizeof_double(num) <= 9);
	char *data = mpstream_reserve(stream, 9);
	if (data == NULL)
		return;
	char *pos = mp_encode_double(data, num);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_strn(struct mpstream *stream, const char *str, uint32_t len)
{
	assert(mp_sizeof_str(len) <= 5 + len);
	char *data = mpstream_reserve(stream, 5 + len);
	if (data == NULL)
		return;
	char *pos = mp_encode_str(data, str, len);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_nil(struct mpstream *stream)
{
	assert(mp_sizeof_nil() <= 1);
	char *data = mpstream_reserve(stream, 1);
	if (data == NULL)
		return;
	char *pos = mp_encode_nil(data);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_bool(struct mpstream *stream, bool val)
{
	assert(mp_sizeof_bool(val) <= 1);
	char *data = mpstream_reserve(stream, 1);
	if (data == NULL)
		return;
	char *pos = mp_encode_bool(data, val);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_binl(struct mpstream *stream, uint32_t len)
{
	char *data = mpstream_reserve(stream, mp_sizeof_binl(len));
	if (data == NULL)
		return;
	char *pos = mp_encode_binl(data, len);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_decimal(struct mpstream *stream, const decimal_t *val)
{
	char *data = mpstream_reserve(stream, mp_sizeof_decimal(val));
	if (data == NULL)
		return;
	char *pos = mp_encode_decimal(data, val);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_uuid(struct mpstream *stream, const struct tt_uuid *uuid)
{
	char *data = mpstream_reserve(stream, mp_sizeof_uuid());
	if (data == NULL)
		return;
	char *pos = mp_encode_uuid(data, uuid);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_datetime(struct mpstream *stream, const struct datetime *val)
{
	char *data = mpstream_reserve(stream, mp_sizeof_datetime(val));
	if (data == NULL)
		return;
	char *pos = mp_encode_datetime(data, val);
	mpstream_advance(stream, pos - data);
}

void
mpstream_encode_interval(struct mpstream *stream, const struct interval *val)
{
	char *data = mpstream_reserve(stream, mp_sizeof_interval(val));
	if (data == NULL)
		return;
	char *pos = mp_encode_interval(data, val);
	mpstream_advance(stream, pos - data);
}

void
mpstream_memcpy(struct mpstream *stream, const void *src, uint32_t n)
{
	char *data = mpstream_reserve(stream, n);
	if (data == NULL)
		return;
	memcpy(data, src, n);
	mpstream_advance(stream, n);
}

void
mpstream_memset(struct mpstream *stream, int c, uint32_t n)
{
	char *data = mpstream_reserve(stream, n);
	if (data == NULL)
		return;
	memset(data, c, n);
	mpstream_advance(stream, n);
}
