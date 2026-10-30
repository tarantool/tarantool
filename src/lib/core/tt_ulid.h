/**
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum { ULID_LEN = 16, ULID_STR_LEN = 26, ULID_RAND_LEN = 10 };

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
 * \brief Generate new ULID.
 *
 * The timestamp is taken from real-time clock in milliseconds since
 * Unix epoch. For the same millisecond the entropy part is made
 * strictly monotonic to preserve total order.
 *
 * \param[out] u Pointer to ULID structure to write the result into.
 *
 * \retval 0  Success.
 * \retval 1  Random component overflow (no ULID generated).
 */
int
tt_ulid_create(struct tt_ulid *u);

/**
 * \brief Parse ULID from Crockford Base32 string.
 *
 * The input string must be exactly ULID_STR_LEN (26) characters long
 * and use Crockford Base32 alphabet.
 *
 * \param[in]  in ULID string representation.
 * \param[out] u  Parsed ULID.
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
 * \param[in]  u   ULID value.
 * \param[out] out Buffer for a NUL-terminated string.
 */
void
tt_ulid_to_string(const struct tt_ulid *u, char *out);

/**
 * \brief Compare ULIDs lexicographically.
 *
 * Returns:
 *   < 0 if lhs < rhs
 *   = 0 if lhs = rhs
 *   > 0 if lhs > rhs
 *
 * \param[in] lhs First ULID.
 * \param[in] rhs Second ULID.
 *
 * \retval comparison result, as in strcmp()
 */
inline int
tt_ulid_compare(const struct tt_ulid *lhs, const struct tt_ulid *rhs)
{
	return memcmp(lhs, rhs, ULID_LEN);
}

/**
 * \brief Test that two ULIDs are equal.
 *
 * \param[in] lhs  First ULID.
 * \param[in] rhs  Second ULID.
 *
 * \retval true  Values are equal.
 * \retval false Values differ.
 */
inline bool
tt_ulid_is_equal(const struct tt_ulid *lhs, const struct tt_ulid *rhs)
{
	return memcmp(lhs, rhs, sizeof(*lhs)) == 0;
}

/**
 * \brief Test that ULID is nil.
 *
 * \param[in] u  ULID to check.
 *
 * \retval true  All bytes are zero.
 * \retval false Otherwise.
 */
inline bool
tt_ulid_is_nil(const struct tt_ulid *u)
{
	return tt_ulid_is_equal(u, &ulid_nil);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif
