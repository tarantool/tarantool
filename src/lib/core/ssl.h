/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_SSL)
# include "ssl_impl.h"
#else /* !defined(ENABLE_SSL) */

#include <assert.h>

#include "iostream.h"
#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct uri;
struct ssl_iostream_ctx;

void
ssl_init(void);

void
ssl_free(void);

struct ssl_iostream_ctx *
ssl_iostream_ctx_new(enum iostream_mode mode, const struct uri *uri);

static inline struct ssl_iostream_ctx *
ssl_iostream_ctx_dup(struct ssl_iostream_ctx *ctx)
{
	assert(ctx == NULL);
	return ctx;
}

static inline void
ssl_iostream_ctx_delete(struct ssl_iostream_ctx *ctx)
{
	(void)ctx;
	unreachable();
}

static inline int
ssl_iostream_create(struct iostream *io, int fd, enum iostream_mode mode,
		    const struct ssl_iostream_ctx *ctx)
{
	(void)io;
	(void)fd;
	(void)mode;
	(void)ctx;
	unreachable();
	return 0;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_SSL) */
