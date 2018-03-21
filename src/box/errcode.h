#ifndef TARANTOOL_BOX_ERRCODE_H_INCLUDED
#define TARANTOOL_BOX_ERRCODE_H_INCLUDED
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

#include "trivia/util.h"

#ifdef __cplusplus
extern "C" {
#endif

struct errcode_record {
	const char *errstr;
	const char *errdesc;
};

/*
 * To add a new error code to Tarantool, extend this array.
 *
 * !IMPORTANT! Currently you need to manually update the user
 * guide (doc/user/errcode.xml) with each added error code.
 * Please don't forget to do it!
 */

#define ERROR_CODES(_)					    \
	/*  0 */_(ER_UNKNOWN,			"Unknown error") \
	/*  1 */_(ER_ILLEGAL_PARAMS,		"Illegal parameters, %s") \
	/*  2 */_(ER_MEMORY_ISSUE,		"Failed to allocate %u bytes in %s for %s") \
	/*  3 */_(ER_TUPLE_FOUND,		"Duplicate key exists in unique index '%s' in space '%s'") \
	/*  4 */_(ER_TUPLE_NOT_FOUND,		"Tuple doesn't exist in index '%s' in space '%s'") \
	/*  5 */_(ER_UNSUPPORTED,		"%s does not support %s") \
	/*  6 */_(ER_NONMASTER,			"Can't modify data on a replication slave. My master is: %s") \
	/*  7 */_(ER_READONLY,			"Can't modify data because this instance is in read-only mode.") \
	/*  8 */_(ER_INJECTION,			"Error injection '%s'") \
	/*  9 */_(ER_CREATE_SPACE,		"Failed to create space '%s': %s") \
	/* 10 */_(ER_SPACE_EXISTS,		"Space '%s' already exists") \
	/* 11 */_(ER_DROP_SPACE,		"Can't drop space '%s': %s") \
	/* 12 */_(ER_ALTER_SPACE,		"Can't modify space '%s': %s") \
	/* 13 */_(ER_INDEX_TYPE,		"Unsupported index type supplied for index '%s' in space '%s'") \
	/* 14 */_(ER_MODIFY_INDEX,		"Can't create or modify index '%s' in space '%s': %s") \
	/* 15 */_(ER_LAST_DROP,			"Can't drop the primary key in a system space, space '%s'") \
	/* 16 */_(ER_TUPLE_FORMAT_LIMIT,	"Tuple format limit reached: %u") \
	/* 17 */_(ER_DROP_PRIMARY_KEY,		"Can't drop primary key in space '%s' while secondary keys exist") \
	/* 18 */_(ER_KEY_PART_TYPE,		"Supplied key type of part %u does not match index part type: expected %s") \
	/* 19 */_(ER_EXACT_MATCH,		"Invalid key part count in an exact match (expected %u, got %u)") \
	/* 20 */_(ER_INVALID_MSGPACK,		"Invalid MsgPack - %s") \
	/* 21 */_(ER_PROC_RET,			"msgpack.encode: can not encode Lua type '%s'") \
	/* 22 */_(ER_TUPLE_NOT_ARRAY,		"Tuple/Key must be MsgPack array") \
	/* 23 */_(ER_FIELD_TYPE,		"Tuple field %u type does not match one required by operation: expected %s") \
	/* 24 */_(ER_INDEX_PART_TYPE_MISMATCH,	"Field %s has type '%s' in one index, but type '%s' in another") \
	/* 25 */_(ER_SPLICE,			"SPLICE error on field %u: %s") \
	/* 26 */_(ER_UPDATE_ARG_TYPE,		"Argument type in operation '%c' on field %u does not match field type: expected %s") \
	/* 27 */_(ER_FORMAT_MISMATCH_INDEX_PART, "Field %s has type '%s' in space format, but type '%s' in index definition") \
	/* 28 */_(ER_UNKNOWN_UPDATE_OP,		"Unknown UPDATE operation") \
	/* 29 */_(ER_UPDATE_FIELD,		"Field %u UPDATE error: %s") \
	/* 30 */_(ER_FUNCTION_TX_ACTIVE,	"Transaction is active at return from function") \
	/* 31 */_(ER_KEY_PART_COUNT,		"Invalid key part count (expected [0..%u], got %u)") \
	/* 32 */_(ER_PROC_LUA,			"%s") \
	/* 33 */_(ER_NO_SUCH_PROC,		"Procedure '%.*s' is not defined") \
	/* 34 */_(ER_NO_SUCH_TRIGGER,		"Trigger is not found") \
	/* 35 */_(ER_NO_SUCH_INDEX,		"No index #%u is defined in space '%s'") \
	/* 36 */_(ER_NO_SUCH_SPACE,		"Space '%s' does not exist") \
	/* 37 */_(ER_NO_SUCH_FIELD,		"Field %d was not found in the tuple") \
	/* 38 */_(ER_EXACT_FIELD_COUNT,		"Tuple field count %u does not match space field count %u") \
	/* 39 */_(ER_MIN_FIELD_COUNT,		"Tuple field count %u is less than required by space format or defined indexes (expected at least %u)") \
	/* 40 */_(ER_WAL_IO,			"Failed to write to disk") \
	/* 41 */_(ER_MORE_THAN_ONE_TUPLE,	"Get() doesn't support partial keys and non-unique indexes") \
	/* 42 */_(ER_ACCESS_DENIED,		"%s access to %s '%s' is denied for user '%s'") \
	/* 43 */_(ER_CREATE_USER,		"Failed to create user '%s': %s") \
	/* 44 */_(ER_DROP_USER,			"Failed to drop user or role '%s': %s") \
	/* 45 */_(ER_NO_SUCH_USER,		"User '%s' is not found") \
	/* 46 */_(ER_USER_EXISTS,		"User '%s' already exists") \
	/* 47 */_(ER_PASSWORD_MISMATCH,		"Incorrect password supplied for user '%s'") \
	/* 48 */_(ER_UNKNOWN_REQUEST_TYPE,	"Unknown request type %u") \
	/* 49 */_(ER_UNKNOWN_SCHEMA_OBJECT,	"Unknown object type '%s'") \
	/* 50 */_(ER_CREATE_FUNCTION,		"Failed to create function '%s': %s") \
	/* 51 */_(ER_NO_SUCH_FUNCTION,		"Function '%s' does not exist") \
	/* 52 */_(ER_FUNCTION_EXISTS,		"Function '%s' already exists") \
	/* 53 */_(ER_BEFORE_REPLACE_RET,	"Invalid return value of space:before_replace trigger: expected tuple or nil, got %s") \
	/* 54 */_(ER_FUNCTION_MAX,		"A limit on the total number of functions has been reached: %u") \
	/* 55 */_(ER_UNUSED4,			"") \
	/* 56 */_(ER_USER_MAX,			"A limit on the total number of users has been reached: %u") \
	/* 57 */_(ER_NO_SUCH_ENGINE,		"Space engine '%s' does not exist") \
	/* 58 */_(ER_RELOAD_CFG,		"Can't set option '%s' dynamically") \
	/* 59 */_(ER_CFG,			"Incorrect value for option '%s': %s") \
	/* 60 */_(ER_SAVEPOINT_EMPTY_TX,	"Can not set a savepoint in an empty transaction") \
	/* 61 */_(ER_NO_SUCH_SAVEPOINT,		"Can not rollback to savepoint: the savepoint does not exist") \
	/* 62 */_(ER_UNKNOWN_REPLICA,		"Replica %s is not registered with replica set %s") \
	/* 63 */_(ER_REPLICASET_UUID_MISMATCH,	"Replica set UUID mismatch: expected %s, got %s") \
	/* 64 */_(ER_INVALID_UUID,		"Invalid UUID: %s") \
	/* 65 */_(ER_REPLICASET_UUID_IS_RO,	"Can't reset replica set UUID: it is already assigned") \
	/* 66 */_(ER_INSTANCE_UUID_MISMATCH,	"Instance UUID mismatch: expected %s, got %s") \
	/* 67 */_(ER_REPLICA_ID_IS_RESERVED,	"Can't initialize replica id with a reserved value %u") \
	/* 68 */_(ER_INVALID_ORDER,		"Invalid LSN order for instance %u: previous LSN = %llu, new lsn = %llu") \
	/* 69 */_(ER_MISSING_REQUEST_FIELD,	"Missing mandatory field '%s' in request") \
	/* 70 */_(ER_IDENTIFIER,		"Invalid identifier '%s' (expected printable symbols only)") \
	/* 71 */_(ER_DROP_FUNCTION,		"Can't drop function %u: %s") \
	/* 72 */_(ER_ITERATOR_TYPE,		"Unknown iterator type '%s'") \
	/* 73 */_(ER_REPLICA_MAX,		"Replica count limit reached: %u") \
	/* 74 */_(ER_INVALID_XLOG,		"Failed to read xlog: %lld") \
	/* 75 */_(ER_INVALID_XLOG_NAME,		"Invalid xlog name: expected %lld got %lld") \
	/* 76 */_(ER_INVALID_XLOG_ORDER,	"Invalid xlog order: %lld and %lld") \
	/* 77 */_(ER_NO_CONNECTION,		"Connection is not established") \
	/* 78 */_(ER_TIMEOUT,			"Timeout exceeded") \
	/* 79 */_(ER_ACTIVE_TRANSACTION,	"Operation is not permitted when there is an active transaction ") \
	/* 80 */_(ER_CURSOR_NO_TRANSACTION,	"The transaction the cursor belongs to has ended") \
	/* 81 */_(ER_CROSS_ENGINE_TRANSACTION,	"A multi-statement transaction can not use multiple storage engines") \
	/* 82 */_(ER_NO_SUCH_ROLE,		"Role '%s' is not found") \
	/* 83 */_(ER_ROLE_EXISTS,		"Role '%s' already exists") \
	/* 84 */_(ER_CREATE_ROLE,		"Failed to create role '%s': %s") \
	/* 85 */_(ER_INDEX_EXISTS,		"Index '%s' already exists") \
	/* 86 */_(ER_TUPLE_REF_OVERFLOW,	"Tuple reference counter overflow") \
	/* 87 */_(ER_ROLE_LOOP,			"Granting role '%s' to role '%s' would create a loop") \
	/* 88 */_(ER_GRANT,			"Incorrect grant arguments: %s") \
	/* 89 */_(ER_PRIV_GRANTED,		"User '%s' already has %s access on %s '%s'") \
	/* 90 */_(ER_ROLE_GRANTED,		"User '%s' already has role '%s'") \
	/* 91 */_(ER_PRIV_NOT_GRANTED,		"User '%s' does not have %s access on %s '%s'") \
	/* 92 */_(ER_ROLE_NOT_GRANTED,		"User '%s' does not have role '%s'") \
	/* 93 */_(ER_MISSING_SNAPSHOT,		"Can't find snapshot") \
	/* 94 */_(ER_CANT_UPDATE_PRIMARY_KEY,	"Attempt to modify a tuple field which is part of index '%s' in space '%s'") \
	/* 95 */_(ER_UPDATE_INTEGER_OVERFLOW,   "Integer overflow when performing '%c' operation on field %u") \
	/* 96 */_(ER_GUEST_USER_PASSWORD,       "Setting password for guest user has no effect") \
	/* 97 */_(ER_TRANSACTION_CONFLICT,      "Transaction has been aborted by conflict") \
	/* 98 */_(ER_UNSUPPORTED_ROLE_PRIV,     "Unsupported role privilege '%s'") \
	/* 99 */_(ER_LOAD_FUNCTION,		"Failed to dynamically load function '%s': %s") \
	/*100 */_(ER_FUNCTION_LANGUAGE,		"Unsupported language '%s' specified for function '%s'") \
	/*101 */_(ER_RTREE_RECT,		"RTree: %s must be an array with %u (point) or %u (rectangle/box) numeric coordinates") \
	/*102 */_(ER_PROC_C,			"%s") \
	/*103 */_(ER_UNKNOWN_RTREE_INDEX_DISTANCE_TYPE,	"Unknown RTREE index distance type %s") \
	/*104 */_(ER_PROTOCOL,			"%s") \
	/*105 */_(ER_UPSERT_UNIQUE_SECONDARY_KEY, "Space %s has a unique secondary index and does not support UPSERT") \
	/*106 */_(ER_WRONG_INDEX_RECORD,	"Wrong record in _index space: got {%s}, expected {%s}") \
	/*107 */_(ER_WRONG_INDEX_PARTS,		"Wrong index parts: %s; expected field1 id (number), field1 type (string), ...") \
	/*108 */_(ER_WRONG_INDEX_OPTIONS,	"Wrong index options (field %u): %s") \
	/*109 */_(ER_WRONG_SCHEMA_VERSION,	"Wrong schema version, current: %d, in request: %u") \
	/*110 */_(ER_MEMTX_MAX_TUPLE_SIZE,	"Failed to allocate %u bytes for tuple: tuple is too large. Check 'memtx_max_tuple_size' configuration option.") \
	/*111 */_(ER_WRONG_SPACE_OPTIONS,	"Wrong space options (field %u): %s") \
	/*112 */_(ER_UNSUPPORTED_INDEX_FEATURE,	"Index '%s' (%s) of space '%s' (%s) does not support %s") \
	/*113 */_(ER_VIEW_IS_RO,		"View '%s' is read-only") \
	/*114 */_(ER_SAVEPOINT_NO_TRANSACTION,	"Can not set a savepoint in absence of active transaction") \
	/*115 */_(ER_SYSTEM,			"%s") \
	/*116 */_(ER_LOADING,			"Instance bootstrap hasn't finished yet") \
	/*117 */_(ER_CONNECTION_TO_SELF,	"Connection to self") \
	/*118 */_(ER_KEY_PART_IS_TOO_LONG,	"Key part is too long: %u of %u bytes") \
	/*119 */_(ER_COMPRESSION,		"Compression error: %s") \
	/*120 */_(ER_CHECKPOINT_IN_PROGRESS,	"Snapshot is already in progress") \
	/*121 */_(ER_SUB_STMT_MAX,		"Can not execute a nested statement: nesting limit reached") \
	/*122 */_(ER_COMMIT_IN_SUB_STMT,	"Can not commit transaction in a nested statement") \
	/*123 */_(ER_ROLLBACK_IN_SUB_STMT,	"Rollback called in a nested statement") \
	/*124 */_(ER_DECOMPRESSION,		"Decompression error: %s") \
	/*125 */_(ER_INVALID_XLOG_TYPE,		"Invalid xlog type: expected %s, got %s") \
	/*126 */_(ER_ALREADY_RUNNING,		"Failed to lock WAL directory %s and hot_standby mode is off") \
	/*127 */_(ER_INDEX_FIELD_COUNT_LIMIT,	"Indexed field count limit reached: %d indexed fields") \
	/*128 */_(ER_LOCAL_INSTANCE_ID_IS_READ_ONLY, "The local instance id %u is read-only") \
	/*129 */_(ER_BACKUP_IN_PROGRESS,	"Backup is already in progress") \
	/*130 */_(ER_READ_VIEW_ABORTED,         "The read view is aborted") \
	/*131 */_(ER_INVALID_INDEX_FILE,	"Invalid INDEX file %s: %s") \
	/*132 */_(ER_INVALID_RUN_FILE,		"Invalid RUN file: %s") \
	/*133 */_(ER_INVALID_VYLOG_FILE,	"Invalid VYLOG file: %s") \
	/*134 */_(ER_CHECKPOINT_ROLLBACK,	"Can't start a checkpoint while in cascading rollback") \
	/*135 */_(ER_VY_QUOTA_TIMEOUT,		"Timed out waiting for Vinyl memory quota") \
	/*136 */_(ER_PARTIAL_KEY,		"%s index  does not support selects via a partial key (expected %u parts, got %u). Please Consider changing index type to TREE.") \
	/*137 */_(ER_TRUNCATE_SYSTEM_SPACE,	"Can't truncate a system space, space '%s'") \
	/*138 */_(ER_LOAD_MODULE,		"Failed to dynamically load module '%.*s': %s") \
	/*139 */_(ER_VINYL_MAX_TUPLE_SIZE,	"Failed to allocate %u bytes for tuple: tuple is too large. Check 'vinyl_max_tuple_size' configuration option.") \
	/*140 */_(ER_WRONG_DD_VERSION,		"Wrong _schema version: expected 'major.minor[.patch]'") \
	/*141 */_(ER_WRONG_SPACE_FORMAT,	"Wrong space format (field %u): %s") \
	/*142 */_(ER_CREATE_SEQUENCE,		"Failed to create sequence '%s': %s") \
	/*143 */_(ER_ALTER_SEQUENCE,		"Can't modify sequence '%s': %s") \
	/*144 */_(ER_DROP_SEQUENCE,		"Can't drop sequence '%s': %s") \
	/*145 */_(ER_NO_SUCH_SEQUENCE,		"Sequence '%s' does not exist") \
	/*146 */_(ER_SEQUENCE_EXISTS,		"Sequence '%s' already exists") \
	/*147 */_(ER_SEQUENCE_OVERFLOW,		"Sequence '%s' has overflowed") \
	/*148 */_(ER_UNUSED5,			"") \
	/*149 */_(ER_SPACE_FIELD_IS_DUPLICATE,	"Space field '%s' is duplicate") \
	/*150 */_(ER_CANT_CREATE_COLLATION,	"Failed to initialize collation: %s.") \
	/*151 */_(ER_WRONG_COLLATION_OPTIONS,	"Wrong collation options (field %u): %s") \
	/*152 */_(ER_NULLABLE_PRIMARY,		"Primary index of the space '%s' can not contain nullable parts") \
	/*153 */_(ER_NULLABLE_MISMATCH,		"Field %d is %s in space format, but %s in index parts") \
	/*154 */_(ER_SQL_BIND_VALUE,            "Bind value for parameter %s is out of range for type %s") \
	/*155 */_(ER_SQL_BIND_TYPE,             "Bind value type %s for parameter %s is not supported") \
	/*156 */_(ER_SQL_BIND_PARAMETER_MAX,    "SQL bind parameter limit reached: %d") \
	/*157 */_(ER_SQL_EXECUTE,               "Failed to execute SQL statement: %s") \
	/*158 */_(ER_SQL,			"SQL error: %s") \
	/*159 */_(ER_SQL_BIND_NOT_FOUND,	"Parameter %s was not found in the statement") \
	/*160 */_(ER_ACTION_MISMATCH,		"Field %d contains %s on conflict action, but %s in index parts") \
	/*161 */_(ER_VIEW_MISSING_SQL,		"Space declared as a view must have SQL statement") \

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
