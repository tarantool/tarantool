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

enum tuple_constraint_type {
	CONSTR_FUNC,
	CONSTR_FKEY,
};

extern const char *tuple_constraint_type_strs[];

/**
 * Definition of a func.
 */
struct tuple_constraint_func_def {
	/** ID of the function. */
	uint32_t id;
};

/**
 * Definition of field that can be defined by ID or name. Which definition
 * is used can be recognized by 'name_len' member:
 * name_len == 0: definition by ID, see 'id' member.
 * name_len != 0: definition by name, see also 'name' member.
 */
struct tuple_constraint_field_id {
	/** ID of the entity if is defined by ID. */
	uint32_t id;
	/** Name size of the entity if is defined by name or 0 if by ID. */
	uint32_t name_len;
	/** Name size of the entity if is defined by name or "" if by ID. */
	const char *name;
};

/**
 * Definition of one pair in foreign key field mapping.
 * Used only for complex foreign keys.
 */
struct tuple_constraint_fkey_field_mapping {
	/** Field in local space. */
	struct tuple_constraint_field_id local_field;
	/** Field in foreign space. */
	struct tuple_constraint_field_id foreign_field;
};

/**
 * Definition of a foreign key.
 */
struct tuple_constraint_fkey_def {
	/** Definition of space. */
	uint32_t space_id;
	/**
	 * Number of records in field map. Nonzero only for complex foreign
	 * keys. Zero for field foreign keys.
	 */
	uint32_t field_mapping_size;
	union {
		/** Definition of field. */
		struct tuple_constraint_field_id field;
		/** Field mapping. */
		struct tuple_constraint_fkey_field_mapping *field_mapping;
	};
};

/**
 * Generic constraint of a tuple or a tuple field.
 */
struct tuple_constraint_def {
	/** Name of the constraint (null-terminated). */
	const char *name;
	/** Length of name of the constraint. */
	uint32_t name_len;
	/** Type of the constraint. */
	enum tuple_constraint_type type;
	union {
		/** Function to check the constraint. */
		struct tuple_constraint_func_def func;
		/** Definition of foreign key. */
		struct tuple_constraint_fkey_def fkey;
	};
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
 * Compute the tuple constraint definition's hash with `PMurHash32_Process` and
 * return the size of the data processed.
 */
uint32_t
tuple_constraint_def_hash_process(const struct tuple_constraint_def *def,
				  uint32_t *ph, uint32_t *pcarry);

/**
 * Parse constraint array from msgpack @a *data with the following format:
 * {constraint_name=function_name,...}
 * Allocate a temporary constraint array on @a region and save it in @a def.
 * Allocate needed for constraints strings also on @a region.
 * If there are some constraints already (*def != NULL, *count != 0) then
 * append the array with parsed constraints.
 * Set @a count to the count of parsed constraints.
 * Move @a data msgpack pointer to the end of msgpack value.
 *
 * Return:
 *   0 - success.
 *  -1 - failure, diag is set (IllegalParams).
 */
int
tuple_constraint_def_decode(const char **data,
			    struct tuple_constraint_def **def, uint32_t *count,
			    struct region *region);

/**
 * Parse constraint array from msgpack @a *data with the following format:
 * {foreign_key_name={space=.., field=...},...}
 * If @a is_complex is false, the field is parsed as ID or name.
 * If @a is_complex is true, the field is parsed as msgpack map of
 * local (as ID or name) to foreign (ad ID or name) field pairs.
 * Allocate a temporary constraint array on @a region and save it in @a def.
 * If there are some constraints already (*def != NULL, *count != 0) then
 * append the array with parsed constraints.
 * Allocate needed for foreign key strings also on @a region.
 * Set @a count to the total count of constraints.
 * Move @a data msgpack pointer to the end of msgpack value.
 *
 * Return:
 *   0 - success.
 *  -1 - failure, diag is set (IllegalParams).
 */
int
tuple_constraint_def_decode_fkey(const char **data,
				 struct tuple_constraint_def **def,
				 uint32_t *count, struct region *region,
				 bool is_complex);

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
