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
#include <openssl/sha.h>
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
		 ERR_reason_error_string(ERR_get_error()))

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

#if OPENSSL_VERSION_NUMBER < 0x30000000L
# define crypto_HMAC_CTX HMAC_CTX
#else
# define crypto_HMAC_CTX EVP_MAC_CTX
#endif

crypto_HMAC_CTX *
crypto_HMAC_CTX_new(void)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	HMAC_CTX *ctx = (HMAC_CTX *)OPENSSL_malloc(sizeof(HMAC_CTX));
	if(!ctx){
		return NULL;
	}
	HMAC_CTX_init(ctx);
	return ctx;
#elif OPENSSL_VERSION_NUMBER < 0x30000000L
	return HMAC_CTX_new();
#else
	static EVP_MAC *mac;
	if (mac == NULL)
		mac = EVP_MAC_fetch(NULL, "hmac", NULL);
	if (mac == NULL)
		return NULL;
	return EVP_MAC_CTX_new(mac);
#endif

}

void
crypto_HMAC_CTX_free(crypto_HMAC_CTX *ctx)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	HMAC_cleanup(ctx); /* Remove key from memory */
	OPENSSL_free(ctx);
#elif OPENSSL_VERSION_NUMBER < 0x30000000L
	HMAC_CTX_free(ctx);
#else
	EVP_MAC_CTX_free(ctx);
#endif
}

unsigned long
crypto_ERR_get_error(void)
{
	return ERR_get_error();
}

char *
crypto_ERR_error_string(unsigned long e, char *buf)
{
	return ERR_error_string(e, buf);
}
int
crypto_EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl)
{
	return EVP_DigestInit_ex(ctx, type, impl);
}

int
crypto_EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt)
{
	return EVP_DigestUpdate(ctx, d, cnt);
}

int
crypto_EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s)
{
	return EVP_DigestFinal_ex(ctx, md, s);
}

const EVP_MD *
crypto_EVP_get_digestbyname(const char *name)
{
	return EVP_get_digestbyname(name);
}

int
crypto_HMAC_Init_ex(crypto_HMAC_CTX *ctx, const void *key, int len,
		    const char *digest, const EVP_MD *md, ENGINE *impl)
{
#if OPENSSL_VERSION_NUMBER < 0x30000000L
	(void)digest;
	return HMAC_Init_ex(ctx, key, len, md, impl);
#else
	(void)md;
	(void)impl;
	OSSL_PARAM params[2];
	params[0] = OSSL_PARAM_construct_utf8_string("digest",
						     (char *)digest, 0);
	params[1] = OSSL_PARAM_construct_end();
	return EVP_MAC_init(ctx, key, len, params);
#endif
}

int
crypto_HMAC_Update(crypto_HMAC_CTX *ctx, const unsigned char *data, size_t len)
{
#if OPENSSL_VERSION_NUMBER < 0x30000000L
	return HMAC_Update(ctx, data, len);
#else
	return EVP_MAC_update(ctx, data, len);
#endif
}

int
crypto_HMAC_Final(crypto_HMAC_CTX *ctx, unsigned char *md, unsigned int *len,
		  unsigned int size)
{
#if OPENSSL_VERSION_NUMBER < 0x30000000L
	(void)size;
	return HMAC_Final(ctx, md, len);
#else
	size_t l;
	int rc = EVP_MAC_final(ctx, md, &l, size);
	*len = l;
	return rc;
#endif
}

const char *
crypto_X509_get_default_cert_dir_env(void)
{
	return X509_get_default_cert_dir_env();
}

const char *
crypto_X509_get_default_cert_file_env(void)
{
	return X509_get_default_cert_file_env();
}

static unsigned char*
sha256_calc_digest(const unsigned char *text, size_t len)
{
	unsigned char *hash = OPENSSL_malloc(SHA256_DIGEST_LENGTH);
	if (hash == NULL) {
		diag_set(OutOfMemory, SHA256_DIGEST_LENGTH, "OPENSSL_malloc",
			 "hash");
		return NULL;
	}

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (ctx == NULL) {
		diag_set_OpenSSL();
		goto err;
	}
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 0) {
		diag_set_OpenSSL();
		goto err;
	}
	if (EVP_DigestUpdate(ctx, text, len) == 0) {
		diag_set_OpenSSL();
		goto err;
	}
	if (EVP_DigestFinal_ex(ctx, hash, NULL) == 0) {
		diag_set_OpenSSL();
		goto err;
	}

	EVP_MD_CTX_free(ctx);
	return hash;

err:
	EVP_MD_CTX_free(ctx);
	OPENSSL_free(hash);
	return NULL;
}

int
crypto_RSA_PSS_verify(const unsigned char *text, size_t text_len,
		      const unsigned char *pub_key, size_t key_len,
		      const unsigned char *sig, size_t sig_len)
{
	OSSL_LIB_CTX *libctx = NULL;
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *ctx = NULL;
	EVP_MD *md = NULL;
	bool result = false;
	unsigned char *digest = sha256_calc_digest(text, text_len);
	if (digest == NULL) {
		goto end;
	}

	BIO *key_bio = BIO_new_mem_buf(pub_key, key_len);
	if (key_bio == NULL) {
		diag_set_OpenSSL();
		goto end;
	}
	pkey = PEM_read_bio_PUBKEY(key_bio, NULL, NULL, NULL);
	BIO_free(key_bio);
	if (pkey == NULL) {
		diag_set_OpenSSL();
		goto end;
	}

	md = EVP_MD_fetch(libctx, "SHA256", NULL);
	if (md == NULL) {
		diag_set_OpenSSL();
		goto end;
	}

	ctx = EVP_PKEY_CTX_new_from_pkey(libctx, pkey, NULL);
	if (ctx == NULL) {
		diag_set_OpenSSL();
		goto end;
	}

	if (EVP_PKEY_verify_init(ctx) == 0) {
		diag_set_OpenSSL();
		goto end;
	}

	if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PSS_PADDING) == 0) {
		diag_set_OpenSSL();
		goto end;
	}

	if (EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx, 32) == 0) {
		diag_set_OpenSSL();
		goto end;
	}

	if (EVP_PKEY_CTX_set_signature_md(ctx, md) == 0) {
		diag_set_OpenSSL();
		goto end;
	}

	if (EVP_PKEY_verify(ctx, sig, sig_len, digest, 32) == 0) {
		diag_set_OpenSSL();
		goto end;
	}

	result = true;
end:
	EVP_PKEY_CTX_free(ctx);
	EVP_PKEY_free(pkey);
	EVP_MD_free(md);
	OPENSSL_free(digest);
	OSSL_LIB_CTX_free(libctx);
	return result;
}
