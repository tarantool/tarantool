/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ssl.h"

#include <stddef.h>

#include "diag.h"
#include "iostream.h"
#include "ssl_init.h"
#include "trivia/config.h"

#if defined(ENABLE_SSL)
# error unimplemented
#endif

void
ssl_init(void)
{
	ssl_init_impl();
}

void
ssl_free(void)
{
	ssl_free_impl();
}

struct ssl_iostream_ctx *
ssl_iostream_ctx_new(enum iostream_mode mode, const struct uri *uri)
{
	(void)mode;
	(void)uri;
	diag_set(IllegalParams, "SSL is not available in this build");
	return NULL;
}
