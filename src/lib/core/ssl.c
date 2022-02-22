/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ssl.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <stddef.h>

#include "diag.h"
#include "iostream.h"
#include "trivia/config.h"

#if defined(ENABLE_SSL)
# error unimplemented
#endif

void
ssl_init(void)
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
ssl_free(void)
{
#ifdef OPENSSL_cleanup
	OPENSSL_cleanup();
#endif
}

struct ssl_iostream_ctx *
ssl_iostream_ctx_new(enum iostream_mode mode, const struct uri *uri)
{
	(void)mode;
	(void)uri;
	diag_set(IllegalParams, "SSL is not available in this build");
	return NULL;
}
