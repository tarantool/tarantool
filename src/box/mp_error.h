#pragma once
/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <stdint.h>
#include <assert.h>

struct mpstream;

/**
 * @brief Encode error to mpstream as MP_ERROR.
 * @param error pointer to struct error for encoding.
 * @param stream pointer to output stream.
 */
void
error_to_mpstream(const struct error *error, struct mpstream *stream);

void
error_to_mpstream_noext(const struct error *error, struct mpstream *stream);

struct error *
error_unpack_unsafe(const char **data);

/**
 * @brief Unpack MP_ERROR to error.
 * @param data pointer to MP_ERROR.
 * @param len data size.
 * @return struct error * or NULL if failed.
 */
static inline struct error *
error_unpack(const char **data, uint32_t len)
{
	const char *end = *data + len;
	struct error *e = error_unpack_unsafe(data);
	(void)end;
	assert(*data == end);
	return e;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
