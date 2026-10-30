#include "base32_crockford.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#include <stdint.h>
#include <string.h>

enum {
	ULID_LEN = 16,
	ULID_STR_LEN = 26,
};

/**
 * Fixed vectors:
 * - empty buffer;
 * - single-byte encodings (0x00 -> "00", 0x01 -> "04");
 * - ULID-sized payload (16 bytes -> 26 Crockford chars).
 */
static void
fixed_vectors_test(void)
{
	plan(11);
	header();

	/* 1) Empty buffer -> empty string. */
	{
		const uint8_t in[] = {};
		char enc[8];
		uint8_t out[8];

		base32_crockford_encode(in, sizeof(in), enc);
		is(strcmp(enc, ""), 0,
		   "encode empty buffer gives empty string");

		int rc = base32_crockford_decode(enc, out, sizeof(out));
		is(rc, 0, "decode empty string returns 0");
	}

	/* 2) 0x00 -> "00". */
	{
		const uint8_t in[] = {0x00};
		char enc[8];
		uint8_t out[1];

		base32_crockford_encode(in, sizeof(in), enc);
		is(strcmp(enc, "00"), 0, "0x00 encodes to \"00\"");

		int rc = base32_crockford_decode(enc, out, sizeof(out));
		is(rc, 0, "decode \"00\" returns 0");
		is(out[0], 0x00, "decode(\"00\") = 0x00");
	}

	/* 3) 0x01 -> "04". */
	{
		const uint8_t in[] = {0x01};
		char enc[8];
		uint8_t out[1];

		base32_crockford_encode(in, sizeof(in), enc);
		is(strcmp(enc, "04"), 0, "0x01 encodes to \"04\"");

		int rc = base32_crockford_decode(enc, out, sizeof(out));
		is(rc, 0, "decode \"04\" returns 0");
		is(out[0], 0x01, "decode(\"04\") = 0x01");
	}

	/* 4) ULID-sized payload: 16 bytes -> 26 chars. */
	{
		uint8_t in[ULID_LEN];
		for (int i = 0; i < ULID_LEN; i++)
			in[i] = (uint8_t)i;

		char enc[ULID_STR_LEN + 1];
		uint8_t out[ULID_LEN];

		base32_crockford_encode(in, ULID_LEN, enc);
		is(strlen(enc), ULID_STR_LEN,
		   "16 bytes encode into %d chars", ULID_STR_LEN);

		int rc = base32_crockford_decode(enc, out, sizeof(out));
		is(rc, 0, "decode ULID-sized string returns 0");
		is(memcmp(in, out, ULID_LEN), 0,
		   "ULID-sized round-trip matches");
	}

	footer();
	check_plan();
}

/**
 * Invalid characters and buffer size errors:
 */
static void
invalid_and_bounds_test(void)
{
	plan(3);
	header();

	uint8_t out[4];
	int rc;

	/* 1) Invalid char '@'. */
	rc = base32_crockford_decode("00@", out, sizeof(out));
	is(rc, -1, "\"00@\" is rejected");

	/**
	 * 2) Too small output buffer: even 1 byte would not fit full decode.
	 */
	rc = base32_crockford_decode("0000", out, 1);
	is(rc, -1, "decode fails when output buffer is too small");

	/* 3) Zero-sized buffer. */
	rc = base32_crockford_decode("00", out, 0);
	is(rc, -1, "decode fails with zero-sized buffer");

	footer();
	check_plan();
}

/**
 * Round-trip test: encode -> decode -> match.
 */
static void
round_trip_test(void)
{
	plan(14);
	header();

	/* 1) Explicit small buffers. */
	uint8_t buf1[] = {0xAB};
	uint8_t out1[sizeof(buf1)];
	char enc1[16];
	base32_crockford_encode(buf1, sizeof(buf1), enc1);
	ok(base32_crockford_decode(enc1, out1, sizeof(out1)) == 0,
	   "round-trip 1-byte decode ok");
	is(memcmp(buf1, out1, sizeof(buf1)), 0,
	   "round-trip 1-byte matches");

	/* 2) Medium buffer. */
	uint8_t buf2[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	uint8_t out2[sizeof(buf2)];
	char enc2[64];
	base32_crockford_encode(buf2, sizeof(buf2), enc2);
	ok(base32_crockford_decode(enc2, out2, sizeof(out2)) == 0,
	   "round-trip medium decode ok");
	is(memcmp(buf2, out2, sizeof(buf2)), 0,
	   "round-trip medium matches");

	/* 3) Randomized round-trip (5 iterations). */
	for (int it = 0; it < 5; it++) {
		uint8_t in[32];
		uint8_t out[32];
		char enc[128];

		for (int i = 0; i < 32; i++)
			in[i] = (uint8_t)rand();

		base32_crockford_encode(in, 32, enc);
		ok(base32_crockford_decode(enc, out, 32) == 0,
		   "round-trip random decode ok");

		is(memcmp(in, out, 32), 0,
		   "round-trip random matches");
	}

	footer();
	check_plan();
}

/**
 * Invalid tail bits:
 * - input length that leaves non-zero leftover bits after decoding
 *   must be rejected (e.g. 5 Crockford chars → 25 bits, which
 *   cannot be aligned to full bytes).
 */
static void
invalid_tail_test(void)
{
	plan(1);
	header();

	/* One extra Crockford char: 5 leftover bits → must be rejected. */
	const char *s = "00001";
	uint8_t out[8];

	is(base32_crockford_decode(s, out, sizeof(out)), -1,
	   "reject non-zero leftover bits in acc");

	footer();
	check_plan();
}

int
main(void)
{
	plan(4);
	fixed_vectors_test();
	invalid_and_bounds_test();
	round_trip_test();
	invalid_tail_test();
	return check_plan();
}
