#ifndef TARANTOOL_UUID_H_INCLUDED
#define TARANTOOL_UUID_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h> /* snprintf */
#include <lib/bit/bit.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum { UUID_LEN = 16, UUID_STR_LEN = 36 };

/**
 * \brief UUID structure struct
 */
struct tt_uuid {
	uint32_t time_low;
	uint16_t time_mid;
	uint16_t time_hi_and_version;
	uint8_t clock_seq_hi_and_reserved;
	uint8_t clock_seq_low;
	uint8_t node[6];
};

/**
 * \brief Generate new UUID
 * \param uu[out] UUID
 */
void
tt_uuid_create(struct tt_uuid *uu);

/**
 * \brief Parse UUID from string.
 * \param in string
 * \param uu[out] UUID
 * \return
 */
inline int
tt_uuid_from_string(const char *in, struct tt_uuid *uu)
{
	if (strlen(in) != UUID_STR_LEN ||
	    sscanf(in, "%8x-%4hx-%4hx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
		   &uu->time_low, &uu->time_mid, &uu->time_hi_and_version,
		   &uu->clock_seq_hi_and_reserved, &uu->clock_seq_low,
		   &uu->node[0], &uu->node[1], &uu->node[2], &uu->node[3],
		   &uu->node[4], &uu->node[5]) != 11)
		return 1;

	/* Check variant (NCS, RFC4122, MSFT) */
	uint8_t n = uu->clock_seq_hi_and_reserved;
	if ((n & 0x80) != 0x00 && (n & 0xc0) != 0x80 &&	(n & 0xe0) != 0xc0)
		return 1;
	return 0;
}

/**
 * \brief Compare UUIDs lexicographically.
 * \param a UUID
 * \param b UUID
 * \retval comparison result, as in strcmp()
 */
inline int
tt_uuid_compare(const struct tt_uuid *a, const struct tt_uuid *b)
{
#define cmp_tt_uuid_field(field)                \
        if (a->field > b->field) return 1;      \
        if (a->field < b->field) return -1;

        cmp_tt_uuid_field(time_low);
        cmp_tt_uuid_field(time_mid);
        cmp_tt_uuid_field(time_hi_and_version);
        cmp_tt_uuid_field(clock_seq_hi_and_reserved);
        cmp_tt_uuid_field(clock_seq_low);
        cmp_tt_uuid_field(node[0]);
        cmp_tt_uuid_field(node[1]);
        cmp_tt_uuid_field(node[2]);
        cmp_tt_uuid_field(node[3]);
        cmp_tt_uuid_field(node[4]);
        cmp_tt_uuid_field(node[5]);

#undef cmp_tt_uuid_field

        return 0;
}

/**
 * \brief Format UUID to RFC 4122 string.
 * \param uu uuid
 * \param[out] out buffer, must be at least UUID_STR_LEN + 1 length
 */
inline void
tt_uuid_to_string(const struct tt_uuid *uu, char *out)
{
	snprintf(out, UUID_STR_LEN + 1,
		"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uu->time_low, uu->time_mid, uu->time_hi_and_version,
		uu->clock_seq_hi_and_reserved, uu->clock_seq_low, uu->node[0],
		uu->node[1], uu->node[2], uu->node[3], uu->node[4], uu->node[5]);
}

/**
 * \brief Return byte order swapped UUID (LE -> BE and vice versa)
 * \param uu
 */
inline void
tt_uuid_bswap(struct tt_uuid *uu)
{
	uu->time_low = bswap_u32(uu->time_low);
	uu->time_mid = bswap_u16(uu->time_mid);
	uu->time_hi_and_version = bswap_u16(uu->time_hi_and_version);
}

/**
 * \brief Test that uuid is nil
 * \param uu UUID
 * \retval true if all members of \a uu 0
 * \retval false otherwise
 */
inline bool
tt_uuid_is_nil(const struct tt_uuid *uu)
{
	const uint64_t *p = (const uint64_t *) uu;
	return !p[0] && !p[1];
}

/**
 * \brief Test that \a lhs equal \a rhs
 * \param lhs UUID
 * \param rhs UUID
 * \retval true if \a lhs equal \a rhs
 * \retval false otherwise
 */
inline bool
tt_uuid_is_equal(const struct tt_uuid *lhs, const struct tt_uuid *rhs)
{
	const uint64_t *lp = (const uint64_t *) lhs;
	const uint64_t *rp = (const uint64_t *) rhs;
	return lp[0] == rp[0] && lp[1] == rp[1];
}

extern const struct tt_uuid uuid_nil;

char *
tt_uuid_str(const struct tt_uuid *uu);

int
tt_uuid_from_strl(const char *in, size_t len, struct tt_uuid *uu);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_UUID_H_INCLUDED */
