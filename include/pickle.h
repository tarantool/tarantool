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
