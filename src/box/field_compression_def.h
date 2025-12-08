/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "tt_compression.h"

#ifdef __cplusplus
extern "C" {
#endif

struct region;

#if defined(ENABLE_TUPLE_COMPRESSION)
# include "field_compression_def_impl.h"
#else /* !defined(ENABLE_TUPLE_COMPRESSION) */

/**
 * Definition of a field compression.
 */
struct field_compression_def {
	/* Expected to be COMPRESSION_TYPE_NONE. */
	enum compression_type type;
};

/**
 * The default values of compression definition fields.
 * Must be in-header constant to initialize the field_def_default.
 */
static const struct field_compression_def field_compression_def_default = {
	.type = COMPRESSION_TYPE_NONE,
};

#endif

/**
 * Compare two compression definition, return 0 if equal.
 */
int
field_compression_def_cmp(const struct field_compression_def *def1,
			  const struct field_compression_def *def2);

/**
 * Compute the field compression definition's hash with `PMurHash32_Process` and
 * return the size of the data processed.
 */
uint32_t
field_compression_def_hash_process(const struct field_compression_def *def,
				   uint32_t *ph, uint32_t *pcarry);

/*
 * Check if the compression definition is valid: the compression type specified
 * is valid and the parameters set in the definition are valid for the type.
 */
int
field_compression_def_check(struct field_compression_def *def);

/**
 * Parse compression from msgpack @a *data with one of following formats:
 * - 'compression_name'
 * - {'compression_name', option=value,...}
 * If the compression is set already, return error.
 * Move @a data msgpack pointer to the end of msgpack value.
 *
 * Return:
 *   0 - success.
 *  -1 - failure, diag is set (IllegalParams).
 */
int
field_compression_def_decode(const char **data,
			     struct field_compression_def *def,
			     struct region *region);

#ifdef __cplusplus
} /* extern "C" */
#endif
