#ifndef TARANTOOL_PICKLE_H_INCLUDED
#define TARANTOOL_PICKLE_H_INCLUDED
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
#include <stdbool.h>
#include <stdint.h>
#include "exception.h"

/**
 * pickle (pick-little-endian) -- serialize/de-serialize data from
 * tuple and iproto binary formats.
 *
 * load_* - no boundary checking
 * pick_* - throws exception if no data in the buffer
 */

static inline uint32_t
load_u32(const char **data)
{
	const uint32_t *b = (const uint32_t *) *data;
	*data += sizeof(uint32_t);
	return *b;
}

static inline uint32_t
load_varint32(const char **data)
{
	assert(data != NULL && *data != NULL);
	const uint8_t *b = (const uint8_t *) *data;

	if (!(b[0] & 0x80)) {
		*data += 1;
		return (b[0] & 0x7f);
	}
	if (!(b[1] & 0x80)) {
		*data += 2;
		return (b[0] & 0x7f) << 7 | (b[1] & 0x7f);
	}
	if (!(b[2] & 0x80)) {
		*data += 3;
		return (b[0] & 0x7f) << 14 | (b[1] & 0x7f) << 7 | (b[2] & 0x7f);
	}
	if (!(b[3] & 0x80)) {
		*data += 4;
		return (b[0] & 0x7f) << 21 | (b[1] & 0x7f) << 14 |
			(b[2] & 0x7f) << 7 | (b[3] & 0x7f);
	}
	if (!(b[4] & 0x80)) {
		*data += 5;
		return (b[0] & 0x7f) << 28 | (b[1] & 0x7f) << 21 |
			(b[2] & 0x7f) << 14 | (b[3] & 0x7f) << 7 | (b[4] & 0x7f);
	}
	assert(false);
	return 0;
}

static inline uint32_t
pick_varint32(const char **data, const char *end)
{
	assert(data != NULL && *data != NULL);
	const uint8_t *b = (const uint8_t *) *data;
	ssize_t size = end - *data;

	if (unlikely(size < 1))
		tnt_raise(IllegalParams, "varint is too short (expected 1+ bytes)");

	if (!(b[0] & 0x80)) {
		*data += 1;
		return (b[0] & 0x7f);
	}

	if (unlikely(size < 2))
		tnt_raise(IllegalParams, "varint is too short (expected 2+ bytes)");

	if (!(b[1] & 0x80)) {
		*data += 2;
		return (b[0] & 0x7f) << 7 | (b[1] & 0x7f);
	}

	if (unlikely(size < 3))
		tnt_raise(IllegalParams, "BER int is too short (expected 3+ bytes)");

	if (!(b[2] & 0x80)) {
		*data += 3;
		return (b[0] & 0x7f) << 14 | (b[1] & 0x7f) << 7 | (b[2] & 0x7f);
	}

	if (unlikely(size < 4))
		tnt_raise(IllegalParams, "BER int is too short (expected 4+ bytes)");

	if (!(b[3] & 0x80)) {
		*data += 4;
		return (b[0] & 0x7f) << 21 | (b[1] & 0x7f) << 14 |
			(b[2] & 0x7f) << 7 | (b[3] & 0x7f);
	}

	if (unlikely(size < 5))
		tnt_raise(IllegalParams, "BER int is too short (expected 5+ bytes)");

	if (!(b[4] & 0x80)) {
		*data += 5;
		return (b[0] & 0x7f) << 28 | (b[1] & 0x7f) << 21 |
			(b[2] & 0x7f) << 14 | (b[3] & 0x7f) << 7 | (b[4] & 0x7f);
	}

	tnt_raise(IllegalParams, "incorrect BER integer format");
}

#define pick_u(bits)						\
static inline uint##bits##_t					\
pick_u##bits(const char **begin, const char *end)		\
{								\
	if (end - *begin < (bits)/8)				\
		tnt_raise(IllegalParams,			\
			  "packet too short (expected "#bits" bits)");\
	uint##bits##_t r = *(uint##bits##_t *)*begin;		\
	*begin += (bits)/8;					\
	return r;						\
}

pick_u(8)
pick_u(16)
pick_u(32)
pick_u(64)

static inline const char *
pick_str(const char **data, const char *end, uint32_t size)
{
	const char *str = *data;
	if (str + size > end)
		tnt_raise(IllegalParams,
			  "packet too short (expected a field)");
	*data += size;
	return str;
}

static inline const char *
pick_field(const char **data, const char *end)
{
	const char *field = *data;
	uint32_t field_len = pick_varint32(data, end);
	pick_str(data, end, field_len);
	return field;
}

static inline const char *
pick_field_str(const char **data, const char *end, uint32_t *size)
{
	*size = pick_varint32(data, end);
	return pick_str(data, end, *size);
}


static inline uint32_t
pick_field_u32(const char **data, const char *end)
{
	uint32_t size = pick_varint32(data, end);
	if (size != sizeof(uint32_t))
		tnt_raise(IllegalParams,
			  "incorrect packet format (expected a 32-bit int)");
	return *(uint32_t *) pick_str(data, end, size);
}

static inline size_t
varint32_sizeof(uint32_t value)
{
	if (value < (1 << 7))
		return 1;
	if (value < (1 << 14))
		return 2;
	if (value < (1 << 21))
		return 3;
	if (value < (1 << 28))
		return 4;
	return 5;
}

/** The caller must ensure that there is space in the buffer */
static inline char *
pack_varint32(char *buf, uint32_t value)
{
	uint8_t *target = (uint8_t *) buf;
	if (value >= (1 << 7)) {
		if (value >= (1 << 14)) {
			if (value >= (1 << 21)) {
				if (value >= (1 << 28))
					*(target++) = (uint8_t)(value >> 28) | 0x80;
				*(target++) = (uint8_t)(value >> 21) | 0x80;
			}
			*(target++) = (uint8_t)((value >> 14) | 0x80);
		}
		*(target++) = (uint8_t)((value >> 7) | 0x80);
	}
	*(target++) = (uint8_t)((value) & 0x7F);

	return (char *) target;
}

static inline char *
pack_lstr(char *buf, const void *str, uint32_t len)
{
	return (char *) memcpy(pack_varint32(buf, len), str, len) + len;
}

#define pack_u(bits)						\
static inline char *						\
pack_u##bits(char *buf, uint##bits##_t val)			\
{								\
	*(uint##bits##_t *) buf = val;				\
	return buf + sizeof(uint##bits##_t);			\
}

pack_u(8)
pack_u(16)
pack_u(32)
pack_u(64)

#endif /* TARANTOOL_PICKLE_H_INCLUDED */
