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

/** Error code field type. */
enum errcode_field_type {
	ERRCODE_FIELD_TYPE_CHAR,
	ERRCODE_FIELD_TYPE_INT,
	ERRCODE_FIELD_TYPE_UINT,
	ERRCODE_FIELD_TYPE_LONG,
	ERRCODE_FIELD_TYPE_ULONG,
	ERRCODE_FIELD_TYPE_LLONG,
	ERRCODE_FIELD_TYPE_ULLONG,
	ERRCODE_FIELD_TYPE_STRING,
	ERRCODE_FIELD_TYPE_MSGPACK,
	ERRCODE_FIELD_TYPE_TUPLE,
};

/** Error code field description. */
struct errcode_field {
	/** Field name. */
	const char *name;
	/** Field type. */
	enum errcode_field_type type;
};

/** Description of a known error code. */
struct errcode_record {
	/**
	 * String representation of error code. Equal to corresponding token
	 * in ERROR_CODES macro.
	 */
	const char *errstr;
	/**
	 * Error code message. Equal to corresponding string literal in
	 * ERROR_CODES macro.
	 */
	const char *errdesc;
	/**
	 * Error code fields. Described by arguments after error code message
	 * in ERROR_CODES macro.
	 */
	const struct errcode_field *errfields;
	/** Error code fields count. */
	int errfields_count;
};

#ifdef TEST_BUILD
/**
 * Test error codes are used only for testing. Let's name them with ER_TEST_
 * prefix (we rely on that in tests). Test error code should be >= 10000
 * to avoid clash with actual error codes.
 */
#define TEST_ERROR_CODES(_) \
	_(ER_TEST_FIRST, 10000,			"Test error first") \
	_(ER_TEST_TYPE_CHAR, 10001,		"Test error", "field", CHAR) \
	_(ER_TEST_TYPE_INT, 10002,		"Test error", "field", INT) \
	_(ER_TEST_TYPE_UINT, 10003,		"Test error", "field", UINT) \
	_(ER_TEST_TYPE_LONG, 10004,		"Test error", "field", LONG) \
	_(ER_TEST_TYPE_ULONG, 10005,		"Test error", "field", ULONG) \
	_(ER_TEST_TYPE_LLONG, 10006,		"Test error", "field", LLONG) \
	_(ER_TEST_TYPE_ULLONG, 10007,		"Test error", "field", UULONG) \
	_(ER_TEST_TYPE_STRING, 10008,		"Test error", "field", STRING) \
	_(ER_TEST_TYPE_MSGPACK, 10009,		"Test error", "field", MSGPACK) \
	_(ER_TEST_TYPE_TUPLE, 10010,		"Test error", "field", TUPLE) \
	_(ER_TEST_5_ARGS, 10011,		"Test error", "f1", INT, "f2", INT, "f3", INT, "f4", INT, "f5", INT) \

#else
#define TEST_ERROR_CODES(_)
#endif

/*
 * To add a new error code to Tarantool, extend this array.
 *
 * !IMPORTANT! Currently you need to manually update the user
 * guide (doc/user/errcode.xml) with each added error code.
 * Please don't forget to do it!
 */

#define ERROR_CODES(_) \
	_(ER_UNKNOWN, 0,			"Unknown error") \
	_(ER_ILLEGAL_PARAMS, 1,			"Illegal parameters, %s") \
	_(ER_MEMORY_ISSUE, 2,			"Failed to allocate %u bytes in %s for %s") \
	_(ER_TUPLE_FOUND, 3,			"Duplicate key exists in unique index \"%s\" in space \"%s\" with old tuple - %s and new tuple - %s") \
	_(ER_TUPLE_NOT_FOUND, 4,		"Tuple doesn't exist in index '%s' in space '%s'") \
	_(ER_UNSUPPORTED, 5,			"%s does not support %s") \
	_(ER_NONMASTER, 6,			"Can't modify data on a replication slave. My master is: %s") \
	_(ER_READONLY, 7,			"Can't modify data on a read-only instance") \
	_(ER_INJECTION, 8,			"Error injection '%s'") \
	_(ER_CREATE_SPACE, 9,			"Failed to create space '%s': %s") \
	_(ER_SPACE_EXISTS, 10,			"Space '%s' already exists") \
	_(ER_DROP_SPACE, 11,			"Can't drop space '%s': %s") \
	_(ER_ALTER_SPACE, 12,			"Can't modify space '%s': %s") \
	_(ER_INDEX_TYPE, 13,			"Unsupported index type supplied for index '%s' in space '%s'") \
	_(ER_MODIFY_INDEX, 14,			"Can't create or modify index '%s' in space '%s': %s") \
	_(ER_LAST_DROP, 15,			"Can't drop the primary key in a system space, space '%s'") \
	_(ER_TUPLE_FORMAT_LIMIT, 16,		"Tuple format limit reached: %u") \
	_(ER_DROP_PRIMARY_KEY, 17,		"Can't drop primary key in space '%s' while secondary keys exist") \
	_(ER_KEY_PART_TYPE, 18,			"Supplied key type of part %u does not match index part type: expected %s") \
	_(ER_EXACT_MATCH, 19,			"Invalid key part count in an exact match (expected %u, got %u)") \
	_(ER_INVALID_MSGPACK, 20,		"Invalid MsgPack - %s") \
	_(ER_PROC_RET, 21,			"msgpack.encode: can not encode Lua type '%s'") \
	_(ER_TUPLE_NOT_ARRAY, 22,		"Tuple/Key must be MsgPack array") \
	_(ER_FIELD_TYPE, 23,			"Tuple field %s type does not match one required by operation: expected %s, got %s") \
	_(ER_INDEX_PART_TYPE_MISMATCH, 24,	"Field %s has type '%s' in one index, but type '%s' in another") \
	_(ER_UPDATE_SPLICE, 25,			"SPLICE error on field %s: %s") \
	_(ER_UPDATE_ARG_TYPE, 26,		"Argument type in operation '%c' on field %s does not match field type: expected %s") \
	_(ER_FORMAT_MISMATCH_INDEX_PART, 27,	"Field %s has type '%s' in space format, but type '%s' in index definition") \
	_(ER_UNKNOWN_UPDATE_OP, 28,		"Unknown UPDATE operation #%d: %s") \
	_(ER_UPDATE_FIELD, 29,			"Field %s UPDATE error: %s") \
	_(ER_FUNCTION_TX_ACTIVE, 30,		"Transaction is active at return from function") \
	_(ER_KEY_PART_COUNT, 31,		"Invalid key part count (expected [0..%u], got %u)") \
	_(ER_PROC_LUA, 32,			"%s") \
	_(ER_NO_SUCH_PROC, 33,			"Procedure '%.*s' is not defined") \
	_(ER_NO_SUCH_TRIGGER, 34,		"Trigger '%s' doesn't exist") \
	_(ER_NO_SUCH_INDEX_ID, 35,		"No index #%u is defined in space '%s'") \
	_(ER_NO_SUCH_SPACE, 36,			"Space '%s' does not exist") \
	_(ER_NO_SUCH_FIELD_NO, 37,		"Field %d was not found in the tuple") \
	_(ER_EXACT_FIELD_COUNT, 38,		"Tuple field count %u does not match space field count %u") \
	_(ER_FIELD_MISSING, 39,			"Tuple field %s required by space format is missing") \
	_(ER_WAL_IO, 40,			"Failed to write to disk") \
	_(ER_MORE_THAN_ONE_TUPLE, 41,		"Get() doesn't support partial keys and non-unique indexes") \
	_(ER_ACCESS_DENIED, 42,			"%s access to %s '%s' is denied for user '%s'") \
	_(ER_CREATE_USER, 43,			"Failed to create user '%s': %s") \
	_(ER_DROP_USER, 44,			"Failed to drop user or role '%s': %s") \
	_(ER_NO_SUCH_USER, 45,			"User '%s' is not found") \
	_(ER_USER_EXISTS, 46,			"User '%s' already exists") \
	_(ER_CREDS_MISMATCH, 47,		"User not found or supplied credentials are invalid") \
	_(ER_UNKNOWN_REQUEST_TYPE, 48,		"Unknown request type %u") \
	_(ER_UNKNOWN_SCHEMA_OBJECT, 49,		"Unknown object type '%s'") \
	_(ER_CREATE_FUNCTION, 50,		"Failed to create function '%s': %s") \
	_(ER_NO_SUCH_FUNCTION, 51,		"Function '%s' does not exist") \
	_(ER_FUNCTION_EXISTS, 52,		"Function '%s' already exists") \
	_(ER_BEFORE_REPLACE_RET, 53,		"Invalid return value of space:before_replace trigger: expected tuple or nil") \
	_(ER_MULTISTATEMENT_TRANSACTION, 54,	"Can not perform %s in a multi-statement transaction") \
	_(ER_TRIGGER_EXISTS, 55,		"Trigger '%s' already exists") \
	_(ER_USER_MAX, 56,			"A limit on the total number of users has been reached: %u") \
	_(ER_NO_SUCH_ENGINE, 57,		"Space engine '%s' does not exist") \
	_(ER_RELOAD_CFG, 58,			"Can't set option '%s' dynamically") \
	_(ER_CFG, 59,				"Incorrect value for option '%s': %s") \
	_(ER_SAVEPOINT_EMPTY_TX, 60,		"Can not set a savepoint in an empty transaction") \
	_(ER_NO_SUCH_SAVEPOINT, 61,		"Can not rollback to savepoint: the savepoint does not exist") \
	_(ER_UNKNOWN_REPLICA, 62,		"Replica %s is not registered with replica set %s") \
	_(ER_REPLICASET_UUID_MISMATCH, 63,	"Replica set UUID mismatch: expected %s, got %s") \
	_(ER_INVALID_UUID, 64,			"Invalid UUID: %s") \
	_(ER_REPLICASET_UUID_IS_RO, 65,		"Can't reset replica set UUID: it is already assigned") \
	_(ER_INSTANCE_UUID_MISMATCH, 66,	"Instance UUID mismatch: expected %s, got %s") \
	_(ER_REPLICA_ID_IS_RESERVED, 67,	"Can't initialize replica id with a reserved value %u") \
	_(ER_INVALID_ORDER, 68,			"Invalid LSN order for instance %u: previous LSN = %llu, new lsn = %llu") \
	_(ER_MISSING_REQUEST_FIELD, 69,		"Missing mandatory field '%s' in request") \
	_(ER_IDENTIFIER, 70,			"Invalid identifier '%s' (expected printable symbols only or it is too long)") \
	_(ER_DROP_FUNCTION, 71,			"Can't drop function %u: %s") \
	_(ER_ITERATOR_TYPE, 72,			"Unknown iterator type '%s'") \
	_(ER_REPLICA_MAX, 73,			"Replica count limit reached: %u") \
	_(ER_INVALID_XLOG, 74,			"Failed to read xlog: %lld") \
	_(ER_INVALID_XLOG_NAME, 75,		"Invalid xlog name: expected %lld got %lld") \
	_(ER_INVALID_XLOG_ORDER, 76,		"Invalid xlog order: %lld and %lld") \
	_(ER_NO_CONNECTION, 77,			"Connection is not established") \
	_(ER_TIMEOUT, 78,			"Timeout exceeded") \
	_(ER_ACTIVE_TRANSACTION, 79,		"Operation is not permitted when there is an active transaction ") \
	_(ER_CURSOR_NO_TRANSACTION, 80,		"The transaction the cursor belongs to has ended") \
	_(ER_CROSS_ENGINE_TRANSACTION, 81,	"A multi-statement transaction can not use multiple storage engines") \
	_(ER_NO_SUCH_ROLE, 82,			"Role '%s' is not found") \
	_(ER_ROLE_EXISTS, 83,			"Role '%s' already exists") \
	_(ER_CREATE_ROLE, 84,			"Failed to create role '%s': %s") \
	_(ER_INDEX_EXISTS, 85,			"Index '%s' already exists") \
	_(ER_SESSION_CLOSED, 86,		"Session is closed") \
	_(ER_ROLE_LOOP, 87,			"Granting role '%s' to role '%s' would create a loop") \
	_(ER_GRANT, 88,				"Incorrect grant arguments: %s") \
	_(ER_PRIV_GRANTED, 89,			"User '%s' already has %s access on %s%s") \
	_(ER_ROLE_GRANTED, 90,			"User '%s' already has role '%s'") \
	_(ER_PRIV_NOT_GRANTED, 91,		"User '%s' does not have %s access on %s '%s'") \
	_(ER_ROLE_NOT_GRANTED, 92,		"User '%s' does not have role '%s'") \
	_(ER_MISSING_SNAPSHOT, 93,		"Can't find snapshot") \
	_(ER_CANT_UPDATE_PRIMARY_KEY, 94,	"Attempt to modify a tuple field which is part of primary index in space '%s'") \
	_(ER_UPDATE_INTEGER_OVERFLOW, 95,	"Integer overflow when performing '%c' operation on field %s") \
	_(ER_GUEST_USER_PASSWORD, 96,		"Setting password for guest user has no effect") \
	_(ER_TRANSACTION_CONFLICT, 97,		"Transaction has been aborted by conflict") \
	_(ER_UNSUPPORTED_PRIV, 98,		"Unsupported %s privilege '%s'") \
	_(ER_LOAD_FUNCTION, 99,			"Failed to dynamically load function '%s': %s") \
	_(ER_FUNCTION_LANGUAGE, 100,		"Unsupported language '%s' specified for function '%s'") \
	_(ER_RTREE_RECT, 101,			"RTree: %s must be an array with %u (point) or %u (rectangle/box) numeric coordinates") \
	_(ER_PROC_C, 102,			"%s") \
	_(ER_UNKNOWN_RTREE_INDEX_DISTANCE_TYPE, 103, "Unknown RTREE index distance type %s") \
	_(ER_PROTOCOL, 104,			"%s") \
	_(ER_UPSERT_UNIQUE_SECONDARY_KEY, 105,	"Space %s has a unique secondary index and does not support UPSERT") \
	_(ER_WRONG_INDEX_RECORD, 106,		"Wrong record in _index space: got {%s}, expected {%s}") \
	_(ER_WRONG_INDEX_PARTS, 107,		"Wrong index part %u: %s") \
	_(ER_WRONG_INDEX_OPTIONS, 108,		"Wrong index options: %s") \
	_(ER_WRONG_SCHEMA_VERSION, 109,		"Wrong schema version, current: %d, in request: %llu") \
	_(ER_MEMTX_MAX_TUPLE_SIZE, 110,		"Failed to allocate %u bytes for tuple: tuple is too large. Check 'memtx_max_tuple_size' configuration option.") \
	_(ER_WRONG_SPACE_OPTIONS, 111,		"Wrong space options: %s") \
	_(ER_UNSUPPORTED_INDEX_FEATURE, 112,	"Index '%s' (%s) of space '%s' (%s) does not support %s") \
	_(ER_VIEW_IS_RO, 113,			"View '%s' is read-only") \
	_(ER_NO_TRANSACTION, 114,		"No active transaction") \
	_(ER_SYSTEM, 115,			"%s") \
	_(ER_LOADING, 116,			"Instance bootstrap hasn't finished yet") \
	_(ER_CONNECTION_TO_SELF, 117,		"Connection to self") \
	_(ER_KEY_PART_IS_TOO_LONG, 118,		"Key part is too long: %u of %u bytes") \
	_(ER_COMPRESSION, 119,			"Compression error: %s") \
	_(ER_CHECKPOINT_IN_PROGRESS, 120,	"Snapshot is already in progress") \
	_(ER_SUB_STMT_MAX, 121,			"Can not execute a nested statement: nesting limit reached") \
	_(ER_COMMIT_IN_SUB_STMT, 122,		"Can not commit transaction in a nested statement") \
	_(ER_ROLLBACK_IN_SUB_STMT, 123,		"Rollback called in a nested statement") \
	_(ER_DECOMPRESSION, 124,		"Decompression error: %s") \
	_(ER_INVALID_XLOG_TYPE, 125,		"Invalid xlog type: expected %s, got %s") \
	_(ER_ALREADY_RUNNING, 126,		"Failed to lock WAL directory %s and hot_standby mode is off") \
	_(ER_INDEX_FIELD_COUNT_LIMIT, 127,	"Indexed field count limit reached: %d indexed fields") \
	_(ER_LOCAL_INSTANCE_ID_IS_READ_ONLY, 128, "The local instance id %u is read-only") \
	_(ER_BACKUP_IN_PROGRESS, 129,		"Backup is already in progress") \
	_(ER_READ_VIEW_ABORTED, 130,		"The read view is aborted") \
	_(ER_INVALID_INDEX_FILE, 131,		"Invalid INDEX file %s: %s") \
	_(ER_INVALID_RUN_FILE, 132,		"Invalid RUN file: %s") \
	_(ER_INVALID_VYLOG_FILE, 133,		"Invalid VYLOG file: %s") \
	_(ER_CASCADE_ROLLBACK, 134,		"WAL has a rollback in progress") \
	_(ER_VY_QUOTA_TIMEOUT, 135,		"Timed out waiting for Vinyl memory quota") \
	_(ER_PARTIAL_KEY, 136,			"%s index  does not support selects via a partial key (expected %u parts, got %u). Please Consider changing index type to TREE.") \
	_(ER_TRUNCATE_SYSTEM_SPACE, 137,	"Can't truncate a system space, space '%s'") \
	_(ER_LOAD_MODULE, 138,			"Failed to dynamically load module '%.*s': %s") \
	_(ER_VINYL_MAX_TUPLE_SIZE, 139,		"Failed to allocate %u bytes for tuple: tuple is too large. Check 'vinyl_max_tuple_size' configuration option.") \
	_(ER_WRONG_DD_VERSION, 140,		"Wrong _schema version: expected 'major.minor[.patch]'") \
	_(ER_WRONG_SPACE_FORMAT, 141,		"Wrong space format field %u: %s") \
	_(ER_CREATE_SEQUENCE, 142,		"Failed to create sequence '%s': %s") \
	_(ER_ALTER_SEQUENCE, 143,		"Can't modify sequence '%s': %s") \
	_(ER_DROP_SEQUENCE, 144,		"Can't drop sequence '%s': %s") \
	_(ER_NO_SUCH_SEQUENCE, 145,		"Sequence '%s' does not exist") \
	_(ER_SEQUENCE_EXISTS, 146,		"Sequence '%s' already exists") \
	_(ER_SEQUENCE_OVERFLOW, 147,		"Sequence '%s' has overflowed") \
	_(ER_NO_SUCH_INDEX_NAME, 148,		"No index '%s' is defined in space '%s'") \
	_(ER_SPACE_FIELD_IS_DUPLICATE, 149,	"Space field '%s' is duplicate") \
	_(ER_CANT_CREATE_COLLATION, 150,	"Failed to initialize collation: %s.") \
	_(ER_WRONG_COLLATION_OPTIONS, 151,	"Wrong collation options: %s") \
	_(ER_NULLABLE_PRIMARY, 152,		"Primary index of space '%s' can not contain nullable parts") \
	_(ER_NO_SUCH_FIELD_NAME_IN_SPACE, 153,	"Field '%s' was not found in space '%s' format") \
	_(ER_TRANSACTION_YIELD, 154,		"Transaction has been aborted by a fiber yield") \
	_(ER_NO_SUCH_GROUP, 155,		"Replication group '%s' does not exist") \
	_(ER_SQL_BIND_VALUE, 156,		"Bind value for parameter %s is out of range for type %s") \
	_(ER_SQL_BIND_TYPE, 157,		"Bind value type %s for parameter %s is not supported") \
	_(ER_SQL_BIND_PARAMETER_MAX, 158,	"SQL bind parameter limit reached: %d") \
	_(ER_SQL_EXECUTE, 159,			"Failed to execute SQL statement: %s") \
	_(ER_UPDATE_DECIMAL_OVERFLOW, 160,	"Decimal overflow when performing operation '%c' on field %s") \
	_(ER_SQL_BIND_NOT_FOUND, 161,		"Parameter %s was not found in the statement") \
	_(ER_ACTION_MISMATCH, 162,		"Field %s contains %s on conflict action, but %s in index parts") \
	_(ER_VIEW_MISSING_SQL, 163,		"Space declared as a view must have SQL statement") \
	_(ER_FOREIGN_KEY_CONSTRAINT, 164,	"Can not commit transaction: deferred foreign keys violations are not resolved") \
	_(ER_NO_SUCH_MODULE, 165,		"Module '%s' does not exist") \
	_(ER_NO_SUCH_COLLATION, 166,		"Collation '%s' does not exist") \
	_(ER_CREATE_FK_CONSTRAINT, 167,		"Failed to create foreign key constraint '%s': %s") \
	_(ER_DROP_FK_CONSTRAINT, 168,		"Failed to drop foreign key constraint '%s': %s") \
	_(ER_NO_SUCH_CONSTRAINT, 169,		"Constraint '%s' does not exist in space '%s'") \
	_(ER_CONSTRAINT_EXISTS, 170,		"%s constraint '%s' already exists in space '%s'") \
	_(ER_SQL_TYPE_MISMATCH, 171,		"Type mismatch: can not convert %s to %s") \
	_(ER_ROWID_OVERFLOW, 172,		"Rowid is overflowed: too many entries in ephemeral space") \
	_(ER_DROP_COLLATION, 173,		"Can't drop collation '%s': %s") \
	_(ER_ILLEGAL_COLLATION_MIX, 174,	"Illegal mix of collations") \
	_(ER_SQL_NO_SUCH_PRAGMA, 175,		"Pragma '%s' does not exist") \
	_(ER_SQL_CANT_RESOLVE_FIELD, 176,	"Can't resolve field '%s'") \
	_(ER_INDEX_EXISTS_IN_SPACE, 177,	"Index '%s' already exists in space '%s'") \
	_(ER_INCONSISTENT_TYPES, 178,		"Inconsistent types: expected %s got %s") \
	_(ER_SQL_SYNTAX_WITH_POS, 179,		"Syntax error at line %d at or near position %d: %s") \
	_(ER_SQL_STACK_OVERFLOW, 180,		"Failed to parse SQL statement: parser stack limit reached") \
	_(ER_SQL_SELECT_WILDCARD, 181,		"Failed to expand '*' in SELECT statement without FROM clause") \
	_(ER_SQL_STATEMENT_EMPTY, 182,		"Failed to execute an empty SQL statement") \
	_(ER_SQL_KEYWORD_IS_RESERVED, 183,	"At line %d at or near position %d: keyword '%.*s' is reserved. Please use double quotes if '%.*s' is an identifier.") \
	_(ER_SQL_SYNTAX_NEAR_TOKEN, 184,	"Syntax error at line %d near '%.*s'") \
	_(ER_SQL_UNKNOWN_TOKEN, 185,		"At line %d at or near position %d: unrecognized token '%.*s'") \
	_(ER_SQL_PARSER_GENERIC, 186,		"%s") \
	_(ER_SQL_ANALYZE_ARGUMENT, 187,		"ANALYZE statement argument %s is not a base table") \
	_(ER_SQL_COLUMN_COUNT_MAX, 188,		"Failed to create space '%s': space column count %d exceeds the limit (%d)") \
	_(ER_HEX_LITERAL_MAX, 189,		"Hex literal %s%s length %d exceeds the supported limit (%d)") \
	_(ER_INT_LITERAL_MAX, 190,		"Integer literal %s%s exceeds the supported range [-9223372036854775808, 18446744073709551615]") \
	_(ER_SQL_PARSER_LIMIT, 191,		"%s %d exceeds the limit (%d)") \
	_(ER_INDEX_DEF_UNSUPPORTED, 192,	"%s are prohibited in an index definition") \
	_(ER_CK_DEF_UNSUPPORTED, 193,		"%s are prohibited in a ck constraint definition") \
	_(ER_MULTIKEY_INDEX_MISMATCH, 194,	"Field %s is used as multikey in one index and as single key in another") \
	_(ER_CREATE_CK_CONSTRAINT, 195,		"Failed to create check constraint '%s': %s") \
	_(ER_CK_CONSTRAINT_FAILED, 196,		"Check constraint failed '%s': %s") \
	_(ER_SQL_COLUMN_COUNT, 197,		"Unequal number of entries in row expression: left side has %u, but right side - %u") \
	_(ER_FUNC_INDEX_FUNC, 198,		"Failed to build a key for functional index '%s' of space '%s': %s") \
	_(ER_FUNC_INDEX_FORMAT, 199,		"Key format doesn't match one defined in functional index '%s' of space '%s': %s") \
	_(ER_FUNC_INDEX_PARTS, 200,		"Wrong functional index definition: %s") \
	_(ER_NO_SUCH_FIELD_NAME, 201,		"Field '%s' was not found in the tuple") \
	_(ER_FUNC_WRONG_ARG_COUNT, 202,		"Wrong number of arguments is passed to %s(): expected %s, got %d") \
	_(ER_BOOTSTRAP_READONLY, 203,		"Trying to bootstrap a local read-only instance as master") \
	_(ER_SQL_FUNC_WRONG_RET_COUNT, 204,	"SQL expects exactly one argument returned from %s, got %d")\
	_(ER_FUNC_INVALID_RETURN_TYPE, 205,	"Function '%s' returned value of invalid type: expected %s got %s") \
	_(ER_SQL_PARSER_GENERIC_WITH_POS, 206,	"At line %d at or near position %d: %s") \
	_(ER_REPLICA_NOT_ANON, 207,		"Replica '%s' is not anonymous and cannot register.") \
	_(ER_CANNOT_REGISTER, 208,		"Couldn't find an instance to register this replica on.") \
	_(ER_SESSION_SETTING_INVALID_VALUE, 209, "Session setting %s expected a value of type %s") \
	_(ER_SQL_PREPARE, 210,			"Failed to prepare SQL statement: %s") \
	_(ER_WRONG_QUERY_ID, 211,		"Prepared statement with id %u does not exist") \
	_(ER_SEQUENCE_NOT_STARTED, 212,		"Sequence '%s' is not started") \
	_(ER_NO_SUCH_SESSION_SETTING, 213,	"Session setting %s doesn't exist") \
	_(ER_UNCOMMITTED_FOREIGN_SYNC_TXNS, 214, "Found uncommitted sync transactions from other instance with id %u") \
	_(ER_SYNC_MASTER_MISMATCH, 215,		"CONFIRM message arrived for an unknown master id %d, expected %d") \
	_(ER_SYNC_QUORUM_TIMEOUT, 216,		"Quorum collection for a synchronous transaction is timed out") \
	_(ER_SYNC_ROLLBACK, 217,		"A rollback for a synchronous transaction is received") \
	_(ER_TUPLE_METADATA_IS_TOO_BIG, 218,	"Can't create tuple: metadata size %u is too big") \
	_(ER_XLOG_GAP, 219,			"%s") \
	_(ER_TOO_EARLY_SUBSCRIBE, 220,		"Can't subscribe non-anonymous replica %s until join is done") \
	_(ER_SQL_CANT_ADD_AUTOINC, 221,		"Can't add AUTOINCREMENT: space %s can't feature more than one AUTOINCREMENT field") \
	_(ER_QUORUM_WAIT, 222,			"Couldn't wait for quorum %d: %s") \
	_(ER_INTERFERING_PROMOTE, 223,		"Instance with replica id %u was promoted first") \
	_(ER_ELECTION_DISABLED, 224,		"Elections were turned off")\
	_(ER_TXN_ROLLBACK, 225,			"Transaction was rolled back") \
	_(ER_NOT_LEADER, 226,			"The instance is not a leader. New leader is %u")\
	_(ER_SYNC_QUEUE_UNCLAIMED, 227,		"The synchronous transaction queue doesn't belong to any instance")\
	_(ER_SYNC_QUEUE_FOREIGN, 228,		"The synchronous transaction queue belongs to other instance with id %u")\
	_(ER_UNABLE_TO_PROCESS_IN_STREAM, 229,	"Unable to process %s request in stream") \
	_(ER_UNABLE_TO_PROCESS_OUT_OF_STREAM, 230, "Unable to process %s request out of stream") \
	_(ER_TRANSACTION_TIMEOUT, 231,		"Transaction has been aborted by timeout") \
	_(ER_ACTIVE_TIMER, 232,			"Operation is not permitted if timer is already running") \
	_(ER_TUPLE_FIELD_COUNT_LIMIT, 233,	"Tuple field count limit reached: see box.schema.FIELD_MAX") \
	_(ER_CREATE_CONSTRAINT, 234,		"Failed to create constraint '%s' in space '%s': %s") \
	_(ER_FIELD_CONSTRAINT_FAILED, 235,	"Check constraint '%s' failed for field '%s'") \
	_(ER_TUPLE_CONSTRAINT_FAILED, 236,	"Check constraint '%s' failed for a tuple") \
	_(ER_CREATE_FOREIGN_KEY, 237,		"Failed to create foreign key '%s' in space '%s': %s") \
	_(ER_FOREIGN_KEY_INTEGRITY, 238,	"Foreign key '%s' integrity check failed: %s") \
	_(ER_FIELD_FOREIGN_KEY_FAILED, 239,	"Foreign key constraint '%s' failed for field '%s': %s") \
	_(ER_COMPLEX_FOREIGN_KEY_FAILED, 240,	"Foreign key constraint '%s' failed: %s") \
	_(ER_WRONG_SPACE_UPGRADE_OPTIONS, 241,	"Wrong space upgrade options: %s") \
	_(ER_NO_ELECTION_QUORUM, 242,		"Not enough peers connected to start elections: %d out of minimal required %d")\
	_(ER_SSL, 243,				"%s") \
	_(ER_SPLIT_BRAIN, 244,			"Split-Brain discovered: %s") \
	_(ER_OLD_TERM, 245,			"The term is outdated: old - %llu, new - %llu") \
	_(ER_INTERFERING_ELECTIONS, 246,	"Interfering elections started")\
	_(ER_ITERATOR_POSITION, 247,		"Iterator position is invalid") \
	_(ER_DEFAULT_VALUE_TYPE, 248,		"Type of the default value does not match tuple field %s type: expected %s, got %s") \
	_(ER_UNKNOWN_AUTH_METHOD, 249,		"Unknown authentication method '%s'") \
	_(ER_INVALID_AUTH_DATA, 250,		"Invalid '%s' data: %s") \
	_(ER_INVALID_AUTH_REQUEST, 251,		"Invalid '%s' request: %s") \
	_(ER_WEAK_PASSWORD, 252,		"Password doesn't meet security requirements: %s") \
	_(ER_OLD_PASSWORD, 253,			"Password must differ from last %d passwords") \
	_(ER_NO_SUCH_SESSION, 254,		"Session %llu does not exist") \
	_(ER_WRONG_SESSION_TYPE, 255,		"Session '%s' is not supported") \
	_(ER_PASSWORD_EXPIRED, 256,		"Password expired") \
	_(ER_AUTH_DELAY, 257,			"Too many authentication attempts") \
	_(ER_AUTH_REQUIRED, 258,		"Authentication required") \
	_(ER_SQL_SEQ_SCAN, 259,			"Scanning is not allowed for %s") \
	_(ER_NO_SUCH_EVENT, 260,		"Unknown event %s") \
	_(ER_BOOTSTRAP_NOT_UNANIMOUS, 261,	"Replica %s chose a different bootstrap leader %s") \
	_(ER_CANT_CHECK_BOOTSTRAP_LEADER, 262,	"Can't check who replica %s chose its bootstrap leader") \
	_(ER_BOOTSTRAP_CONNECTION_NOT_TO_ALL, 263, "Some replica set members were not specified in box.cfg.replication") \
	_(ER_NIL_UUID, 264,			"Nil UUID is reserved and can't be used in replication") \
	_(ER_WRONG_FUNCTION_OPTIONS, 265,	"Wrong function options: %s") \
	_(ER_MISSING_SYSTEM_SPACES, 266,	"Snapshot has no system spaces") \
	_(ER_CLUSTER_NAME_MISMATCH, 267,	"Cluster name mismatch: name '%s' provided in config confilcts with the instance one '%s'") \
	_(ER_REPLICASET_NAME_MISMATCH, 268,	"Replicaset name mismatch: name '%s' provided in config confilcts with the instance one '%s'") \
	_(ER_INSTANCE_NAME_DUPLICATE, 269,	"Duplicate replica name %s, already occupied by %s") \
	_(ER_INSTANCE_NAME_MISMATCH, 270,	"Instance name mismatch: name '%s' provided in config confilcts with the instance one '%s'") \
	_(ER_SCHEMA_NEEDS_UPGRADE, 271,		"Your schema version is %u.%u.%u while Tarantool %s requires a more recent schema version. Please, consider using box.schema.upgrade().") \
	_(ER_SCHEMA_UPGRADE_IN_PROGRESS, 272,	"Schema upgrade is already in progress") \
	_(ER_DEPRECATED, 273,			"%s is deprecated") \
	_(ER_UNCONFIGURED, 274,			"Please call box.cfg{} first") \
	_(ER_CREATE_DEFAULT_FUNC, 275,		"Failed to create field default function '%s': %s") \
	_(ER_DEFAULT_FUNC_FAILED, 276,		"Error calling field default function '%s': %s") \
	_(ER_INVALID_DEC, 277,			"Invalid decimal: '%s'") \
	_(ER_IN_ANOTHER_PROMOTE, 278,		"box.ctl.promote() is already running") \
	_(ER_SHUTDOWN, 279,			"Server is shutting down") \
	_(ER_FIELD_VALUE_OUT_OF_RANGE, 280,	"The value of field %s exceeds the supported range for type '%s': expected [%s..%s], got %s") \
	TEST_ERROR_CODES(_)

/*
 * !IMPORTANT! Please follow instructions at start of the file
 * when adding new errors.
 */

ENUM(box_error_code, ERROR_CODES);
extern const struct errcode_record box_error_codes[];

/** Record for unknown errocode. */
extern const struct errcode_record errcode_record_unknown;

/** Return the error fields info for the errorcode. */
static inline const struct errcode_record *
tnt_errcode_record(uint32_t errcode)
{
	if (errcode >= box_error_code_MAX ||
	    box_error_codes[errcode].errstr == NULL)
		return &errcode_record_unknown;
	return &box_error_codes[errcode];
}

/** Return a string representation of error name, e.g. "ER_OK". */
static inline const char *
tnt_errcode_str(uint32_t errcode)
{
	return tnt_errcode_record(errcode)->errstr;
}

/** Return a description of the error. */
static inline const char *
tnt_errcode_desc(uint32_t errcode)
{
	return tnt_errcode_record(errcode)->errdesc;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TARANTOOL_BOX_ERRCODE_H_INCLUDED */
