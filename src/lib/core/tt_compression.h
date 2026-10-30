#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "small/region.h"
#include "trivia/config.h"
#include <stdint.h>

#if defined(ENABLE_TUPLE_COMPRESSION)
# include "tt_compression_impl.h"
#else /* !defined(ENABLE_TUPLE_COMPRESSION) */

enum compression_type {
        COMPRESSION_TYPE_NONE = 0,
        compression_type_MAX
};

/**
 * Dummy compression options.
 */
struct compression_opts {
	/* Expected to be COMPRESSION_TYPE_NONE. */
	enum compression_type type;
};

/**
 * The default values of compression options.
 * Must be in-header constant to initialize the field_def_default.
 */
static const struct compression_opts compression_opts_default = {
	.type = COMPRESSION_TYPE_NONE,
};

#endif /* !defined(ENABLE_TUPLE_COMPRESSION) */

#if defined(__cplusplus)
extern "C" {
#endif

/** Compression type strings. */
extern const char *compression_type_strs[];

/**
 * Parse compression from msgpack @a *data with one of following formats:
 * - 'compression_name'
 * - {'compression_name', option=value,...}
 * Move @a data msgpack pointer to the end of msgpack value.
 *
 * Return:
 *   0 - success.
 *  -1 - failure, diag is set (IllegalParams).
 */
int
compression_opts_decode(const char **data, struct compression_opts *opts,
			struct region *region);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
