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
#include "opt_def.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

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
	FIELD_TYPE_INTEGER,
	FIELD_TYPE_BOOLEAN,
	FIELD_TYPE_SCALAR,
	FIELD_TYPE_ARRAY,
	FIELD_TYPE_MAP,
	field_type_MAX
};

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

extern const char *on_conflict_action_strs[];

/** Check if @a type1 can store values of @a type2. */
bool
field_type1_contains_type2(enum field_type type1, enum field_type type2);

/**
 * Get field type by name
 */
enum field_type
field_type_by_name(const char *name, size_t len);

extern const struct opt_def field_def_reg[];
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
	/** 0-terminated field name. */
	char *name;
	/** True, if a field can store NULL. */
	bool is_nullable;
	/** Action to perform if NULL constraint failed. */
	enum on_conflict_action nullable_action;
	/** Collation ID for string comparison. */
	uint32_t coll_id;
	/** 0-terminated SQL expression for DEFAULT value. */
	char *default_value;
	/** AST for parsed default value. */
	struct Expr *default_value_expr;
};

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_FIELD_DEF_H_INCLUDED */
