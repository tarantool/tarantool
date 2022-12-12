/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "box/lua/iproto.h"

#include "box/box.h"
#include "box/error.h"
#include "box/iproto.h"
#include "box/iproto_constants.h"
#include "box/iproto_features.h"

#include "core/assoc.h"
#include "core/fiber.h"
#include "core/tt_static.h"

#include "lua/msgpack.h"
#include "lua/utils.h"

#include "mpstream/mpstream.h"

#include "small/region.h"

#include <ctype.h>
#include <lauxlib.h>

/**
 * Translation table for `box.iproto.key` constants encoding and aliasing: used
 * in `luamp_encode_with_translation` and `luamp_push_with_translation`.
 */
static struct mh_strnu32_t *iproto_key_translation;

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
	for (size_t i = 0; i < lengthof(keys); ++i) {
		size_t len = strlen(keys[i].name);
		char *lowercase = strtolowerdup(keys[i].name);
		struct mh_strnu32_node_t translation = {
			.str = lowercase,
			.len = len,
			.hash = lua_hash(lowercase, len),
			.val = keys[i].val,
		};
		mh_strnu32_put(iproto_key_translation, &translation,
			       NULL, NULL);
		translation.str = xstrdup(keys[i].name);
		translation.hash = lua_hash(translation.str, len);
		mh_strnu32_put(iproto_key_translation, &translation,
			       NULL, NULL);
	}
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
		{"BOOTSTRAP_LEADER_UUID",
		 IPROTO_BALLOT_BOOTSTRAP_LEADER_UUID},
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
		{"UNKNOWN", IPROTO_UNKNOWN},
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
 * Encodes a packet header/body argument to MsgPack: if the argument is a
 * string, then no encoding is needed — otherwise the argument must be a Lua
 * table. The Lua table is encoded to MsgPack using IPROTO key translation
 * table.
 * In both cases, the result is stored on the fiber region.
 */
static const char *
encode_packet(struct lua_State *L, int idx, size_t *mp_len)
{
	int packet_part_type = lua_type(L, idx);
	struct region *gc = &fiber()->gc;
	if (packet_part_type == LUA_TSTRING) {
		const char *arg = lua_tolstring(L, idx, mp_len);
		char *mp = xregion_alloc(gc, *mp_len);
		return memcpy(mp, arg, *mp_len);
	}
	assert(packet_part_type == LUA_TTABLE);
	/*
	 * FIXME(gh-7939): `luamp_error` and `luamp_encode_with_translation` can
	 * throw a Lua exception, which we cannot catch, and cause the fiber
	 * region to leak.
	 */
	struct mpstream stream;
	mpstream_init(&stream, gc, region_reserve_cb, region_alloc_cb,
		      luamp_error, L);
	size_t used = region_used(gc);
	luamp_encode_with_translation(L, luaL_msgpack_default, &stream, idx,
				      iproto_key_translation);
	mpstream_flush(&stream);
	*mp_len = region_used(gc) - used;
	return xregion_join(gc, *mp_len);
}

/**
 * Sends an IPROTO packet consisting of a header (second argument) and an
 * optional body (third argument) over the IPROTO session identified by first
 * argument.
 */
static int
lbox_iproto_send(struct lua_State *L)
{
	int n_args = lua_gettop(L);
	if (n_args < 2 || n_args > 3)
		return luaL_error(L, "Usage: "
				     "box.iproto.send(sid, header[, body])");
	uint64_t sid = luaL_checkuint64(L, 1);
	int header_type = lua_type(L, 2);
	if (header_type != LUA_TSTRING && header_type != LUA_TTABLE)
		return luaL_error(L, "expected table or string as 2 argument");
	if (n_args == 3) {
		int body_type = lua_type(L, 3);
		if (body_type != LUA_TSTRING && body_type != LUA_TTABLE)
			return luaL_error(
				L, "expected table or string as 3 argument");
	}

	struct region *gc = &fiber()->gc;
	size_t used = region_used(gc);
	size_t header_len;
	const char *header = encode_packet(L, 2, &header_len);
	size_t body_len = 0;
	const char *body = NULL;
	if (n_args == 3)
		body = encode_packet(L, 3, &body_len);
	int rc = box_iproto_send(sid,
				 header, header + header_len,
				 body, body + body_len);
	region_truncate(gc, used);
	return (rc == 0) ? 0 : luaT_error(L);
}

/**
 * Lua request handler callback: creates new Lua execution context, gets the Lua
 * callback function, pushes the request header and body as MsgPack objects and
 * calls the Lua callback.
 */
static enum iproto_handler_status
lua_req_handler_cb(const char *header, const char *header_end,
		   const char *body, const char *body_end,
		   void *ctx)
{
	struct lua_State *L = luaT_newthread(tarantool_L);
	if (L == NULL)
		return IPROTO_HANDLER_ERROR;
	int cb_ref = (int)(uintptr_t)ctx;
	lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);
	luamp_push_with_translation(L, header, header_end,
				    iproto_key_translation);
	luamp_push_with_translation(L, body, body_end,
				    iproto_key_translation);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	if (luaT_call(L, 2, 1) != 0)
		return IPROTO_HANDLER_ERROR;
	if (!lua_isboolean(L, 1)) {
		diag_set(ClientError, ER_PROC_LUA,
			 tt_sprintf("Invalid Lua IPROTO handler return type "
				    "'%s' (expected boolean)",
				    luaL_typename(L, 1)));
		return IPROTO_HANDLER_ERROR;
	}
	bool ok = lua_toboolean(L, 1);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
	return ok ? IPROTO_HANDLER_OK : IPROTO_HANDLER_FALLBACK;
}

/**
 * Lua request handler destructor: unreferences the request handler's Lua
 * callback function.
 */
static void
lua_req_handler_destroy(void *ctx)
{
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, (int)(uintptr_t)ctx);
}

/**
 * Sets IPROTO request handler callback (second argument) for the given request
 * type (first argument): the Lua callback function is referenced in Lua and
 * unreferenced in `lua_req_handler_destroy`.
 * Passing nil as the callback resets the corresponding request handler.
 */
static int
lbox_iproto_override(struct lua_State *L)
{
	int n_args = lua_gettop(L);
	if (n_args != 2)
		return luaL_error(L, "Usage: "
				     "box.iproto.override(request_type, "
				     "callback)");
	uint32_t req_type = luaL_checkuint64(L, 1);
	if (lua_isnil(L, 2)) {
		if (iproto_override(req_type, NULL, NULL, NULL) != 0)
			return luaT_error(L);
		return 0;
	}
	luaL_checktype(L, 2, LUA_TFUNCTION);
	int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	if (iproto_override(req_type, lua_req_handler_cb,
			    lua_req_handler_destroy,
			    (void *)(uintptr_t)cb_ref) != 0) {
		luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
		return luaT_error(L);
	}
	return 0;
}

/**
 * Initializes module for working with Tarantool's network subsystem.
 */
void
box_lua_iproto_init(struct lua_State *L)
{
	iproto_key_translation = mh_strnu32_new();

	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_newtable(L);

	push_iproto_constants(L);
	push_iproto_protocol_features(L);

	const struct luaL_Reg iproto_methods[] = {
		{"send", lbox_iproto_send},
		{"override", lbox_iproto_override},
		{NULL, NULL}
	};
	luaL_register(L, NULL, iproto_methods);

	lua_setfield(L, -2, "iproto");
	lua_pop(L, 1);
}

/**
 * Deletes the IPROTO key translation and all its dynamically allocated key
 * strings.
 */
void
box_lua_iproto_free(void)
{
	struct mh_strnu32_t *h = iproto_key_translation;
	mh_int_t k;
	mh_foreach(h, k)
		free((void *)mh_strnu32_node(h, k)->str);
	mh_strnu32_delete(iproto_key_translation);
}
