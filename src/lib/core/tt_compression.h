#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "trivia/config.h"

#if defined(ENABLE_TUPLE_COMPRESSION)
# include "tt_compression_impl.h"
#else /* !defined(ENABLE_TUPLE_COMPRESSION) */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum compression_type {
        COMPRESSION_TYPE_NONE = 0,
        compression_type_MAX
};

extern const char *compression_type_strs[];

/**
 * Dummy compression options struct.
 */
struct compression_opts {
	/* Expected to be COMPRESSION_TYPE_NONE. */
	enum compression_type type;
};

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_TUPLE_COMPRESSION) */
