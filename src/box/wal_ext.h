/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_WAL_EXT)
# include "wal_ext_impl.h"
#else /* !defined(ENABLE_WAL_EXT) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <stddef.h>

/** Initialize WAL extensions cache. */
static inline void
wal_ext_init(void)
{
}

/** Cleanup extensions cache and default value. */
static inline void
wal_ext_free(void)
{
}

struct space_wal_ext;
struct txn_stmt;
struct request;

/**
 * Fills in @a request with data from @a stmt depending on space's WAL
 * extensions.
 */
static inline void
space_wal_ext_process_request(struct space_wal_ext *ext, struct txn_stmt *stmt,
			      struct request *request)
{
	(void)ext;
	(void)stmt;
	(void)request;
}

/**
 * Return reference to corresponding WAL extension by given space name.
 * Returned object MUST NOT be freed or changed in any way; it should be
 * read-only.
 */
static inline struct space_wal_ext *
space_wal_ext_by_name(const char *space_name)
{
	(void)space_name;
	return NULL;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_WAL_EXT) */
