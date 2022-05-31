/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ssl_init.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <stddef.h>

#include "say.h"

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/provider.h>
static OSSL_PROVIDER *legacy_provider = NULL;
static OSSL_PROVIDER *default_provider = NULL;
#endif

void
ssl_init_impl(void)
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
}

void
ssl_free_impl(void)
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	if (legacy_provider != NULL)
		OSSL_PROVIDER_unload(legacy_provider);
	if (default_provider != NULL)
		OSSL_PROVIDER_unload(default_provider);
#endif
#ifdef OPENSSL_cleanup
	OPENSSL_cleanup();
#endif
}
