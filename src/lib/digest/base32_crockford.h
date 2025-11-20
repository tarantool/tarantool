/**
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Crockford Base32 codec.
 *
 * The codec implements encoding and decoding routines for Crockford's
 * Base32 alphabet:
 *
 *   "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
 *
 * During decoding, several visually ambiguous characters are accepted
 * and normalized:
 *
 *   'i', 'I', 'l', 'L' -> '1'
 *   'o', 'O'           -> '0'
 *
 * The implementation is case-insensitive and does not use padding.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encode a binary buffer using Crockford Base32 alphabet.
 *
 * The function encodes @a in_len bytes from @a in and writes ASCII
 * representation into @a out. The result is always NUL-terminated.
 *
 * The caller MUST ensure that @a out is large enough to hold the encoded
 * string.
 *
 * \param in       Pointer to input buffer.
 * \param in_len   Size of input buffer in bytes.
 * \param out      Pointer to output buffer for encoded string.
 */
void
base32_crockford_encode(const uint8_t *in, size_t in_len, char *out);

/**
 * Decode Crockford Base32 encoded string.
 *
 * The function decodes @a in_len characters from @a in and writes
 * resulting bytes into @a out. The input is case-insensitive and
 * accepts ambiguous characters listed in the module description.
 *
 * \param in       Pointer to input string.
 * \param out      Pointer to output buffer for decoded bytes.
 * \param out_len  Size of output buffer in bytes.
 *
 * \retval  0  Success.
 * \retval -1  Invalid input or buffer @a out is too small.
 */
int
base32_crockford_decode(const char *in, uint8_t *out, size_t out_len);

#ifdef __cplusplus
}
#endif
