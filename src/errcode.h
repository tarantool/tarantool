#ifndef TARANTOOL_ERRCODE_H_INCLUDED
#define TARANTOOL_ERRCODE_H_INCLUDED
/*
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

#include "trivia/util.h"

struct errcode_record {
	const char *errstr;
	const char *errdesc;
	uint8_t errflags;
};

enum { TNT_ERRMSG_MAX = 512 };

/*
 * To add a new error code to Tarantool, extend this array.
 *
 * !IMPORTANT! Currently you need to manually update the user
 * guide (doc/user/errcode.xml) with each added error code.
 * Please don't forget to do it!
 */

#define ERROR_CODES(_)					    \
	/*  0 */_(ER_OK,			0, "OK") \
	/*  1 */_(ER_ILLEGAL_PARAMS,		2, "Illegal parameters, %s") \
	/*  2 */_(ER_MEMORY_ISSUE,		1, "Failed to allocate %u bytes in %s for %s") \
	/*  3 */_(ER_TUPLE_FOUND,		2, "Duplicate key exists in unique index %u") \
	/*  4 */_(ER_TUPLE_NOT_FOUND,		2, "Tuple doesn't exist in index %u") \
	/*  5 */_(ER_UNSUPPORTED,		2, "%s does not support %s") \
	/*  6 */_(ER_NONMASTER,			2, "Can't modify data on a replication slave. My master is: %s") \
	/*  7 */_(ER_SECONDARY,			2, "Can't modify data upon a request on the secondary port.") \
	/*  8 */_(ER_INJECTION,			2, "Error injection '%s'") \
	/*  9 */_(ER_CREATE_SPACE,		2, "Failed to create space %u: %s") \
	/* 10 */_(ER_SPACE_EXISTS,		2, "Space %u already exists") \
	/* 11 */_(ER_DROP_SPACE,		2, "Can't drop space %u: %s") \
	/* 12 */_(ER_ALTER_SPACE,		2, "Can't modify space %u: %s") \
	/* 13 */_(ER_INDEX_TYPE,		2, "Unsupported index type supplied for index %u in space %u") \
	/* 14 */_(ER_MODIFY_INDEX,		2, "Can't create or modify index %u in space %u: %s") \
	/* 15 */_(ER_LAST_DROP,			2, "Can't drop the primary key in a system space, space id %u") \
	/* 16 */_(ER_TUPLE_FORMAT_LIMIT,	2, "Tuple format limit reached: %u") \
	/* 17 */_(ER_DROP_PRIMARY_KEY,		2, "Can't drop primary key in space %u while secondary keys exist") \
	/* 18 */_(ER_KEY_FIELD_TYPE,		2, "Supplied key type of part %u does not match index part type: expected %s") \
	/* 19 */_(ER_EXACT_MATCH,		2, "Invalid key part count in an exact match (expected %u, got %u)") \
	/* 20 */_(ER_INVALID_MSGPACK,		2, "Invalid MsgPack - %s") \
	/* 21 */_(ER_PROC_RET,			2, "msgpack.encode: can not encode Lua type '%s'") \
	/* 22 */_(ER_TUPLE_NOT_ARRAY,		2, "Tuple/Key must be MsgPack array") \
	/* 23 */_(ER_FIELD_TYPE,		2, "Tuple field %u type does not match one required by operation: expected %s") \
	/* 24 */_(ER_FIELD_TYPE_MISMATCH,	2, "Ambiguous field type in index %u, key part %u. Requested type is %s but the field has previously been defined as %s") \
	/* 25 */_(ER_SPLICE,			2, "Field SPLICE error: %s") \
	/* 26 */_(ER_ARG_TYPE,			2, "Argument type in operation on field %u does not match field type: expected a %s") \
	/* 27 */_(ER_TUPLE_IS_TOO_LONG,		2, "Tuple is too long %u") \
	/* 28 */_(ER_UNKNOWN_UPDATE_OP,		2, "Unknown UPDATE operation") \
	/* 29 */_(ER_UPDATE_FIELD,		2, "Field %u UPDATE error: %s") \
	/* 30 */_(ER_FIBER_STACK,		2, "Can not create a new fiber: recursion limit reached") \
	/* 31 */_(ER_KEY_PART_COUNT,		2, "Invalid key part count (expected [0..%u], got %u)") \
	/* 32 */_(ER_PROC_LUA,			2, "%s") \
	/* 33 */_(ER_NO_SUCH_PROC,		2, "Procedure '%.*s' is not defined") \
	/* 34 */_(ER_NO_SUCH_TRIGGER,		2, "Trigger is not found") \
	/* 35 */_(ER_NO_SUCH_INDEX,		2, "No index #%u is defined in space %u") \
	/* 36 */_(ER_NO_SUCH_SPACE,		2, "Space %u does not exist") \
	/* 37 */_(ER_NO_SUCH_FIELD,		2, "Field %u was not found in the tuple") \
	/* 38 */_(ER_SPACE_ARITY,		2, "Tuple field count %u does not match space %u arity %u") \
	/* 39 */_(ER_INDEX_ARITY,		2, "Tuple field count %u is less than required by a defined index (expected %u)") \
	/* 40 */_(ER_WAL_IO,			2, "Failed to write to disk") \
	/* 41 */_(ER_MORE_THAN_ONE_TUPLE,	2, "More than one tuple found") \
	/* 42 */_(ER_ACCESS_DENIED,		2, "%s access denied for user '%s'") \
	/* 43 */_(ER_CREATE_USER,		2, "Failed to create user '%s': %s") \
	/* 44 */_(ER_DROP_USER,			2, "Failed to drop user '%s': %s") \
	/* 45 */_(ER_NO_SUCH_USER,		2, "User '%s' is not found") \
	/* 46 */_(ER_USER_EXISTS,		2, "User '%s' already exists") \
	/* 47 */_(ER_PASSWORD_MISMATCH,		2, "Incorrect password supplied for user '%s'") \
	/* 48 */_(ER_UNKNOWN_REQUEST_TYPE,	2, "Unknown request type %u") \
	/* 49 */_(ER_UNKNOWN_SCHEMA_OBJECT,	2, "Unknown object type '%s'") \
	/* 50 */_(ER_CREATE_FUNCTION,		2, "Failed to create function: %s") \
	/* 51 */_(ER_NO_SUCH_FUNCTION,		2, "Function '%s' does not exist") \
	/* 52 */_(ER_FUNCTION_EXISTS,		2, "Function '%s' already exists") \
	/* 53 */_(ER_FUNCTION_ACCESS_DENIED,	2, "%s access denied for user '%s' to function '%s'") \
	/* 54 */_(ER_FUNCTION_MAX,		2, "A limit on the total number of functions has been reached: %u") \
	/* 55 */_(ER_SPACE_ACCESS_DENIED,	2, "%s access denied for user '%s' to space '%s'") \
	/* 56 */_(ER_USER_MAX,			2, "A limit on the total number of users has been reached: %u") \
	/* 57 */_(ER_NO_SUCH_ENGINE,		2, "Space engine '%s' does not exist") \
	/* 58 */_(ER_RELOAD_CFG,		2, "Can't set option '%s' dynamically") \
	/* 59 */_(ER_CFG,			2, "Incorrect option value: %s") \
	/* 60 */_(ER_SOPHIA,			2, "%s") \
	/* 61 */_(ER_LOCAL_NODE_IS_NOT_ACTIVE,	2, "Local node is not active") \
	/* 62 */_(ER_UNKNOWN_NODE,		2, "Unknown node %u") \
	/* 63 */_(ER_INVALID_CLUSTER,		2, "Invalid cluster id") \



/*
 * !IMPORTANT! Please follow instructions at start of the file
 * when adding new errors.
 */

ENUM0(tnt_error_codes_enum, ERROR_CODES);
extern struct errcode_record tnt_error_codes[];

/** Return a string representation of error name, e.g. "ER_OK".
 */

static inline const char *tnt_errcode_str(uint32_t errcode)
{
	if (errcode >= tnt_error_codes_enum_MAX) {
		/* Unknown error code - can be triggered using box.raise() */
		return "ER_UNKNOWN";
	}
	return tnt_error_codes[errcode].errstr;
}


/** Return a 4-byte numeric error code, with status flags. */


static inline uint32_t tnt_errcode_val(uint32_t errcode)
{
	uint32_t errflags = errcode < tnt_error_codes_enum_MAX ?
		tnt_error_codes[errcode].errflags : 2; /* non-recoverable */
	return (errcode << 8) | errflags;
}


/** Return a description of the error. */

static inline const char *tnt_errcode_desc(uint32_t errcode)
{
	if (errcode >= tnt_error_codes_enum_MAX)
		return "Unknown error";

	return tnt_error_codes[errcode].errdesc;
}

#endif /* TARANTOOL_ERRCODE_H_INCLUDED */
