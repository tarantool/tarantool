/*
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
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
 *    copyright notice, this list of conditions and the
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
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum { ULID_LEN = 16, ULID_STR_LEN = 26 };

/**
 * \brief ULID structure.
 *
 * Binary representation:
 *  - bytes[0..5]  : 48-bit big-endian timestamp in milliseconds
 *  - bytes[6..15] : 80-bit entropy / monotonic counter
 */
struct tt_ulid {
	uint8_t bytes[ULID_LEN];
};

/** \brief All-zero ULID value. */
extern const struct tt_ulid ulid_nil;

/**
 * \brief Generate new ULID (monotonic variant).
 *
 * The timestamp is taken from real-time clock in milliseconds since
 * Unix epoch and clamped not to go backwards. For the same millisecond
 * the entropy part is made strictly monotonic to preserve total order.
 *
 * \param u[out] ULID.
 */
void
tt_ulid_create(struct tt_ulid *u);

/**
 * \brief Parse ULID from Crockford Base32 string.
 *
 * The input string must be exactly ULID_STR_LEN (26) characters long
 * and use Crockford Base32 alphabet.
 *
 * \param in   string representation.
 * \param u[out] parsed ULID.
 *
 * \retval  0 Success.
 * \retval -1 Error (invalid length or characters).
 */
int
tt_ulid_from_string(const char *in, struct tt_ulid *u);

/**
 * \brief Format ULID to Crockford Base32 string.
 *
 * The output buffer must be at least ULID_STR_LEN + 1 bytes long.
 *
 * \param u   ULID.
 * \param[out] out buffer for NUL-terminated string.
 */
void
tt_ulid_to_string(const struct tt_ulid *u, char *out);

/**
 * \brief Test that two ULIDs are equal.
 *
 * \param lhs ULID.
 * \param rhs ULID.
 *
 * \retval true  if \a lhs equal \a rhs.
 * \retval false otherwise.
 */
inline bool
tt_ulid_is_equal(const struct tt_ulid *lhs, const struct tt_ulid *rhs)
{
	return memcmp(lhs, rhs, sizeof(*lhs)) == 0;
}

/**
 * \brief Test that ULID is nil.
 *
 * \param u ULID.
 *
 * \retval true  if all bytes are zero.
 * \retval false otherwise.
 */
inline bool
tt_ulid_is_nil(const struct tt_ulid *u)
{
	return tt_ulid_is_equal(u, &ulid_nil);
}

/**
 * \brief Compare ULIDs lexicographically.
 *
 * \param a ULID.
 * \param b ULID.
 *
 * \retval comparison result, as in strcmp()
 */
inline int
tt_ulid_compare(const struct tt_ulid *a, const struct tt_ulid *b)
{
	return memcmp(a, b, ULID_LEN);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif
