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
#include "scramble.h"
#include <sha1.h>
#include <base64.h>
#include <string.h>
#include <stdio.h>

static void
xor(unsigned char *to, unsigned const char *left,
    unsigned const char *right, uint32_t len)
{
	const uint8_t *end = to + len;
	while (to < end)
		*to++= *left++ ^ *right++;
}

void
scramble_prepare(void *out, const void *salt, const void *password,
		 int password_len)
{
	unsigned char hash1[SCRAMBLE_SIZE];
	unsigned char hash2[SCRAMBLE_SIZE];
	SHA1_CTX ctx;

	SHA1Init(&ctx);
	SHA1Update(&ctx, password, password_len);
	SHA1Final(hash1, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, hash1, SCRAMBLE_SIZE);
	SHA1Final(hash2, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, salt, SCRAMBLE_SIZE);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(out, &ctx);

	xor(out, hash1, out, SCRAMBLE_SIZE);
}

void
scramble_reencode(void *out, const void *in, const void *salt,
		  const void *msalt, const void *hash2)
{
	unsigned char hash1[SCRAMBLE_SIZE];
	unsigned char sh[SCRAMBLE_SIZE];
	SHA1_CTX ctx;

	SHA1Init(&ctx);
	SHA1Update(&ctx, salt, SCRAMBLE_SIZE);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(sh, &ctx);

	xor(hash1, in, sh, SCRAMBLE_SIZE);

	SHA1Init(&ctx);
	SHA1Update(&ctx, msalt, SCRAMBLE_SIZE);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(out, &ctx);

	xor(out, hash1, out, SCRAMBLE_SIZE);
}

int
scramble_check(const void *scramble, const void *salt, const void *hash2)
{
	SHA1_CTX ctx;
	unsigned char candidate_hash2[SCRAMBLE_SIZE];

	SHA1Init(&ctx);
	SHA1Update(&ctx, salt, SCRAMBLE_SIZE);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(candidate_hash2, &ctx);

	xor(candidate_hash2, candidate_hash2, scramble, SCRAMBLE_SIZE);
	/*
	 * candidate_hash2 now supposedly contains hash1, turn it
	 * into hash2
	 */
	SHA1Init(&ctx);
	SHA1Update(&ctx, candidate_hash2, SCRAMBLE_SIZE);
	SHA1Final(candidate_hash2, &ctx);

	return memcmp(hash2, candidate_hash2, SCRAMBLE_SIZE);
}

void
password_prepare(const char *password, int len, char *out, int out_len)
{
	unsigned char hash2[SCRAMBLE_SIZE];
	SHA1_CTX ctx;

	SHA1Init(&ctx);
	SHA1Update(&ctx, (const unsigned char *) password, len);
	SHA1Final(hash2, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(hash2, &ctx);

	base64_encode((char *) hash2, SCRAMBLE_SIZE, out, out_len, 0);
}
