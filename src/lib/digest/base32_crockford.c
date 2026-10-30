/**
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "base32_crockford.h"

#include <ctype.h>
#include <string.h>

/**
 * Crockford's Base32 alphabet:
 *  - digits 0-9;
 *  - letters A-Z without I, L, O, U.
 */
static const unsigned char crockford_alphabet[] =
	"0123456789ABCDEFGHJKMNPQRSTVWXYZ";

/** Fast mapping from ASCII char to 5-bit Crockford value, -1 for invalid. */
static int8_t crockford_inv[256];

__attribute__((constructor))
static void
base32_crockford_init(void)
{
	memset(crockford_inv, 0xff, sizeof(crockford_inv));

	for (int i = 0; crockford_alphabet[i]; i++) {
		crockford_inv[crockford_alphabet[i]] = i;
		crockford_inv[tolower(crockford_alphabet[i])] = i;
	}

	/* Ambiguous aliases */
	crockford_inv['I'] = 1;
	crockford_inv['i'] = 1;
	crockford_inv['L'] = 1;
	crockford_inv['l'] = 1;
	crockford_inv['O'] = 0;
	crockford_inv['o'] = 0;
}

void
base32_crockford_encode(const uint8_t *in, size_t len, char *out)
{
	uint32_t acc = 0;
	int bits = 0;
	size_t pos = 0;

	for (size_t i = 0; i < len; i++) {
		acc = (acc << 8) | in[i];
		bits += 8;

		while (bits >= 5) {
			bits -= 5;
			out[pos++] = crockford_alphabet[(acc >> bits) & 0x1f];
		}
	}

	if (bits > 0)
		out[pos++] = crockford_alphabet[(acc << (5 - bits)) & 0x1f];

	out[pos] = '\0';
}

int
base32_crockford_decode(const char *in, uint8_t *out, size_t out_len)
{
	uint32_t acc = 0;
	int bits = 0;
	size_t pos = 0;

	for (size_t i = 0; in[i] != '\0'; i++) {
		int8_t v = crockford_inv[(uint8_t)in[i]];
		if (v < 0)
			return -1;

		acc = (acc << 5) | v;
		bits += 5;

		if (bits >= 8) {
			if (pos >= out_len)
				return -1;

			bits -= 8;
			out[pos++] = (acc >> bits);
		}
	}

	if (bits > 0) {
		uint32_t mask = (1u << bits) - 1u;
		if ((acc & mask) != 0)
			return -1;
	}
	return 0;
}
