#ifndef TARANTOOL_BOX_FIELD_DEF_H_INCLUDED
#define TARANTOOL_BOX_FIELD_DEF_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <msgpuck.h>
#include "opt_def.h"

#include "tt_compression.h"
#include "mp_extension_types.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct region;

/** \cond public */

/*
 * Possible field data types. Can't use STRS/ENUM macros for them,
 * since there is a mismatch between enum name (STRING) and type
 * name literal ("STR"). STR is already used as Objective C type.
 */
enum field_type {
	FIELD_TYPE_ANY = 0,
	FIELD_TYPE_UNSIGNED,
	FIELD_TYPE_STRING,
	FIELD_TYPE_NUMBER,
	FIELD_TYPE_DOUBLE,
	FIELD_TYPE_INTEGER,
	FIELD_TYPE_BOOLEAN,
	FIELD_TYPE_VARBINARY,
	FIELD_TYPE_SCALAR,
	FIELD_TYPE_DECIMAL,
	FIELD_TYPE_UUID,
	FIELD_TYPE_DATETIME,
	FIELD_TYPE_INTERVAL,
	FIELD_TYPE_ARRAY,
	FIELD_TYPE_MAP,
	FIELD_TYPE_INT8,
	FIELD_TYPE_UINT8,
	FIELD_TYPE_INT16,
	FIELD_TYPE_UINT16,
	FIELD_TYPE_INT32,
	FIELD_TYPE_UINT32,
	FIELD_TYPE_INT64,
	FIELD_TYPE_UINT64,
	FIELD_TYPE_FLOAT32,
	FIELD_TYPE_FLOAT64,
	FIELD_TYPE_DECIMAL32,
	FIELD_TYPE_DECIMAL64,
	FIELD_TYPE_DECIMAL128,
	FIELD_TYPE_DECIMAL256,
	field_type_MAX
};

/**
 * Extra parameters for parametric types like FIELD_TYPE_DECIMAL32 etc.
 */
union field_type_params {
	/** Used by fix fixed point decimals. */
	int64_t scale;
};

/**
 * Possible actions on conflict.
 */
enum on_conflict_action {
	ON_CONFLICT_ACTION_NONE = 0,
	ON_CONFLICT_ACTION_ROLLBACK,
	ON_CONFLICT_ACTION_ABORT,
	ON_CONFLICT_ACTION_FAIL,
	ON_CONFLICT_ACTION_IGNORE,
	ON_CONFLICT_ACTION_REPLACE,
	ON_CONFLICT_ACTION_DEFAULT,
	on_conflict_action_MAX
};

/** \endcond public */

extern const char *field_type_strs[];
extern const bool field_type_is_fixed_signed[];
extern const bool field_type_is_fixed_unsigned[];
extern const int64_t field_type_min_value[];
extern const uint64_t field_type_max_value[];
extern const bool field_type_is_fixed_decimal[];
extern const int field_type_decimal_precision[];

extern const char *on_conflict_action_strs[];

/** Check if @a type1 can store values of @a type2. */
bool
field_type1_contains_type2(enum field_type type1,
			   const union field_type_params *type_params1,
			   enum field_type type2,
			   const union field_type_params *type_params2);

/**
 * Return true for fixed-size integer field `type'.
 */
static inline bool
field_type_is_fixed_int(enum field_type type)
{
	assert(type < field_type_MAX);
	return field_type_is_fixed_signed[type] ||
	       field_type_is_fixed_unsigned[type];
}

/**
 * Get field type by name
 */
enum field_type
field_type_by_name(const char *name, size_t len);

/* MsgPack type names */
extern const char *mp_type_strs[];

/** Two helper tables for field_mp_type_is_compatible */
extern const uint32_t field_mp_type[];
extern const uint32_t field_ext_type[];

extern const struct field_def field_def_default;

/**
 * @brief Field definition
 * Contains information about of one tuple field.
 */
struct field_def {
	/**
	 * Field type of an indexed field.
	 * If a field participates in at least one of space indexes
	 * then its type is stored in this member.
	 * If a field does not participate in an index
	 * then UNKNOWN is stored for it.
	 */
	enum field_type type;
	/** Extra parameters for parametric types like decimal32 etc. */
	union field_type_params type_params;
	/** 0-terminated field name. */
	char *name;
	/** True, if a field can store NULL. */
	bool is_nullable;
	/** Action to perform if NULL constraint failed. */
	enum on_conflict_action nullable_action;
	/** Collation ID for string comparison. */
	uint32_t coll_id;
	/** MsgPack with the default value. */
	char *default_value;
	/** Size of the default value. */
	size_t default_value_size;
	/** ID of the field default function. */
	uint32_t default_func_id;
	/** Compression type for this field. */
	enum compression_type compression_type;
	/** Array of constraints. Can be NULL if constraints_count == 0. */
	struct tuple_constraint_def *constraint_def;
	/** Number of constraints. */
	uint32_t constraint_count;
};

/**
 * Checks if mp_type (except MP_EXT) (MsgPack) is compatible
 * with given field type.
 */
static inline bool
field_mp_plain_type_is_compatible(enum field_type type, enum mp_type mp_type,
				  bool is_nullable)
{
	assert(mp_type != MP_EXT);
	uint32_t mask = field_mp_type[type] | (is_nullable * (1U << MP_NIL));
	return (mask & (1U << mp_type)) != 0;
}

/** Checks if mp_type (MsgPack) is compatible with field type. */
static inline bool
field_mp_type_is_compatible(enum field_type type, const char *data,
			    bool is_nullable)
{
	assert(type < field_type_MAX);
	enum mp_type mp_type = mp_typeof(*data);
	assert((size_t)mp_type < CHAR_BIT * sizeof(*field_mp_type));
	uint32_t mask;
	if (mp_type != MP_EXT) {
		return field_mp_plain_type_is_compatible(type, mp_type,
							 is_nullable);
	} else {
		int8_t ext_type;
		mp_decode_extl(&data, &ext_type);
		if (ext_type >= 0) {
			assert(ext_type != MP_COMPRESSION);
			mask = field_ext_type[type];
			return (mask & (1U << ext_type)) != 0;
		} else {
			return false;
		}
	}
}

/**
 * Check if MsgPack value fits into the range for fixed size integer type.
 * @param type - fixed size integer type.
 * @param data - MsgPack value.
 * @param mp_min - buffer to hold MsgPack of minimum value for type
 *   if value does not fit.
 * @param mp_max - buffer to hold MsgPack of maximum value for type
 *   if value does not fit.
 * @param details[out] - pointer to hold pointer to statically allocated
 *   string with error details if value does not fit. Can be NULL if
 *   details are not required.
 * @retval true if fits and false otherwise. If not fit then mp_min, mp_max
 *   and details are filled with error details.
 */
bool
field_mp_is_in_fixed_int_range(enum field_type type, const char *data,
			       char *mp_min, char *mp_max,
			       const char **details);

static inline bool
action_is_nullable(enum on_conflict_action nullable_action)
{
	return nullable_action == ON_CONFLICT_ACTION_NONE;
}

/**
 * Decode MessagePack array of fields.
 * @param[inout] data MessagePack array of fields.
 * @param[out] fields Array of fields.
 * @param[out] field_count Length of a result array.
 * @param region Region to allocate result array.
 * @param names_only Only decode 'name' options, ignore the rest.
 * @retval Error code.
 */
int
field_def_array_decode(const char **data, struct field_def **fields,
		       uint32_t *field_count, struct region *region,
		       bool names_only);

/**
 * Duplicates array of fields using malloc. Never fails.
 * Returns NULL if zero-size array is passed.
 */
struct field_def *
field_def_array_dup(const struct field_def *fields, uint32_t field_count);

/**
 * Frees array of fields that was allocated with field_def_array_dup().
 */
void
field_def_array_delete(struct field_def *fields, uint32_t field_count);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_FIELD_DEF_H_INCLUDED */
