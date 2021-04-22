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

#include "field_def.h"
#include "trivia/util.h"
#include "key_def.h"
#include "mp_extension_types.h"
#include "uuid/mp_uuid.h"
#include "uuid/tt_uuid.h"

const char *mp_type_strs[] = {
	/* .MP_NIL    = */ "nil",
	/* .MP_UINT   = */ "unsigned",
	/* .MP_INT    = */ "integer",
	/* .MP_STR    = */ "string",
	/* .MP_BIN    = */ "blob",
	/* .MP_ARRAY  = */ "array",
	/* .MP_MAP    = */ "map",
	/* .MP_BOOL   = */ "boolean",
	/* .MP_FLOAT  = */ "float",
	/* .MP_DOUBLE = */ "double",
	/* .MP_EXT    = */ "extension",
};

/*
 * messagepack types supported by given field types.
 * MP_EXT requires to parse extension type to check
 * compatibility with field type.
 */
const uint32_t field_mp_type[] = {
	/* [FIELD_TYPE_ANY]      =  */ UINT32_MAX,
	/* [FIELD_TYPE_UNSIGNED] =  */ 1U << MP_UINT,
	/* [FIELD_TYPE_STRING]   =  */ 1U << MP_STR,
	/* [FIELD_TYPE_NUMBER]   =  */ (1U << MP_UINT) | (1U << MP_INT) |
		(1U << MP_FLOAT) | (1U << MP_DOUBLE),
	/* [FIELD_TYPE_DOUBLE]   =  */ 1U << MP_DOUBLE,
	/* [FIELD_TYPE_INTEGER]  =  */ (1U << MP_UINT) | (1U << MP_INT),
	/* [FIELD_TYPE_BOOLEAN]  =  */ 1U << MP_BOOL,
	/* [FIELD_TYPE_VARBINARY] =  */ 1U << MP_BIN,
	/* [FIELD_TYPE_SCALAR]   =  */ (1U << MP_UINT) | (1U << MP_INT) |
		(1U << MP_FLOAT) | (1U << MP_DOUBLE) | (1U << MP_STR) |
		(1U << MP_BIN) | (1U << MP_BOOL),
	/* [FIELD_TYPE_DECIMAL]  =  */ 0, /* only MP_DECIMAL is supported */
	/* [FIELD_TYPE_UUID]     =  */ 0, /* only MP_UUID is supported */
	/* [FIELD_TYPE_ARRAY]    =  */ 1U << MP_ARRAY,
	/* [FIELD_TYPE_MAP]      =  */ (1U << MP_MAP),
};

const uint32_t field_ext_type[] = {
	/* [FIELD_TYPE_ANY]       = */ UINT32_MAX ^ (1U << MP_UNKNOWN_EXTENSION),
	/* [FIELD_TYPE_UNSIGNED]  = */ 0,
	/* [FIELD_TYPE_STRING]    = */ 0,
	/* [FIELD_TYPE_NUMBER]    = */ 1U << MP_DECIMAL,
	/* [FIELD_TYPE_DOUBLE]    = */ 0,
	/* [FIELD_TYPE_INTEGER]   = */ 0,
	/* [FIELD_TYPE_BOOLEAN]   = */ 0,
	/* [FIELD_TYPE_VARBINARY] = */ 0,
	/* [FIELD_TYPE_SCALAR]    = */ (1U << MP_DECIMAL) | (1U << MP_UUID),
	/* [FIELD_TYPE_DECIMAL]   = */ 1U << MP_DECIMAL,
	/* [FIELD_TYPE_UUID]      = */ 1U << MP_UUID,
	/* [FIELD_TYPE_ARRAY]     = */ 0,
	/* [FIELD_TYPE_MAP]       = */ 0,
};

const char *field_type_strs[] = {
	/* [FIELD_TYPE_ANY]      = */ "any",
	/* [FIELD_TYPE_UNSIGNED] = */ "unsigned",
	/* [FIELD_TYPE_STRING]   = */ "string",
	/* [FIELD_TYPE_NUMBER]   = */ "number",
	/* [FIELD_TYPE_DOUBLE]   = */ "double",
	/* [FIELD_TYPE_INTEGER]  = */ "integer",
	/* [FIELD_TYPE_BOOLEAN]  = */ "boolean",
	/* [FIELD_TYPE_VARBINARY] = */"varbinary",
	/* [FIELD_TYPE_SCALAR]   = */ "scalar",
	/* [FIELD_TYPE_DECIMAL]  = */ "decimal",
	/* [FIELD_TYPE_UUID]     = */ "uuid",
	/* [FIELD_TYPE_ARRAY]    = */ "array",
	/* [FIELD_TYPE_MAP]      = */ "map",
};

const char *on_conflict_action_strs[] = {
	/* [ON_CONFLICT_ACTION_NONE]     = */ "none",
	/* [ON_CONFLICT_ACTION_ROLLBACK] = */ "rollback",
	/* [ON_CONFLICT_ACTION_ABORT]    = */ "abort",
	/* [ON_CONFLICT_ACTION_FAIL]     = */ "fail",
	/* [ON_CONFLICT_ACTION_IGNORE]   = */ "ignore",
	/* [ON_CONFLICT_ACTION_REPLACE]  = */ "replace",
	/* [ON_CONFLICT_ACTION_DEFAULT]  = */ "default"
};

static int64_t
field_type_by_name_wrapper(const char *str, uint32_t len)
{
	return field_type_by_name(str, len);
}

/**
 * Table of a field types compatibility.
 * For an i row and j column the value is true, if the i type
 * values can be stored in the j type.
 */
static const bool field_type_compatibility[] = {
	   /*   ANY   UNSIGNED  STRING   NUMBER  DOUBLE  INTEGER  BOOLEAN VARBINARY SCALAR  DECIMAL   UUID    ARRAY    MAP  */
/*   ANY    */ true,   false,   false,   false,   false,   false,   false,   false,  false,  false,  false,   false,   false,
/* UNSIGNED */ true,   true,    false,   true,    false,   true,    false,   false,  true,   false,  false,   false,   false,
/*  STRING  */ true,   false,   true,    false,   false,   false,   false,   false,  true,   false,  false,   false,   false,
/*  NUMBER  */ true,   false,   false,   true,    false,   false,   false,   false,  true,   false,  false,   false,   false,
/*  DOUBLE  */ true,   false,   false,   true,    true,    false,   false,   false,  true,   false,  false,   false,   false,
/*  INTEGER */ true,   false,   false,   true,    false,   true,    false,   false,  true,   false,  false,   false,   false,
/*  BOOLEAN */ true,   false,   false,   false,   false,   false,   true,    false,  true,   false,  false,   false,   false,
/* VARBINARY*/ true,   false,   false,   false,   false,   false,   false,   true,   true,   false,  false,   false,   false,
/*  SCALAR  */ true,   false,   false,   false,   false,   false,   false,   false,  true,   false,  false,   false,   false,
/*  DECIMAL */ true,   false,   false,   true,    false,   false,   false,   false,  true,   true,   false,   false,   false,
/*   UUID   */ true,   false,   false,   false,   false,   false,   false,   false,  false,  false,  true,    false,   false,
/*   ARRAY  */ true,   false,   false,   false,   false,   false,   false,   false,  false,  false,  false,   true,    false,
/*    MAP   */ true,   false,   false,   false,   false,   false,   false,   false,  false,  false,  false,   false,   true,
};

bool
field_type1_contains_type2(enum field_type type1, enum field_type type2)
{
	int idx = type2 * field_type_MAX + type1;
	return field_type_compatibility[idx];
}

const struct opt_def field_def_reg[] = {
	OPT_DEF_ENUM("type", field_type, struct field_def, type,
		     field_type_by_name_wrapper),
	OPT_DEF("name", OPT_STRPTR, struct field_def, name),
	OPT_DEF("is_nullable", OPT_BOOL, struct field_def, is_nullable),
	OPT_DEF_ENUM("nullable_action", on_conflict_action, struct field_def,
		     nullable_action, NULL),
	OPT_DEF("collation", OPT_UINT32, struct field_def, coll_id),
	OPT_DEF("default", OPT_STRPTR, struct field_def, default_value),
	OPT_END,
};

const struct field_def field_def_default = {
	.type = FIELD_TYPE_ANY,
	.name = NULL,
	.is_nullable = false,
	.nullable_action = ON_CONFLICT_ACTION_DEFAULT,
	.coll_id = COLL_NONE,
	.default_value = NULL,
	.default_value_expr = NULL
};

enum field_type
field_type_by_name(const char *name, size_t len)
{
	enum field_type field_type = strnindex(field_type_strs, name, len,
					       field_type_MAX);
	if (field_type != field_type_MAX)
		return field_type;
	/* 'num' and 'str' in _index are deprecated since Tarantool 1.7 */
	if (strncasecmp(name, "num", len) == 0)
		return FIELD_TYPE_UNSIGNED;
	else if (strncasecmp(name, "str", len) == 0)
		return FIELD_TYPE_STRING;
	else if (len == 1 && name[0] == '*')
		return FIELD_TYPE_ANY;
	return field_type_MAX;
}
