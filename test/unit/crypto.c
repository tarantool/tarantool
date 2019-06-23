/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "crypto/crypto.h"
#include "core/random.h"
#include "unit.h"
#include "trivia/util.h"
#include "memory.h"
#include "fiber.h"

static void
test_aes128_codec(void)
{
	header();
	plan(20);

	char key[CRYPTO_AES128_KEY_SIZE];
	char iv[CRYPTO_AES_IV_SIZE], iv2[CRYPTO_AES_IV_SIZE];
	random_bytes(key, sizeof(key));
	struct crypto_codec *c =
		crypto_codec_new(CRYPTO_ALGO_AES128, CRYPTO_MODE_CBC,
				 key, sizeof(key));

	int rc = crypto_codec_encrypt(c, NULL, NULL, 10, NULL, 0);
	is(rc, 26, "encrypt returns needed number of bytes");
	rc = crypto_codec_encrypt(c, NULL, NULL, 10, NULL, 15);
	is(rc, 26, "encrypt does not write anything when too small "\
	   "buffer");
	rc = crypto_codec_encrypt(c, NULL, NULL, 0, NULL, 0);
	is(rc, 16, "encrypt does not allow 0 sized buffer");
	rc = crypto_codec_encrypt(c, NULL, NULL, 32, NULL, 0);
	is(rc, 48, "encrypt requires additional block when buffer "\
	   "size is multiple of block size");

	const char *plain = "plain text";
	int plain_size = strlen(plain) + 1;
	char buffer1[128], buffer2[128];
	memset(buffer1, 0, sizeof(buffer1));
	memset(buffer2, 0, sizeof(buffer2));
	int buffer_size = sizeof(buffer1);
	int iv_size = crypto_codec_gen_iv(c, iv, sizeof(iv));
	is(iv_size, CRYPTO_AES_IV_SIZE, "AES 126 IV size is %d",
	   CRYPTO_AES_IV_SIZE);

	rc = crypto_codec_encrypt(c, iv, plain, plain_size,
				  buffer1, buffer_size);
	is(rc, 16, "encrypt works when buffer is big enough");
	rc = crypto_codec_encrypt(c, iv, plain, plain_size,
				  buffer2, buffer_size);
	is(rc, 16, "encrypt returns the same on second call");
	is(memcmp(buffer1, buffer2, rc), 0, "encrypted data is the same");
	isnt(memcmp(buffer1, plain, plain_size), 0,
	     "and it is not just copied from the plain text");

	rc = crypto_codec_decrypt(c, iv, NULL, 16, NULL, 0);
	is(rc, 32, "decrypt also checks length and returns needed number "\
	   "of bytes");
	rc = crypto_codec_decrypt(c, iv, buffer1, 16, buffer2, buffer_size);
	is(rc, plain_size, "decrypt returns correct number of bytes");
	is(memcmp(buffer2, plain, plain_size), 0,
	   "and correctly decrypts data");
	/*
	 * Create a different IV to ensure it does not decrypt a
	 * message with the original IV.
	 */
	for (int i = 0; i < CRYPTO_AES_IV_SIZE; ++i)
		iv[i]++;
	rc = crypto_codec_decrypt(c, iv, buffer1, 16, buffer2, buffer_size);
	ok(rc == -1 || rc != plain_size || memcmp(buffer1, buffer2, rc) != 0,
	   "decrypt can't correctly decode anything with a wrong IV");
	ok(rc != -1 || ! diag_is_empty(diag_get()),
	   "in case decrypt has totally failed, diag is set");

	crypto_codec_gen_iv(c, iv2, sizeof(iv2));
	rc = crypto_codec_encrypt(c, iv2, plain, plain_size,
				  buffer2, buffer_size);
	is(rc, 16, "encrypt with different IV and the same number of written "\
	   "bytes returned")
	isnt(memcmp(buffer2, buffer1, rc), 0,
	     "the encrypted data looks different");
	rc = crypto_codec_decrypt(c, iv2, buffer2, 16, buffer1, buffer_size);
	is(rc, plain_size, "decrypt works with correct but another IV");
	is(memcmp(buffer1, plain, plain_size), 0, "data is the same");

	struct crypto_codec *c2 =
		crypto_codec_new(CRYPTO_ALGO_AES128, CRYPTO_MODE_CBC,
				 key, sizeof(key));
	rc = crypto_codec_encrypt(c, iv2, plain, plain_size,
				  buffer1, buffer_size);
	memset(buffer2, 0, rc);
	rc = crypto_codec_decrypt(c2, iv2, buffer1, rc, buffer2, buffer_size);
	is(rc, plain_size, "encrypt with one codec, but decrypt with another "\
	   "codec and the same key");
	is(memcmp(plain, buffer2, plain_size), 0, "data is the same");

	crypto_codec_delete(c2);
	crypto_codec_delete(c);

	check_plan();
	footer();
}

static void
test_aes128_stress(void)
{
	header();
	plan(1);
	char key[CRYPTO_AES128_KEY_SIZE], iv[CRYPTO_AES_IV_SIZE];
	random_bytes(key, sizeof(key));
	struct crypto_codec *c =
		crypto_codec_new(CRYPTO_ALGO_AES128, CRYPTO_MODE_CBC,
				 key, sizeof(key));

	char plain[515], cipher[1024], result[1024];
	int rc, iv_size, size = 10;
	for (int size = 10; size < (int) sizeof(plain); size += 10) {
		random_bytes(plain, size);
		rc = crypto_codec_gen_iv(c, iv, sizeof(iv));
		fail_if(rc != sizeof(iv));
		rc = crypto_codec_encrypt(c, iv, plain, size,
					  cipher, sizeof(cipher));
		rc = crypto_codec_decrypt(c, iv, cipher, rc,
					  result, sizeof(result));
		fail_if(memcmp(result, plain, rc) != 0);
	}
	ok(true, "try encrypt/decrypt on a variety of sizes, keys, and ivs");

	check_plan();
	crypto_codec_delete(c);
	footer();
}

static void
test_algo_mode_key(enum crypto_algo algo, enum crypto_mode mode, int key_size)
{
	char key[CRYPTO_MAX_KEY_SIZE], buffer1[128], buffer2[128], plain[128];
	char iv[CRYPTO_MAX_IV_SIZE];
	int plain_size = rand() % 100;
	random_bytes(plain, plain_size);
	random_bytes(key, key_size);
	int buffer_size = sizeof(buffer1);
	struct crypto_codec *c = crypto_codec_new(algo, mode, key, key_size);
	int iv_size = crypto_codec_gen_iv(c, iv, sizeof(iv));
	is(iv_size, crypto_codec_iv_size(c), "%s %d %s, create iv of size %d",
	   crypto_algo_strs[algo], key_size, crypto_mode_strs[mode], iv_size);
	int encoded = crypto_codec_encrypt(c, iv, plain, plain_size,
					   buffer1, buffer_size);
	ok(encoded >= 0, "encode");
	int decoded = crypto_codec_decrypt(c, iv, buffer1, encoded,
					   buffer2, buffer_size);
	is(decoded, plain_size, "decode");
	is(memcmp(plain, buffer2, plain_size), 0, "data is correct");
	crypto_codec_delete(c);
}

static inline void
test_algo_key(enum crypto_algo algo, int key_size)
{
	for (enum crypto_mode mode = 0; mode < crypto_mode_MAX; ++mode)
		test_algo_mode_key(algo, mode, key_size);
}

static void
test_each(void)
{
	header();
	plan(80);

	test_algo_key(CRYPTO_ALGO_NONE, 0);
	test_algo_key(CRYPTO_ALGO_AES128, CRYPTO_AES128_KEY_SIZE);
	test_algo_key(CRYPTO_ALGO_AES192, CRYPTO_AES192_KEY_SIZE);
	test_algo_key(CRYPTO_ALGO_AES256, CRYPTO_AES256_KEY_SIZE);
	test_algo_key(CRYPTO_ALGO_DES, CRYPTO_DES_KEY_SIZE);

	check_plan();
	footer();
}

static void
test_stream(void)
{
	header();
	plan(11);

	char key[CRYPTO_AES128_KEY_SIZE], iv[CRYPTO_AES_IV_SIZE];
	char buffer1[128], buffer2[128];
	random_bytes(key, sizeof(key));
	random_bytes(iv, sizeof(iv));
	struct crypto_stream *encoder =
		crypto_stream_new(CRYPTO_ALGO_AES128, CRYPTO_MODE_CBC,
				  CRYPTO_DIR_ENCRYPT);
	is(crypto_stream_begin(encoder, key, 3, iv, sizeof(iv)), -1,
	   "stream begin checks key size");
	is(crypto_stream_begin(encoder, key, sizeof(key), iv, 3), -1,
	   "stream begin checks iv size");
	is(crypto_stream_begin(encoder, key, sizeof(key), iv, sizeof(iv)), 0,
	   "begin encryption");
	const char *plain = "long long long long long long long plain text";
	int plain_size = strlen(plain);

	const char *in = plain;
	int in_size = plain_size;
	char *out = buffer1;
	int out_size = sizeof(buffer1);
	int encoded = crypto_stream_append(encoder, in, in_size, NULL, 0);
	is(encoded, in_size + CRYPTO_AES_BLOCK_SIZE, "append checks size");
	int chunk_size = 5;
	int rc = crypto_stream_append(encoder, in, chunk_size, out, out_size);
	ok(rc >= 0, "append %d", chunk_size);
	in += chunk_size;
	in_size -= chunk_size;
	out += rc;
	out_size -= rc;
	encoded = rc;

	chunk_size = 10;
	rc = crypto_stream_append(encoder, in, chunk_size, out, out_size);
	ok(rc >= 0, "append %d", chunk_size);
	in += chunk_size;
	in_size -= chunk_size;
	out += rc;
	out_size -= rc;
	encoded += rc;

	rc = crypto_stream_append(encoder, in, in_size, out, out_size);
	ok(rc >= 0, "last append %d", in_size);
	out += rc;
	out_size -= rc;
	encoded += rc;

	rc = crypto_stream_commit(encoder, NULL, 0);
	is(rc, CRYPTO_AES_BLOCK_SIZE, "commit checks size");
	rc = crypto_stream_commit(encoder, out, out_size);
	ok(rc >= 0, "commit %d", rc);
	out += rc;
	encoded += rc;

	struct crypto_stream *decoder =
		crypto_stream_new(CRYPTO_ALGO_AES128, CRYPTO_MODE_CBC,
				  CRYPTO_DIR_DECRYPT);
	crypto_stream_begin(decoder, key, sizeof(key), iv, sizeof(iv));
	int decoded = crypto_stream_append(decoder, buffer1, encoded,
					   buffer2, sizeof(buffer2));
	decoded += crypto_stream_commit(decoder, buffer2 + decoded,
					sizeof(buffer2) - decoded);
	is(decoded, plain_size, "decoder returned correct size");
	is(memcmp(plain, buffer2, plain_size), 0, "data is decoded correctly");

	crypto_stream_delete(encoder);
	crypto_stream_delete(decoder);

	check_plan();
	footer();
}

int
main(void)
{
	header();
	plan(5);
	random_init();
	crypto_init();
	memory_init();
	fiber_init(fiber_c_invoke);

	struct crypto_codec *c = crypto_codec_new(-1, -1, "1234", 4);
	is(c, NULL, "crypto checks that algo argument is correct");

	test_aes128_codec();
	test_aes128_stress();
	test_each();
	test_stream();

	fiber_free();
	memory_free();
	crypto_free();
	random_free();
	int rc = check_plan();
	footer();
	return rc;
}
