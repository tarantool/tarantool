/**
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "tt_ulid.h"
#include "core/random.h"
#include "core/clock.h"
#include "errinj.h"
#include "base32_crockford.h"
#include "bit/bit.h"

#include <stdint.h>
#include <string.h>

/** Zeroed by the linker. */
const struct tt_ulid ulid_nil;

static_assert(sizeof(struct tt_ulid) == ULID_LEN, "ULID size mismatch");

/** State for monotonic generator. */
static uint64_t last_ms;
static uint8_t last_rand[ULID_RAND_LEN];

int
tt_ulid_create(struct tt_ulid *u)
{
	uint8_t rand80[ULID_RAND_LEN];

	/* Real Unix time in milliseconds. */
	uint64_t ms = (uint64_t)(clock_realtime() * 1000);

	/* ULID must be monotonic even if clock goes backwards. */
	if (ms < last_ms)
		ms = last_ms;

	ERROR_INJECT(ERRINJ_ULID_TIME_FREEZE, {
		if (last_ms != 0)
			ms = last_ms;
	});

	if (ms != last_ms) {
		random_bytes((char *)rand80, sizeof(rand80));
	} else {
		memcpy(rand80, last_rand, sizeof(rand80));

		bool overflow = true;
		for (int i = ULID_RAND_LEN - 1; i >= 0; i--) {
			rand80[i]++;
			if (rand80[i] != 0) {
				overflow = false;
				break;
			}
		}

		ERROR_INJECT(ERRINJ_ULID_RAND_OVERFLOW, {
			overflow = true;
		});

		/*
		 * Real overflow can only happen after generating 2^80
		 * ULIDs within the same millisecond - practically
		 * impossible, but the check is required for
		 * correctness of the ULID specification.
		 */
		if (overflow)
			return 1;
	}

	last_ms = ms;
	memcpy(last_rand, rand80, sizeof(last_rand));

	uint64_t ts_be = bswap_u64(ms);
	memcpy(u->bytes, (const uint8_t *)&ts_be + 2, 6);
	memcpy(&u->bytes[6], rand80, sizeof(rand80));

	return 0;
}

int
tt_ulid_from_string(const char *in, struct tt_ulid *u)
{
	if (strlen(in) != ULID_STR_LEN)
		return 1;

	int rc = base32_crockford_decode(in, u->bytes, ULID_LEN);
	return rc == 0 ? 0 : 1;
}

void
tt_ulid_to_string(const struct tt_ulid *u, char *out)
{
	base32_crockford_encode(u->bytes, ULID_LEN, out);
}

extern inline bool
tt_ulid_is_equal(const struct tt_ulid *lhs, const struct tt_ulid *rhs);

extern inline int
tt_ulid_compare(const struct tt_ulid *lhs, const struct tt_ulid *rhs);

extern inline bool
tt_ulid_is_nil(const struct tt_ulid *u);
