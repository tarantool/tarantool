#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

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

HMAC_CTX *tnt_HMAC_CTX_new(void)
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

void tnt_HMAC_CTX_free(HMAC_CTX *ctx)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	HMAC_cleanup(ctx); /* Remove key from memory */
	OPENSSL_free(ctx);
#else
	HMAC_CTX_free(ctx);
#endif
}
