/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "field_compression_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Generic compression of a field.
 */
struct field_compression {
	/* The field compression definition copy. */
	struct field_compression_def def;
	/* Type-dependent compression options. */
	struct compression_opts opts;
};

/**
 * Compare two compression objects, return 0 if they are equal.
 * Don't compare function pointers, only compression definition.
 * If @a ignore_name is true then ignore compression's name and compare only
 * compression's entity.
 */
int
field_compression_cmp(const struct field_compression *compr1,
		      const struct field_compression *compr2);

/**
 * Compute the tuple compression's hash with `PMurHash32_Process` and return the
 * size of the data processed.
 */
uint32_t
field_compression_hash_process(const struct field_compression *compr,
			       uint32_t *ph, uint32_t *pcarry);

/**
 * Create a field_compression object from its definition.
 */
void
field_compression_from_def(const struct field_compression_def *def,
			   struct field_compression *compr);

#ifdef __cplusplus
} /* extern "C" */
#endif
