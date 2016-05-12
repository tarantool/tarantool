#include <openssl/evp.h>

//Helper functions for tarantool crypto api

int tnt_EVP_CIPHER_key_length(const EVP_CIPHER *cipher)
{
	return EVP_CIPHER_key_length(cipher);
}

int tnt_EVP_CIPHER_iv_length(const EVP_CIPHER *cipher)
{
	return EVP_CIPHER_iv_length(cipher);
}

