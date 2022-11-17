/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "box/lua/iproto.h"

#include "box/iproto_constants.h"
#include "box/iproto_features.h"

#include <lua.h>

/**
 * IPROTO constant from `src/box/iproto_{constants, features}.h`.
 */
struct iproto_constant {
	/**
	 * Constant literal, name of constant.
	 */
	const char *const name;
	/**
	 * Constant literal, value of constant.
	 */
	const lua_Integer val;
};

/**
 * Pushes an array of IPROTO constants onto Lua stack.
 */
static void
push_iproto_constant_subnamespace(struct lua_State *L, const char *subnamespace,
				  const struct iproto_constant *constants,
				  int constants_len)
{
	lua_createtable(L, 0, constants_len);
	for (int i = 0; i < constants_len; ++i) {
		lua_pushinteger(L, constants[i].val);
		lua_setfield(L, -2, constants[i].name);
	}
	lua_setfield(L, -2, subnamespace);
}

/**
 * Pushes IPROTO constants related to `IPROTO_FLAG` key onto Lua stack.
 */
static void
push_iproto_flag_constants(struct lua_State *L)
{
	const struct iproto_constant flags[] = {
		{"COMMIT", IPROTO_FLAG_COMMIT},
		{"WAIT_SYNC", IPROTO_FLAG_WAIT_SYNC},
		{"WAIT_ACK",  IPROTO_FLAG_WAIT_ACK},
	};
	push_iproto_constant_subnamespace(L, "flag", flags, lengthof(flags));
}

/**
 * Pushes IPROTO constants from `iproto_key` enumeration onto Lua stack.
 */
static void
push_iproto_key_enum(struct lua_State *L)
{
	const struct iproto_constant keys[] = {
		{"REQUEST_TYPE", IPROTO_REQUEST_TYPE},
		{"SYNC", IPROTO_SYNC},
		{"REPLICA_ID", IPROTO_REPLICA_ID},
		{"LSN", IPROTO_LSN},
		{"TIMESTAMP", IPROTO_TIMESTAMP},
		{"SCHEMA_VERSION", IPROTO_SCHEMA_VERSION},
		{"SERVER_VERSION", IPROTO_SERVER_VERSION},
		{"GROUP_ID", IPROTO_GROUP_ID},
		{"TSN", IPROTO_TSN},
		{"FLAGS", IPROTO_FLAGS},
		{"STREAM_ID", IPROTO_STREAM_ID},
		{"SPACE_ID", IPROTO_SPACE_ID},
		{"INDEX_ID", IPROTO_INDEX_ID},
		{"LIMIT", IPROTO_LIMIT},
		{"OFFSET", IPROTO_OFFSET},
		{"ITERATOR", IPROTO_ITERATOR},
		{"INDEX_BASE", IPROTO_INDEX_BASE},
		{"FETCH_POSITION", IPROTO_FETCH_POSITION},
		{"KEY", IPROTO_KEY},
		{"TUPLE", IPROTO_TUPLE},
		{"FUNCTION_NAME", IPROTO_FUNCTION_NAME},
		{"USER_NAME", IPROTO_USER_NAME},
		{"INSTANCE_UUID", IPROTO_INSTANCE_UUID},
		{"CLUSTER_UUID", IPROTO_CLUSTER_UUID},
		{"VCLOCK", IPROTO_VCLOCK},
		{"EXPR", IPROTO_EXPR},
		{"OPS", IPROTO_OPS},
		{"BALLOT", IPROTO_BALLOT},
		{"TUPLE_META", IPROTO_TUPLE_META},
		{"OPTIONS", IPROTO_OPTIONS},
		{"OLD_TUPLE", IPROTO_OLD_TUPLE},
		{"NEW_TUPLE", IPROTO_NEW_TUPLE},
		{"IPROTO_AFTER_POSITION", IPROTO_AFTER_POSITION},
		{"IPROTO_AFTER_TUPLE", IPROTO_AFTER_TUPLE},
		{"DATA", IPROTO_DATA},
		{"ERROR_24", IPROTO_ERROR_24},
		{"METADATA", IPROTO_METADATA},
		{"BIND_METADATA", IPROTO_BIND_METADATA},
		{"BIND_COUNT", IPROTO_BIND_COUNT},
		{"IPROTO_POSITION", IPROTO_POSITION},
		{"SQL_TEXT", IPROTO_SQL_TEXT},
		{"SQL_BIND", IPROTO_SQL_BIND},
		{"SQL_INFO", IPROTO_SQL_INFO},
		{"STMT_ID", IPROTO_STMT_ID},
		{"REPLICA_ANON", IPROTO_REPLICA_ANON},
		{"ID_FILTER", IPROTO_ID_FILTER},
		{"ERROR", IPROTO_ERROR},
		{"TERM", IPROTO_TERM},
		{"VERSION", IPROTO_VERSION},
		{"FEATURES", IPROTO_FEATURES},
		{"TIMEOUT", IPROTO_TIMEOUT},
		{"EVENT_KEY", IPROTO_EVENT_KEY},
		{"EVENT_DATA", IPROTO_EVENT_DATA},
		{"TXN_ISOLATION", IPROTO_TXN_ISOLATION},
		{"VCLOCK_SYNC", IPROTO_VCLOCK_SYNC},
	};
	push_iproto_constant_subnamespace(L, "key", keys, lengthof(keys));
}

/**
 * Pushes IPROTO constants from `iproto_metadata_key` enumeration onto Lua
 * stack.
 */
static void
push_iproto_metadata_key_enum(struct lua_State *L)
{
	const struct iproto_constant metadata_keys[] = {
		{"NAME", IPROTO_FIELD_NAME},
        	{"TYPE", IPROTO_FIELD_TYPE},
        	{"COLL", IPROTO_FIELD_COLL},
        	{"IS_NULLABLE", IPROTO_FIELD_IS_NULLABLE},
        	{"IS_AUTOINCREMENT", IPROTO_FIELD_IS_AUTOINCREMENT},
        	{"SPAN", IPROTO_FIELD_SPAN},
	};
	push_iproto_constant_subnamespace(L, "metadata_key", metadata_keys,
					  lengthof(metadata_keys));
}

/**
 * Pushes IPROTO constants from `iproto_ballot_key` enumeration onto Lua stack.
 */
static void
push_iproto_ballot_key_enum(struct lua_State *L)
{
	const struct iproto_constant ballot_keys[] = {
		{"IS_RO_CFG", IPROTO_BALLOT_IS_RO_CFG},
        	{"VCLOCK", IPROTO_BALLOT_VCLOCK},
        	{"GC_VCLOCK", IPROTO_BALLOT_GC_VCLOCK},
        	{"IS_RO", IPROTO_BALLOT_IS_RO},
        	{"IS_ANON", IPROTO_BALLOT_IS_ANON},
        	{"IS_BOOTED", IPROTO_BALLOT_IS_BOOTED},
        	{"CAN_LEAD", IPROTO_BALLOT_CAN_LEAD},
	};
	push_iproto_constant_subnamespace(L, "ballot_key", ballot_keys,
					  lengthof(ballot_keys));
}

/**
 * Pushes IPROTO constants from `iproto_type` enumeration onto Lua stack.
 */
static void
push_iproto_type_enum(struct lua_State *L)
{
	const struct iproto_constant types[] = {
		{"OK", IPROTO_OK},
		{"SELECT", IPROTO_SELECT},
		{"INSERT", IPROTO_INSERT},
		{"REPLACE", IPROTO_REPLACE},
		{"UPDATE", IPROTO_UPDATE},
		{"DELETE", IPROTO_DELETE},
		{"CALL_16", IPROTO_CALL_16},
		{"AUTH", IPROTO_AUTH},
		{"EVAL", IPROTO_EVAL},
		{"UPSERT", IPROTO_UPSERT},
		{"CALL", IPROTO_CALL},
		{"EXECUTE", IPROTO_EXECUTE},
		{"NOP", IPROTO_NOP},
		{"PREPARE", IPROTO_PREPARE},
		{"BEGIN", IPROTO_BEGIN},
		{"COMMIT", IPROTO_COMMIT},
		{"ROLLBACK", IPROTO_ROLLBACK},
		{"RAFT", IPROTO_RAFT},
		{"RAFT_PROMOTE", IPROTO_RAFT_PROMOTE},
		{"RAFT_DEMOTE", IPROTO_RAFT_DEMOTE},
		{"RAFT_CONFIRM", IPROTO_RAFT_CONFIRM},
		{"RAFT_ROLLBACK", IPROTO_RAFT_ROLLBACK},
		{"PING", IPROTO_PING},
		{"JOIN", IPROTO_JOIN},
		{"SUBSCRIBE", IPROTO_SUBSCRIBE},
		{"VOTE_DEPRECATED", IPROTO_VOTE_DEPRECATED},
		{"VOTE", IPROTO_VOTE},
		{"FETCH_SNAPSHOT", IPROTO_FETCH_SNAPSHOT},
		{"REGISTER", IPROTO_REGISTER},
		{"JOIN_META", IPROTO_JOIN_META},
		{"JOIN_SNAPSHOT", IPROTO_JOIN_SNAPSHOT},
		{"ID", IPROTO_ID},
		{"WATCH", IPROTO_WATCH},
		{"UNWATCH", IPROTO_UNWATCH},
		{"EVENT", IPROTO_EVENT},
		{"CHUNK", IPROTO_CHUNK},
		{"TYPE_ERROR", IPROTO_TYPE_ERROR},
	};
	push_iproto_constant_subnamespace(L, "type", types,
					  lengthof(types));
}

/**
 * Pushes IPROTO constants from `iproto_raft_keys` enumeration onto Lua stack.
 */
static void
push_iproto_raft_keys_enum(struct lua_State *L)
{
	const struct iproto_constant raft_keys[] = {
		{"TERM", IPROTO_RAFT_TERM},
		{"VOTE", IPROTO_RAFT_VOTE},
		{"STATE", IPROTO_RAFT_STATE},
		{"VCLOCK", IPROTO_RAFT_VCLOCK},
		{"LEADER_ID", IPROTO_RAFT_LEADER_ID},
		{"IS_LEADER_SEEN", IPROTO_RAFT_IS_LEADER_SEEN},
	};
	push_iproto_constant_subnamespace(L, "raft_key", raft_keys,
					  lengthof(raft_keys));
}

/**
 * Pushes IPROTO constants onto Lua stack.
 */
static void
push_iproto_constants(struct lua_State *L)
{
	push_iproto_flag_constants(L);
	push_iproto_key_enum(L);
	push_iproto_metadata_key_enum(L);
	push_iproto_ballot_key_enum(L);
	push_iproto_type_enum(L);
	push_iproto_raft_keys_enum(L);
}

/**
 * Pushes IPROTO protocol features onto Lua stack.
 */
static void
push_iproto_protocol_features(struct lua_State *L)
{
	lua_pushinteger(L, IPROTO_CURRENT_VERSION);
	lua_setfield(L, -2, "protocol_version");

	const struct iproto_constant features[] = {
		{"streams", IPROTO_FEATURE_STREAMS},
        	{"transactions", IPROTO_FEATURE_TRANSACTIONS},
        	{"error_extension", IPROTO_FEATURE_ERROR_EXTENSION},
        	{"watchers", IPROTO_FEATURE_WATCHERS},
        	{"pagination", IPROTO_FEATURE_PAGINATION},
	};

	lua_createtable(L, 0, lengthof(features));
	for (size_t i = 0; i < lengthof(features); ++i) {
		lua_pushboolean(L, true);
		lua_setfield(L, -2, features[i].name);
	}
	lua_setfield(L, -2, "protocol_features");

	push_iproto_constant_subnamespace(L, "feature", features,
					  lengthof(features));
}

/**
 * Initializes module for working with Tarantool's network subsystem.
 */
void
box_lua_iproto_init(struct lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_newtable(L);
	push_iproto_constants(L);
	push_iproto_protocol_features(L);
	lua_setfield(L, -2, "iproto");
	lua_pop(L, 1);
}
