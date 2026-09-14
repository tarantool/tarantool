/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "iostream.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct uri;

/** Alias for SSL_CTX. */
struct ssl_iostream_ctx;

void
ssl_init(void);

void
ssl_free(void);

/**
 * Creates and returns an SSL context.
 *
 * The SSL method (server or client) is determined by the mode argument.
 * Other SSL parameters are extracted from the given URI:
 *  - ssl_ca_file: path to the trusted certificate authorities file.
 *    Optional: if unset, the peer won't be checked.
 *  - ssl_cert_file: path to the certificate file. Mandatory for server
 *    connections, optional for client connections.
 *  - ssl_key_file: path to the private key file matching the certificate.
 *
 * On error returns NULL and sets diag.
 */
struct ssl_iostream_ctx *
ssl_iostream_ctx_new(enum iostream_mode mode, const struct uri *uri);

/**
 * Duplicates an SSL context and returns a copy.
 * Never fails. Returns NULL if the argument is NULL.
 */
struct ssl_iostream_ctx *
ssl_iostream_ctx_dup(struct ssl_iostream_ctx *ctx);

/**
 * Deletes an SSL context.
 */
void
ssl_iostream_ctx_delete(struct ssl_iostream_ctx *ctx);

/**
 * Creates an encrypted IO stream for the given fd.
 *
 * Encryption parameters are defined by the SSL context. The new stream is
 * set to either accept or connect state, depending on the value of the mode
 * argument.
 *
 * On success returns 0. On error returns -1 and sets diag.
 */
int
ssl_iostream_create(struct iostream *io, int fd, enum iostream_mode mode,
		    const struct ssl_iostream_ctx *ctx);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
