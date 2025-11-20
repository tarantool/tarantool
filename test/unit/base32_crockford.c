#include "base32_crockford.h"

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
 * - single-byte encodings (0x00 → "00", 0x01 → "04");
 * - ULID-sized payload (16 bytes → 26 Crockford chars).
 */
static void
test_fixed_vectors(void)
{
	plan(11);

	/** 1) Empty buffer -> empty string. */
	{
		const uint8_t in[] = {};
		char enc[8];
		uint8_t out[8];

		base32_crockford_encode(in, sizeof(in), enc);
		is(strcmp(enc, "") == 0,
		   "encode empty buffer gives empty string");

		int rc = base32_crockford_decode(enc, out, sizeof(out));
		is(rc, 0, "decode empty string returns 0");
	}

	/** 2) 0x00 → "00". */
	{
		const uint8_t in[] = {0x00};
		char enc[8];
		uint8_t out[1];

		base32_crockford_encode(in, sizeof(in), enc);
		is(strcmp(enc, "00") == 0, "0x00 encodes to \"00\"");

		int rc = base32_crockford_decode(enc, out, sizeof(out));
		is(rc, 0, "decode \"00\" returns 0");
		is(out[0], 0x00, "decode(\"00\") = 0x00");
	}

	/** 3) 0x01 → "04". */
	{
		const uint8_t in[] = {0x01};
		char enc[8];
		uint8_t out[1];

		base32_crockford_encode(in, sizeof(in), enc);
		is(strcmp(enc, "04") == 0, "0x01 encodes to \"04\"");

		int rc = base32_crockford_decode(enc, out, sizeof(out));
		is(rc, 0, "decode \"04\" returns 0");
		is(out[0], 0x01, "decode(\"04\") = 0x01");
	}

	/** 4) ULID-sized payload: 16 bytes -> 26 chars. */
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

	check_plan();
}

/**
 * Ambiguous Crockford characters:
 * - case-insensitive;
 * - 'I', 'i', 'L', 'l' → 1
 * - 'O', 'o'           → 0
 */
static void
test_symbol_aliases(void)
{
	plan(7);

	uint8_t ref[2];
	uint8_t tmp[2];
	int rc;

	memset(ref, 0, sizeof(ref));
	memset(tmp, 0, sizeof(tmp));

	/** "01" as reference. */
	rc = base32_crockford_decode("01", ref, sizeof(ref));
	is(rc, 0, "decode canonical \"01\"");

	/** O → 0 */
	memset(tmp, 0, sizeof(tmp));
	rc = base32_crockford_decode("O1", tmp, sizeof(tmp));
	is(rc, 0, "decode \"O1\" returns 0");
	is(memcmp(ref, tmp, sizeof(ref)), 0,
	   "\"O1\" decodes same as \"01\"");

	/** I → 1 */
	memset(tmp, 0, sizeof(tmp));
	rc = base32_crockford_decode("0I", tmp, sizeof(tmp));
	is(rc, 0, "decode \"0I\" returns 0");
	is(memcmp(ref, tmp, sizeof(ref)), 0,
	   "\"0I\" decodes same as \"01\"");

	/** L → 1 */
	memset(tmp, 0, sizeof(tmp));
	rc = base32_crockford_decode("0L", tmp, sizeof(tmp));
	is(rc, 0, "decode \"0L\" returns 0");
	is(memcmp(ref, tmp, sizeof(ref)), 0,
	   "\"0L\" decodes same as \"01\"");

	check_plan();
}

/**
 * ULID-specific ambiguity test:
 * canonical, lowercase and ambiguous variants of the same ULID
 * must decode to identical 16-byte values.
 */
static void
test_crockford_ambiguities(void)
{
	plan(4);

	const char *canonical = "0000ZWSEQS5JHEQPV3MNWJ7ZEM";
	const char *lower = "0000zwseqs5jheqpv3mnwj7zem";
	const char *ambiguous = "OOOOzwseqs5jheqpv3mnwj7zem"; /** 'O' → 0 */

	uint8_t buf1[ULID_LEN];
	uint8_t buf2[ULID_LEN];
	uint8_t buf3[ULID_LEN];

	int rc = base32_crockford_decode(canonical, buf1, sizeof(buf1));
	is(rc, 0, "decode canonical ULID string");

	rc = base32_crockford_decode(lower, buf2, sizeof(buf2));
	is(rc, 0, "decode lowercase ULID string");
	is(memcmp(buf1, buf2, ULID_LEN), 0, "lowercase decodes identically");

	rc = base32_crockford_decode(ambiguous, buf3, sizeof(buf3));
	is(rc, 0, "decode ambiguous ULID string");
	is(memcmp(buf1, buf3, ULID_LEN), 0,
	   "ambiguous ULID decodes identically");

	check_plan();
}

/**
 * Invalid characters and buffer size errors:
 */
static void
test_invalid_and_bounds(void)
{
	plan(3);

	uint8_t out[4];
	int rc;

	/** 1) Invalid char '@'. */
	rc = base32_crockford_decode("00@", out, sizeof(out));
	is(rc, -1, "\"00@\" is rejected");

	/**
	 * 2) Too small output buffer: even 1 byte would not fit full decode.
	 */
	rc = base32_crockford_decode("0000", out, 1);
	is(rc, -1, "decode fails when output buffer is too small");

	/** 3) Zero-sized buffer. */
	rc = base32_crockford_decode("00", out, 0);
	is(rc, -1, "decode fails with zero-sized buffer");

	check_plan();
}

int
main(void)
{
	plan(4);

	test_fixed_vectors();
	test_symbol_aliases();
	test_crockford_ambiguities();
	test_invalid_and_bounds();

	return check_plan();
}
