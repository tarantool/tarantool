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
#include "crypto.h"
#include "diag.h"
#include "exception.h"
#include "core/random.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/hmac.h>

const char *crypto_algo_strs[] = {
	"none",
	"AES128",
	"AES192",
	"AES256",
	"DES",
};

const char *crypto_mode_strs[] = {
	"ECB",
	"CBC",
	"CFB",
	"OFB",
};

/**
 * Return a EVP_CIPHER object by a given algorithm name and a
 * mode value. An algorithm name should be without quotes and with
 * a format '<standard>[_<key_size>]', lowercase. For a list of
 * supported algorithms see OpenSSL documentation.
 */
#define algo_cipher_by_mode(algo, mode) ({		\
	const EVP_CIPHER *type;				\
	switch (mode) {					\
	case CRYPTO_MODE_ECB:				\
		type = EVP_##algo##_ecb();		\
		break;					\
	case CRYPTO_MODE_CBC:				\
		type = EVP_##algo##_cbc();		\
		break;					\
	case CRYPTO_MODE_CFB:				\
		type = EVP_##algo##_cfb();		\
		break;					\
	case CRYPTO_MODE_OFB:				\
		type = EVP_##algo##_ofb();		\
		break;					\
	default:					\
		type = NULL;				\
		diag_set(CryptoError, "unknown mode");	\
		break;					\
	}						\
	type;						\
})

/**
 * Find an OpenSSL cipher object by specified encryption
 * algorithm and mode.
 */
static inline const EVP_CIPHER *
evp_cipher_find(enum crypto_algo algo, enum crypto_mode mode)
{
	switch (algo) {
	case CRYPTO_ALGO_AES128:
		return algo_cipher_by_mode(aes_128, mode);
	case CRYPTO_ALGO_AES192:
		return algo_cipher_by_mode(aes_192, mode);
	case CRYPTO_ALGO_AES256:
		return algo_cipher_by_mode(aes_256, mode);
	case CRYPTO_ALGO_DES:
		return algo_cipher_by_mode(des, mode);
	case CRYPTO_ALGO_NONE:
		return EVP_enc_null();
	default:
		diag_set(CryptoError, "unknown crypto algorithm");
		return NULL;
	}
}

/**
 * Set a diag error with the latest OpenSSL error message. It is a
 * macro so as to keep untouched line number in the error message.
 */
#define diag_set_OpenSSL()					\
	diag_set(CryptoError, "OpenSSL error: %s",		\
		 ERR_error_string(ERR_get_error(), NULL))

/** Stream to encrypt/decrypt data packets step by step. */
struct crypto_stream {
	/** Cipher type. Depends on algorithm and mode. */
	const EVP_CIPHER *cipher;
	/** Encryption/decryption context. */
	EVP_CIPHER_CTX *ctx;
	/** Stream direction. */
	enum crypto_direction dir;
};

struct crypto_stream *
crypto_stream_new(enum crypto_algo algo, enum crypto_mode mode,
		  enum crypto_direction dir)
{
	const EVP_CIPHER *cipher = evp_cipher_find(algo, mode);
	if (cipher == NULL)
		return NULL;
	struct crypto_stream *s = (struct crypto_stream *) malloc(sizeof(*s));
	if (s == NULL) {
		diag_set(OutOfMemory, sizeof(*s), "malloc", "s");
		return NULL;
	}
	s->ctx = EVP_CIPHER_CTX_new();
	if (s->ctx == NULL) {
		free(s);
		diag_set_OpenSSL();
		return NULL;
	}
	s->cipher = cipher;
	s->dir = dir;
	return s;
}

int
crypto_stream_begin(struct crypto_stream *s, const char *key, int key_size,
		    const char *iv, int iv_size)
{
	int need_size = EVP_CIPHER_key_length(s->cipher);
	if (key_size != need_size) {
		diag_set(CryptoError, "key size expected %d, got %d",
			 need_size, key_size);
		return -1;
	}
	need_size = EVP_CIPHER_iv_length(s->cipher);
	if (iv_size != need_size) {
		diag_set(CryptoError, "IV size expected %d, got %d",
			 need_size, iv_size);
		return -1;
	}
	if (EVP_CIPHER_CTX_cleanup(s->ctx) == 1 &&
	    EVP_CipherInit_ex(s->ctx, s->cipher, NULL,
			      (const unsigned char *) key,
			      (const unsigned char *) iv, s->dir) == 1)
		return 0;
	diag_set_OpenSSL();
	return -1;
}

int
crypto_stream_append(struct crypto_stream *s, const char *in, int in_size,
		     char *out, int out_size)
{
	int len, need = in_size + EVP_CIPHER_block_size(s->cipher);
	if (need > out_size)
		return need;
	if (EVP_CipherUpdate(s->ctx, (unsigned char *) out, &len,
			     (const unsigned char *) in, in_size) == 1)
		return len;
	diag_set_OpenSSL();
	return -1;
}

int
crypto_stream_commit(struct crypto_stream *s, char *out, int out_size)
{
	int need = EVP_CIPHER_block_size(s->cipher);
	if (need > out_size)
		return need;
	int len;
	if (EVP_CipherFinal_ex(s->ctx, (unsigned char *) out, &len) == 1 &&
	    EVP_CIPHER_CTX_cleanup(s->ctx) == 1)
		return len;
	diag_set_OpenSSL();
	return -1;
}

void
crypto_stream_delete(struct crypto_stream *s)
{
	EVP_CIPHER_CTX_free(s->ctx);
	free(s);
}

/**
 * OpenSSL codec. Has a constant secret key, provides API to
 * generate public keys, keeps OpenSSL contexts cached.
 */
struct crypto_codec {
	/** Cipher type. Depends on algorithm and mode. */
	const EVP_CIPHER *cipher;
	/** Encryption context. */
	EVP_CIPHER_CTX *ctx;
	/**
	 * Secret key, usually unchanged among multiple data
	 * packets.
	 */
	unsigned char key[CRYPTO_MAX_KEY_SIZE];
};

struct crypto_codec *
crypto_codec_new(enum crypto_algo algo, enum crypto_mode mode,
		 const char *key, int key_size)
{
	const EVP_CIPHER *cipher = evp_cipher_find(algo, mode);
	if (cipher == NULL)
		return NULL;
	int need = EVP_CIPHER_key_length(cipher);
	if (key_size != need) {
		diag_set(CryptoError, "key size expected %d, got %d",
			 need, key_size);
		return NULL;
	}
	struct crypto_codec *c = (struct crypto_codec *) malloc(sizeof(*c));
	if (c == NULL) {
		diag_set(OutOfMemory, sizeof(*c), "malloc", "c");
		return NULL;
	}
	c->ctx = EVP_CIPHER_CTX_new();
	if (c->ctx == NULL) {
		free(c);
		diag_set_OpenSSL();
		return NULL;
	}
	c->cipher = cipher;
	memcpy(c->key, key, key_size);
	return c;
}

int
crypto_codec_gen_iv(struct crypto_codec *c, char *out, int out_size)
{
	int need = EVP_CIPHER_iv_length(c->cipher);
	if (out_size >= need)
		random_bytes(out, need);
	return need;
}

int
crypto_codec_iv_size(const struct crypto_codec *c)
{
	return EVP_CIPHER_iv_length(c->cipher);
}

/** Generic implementation of encrypt/decrypt methods. */
static int
crypto_codec_do_op(struct crypto_codec *c, const char *iv,
		   const char *in, int in_size, char *out, int out_size,
		   enum crypto_direction dir)
{
	const unsigned char *uin = (const unsigned char *) in;
	const unsigned char *uiv = (const unsigned char *) iv;
	unsigned char *uout = (unsigned char *) out;
	/*
	 * Note, that even if in_size is already multiple of block
	 * size, additional block is still needed. Result is
	 * almost always bigger than input. OpenSSL API advises to
	 * provide buffer of one block bigger than the data to
	 * encode.
	 */
	int need = in_size + EVP_CIPHER_block_size(c->cipher);
	if (need > out_size)
		return need;

	int len1 = 0, len2 = 0;
	if (EVP_CipherInit_ex(c->ctx, c->cipher, NULL, c->key, uiv, dir) == 1 &&
	    EVP_CipherUpdate(c->ctx, uout, &len1, uin, in_size) == 1 &&
	    EVP_CipherFinal_ex(c->ctx, uout + len1, &len2) == 1 &&
	    EVP_CIPHER_CTX_cleanup(c->ctx) == 1) {
		assert(len1 + len2 <= need);
		return len1 + len2;
	}
	EVP_CIPHER_CTX_cleanup(c->ctx);
	diag_set_OpenSSL();
	return -1;
}

int
crypto_codec_encrypt(struct crypto_codec *c, const char *iv,
		     const char *in, int in_size, char *out, int out_size)
{
	return crypto_codec_do_op(c, iv, in, in_size, out, out_size,
				  CRYPTO_DIR_ENCRYPT);
}

int
crypto_codec_decrypt(struct crypto_codec *c, const char *iv,
		     const char *in, int in_size, char *out, int out_size)
{
	return crypto_codec_do_op(c, iv, in, in_size, out, out_size,
				  CRYPTO_DIR_DECRYPT);
}

void
crypto_codec_delete(struct crypto_codec *c)
{
	EVP_CIPHER_CTX_free(c->ctx);
	free(c);
}

void
crypto_init(void)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	OpenSSL_add_all_digests();
	OpenSSL_add_all_ciphers();
	ERR_load_crypto_strings();
#else
	OPENSSL_init_crypto(0, NULL);
	OPENSSL_init_ssl(0, NULL);
#endif
}

void
crypto_free(void)
{
#ifdef OPENSSL_cleanup
	OPENSSL_cleanup();
#endif
}

EVP_MD_CTX *
crypto_EVP_MD_CTX_new(void)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	return EVP_MD_CTX_create();
#else
	return EVP_MD_CTX_new();
#endif
};

void
crypto_EVP_MD_CTX_free(EVP_MD_CTX *ctx)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	return EVP_MD_CTX_destroy(ctx);
#else
	return EVP_MD_CTX_free(ctx);
#endif
}

HMAC_CTX *
crypto_HMAC_CTX_new(void)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	HMAC_CTX *ctx = (HMAC_CTX *)OPENSSL_malloc(sizeof(HMAC_CTX));
	if(!ctx){
		return NULL;
	}
	HMAC_CTX_init(ctx);
	return ctx;
#else
	return HMAC_CTX_new();
#endif

}

void
crypto_HMAC_CTX_free(HMAC_CTX *ctx)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	HMAC_cleanup(ctx); /* Remove key from memory */
	OPENSSL_free(ctx);
#else
	HMAC_CTX_free(ctx);
#endif
}
