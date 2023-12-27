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
#include "box/user.h"
#include "box/xrow.h"

#include "version.h"

#include "core/assoc.h"
#include "core/iostream.h"
#include "core/fiber.h"
#include "core/mp_ctx.h"
#include "core/random.h"

#include "core/tt_static.h"
#include "core/tt_uuid.h"

#include "lua/msgpack.h"
#include "lua/utils.h"

#include "mpstream/mpstream.h"

#include "small/region.h"

#include <ctype.h>
#include <lauxlib.h>

struct mh_strnu32_t *iproto_key_translation;

/**
 * Pushes IPROTO constants generated from `IPROTO_FLAGS` onto Lua stack.
 */
static void
push_iproto_flag_enum(struct lua_State *L)
{
	lua_newtable(L);
	for (int i = 0; i < iproto_flag_bit_MAX; i++) {
		lua_pushinteger(L, 1ULL << i);
		lua_setfield(L, -2, iproto_flag_bit_strs[i]);
	}
	lua_setfield(L, -2, "flag");
}

/**
 * Pushes IPROTO constants generated from `IPROTO_KEYS` onto Lua stack.
 */
static void
push_iproto_key_enum(struct lua_State *L)
{
	lua_newtable(L);
	for (int i = 0; i < iproto_key_MAX; i++) {
		const char *name = iproto_key_strs[i];
		if (name == NULL)
			continue;
		lua_pushinteger(L, i);
		lua_setfield(L, -2, name);
		size_t len = strlen(name);
		char *lowercase = strtolowerdup(name);
		struct mh_strnu32_node_t translation = {
			.str = lowercase,
			.len = len,
			.hash = lua_hash(lowercase, len),
			.val = i,
		};
		mh_strnu32_put(iproto_key_translation, &translation,
			       NULL, NULL);
		translation.str = xstrdup(name);
		translation.hash = lua_hash(translation.str, len);
		mh_strnu32_put(iproto_key_translation, &translation,
			       NULL, NULL);
	}
	lua_setfield(L, -2, "key");
}

/**
 * Pushes IPROTO constants generated from `IPROTO_METADATA_KEYS` onto Lua stack.
 */
static void
push_iproto_metadata_key_enum(struct lua_State *L)
{
	lua_newtable(L);
	for (int i = 0; i < iproto_metadata_key_MAX; i++) {
		const char *name = iproto_metadata_key_strs[i];
		lua_pushinteger(L, i);
		lua_setfield(L, -2, name);
	}
	lua_setfield(L, -2, "metadata_key");
}

/**
 * Pushes IPROTO constants generated from `IPROTO_BALLOT_KEYS` onto Lua stack.
 */
static void
push_iproto_ballot_key_enum(struct lua_State *L)
{
	lua_newtable(L);
	for (int i = 0; i < iproto_ballot_key_MAX; i++) {
		const char *name = iproto_ballot_key_strs[i];
		if (name == NULL)
			continue;
		lua_pushinteger(L, i);
		lua_setfield(L, -2, name);
	}
	lua_setfield(L, -2, "ballot_key");
}

/**
 * Pushes IPROTO constants generated from `IPROTO_TYPES` onto Lua stack.
 */
static void
push_iproto_type_enum(struct lua_State *L)
{
	lua_newtable(L);
	for (int i = 0; i < iproto_type_MAX; i++) {
		const char *name = iproto_type_strs[i];
		if (name == NULL)
			continue;
		lua_pushinteger(L, i);
		lua_setfield(L, -2, name);
	}
	lua_pushinteger(L, IPROTO_TYPE_ERROR);
	lua_setfield(L, -2, "TYPE_ERROR");
	lua_pushinteger(L, IPROTO_UNKNOWN);
	lua_setfield(L, -2, "UNKNOWN");
	lua_setfield(L, -2, "type");
}

/**
 * Pushes IPROTO constants generated from `IPROTO_RAFT_KEYS` onto Lua stack.
 */
static void
push_iproto_raft_keys_enum(struct lua_State *L)
{
	lua_newtable(L);
	for (int i = 0; i < iproto_raft_key_MAX; i++) {
		const char *name = iproto_raft_key_strs[i];
		lua_pushinteger(L, i);
		lua_setfield(L, -2, name);
	}
	lua_setfield(L, -2, "raft_key");
}

/**
 * Pushes IPROTO constants onto Lua stack.
 */
static void
push_iproto_constants(struct lua_State *L)
{
	lua_pushinteger(L, IPROTO_GREETING_SIZE);
	lua_setfield(L, -2, "GREETING_SIZE");
	lua_pushinteger(L, GREETING_PROTOCOL_LEN_MAX);
	lua_setfield(L, -2, "GREETING_PROTOCOL_LEN_MAX");
	lua_pushinteger(L, GREETING_SALT_LEN_MAX);
	lua_setfield(L, -2, "GREETING_SALT_LEN_MAX");
	push_iproto_flag_enum(L);
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

	for (int i = 0; i < 2; i++)
		lua_newtable(L);
	for (int i = 0; i < iproto_feature_id_MAX; i++) {
		char *name = strtolowerdup(iproto_feature_id_strs[i]);
		lua_pushboolean(L, true);
		lua_setfield(L, -2, name);
		lua_pushinteger(L, i);
		lua_setfield(L, -3, name);
		free(name);
	}
	lua_setfield(L, -3, "protocol_features");
	lua_setfield(L, -2, "feature");
}

/**
 * Internal Lua wrapper around iproto_session_new.
 *
 * Takes fd number (mandatory) and user name (optional, default is guest).
 * Returns the new session id on success. On error, raises an exception.
 */
static int
lbox_iproto_session_new(struct lua_State *L)
{
	if (lua_isnoneornil(L, 1)) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "options parameter 'fd' is mandatory");
		return luaT_error(L);
	}
	int fd;
	if (!luaL_tointeger_strict(L, 1, &fd) || fd < 0) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "options parameter 'fd' must be nonnegative integer");
		return luaT_error(L);
	}
	if (!box_is_configured()) {
		diag_set(ClientError, ER_UNCONFIGURED);
		return luaT_error(L);
	}
	struct user *user = NULL;
	if (!lua_isnil(L, 2)) {
		size_t name_len;
		const char *name = luaL_checklstring(L, 2, &name_len);
		user = user_find_by_name(name, name_len);
		if (user == NULL)
			return luaT_error(L);
	}
	struct iostream io;
	plain_iostream_create(&io, fd);
	uint64_t sid;
	if (iproto_session_new(&io, user, &sid) != 0)
		return luaT_error(L);
	luaL_pushuint64(L, sid);
	return 1;
}

/**
 * Encodes a packet header/body argument to MsgPack: if the argument is a
 * string, then no encoding is needed — otherwise the argument must be a Lua
 * table. The Lua table is encoded to MsgPack using IPROTO key translation
 * table.
 * In both cases, the result is stored on the fiber region.
 *
 * Return encoded packet or NULL on encoding error with diag set.
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
	struct mpstream stream;
	mpstream_init(&stream, gc, region_reserve_cb, region_alloc_cb,
		      luamp_error, L);
	size_t used = region_used(gc);
	struct mp_ctx ctx;
	mp_ctx_create_default(&ctx, iproto_key_translation);
	if (luamp_encode_with_ctx(L, luaL_msgpack_default, &stream, idx,
				  &ctx, NULL) != 0) {
		region_truncate(gc, used);
		return NULL;
	}
	mp_ctx_destroy(&ctx);
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
	int rc = -1;
	const char *header = encode_packet(L, 2, &header_len);
	if (header == NULL)
		goto cleanup;
	size_t body_len = 0;
	const char *body = NULL;
	if (n_args == 3) {
		body = encode_packet(L, 3, &body_len);
		if (body == NULL)
			goto cleanup;
	}
	rc = box_iproto_send(sid, header, header + header_len, body,
			     body + body_len);
cleanup:
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
		   void *cb_ctx)
{
	struct lua_State *L = luaT_newthread(tarantool_L);
	if (L == NULL)
		return IPROTO_HANDLER_ERROR;
	int cb_ref = (int)(uintptr_t)cb_ctx;
	lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);
	struct mp_ctx mp_ctx_header;
	mp_ctx_create_default(&mp_ctx_header, iproto_key_translation);
	luamp_push_with_ctx(L, header, header_end, &mp_ctx_header);
	struct mp_ctx mp_ctx_body;
	mp_ctx_create_default(&mp_ctx_body, iproto_key_translation);
	luamp_push_with_ctx(L, body, body_end, &mp_ctx_body);
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
	if (box_check_configured() != 0)
		return luaT_error(L);
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
 * Encodes Tarantool greeting message.
 *
 * Takes a table with the following fields that will be used in
 * the greeting (all fields are optional):
 *  - version: Tarantool version string in the form 'X.Y.Z'.
 *    Default: current Tarantool version.
 *  - uuid: Instance UUID string. Default: Some random UUID.
 *    (We don't use INSTANCE_UUID because it may be uninitialized.)
 *  - salt: Salt string (used for authentication).
 *    Default: Some random salt string.
 *
 * Returns the encoded greeting message string on success.
 * Raises an error on invalid arguments.
 */
static int
lbox_iproto_encode_greeting(struct lua_State *L)
{
	int n_args = lua_gettop(L);
	if (n_args == 0) {
		lua_newtable(L);
	} else if (n_args != 1 || lua_type(L, 1) != LUA_TTABLE) {
		return luaL_error(L, "Usage: box.iproto.encode_greeting({"
				  "version = x, uuid = x, salt = x})");
	}

	uint32_t version;
	lua_getfield(L, 1, "version");
	if (lua_isnil(L, -1)) {
		version = tarantool_version_id();
	} else if (lua_type(L, -1) == LUA_TSTRING) {
		const char *str = lua_tostring(L, -1);
		unsigned major, minor, patch;
		if (sscanf(str, "%u.%u.%u", &major, &minor, &patch) != 3)
			return luaL_error(L, "cannot parse version string");
		version = version_id(major, minor, patch);
	} else {
		return luaL_error(L, "version must be a string");
	}
	lua_pop(L, 1); /* version */

	struct tt_uuid uuid;
	lua_getfield(L, 1, "uuid");
	if (lua_isnil(L, -1)) {
		tt_uuid_create(&uuid);
	} else if (lua_type(L, -1) == LUA_TSTRING) {
		const char *uuid_str = lua_tostring(L, -1);
		if (tt_uuid_from_string(uuid_str, &uuid) != 0)
			return luaL_error(L, "cannot parse uuid string");
	} else {
		return luaL_error(L, "uuid must be a string");
	}
	lua_pop(L, 1); /* uuid */

	uint32_t salt_len;
	char salt[GREETING_SALT_LEN_MAX];
	lua_getfield(L, 1, "salt");
	if (lua_isnil(L, -1)) {
		salt_len = IPROTO_SALT_SIZE;
		random_bytes(salt, IPROTO_SALT_SIZE);
	} else if (lua_type(L, -1) == LUA_TSTRING) {
		size_t len;
		const char *str = lua_tolstring(L, -1, &len);
		if (len > GREETING_SALT_LEN_MAX)
			return luaL_error(L, "salt string length "
					  "cannot be greater than %d",
					  GREETING_SALT_LEN_MAX);
		salt_len = len;
		memcpy(salt, str, len);
	} else {
		return luaL_error(L, "salt must be a string");
	}
	lua_pop(L, 1); /* salt */

	char greeting_str[IPROTO_GREETING_SIZE];
	greeting_encode(greeting_str, version, &uuid, salt, salt_len);

	lua_pushlstring(L, greeting_str, sizeof(greeting_str));
	return 1;
}

/**
 * Decodes Tarantool greeting message.
 *
 * Takes a greeting message string and returns a table with the following
 * fields on success:
 *  - version: Tarantool version string in the form 'X.Y.Z'.
 *  - protocol: Tarantool protocol string ("Binary" for IPROTO).
 *  - uuid: Instance UUID string.
 *  - salt: Salt string (used for authentication).
 *
 * Raises an error on invalid input.
 */
static int
lbox_iproto_decode_greeting(struct lua_State *L)
{
	int n_args = lua_gettop(L);
	if (n_args != 1 || lua_type(L, 1) != LUA_TSTRING) {
		return luaL_error(
			L, "Usage: box.iproto.decode_greeting(string)");
	}

	size_t len;
	const char *greeting_str = lua_tolstring(L, 1, &len);
	if (len != IPROTO_GREETING_SIZE) {
		return luaL_error(L, "greeting length must equal %d",
				  IPROTO_GREETING_SIZE);
	}
	struct greeting greeting;
	if (greeting_decode(greeting_str, &greeting) != 0)
		return luaL_error(L, "cannot parse greeting string");

	lua_newtable(L);
	lua_pushfstring(L, "%u.%u.%u",
			version_id_major(greeting.version_id),
			version_id_minor(greeting.version_id),
			version_id_patch(greeting.version_id));
	lua_setfield(L, -2, "version");
	lua_pushstring(L, greeting.protocol);
	lua_setfield(L, -2, "protocol");
	luaT_pushuuidstr(L, &greeting.uuid);
	lua_setfield(L, -2, "uuid");
	lua_pushlstring(L, greeting.salt, greeting.salt_len);
	lua_setfield(L, -2, "salt");
	return 1;
}

/**
 * Encodes IPROTO packet.
 *
 * Takes a packet header and optionally a body given as a string or a table.
 * If an argument is a table, it will be encoded in MsgPack using the IPROTO
 * key translation table. If an argument is a string, it's supposed to store
 * valid MsgPack data and will be copied as is.
 *
 * On success, returns a string storing the encoded IPROTO packet.
 * On failure, raises a Lua error.
 */
static int
lbox_iproto_encode_packet(struct lua_State *L)
{
	int n_args = lua_gettop(L);
	if (n_args != 1 && n_args != 2)
		return luaL_error(
			L, "Usage: box.iproto.encode_packet(header[, body])");
	int header_type = lua_type(L, 1);
	if (header_type != LUA_TSTRING && header_type != LUA_TTABLE)
		return luaL_error(L, "header must be a string or a table");
	int body_type = lua_type(L, 2);
	if (body_type != LUA_TSTRING && body_type != LUA_TTABLE &&
	    body_type != LUA_TNONE && body_type != LUA_TNIL)
		return luaL_error(L, "body must be a string or a table");
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct mpstream stream;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      mpstream_panic_cb, NULL);
	size_t fixheader_size = mp_sizeof_uint(UINT32_MAX);
	char *fixheader = mpstream_reserve(&stream, fixheader_size);
	mpstream_advance(&stream, fixheader_size);
	struct mp_ctx ctx;
	mp_ctx_create_default(&ctx, iproto_key_translation);
	if (header_type == LUA_TTABLE) {
		int rc = luamp_encode_with_ctx(L, luaL_msgpack_default,
					       &stream, 1, &ctx, NULL);
		if (rc != 0)
			goto error;
	} else if (header_type == LUA_TSTRING) {
		size_t size;
		const char *data = lua_tolstring(L, 1, &size);
		mpstream_memcpy(&stream, data, size);
	}
	if (body_type == LUA_TTABLE) {
		int rc = luamp_encode_with_ctx(L, luaL_msgpack_default,
					       &stream, 2, &ctx, NULL);
		if (rc != 0)
			goto error;
	} else if (body_type == LUA_TSTRING) {
		size_t size;
		const char *data = lua_tolstring(L, 2, &size);
		mpstream_memcpy(&stream, data, size);
	}
	mpstream_flush(&stream);
	size_t data_size = region_used(region) - region_svp;
	*fixheader = 0xce;
	mp_store_u32(fixheader + 1, data_size - fixheader_size);
	char *data = xregion_join(region, data_size);
	lua_pushlstring(L, data, data_size);
	region_truncate(region, region_svp);
	return 1;
error:
	region_truncate(region, region_svp);
	return luaT_error(L);
}

/**
 * Decodes IPROTO packet.
 *
 * Takes a string that contains an encoded IPROTO packet and optionally
 * the position in the string to start decoding from (if the position is
 * omitted, the function will start decoding from the beginning of the
 * input string, i.e. assume that the position equals 1).
 *
 * On success returns three values: the decoded packet header (never nil),
 * the decoded packet body (may be nil), and the position of the following
 * packet in the string. The header and body are returned as MsgPack objects.
 *
 * If the packet is truncated, returns nil and the minimal number of bytes
 * necessary to decode the packet.
 *
 * On failure, raises a Lua error.
 */
static int
lbox_iproto_decode_packet(struct lua_State *L)
{
	int n_args = lua_gettop(L);
	if (n_args == 0 || n_args > 2 ||
	    lua_type(L, 1) != LUA_TSTRING ||
	    (n_args == 2 && lua_type(L, 2) != LUA_TNUMBER))
		return luaL_error(
			L, "Usage: box.iproto.decode_packet(string[, pos])");

	size_t data_size;
	const char *data = lua_tolstring(L, 1, &data_size);
	const char *data_end = data + data_size;
	const char *p = data;
	if (n_args == 2) {
		int pos = lua_tointeger(L, 2);
		if (pos <= 0)
			return luaL_error(L, "position must be greater than 0");
		p += pos - 1;
	}
	ptrdiff_t n = p - data_end + 1;
	if (n > 0)
		goto truncated_input;
	if (mp_typeof(*p) != MP_UINT) {
		diag_set(ClientError, ER_PROTOCOL, "invalid fixheader");
		return luaT_error(L);
	}
	n = mp_check_uint(p, data_end);
	if (n > 0)
		goto truncated_input;
	size_t packet_size = mp_decode_uint(&p);
	if (packet_size == 0) {
		diag_set(ClientError, ER_PROTOCOL, "invalid fixheader");
		return luaT_error(L);
	}
	const char *packet_end = p + packet_size;
	n = packet_end - data_end;
	if (n > 0)
		goto truncated_input;
	const char *header = p;
	if (mp_check(&p, packet_end) != 0)
		return luaT_error(L);
	const char *header_end = p;
	const char *body = p;
	if (p != packet_end && mp_check_exact(&p, packet_end) != 0)
		return luaT_error(L);
	const char *body_end = p;
	struct mp_ctx ctx;
	mp_ctx_create_default(&ctx, iproto_key_translation);
	luamp_push_with_ctx(L, header, header_end, &ctx);
	if (body != body_end) {
		mp_ctx_create_default(&ctx, iproto_key_translation);
		luamp_push_with_ctx(L, body, body_end, &ctx);
	} else {
		lua_pushnil(L);
	}
	lua_pushnumber(L, packet_end - data + 1);
	return 3;
truncated_input:
	assert(n > 0);
	lua_pushnil(L);
	lua_pushnumber(L, n);
	return 2;
}

/**
 * Initializes module for working with Tarantool's network subsystem.
 */
void
box_lua_iproto_init(struct lua_State *L)
{
	iproto_key_translation = mh_strnu32_new();
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.iproto", 0);
	push_iproto_constants(L);
	push_iproto_protocol_features(L);
	static const struct luaL_Reg funcs[] = {
		{"send", lbox_iproto_send},
		{"override", lbox_iproto_override},
		{"encode_greeting", lbox_iproto_encode_greeting},
		{"decode_greeting", lbox_iproto_decode_greeting},
		{"encode_packet", lbox_iproto_encode_packet},
		{"decode_packet", lbox_iproto_decode_packet},
		{NULL, NULL}
	};
	luaL_setfuncs(L, funcs, 0);
	luaL_findtable(L, -1, "internal", 0);
	static const struct luaL_Reg internal_funcs[] = {
		{"session_new", lbox_iproto_session_new},
		{NULL, NULL}
	};
	luaL_setfuncs(L, internal_funcs, 0);
	lua_pop(L, 1); /* box.iproto.internal */
	lua_pop(L, 1); /* box.iproto */
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
	iproto_key_translation = NULL;
}
