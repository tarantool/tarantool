/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct region;

/**
 * Definition of a func.
 */
struct tuple_constraint_func_def {
	/** ID of the function. */
	uint32_t id;
};

/**
 * Generic constraint of a tuple or a tuple field.
 */
struct tuple_constraint_def {
	/** Name of the constraint (null-terminated). */
	const char *name;
	/** Length of name of the constraint. */
	uint32_t name_len;
	/** Function to check the constraint. */
	struct tuple_constraint_func_def func;
};

/**
 * Compare two constraint definition, return 0 if equal.
 * If @a ignore_name is true then ignore constraint's name and compare only
 * constraint's entity.
 */
int
tuple_constraint_def_cmp(const struct tuple_constraint_def *def1,
			 const struct tuple_constraint_def *def2,
			 bool ignore_name);

/**
 * Parse constraint array from msgpack @a *data with the following format:
 * {constraint_name=function_name,...}
 * Allocate a temporary constraint array on @a region and save it in @a def.
 * Allocate needed for constraints strings also on @a region.
 * Set @a count to the count of parsed constraints.
 * Move @a data msgpack pointer to the end of msgpack value.
 *
 * Return:
 *   0 - success.
 *  -1 - failure, diag is set. It can be an OutOfMemory or ClientError with
 *   given @a errcode and @a field_no as first arguments.
 */
int
tuple_constraint_def_decode(const char **data,
			    struct tuple_constraint_def **def, uint32_t *count,
			    struct region *region,
			    uint32_t errcode, uint32_t field_no);

/**
 * Allocate a single memory block needed for given @a count of constraint
 * definitions, including strings in them. Fill the block with strings and
 * construct new definitions using new strings.
 * The resulting array is a deep copy of given definitions allocated in one
 * memory block. Just free it by free() when no more needed.
 * Never fail (uses xmalloc); return NULL if constraint_count == 0.
 *
 * @param def - array of given constraint definitions.
 * @param constraint_count - number of given constraints.
 * @return a single memory block with constraint definition.
 */
struct tuple_constraint_def *
tuple_constraint_def_array_dup(const struct tuple_constraint_def *defs,
			       size_t constraint_count);

/**
 * Common and generic array dup function for constraints.
 * Used by tuple_constraint_def_array_dup and tuple_constraint_array_new.
 * Do all the stuff described in @sa tuple_constraint_def_array_dup, except
 * the thing - allocate an array of objects of given @a object_size that are
 * derived from struct tuple_constraint_def (or in other words must have
 * struct tuple_constraint_def as the first member).
 * It also has @a additional_size argument - size of additional data that
 * will be allocated right after array of resulting objects.
 * Initialize only the tuple_constraint_def part of objects.
 */
void *
tuple_constraint_def_array_dup_raw(const struct tuple_constraint_def *defs,
				   size_t count, size_t object_size,
				   size_t additional_size);

#ifdef __cplusplus
} /* extern "C" */
#endif
