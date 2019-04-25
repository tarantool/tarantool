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
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/hmac.h>

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

int
crypto_EVP_CIPHER_key_length(const EVP_CIPHER *cipher)
{
	return EVP_CIPHER_key_length(cipher);
}

int
crypto_EVP_CIPHER_iv_length(const EVP_CIPHER *cipher)
{
	return EVP_CIPHER_iv_length(cipher);
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
