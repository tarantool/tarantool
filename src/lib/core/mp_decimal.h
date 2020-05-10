#ifndef TARANTOOL_LIB_CORE_MP_DECIMAL_INCLUDED
#define TARANTOOL_LIB_CORE_MP_DECIMAL_INCLUDED
/*
 * Copyright 2019, Tarantool AUTHORS, please see AUTHORS file.
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

#include "decimal.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * \brief Calculate exact buffer size needed to store a decimal
 * pointed to by \a dec.
 */
uint32_t
mp_sizeof_decimal(const decimal_t *dec);

/**
 * \brief Decode a decimal from MsgPack \a data
 * \param data - buffer pointer
 * \return the decoded decimal
 * \post *data = *data + mp_sizeof_decimal(retval)
 */
decimal_t *
mp_decode_decimal(const char **data, decimal_t *dec);

/**
 * \brief Encode a decimal pointed to by \a dec.
 * \parad dec - decimal pointer
 * \param data - a buffer
 * \return \a data + mp_sizeof_decimal(\a dec)
 */
char *
mp_encode_decimal(char *data, const decimal_t *dec);

/**
 * Print decimal's string representation into a given buffer.
 * @param buf Target buffer to write string to.
 * @param size Buffer size.
 * @param data MessagePack encoded decimal, without MP_EXT header.
 * @param len Length of @a data. If not all data is used, it is
 *        an error.
 * @retval <0 Error. Couldn't decode decimal.
 * @retval >=0 How many bytes were written, or would have been
 *        written, if there was enough buffer space.
 */
int
mp_snprint_decimal(char *buf, int size, const char **data, uint32_t len);

/**
 * Print decimal's string representation into a stream.
 * @param file Target stream to write string to.
 * @param data MessagePack encoded decimal, without MP_EXT header.
 * @param len Length of @a data. If not all data is used, it is
 *        an error.
 * @retval <0 Error. Couldn't decode decimal, or couldn't write to
 *        the stream.
 * @retval >=0 How many bytes were written.
 */
int
mp_fprint_decimal(FILE *file, const char **data, uint32_t len);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif
