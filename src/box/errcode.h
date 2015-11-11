#ifndef TARANTOOL_BOX_ERRCODE_H_INCLUDED
#define TARANTOOL_BOX_ERRCODE_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#include "trivia/util.h"

#ifdef __cplusplus
extern "C" {
#endif

struct errcode_record {
	const char *errstr;
	const char *errdesc;
	uint8_t errflags;
};

/*
 * To add a new error code to Tarantool, extend this array.
 *
 * !IMPORTANT! Currently you need to manually update the user
 * guide (doc/user/errcode.xml) with each added error code.
 * Please don't forget to do it!
 */

#define ERROR_CODES(_)					    \
	/*  0 */_(ER_UNKNOWN,			2, "Unknown error") \
	/*  1 */_(ER_ILLEGAL_PARAMS,		2, "Illegal parameters, %s") \
	/*  2 */_(ER_MEMORY_ISSUE,		1, "Failed to allocate %u bytes in %s for %s") \
	/*  3 */_(ER_TUPLE_FOUND,		2, "Duplicate key exists in unique index '%s' in space '%s'") \
	/*  4 */_(ER_TUPLE_NOT_FOUND,		2, "Tuple doesn't exist in index '%s' in space '%s'") \
	/*  5 */_(ER_UNSUPPORTED,		2, "%s does not support %s") \
	/*  6 */_(ER_NONMASTER,			2, "Can't modify data on a replication slave. My master is: %s") \
	/*  7 */_(ER_READONLY,			2, "Can't modify data because this server is in read-only mode.") \
	/*  8 */_(ER_INJECTION,			2, "Error injection '%s'") \
	/*  9 */_(ER_CREATE_SPACE,		2, "Failed to create space '%s': %s") \
	/* 10 */_(ER_SPACE_EXISTS,		2, "Space '%s' already exists") \
	/* 11 */_(ER_DROP_SPACE,		2, "Can't drop space '%s': %s") \
	/* 12 */_(ER_ALTER_SPACE,		2, "Can't modify space '%s': %s") \
	/* 13 */_(ER_INDEX_TYPE,		2, "Unsupported index type supplied for index '%s' in space '%s'") \
	/* 14 */_(ER_MODIFY_INDEX,		2, "Can't create or modify index '%s' in space '%s': %s") \
	/* 15 */_(ER_LAST_DROP,			2, "Can't drop the primary key in a system space, space '%s'") \
	/* 16 */_(ER_TUPLE_FORMAT_LIMIT,	2, "Tuple format limit reached: %u") \
	/* 17 */_(ER_DROP_PRIMARY_KEY,		2, "Can't drop primary key in space '%s' while secondary keys exist") \
	/* 18 */_(ER_KEY_PART_TYPE,		2, "Supplied key type of part %u does not match index part type: expected %s") \
	/* 19 */_(ER_EXACT_MATCH,		2, "Invalid key part count in an exact match (expected %u, got %u)") \
	/* 20 */_(ER_INVALID_MSGPACK,		2, "Invalid MsgPack - %s") \
	/* 21 */_(ER_PROC_RET,			2, "msgpack.encode: can not encode Lua type '%s'") \
	/* 22 */_(ER_TUPLE_NOT_ARRAY,		2, "Tuple/Key must be MsgPack array") \
	/* 23 */_(ER_FIELD_TYPE,		2, "Tuple field %u type does not match one required by operation: expected %s") \
	/* 24 */_(ER_FIELD_TYPE_MISMATCH,	2, "Ambiguous field type in index '%s', key part %u. Requested type is %s but the field has previously been defined as %s") \
	/* 25 */_(ER_SPLICE,			2, "SPLICE error on field %u: %s") \
	/* 26 */_(ER_ARG_TYPE,			2, "Argument type in operation '%c' on field %u does not match field type: expected a %s") \
	/* 27 */_(ER_TUPLE_IS_TOO_LONG,		2, "Tuple is too long %u") \
	/* 28 */_(ER_UNKNOWN_UPDATE_OP,		2, "Unknown UPDATE operation") \
	/* 29 */_(ER_UPDATE_FIELD,		2, "Field %u UPDATE error: %s") \
	/* 30 */_(ER_FIBER_STACK,		2, "Can not create a new fiber: recursion limit reached") \
	/* 31 */_(ER_KEY_PART_COUNT,		2, "Invalid key part count (expected [0..%u], got %u)") \
	/* 32 */_(ER_PROC_LUA,			2, "%s") \
	/* 33 */_(ER_NO_SUCH_PROC,		2, "Procedure '%.*s' is not defined") \
	/* 34 */_(ER_NO_SUCH_TRIGGER,		2, "Trigger is not found") \
	/* 35 */_(ER_NO_SUCH_INDEX,		2, "No index #%u is defined in space '%s'") \
	/* 36 */_(ER_NO_SUCH_SPACE,		2, "Space '%s' does not exist") \
	/* 37 */_(ER_NO_SUCH_FIELD,		2, "Field %d was not found in the tuple") \
	/* 38 */_(ER_SPACE_FIELD_COUNT,		2, "Tuple field count %u does not match space '%s' field count %u") \
	/* 39 */_(ER_INDEX_FIELD_COUNT,		2, "Tuple field count %u is less than required by a defined index (expected %u)") \
	/* 40 */_(ER_WAL_IO,			2, "Failed to write to disk") \
	/* 41 */_(ER_MORE_THAN_ONE_TUPLE,	2, "More than one tuple found by get()") \
	/* 42 */_(ER_ACCESS_DENIED,		2, "%s access denied for user '%s'") \
	/* 43 */_(ER_CREATE_USER,		2, "Failed to create user '%s': %s") \
	/* 44 */_(ER_DROP_USER,			2, "Failed to drop user '%s': %s") \
	/* 45 */_(ER_NO_SUCH_USER,		2, "User '%s' is not found") \
	/* 46 */_(ER_USER_EXISTS,		2, "User '%s' already exists") \
	/* 47 */_(ER_PASSWORD_MISMATCH,		2, "Incorrect password supplied for user '%s'") \
	/* 48 */_(ER_UNKNOWN_REQUEST_TYPE,	2, "Unknown request type %u") \
	/* 49 */_(ER_UNKNOWN_SCHEMA_OBJECT,	2, "Unknown object type '%s'") \
	/* 50 */_(ER_CREATE_FUNCTION,		2, "Failed to create function '%s': %s") \
	/* 51 */_(ER_NO_SUCH_FUNCTION,		2, "Function '%s' does not exist") \
	/* 52 */_(ER_FUNCTION_EXISTS,		2, "Function '%s' already exists") \
	/* 53 */_(ER_FUNCTION_ACCESS_DENIED,	2, "%s access denied for user '%s' to function '%s'") \
	/* 54 */_(ER_FUNCTION_MAX,		2, "A limit on the total number of functions has been reached: %u") \
	/* 55 */_(ER_SPACE_ACCESS_DENIED,	2, "%s access denied for user '%s' to space '%s'") \
	/* 56 */_(ER_USER_MAX,			2, "A limit on the total number of users has been reached: %u") \
	/* 57 */_(ER_NO_SUCH_ENGINE,		2, "Space engine '%s' does not exist") \
	/* 58 */_(ER_RELOAD_CFG,		2, "Can't set option '%s' dynamically") \
	/* 59 */_(ER_CFG,			2, "Incorrect value for option '%s': %s") \
	/* 60 */_(ER_SOPHIA,			2, "%s") \
	/* 61 */_(ER_LOCAL_SERVER_IS_NOT_ACTIVE,2, "Local server is not active") \
	/* 62 */_(ER_UNKNOWN_SERVER,		2, "Server %s is not registered with the cluster") \
	/* 63 */_(ER_CLUSTER_ID_MISMATCH,	2, "Cluster id of the replica %s doesn't match cluster id of the master %s") \
	/* 64 */_(ER_INVALID_UUID,		2, "Invalid UUID: %s") \
	/* 65 */_(ER_CLUSTER_ID_IS_RO,		2, "Can't reset cluster id: it is already assigned") \
	/* 66 */_(ER_SERVER_ID_MISMATCH,	2, "Remote ID mismatch for %s: expected %u, got %u") \
	/* 67 */_(ER_SERVER_ID_IS_RESERVED,	2, "Can't initialize server id with a reserved value %u") \
	/* 68 */_(ER_INVALID_ORDER,		2, "Invalid LSN order for server %u: previous LSN = %llu, new lsn = %llu") \
	/* 69 */_(ER_MISSING_REQUEST_FIELD,	2, "Missing mandatory field '%s' in request") \
	/* 70 */_(ER_IDENTIFIER,		2, "Invalid identifier '%s' (expected letters, digits or an underscore)") \
	/* 71 */_(ER_DROP_FUNCTION,		2, "Can't drop function %u: %s") \
	/* 72 */_(ER_ITERATOR_TYPE,		2, "Unknown iterator type '%s'") \
	/* 73 */_(ER_REPLICA_MAX,		2, "Replica count limit reached: %u") \
	/* 74 */_(ER_INVALID_XLOG,		2, "Failed to read xlog: %lld") \
	/* 75 */_(ER_INVALID_XLOG_NAME,		2, "Invalid xlog name: expected %lld got %lld") \
	/* 76 */_(ER_INVALID_XLOG_ORDER,	2, "Invalid xlog order: %lld and %lld") \
	/* 77 */_(ER_NO_CONNECTION,		2, "Connection is not established") \
	/* 78 */_(ER_TIMEOUT,			2, "Timeout exceeded") \
	/* 79 */_(ER_ACTIVE_TRANSACTION,	2, "Operation is not permitted when there is an active transaction ") \
	/* 80 */_(ER_NO_ACTIVE_TRANSACTION,	2, "Operation is not permitted when there is no active transaction ") \
	/* 81 */_(ER_CROSS_ENGINE_TRANSACTION,	2, "A multi-statement transaction can not use multiple storage engines") \
	/* 82 */_(ER_NO_SUCH_ROLE,		2, "Role '%s' is not found") \
	/* 83 */_(ER_ROLE_EXISTS,		2, "Role '%s' already exists") \
	/* 84 */_(ER_CREATE_ROLE,		2, "Failed to create role '%s': %s") \
	/* 85 */_(ER_INDEX_EXISTS,		2, "Index '%s' already exists") \
	/* 86 */_(ER_TUPLE_REF_OVERFLOW,	1, "Tuple reference counter overflow") \
	/* 87 */_(ER_ROLE_LOOP,			2, "Granting role '%s' to role '%s' would create a loop") \
	/* 88 */_(ER_GRANT,			2, "Incorrect grant arguments: %s") \
	/* 89 */_(ER_PRIV_GRANTED,		2, "User '%s' already has %s access on %s '%s'") \
	/* 90 */_(ER_ROLE_GRANTED,		2, "User '%s' already has role '%s'") \
	/* 91 */_(ER_PRIV_NOT_GRANTED,		2, "User '%s' does not have %s access on %s '%s'") \
	/* 92 */_(ER_ROLE_NOT_GRANTED,		2, "User '%s' does not have role '%s'") \
	/* 93 */_(ER_MISSING_SNAPSHOT,		2, "Can't find snapshot") \
	/* 94 */_(ER_CANT_UPDATE_PRIMARY_KEY,	2, "Attempt to modify a tuple field which is part of index '%s' in space '%s'") \
	/* 95 */_(ER_UPDATE_INTEGER_OVERFLOW,   2, "Integer overflow when performing '%c' operation on field %u") \
	/* 96 */_(ER_GUEST_USER_PASSWORD,       2, "Setting password for guest user has no effect") \
	/* 97 */_(ER_TRANSACTION_CONFLICT,      2, "Transaction has been aborted by conflict") \
	/* 98 */_(ER_UNSUPPORTED_ROLE_PRIV,     2, "Unsupported role privilege '%s'") \
	/* 99 */_(ER_LOAD_FUNCTION,		2, "Failed to dynamically load function '%s': %s") \
	/*100 */_(ER_FUNCTION_LANGUAGE,		2, "Unsupported language '%s' specified for function '%s'") \
	/*101 */_(ER_RTREE_RECT,		2, "RTree: %s must be an array with %u (point) or %u (rectangle/box) numeric coordinates") \
	/*102 */_(ER_PROC_C,			2, "%s") \
	/*103 */_(ER_UNKNOWN_RTREE_INDEX_DISTANCE_TYPE,	2, "Unknown RTREE index distance type %s") \
	/*104 */_(ER_PROTOCOL,			2, "%s") \
	/*105 */_(ER_UPSERT_UNIQUE_SECONDARY_KEY, 2, "Space %s has a unique secondary index and does not support UPSERT") \
	/*106 */_(ER_WRONG_INDEX_RECORD,  2, "Wrong record in _index space: got {%s}, expected {%s}") \
	/*107 */_(ER_WRONG_INDEX_PARTS, 2, "Wrong index parts (field %u): %s; expected field1 id (number), field1 type (string), ...") \
	/*108 */_(ER_WRONG_INDEX_OPTIONS, 2, "Wrong index options (field %u): %s") \
	/*109 */_(ER_WRONG_SCHEMA_VERSION, 2, "Wrong schema version, current: %d, in request: %u") \
	/*110 */_(ER_SLAB_ALLOC_MAX, 2, "Failed to allocate %u bytes for tuple in the slab allocator: tuple is too large. Check 'slab_alloc_maximal' configuration option.") \
	/*111 */_(ER_SERVER_UUID_MISMATCH, 2, "Remote UUID mismatch: expected %s, got %s") \

/*
 * !IMPORTANT! Please follow instructions at start of the file
 * when adding new errors.
 */

ENUM0(box_error_code, ERROR_CODES);
extern struct errcode_record box_error_codes[];

/** Return a string representation of error name, e.g. "ER_OK".
 */

static inline const char *tnt_errcode_str(uint32_t errcode)
{
	if (errcode >= box_error_code_MAX) {
		/* Unknown error code - can be triggered using box.error() */
		return "ER_UNKNOWN";
	}
	return box_error_codes[errcode].errstr;
}

/** Return a description of the error. */
static inline const char *tnt_errcode_desc(uint32_t errcode)
{
	if (errcode >= box_error_code_MAX)
		return "Unknown error";

	return box_error_codes[errcode].errdesc;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TARANTOOL_BOX_ERRCODE_H_INCLUDED */
