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

#include "tt_ulid.h"

#include <random.h>

#include <stdint.h>
#include <string.h>
#include <time.h>

#include "trivia/util.h"

/* Zeroed by the linker. */
const struct tt_ulid ulid_nil;

static_assert(sizeof(struct tt_ulid) == ULID_LEN, "ULID size mismatch");

/* Crockford Base32 alphabet. */
static const char ALPH[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
static int8_t INV[256];

/* State for monotonic generator. */
static uint64_t last_ms;
static uint8_t last_rand[10];

static uint64_t
realtime_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

void
tt_ulid_create(struct tt_ulid *u)
{
	uint64_t ms = realtime_ms();
	if (ms < last_ms)
		ms = last_ms;

	uint8_t rand80[10];
	random_bytes((char *)rand80, sizeof(rand80));

	if (ms == last_ms) {
		if (memcmp(rand80, last_rand, sizeof(rand80)) <= 0) {
			memcpy(rand80, last_rand, sizeof(rand80));
			for (int i = 9; i >= 0; i--) {
				rand80[i]++;
				if (rand80[i] != 0)
					break;
			}
		}
	}

	last_ms = ms;
	memcpy(last_rand, rand80, sizeof(last_rand));

	u->bytes[0] = (ms >> 40) & 0xff;
	u->bytes[1] = (ms >> 32) & 0xff;
	u->bytes[2] = (ms >> 24) & 0xff;
	u->bytes[3] = (ms >> 16) & 0xff;
	u->bytes[4] = (ms >> 8) & 0xff;
	u->bytes[5] = (ms >> 0) & 0xff;

	memcpy(&u->bytes[6], rand80, sizeof(rand80));
}

extern inline bool
tt_ulid_is_nil(const struct tt_ulid *u);

extern inline bool
tt_ulid_is_equal(const struct tt_ulid *a, const struct tt_ulid *b);

extern inline int
tt_ulid_compare(const struct tt_ulid *a, const struct tt_ulid *b);

__attribute__((constructor))
static void
ulid_codec_init(void)
{
	for (int i = 0; i < 256; i++)
		INV[i] = -1;

	for (int i = 0; ALPH[i] != '\0'; i++) {
		char up = ALPH[i];
		INV[(uint8_t)up] = i;
		if (up >= 'A' && up <= 'Z')
			INV[(uint8_t)(up + 32)] = i; /* lower-case */
	}

	/* Alias characters. */
	INV['I'] = INV['1'];
	INV['L'] = INV['1'];
	INV['O'] = INV['0'];
	INV['U'] = -1;
}

int
tt_ulid_from_string(const char *in, struct tt_ulid *u)
{
	if (in == NULL)
		return -1;

	int len = (int)strlen(in);
	if (len != ULID_STR_LEN)
		return -1;

	uint32_t acc = 0;
	int bits = 0;
	int pos = 0;

	for (int i = 0; i < ULID_STR_LEN; i++) {
		int8_t v = INV[(uint8_t)in[i]];
		if (v < 0)
			return -1;

		acc = (acc << 5) | (uint32_t)v;
		bits += 5;

		while (bits >= 8) {
			bits -= 8;
			if (pos >= ULID_LEN)
				return -1;
			u->bytes[pos++] = (acc >> bits) & 0xff;
		}
	}

	return pos == ULID_LEN ? 0 : -1;
}

void
tt_ulid_to_string(const struct tt_ulid *u, char *out)
{
	uint32_t acc = 0;
	int bits = 0;
	int pos = 0;

	for (int i = 0; i < ULID_LEN; i++) {
		acc = (acc << 8) | u->bytes[i];
		bits += 8;

		while (bits >= 5) {
			bits -= 5;
			out[pos++] = ALPH[(acc >> bits) & 31];
		}
	}

	if (bits > 0)
		out[pos++] = ALPH[(acc << (5 - bits)) & 31];

	out[pos] = '\0';
}
