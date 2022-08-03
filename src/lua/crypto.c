#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "say.h"

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/provider.h>
static OSSL_PROVIDER *legacy_provider = NULL;
static OSSL_PROVIDER *default_provider = NULL;
#endif

/* Helper function for openssl init */
int tnt_openssl_init()
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	OpenSSL_add_all_digests();
	OpenSSL_add_all_ciphers();
	ERR_load_crypto_strings();
#else
	OPENSSL_init_crypto(0, NULL);
	OPENSSL_init_ssl(0, NULL);
#endif
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	/* Needed to enable legacy algorithms, such as MD4. */
	legacy_provider = OSSL_PROVIDER_load(NULL, "legacy");
	if (legacy_provider == NULL)
		say_error("cannot load the Legacy OpenSSL provider");
	default_provider = OSSL_PROVIDER_load(NULL, "default");
	if (default_provider == NULL)
		say_error("cannot load the Default OpenSSL provider");
#endif
	return 0;
}

/* Helper functions for tarantool crypto api */

int tnt_EVP_CIPHER_key_length(const EVP_CIPHER *cipher)
{
	return EVP_CIPHER_key_length(cipher);
}

int tnt_EVP_CIPHER_iv_length(const EVP_CIPHER *cipher)
{
	return EVP_CIPHER_iv_length(cipher);
}

int tnt_EVP_CIPHER_block_size(const EVP_CIPHER *cipher)
{
	return EVP_CIPHER_block_size(cipher);
}

EVP_MD_CTX *tnt_EVP_MD_CTX_new(void)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	return EVP_MD_CTX_create();
#else
	return EVP_MD_CTX_new();
#endif
};

void tnt_EVP_MD_CTX_free(EVP_MD_CTX *ctx)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	return EVP_MD_CTX_destroy(ctx);
#else
	return EVP_MD_CTX_free(ctx);
#endif
}

#if OPENSSL_VERSION_NUMBER < 0x30000000L
# define tnt_HMAC_CTX HMAC_CTX
#else
# define tnt_HMAC_CTX EVP_MAC_CTX
#endif

tnt_HMAC_CTX *
tnt_HMAC_CTX_new(void)
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
tnt_HMAC_CTX_free(tnt_HMAC_CTX *ctx)
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

int
tnt_HMAC_Init_ex(tnt_HMAC_CTX *ctx, const void *key, int len,
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
tnt_HMAC_Update(tnt_HMAC_CTX *ctx, const unsigned char *data, size_t len)
{
#if OPENSSL_VERSION_NUMBER < 0x30000000L
	return HMAC_Update(ctx, data, len);
#else
	return EVP_MAC_update(ctx, data, len);
#endif
}

int
tnt_HMAC_Final(tnt_HMAC_CTX *ctx, unsigned char *md, unsigned int *len,
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
