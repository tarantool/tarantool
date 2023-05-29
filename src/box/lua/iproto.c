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
 * Pushes an array of IPROTO constants onto Lua stack.
 */
static void
push_iproto_constant_subnamespace(struct lua_State *L, const char *subnamespace,
				  const struct iproto_constant *constants,
				  int constants_len)
{
	lua_createtable(L, 0, constants_len);
	for (int i = 0; i < constants_len; ++i) {
		const char *name = constants[i].name;
		int value = constants[i].value;
		if (strstr(name, "RESERVED"))
			continue;
		lua_pushinteger(L, value);
		lua_setfield(L, -2, name);
	}
	lua_setfield(L, -2, subnamespace);
}

/**
 * Pushes IPROTO constants generated from `IPROTO_FLAGS` onto Lua stack.
 */
static void
push_iproto_flag_constants(struct lua_State *L)
{
	push_iproto_constant_subnamespace(L, "flag", iproto_flag_constants,
					  iproto_flag_constants_size);
}

/**
 * Pushes IPROTO constants generated from `IPROTO_KEYS` onto Lua stack.
 */
static void
push_iproto_key_enum(struct lua_State *L)
{
	push_iproto_constant_subnamespace(L, "key", iproto_key_constants,
					  iproto_key_constants_size);
	for (size_t i = 0; i < iproto_key_constants_size; ++i) {
		const char *name = iproto_key_constants[i].name;
		size_t len = strlen(name);
		char *lowercase = strtolowerdup(name);
		struct mh_strnu32_node_t translation = {
			.str = lowercase,
			.len = len,
			.hash = lua_hash(lowercase, len),
			.val = iproto_key_constants[i].value,
		};
		mh_strnu32_put(iproto_key_translation, &translation,
			       NULL, NULL);
		translation.str = xstrdup(name);
		translation.hash = lua_hash(translation.str, len);
		mh_strnu32_put(iproto_key_translation, &translation,
			       NULL, NULL);
	}
}

/**
 * Pushes IPROTO constants generated from `IPROTO_METADATA_KEYS` onto Lua stack.
 */
static void
push_iproto_metadata_key_enum(struct lua_State *L)
{
	push_iproto_constant_subnamespace(L, "metadata_key",
					  iproto_metadata_key_constants,
					  iproto_metadata_key_constants_size);
}

/**
 * Pushes IPROTO constants generated from `IPROTO_BALLOT_KEYS` onto Lua stack.
 */
static void
push_iproto_ballot_key_enum(struct lua_State *L)
{
	push_iproto_constant_subnamespace(L, "ballot_key",
					  iproto_ballot_key_constants,
					  iproto_ballot_key_constants_size);
}

/**
 * Pushes IPROTO constants generated from `IPROTO_TYPES` onto Lua stack.
 */
static void
push_iproto_type_enum(struct lua_State *L)
{
	push_iproto_constant_subnamespace(L, "type", iproto_type_constants,
					  iproto_type_constants_size);
}

/**
 * Pushes IPROTO constants generated from `IPROTO_RAFT_KEYS` onto Lua stack.
 */
static void
push_iproto_raft_keys_enum(struct lua_State *L)
{
	push_iproto_constant_subnamespace(L, "raft_key",
					  iproto_raft_keys_constants,
					  iproto_raft_keys_constants_size);
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

	for (size_t i = 0; i < 2; ++i)
		lua_createtable(L, 0, iproto_feature_id_constants_size);
	for (size_t i = 0; i < iproto_feature_id_constants_size; ++i) {
		struct iproto_constant constant =
			iproto_feature_id_constants[i];
		char *name = strtolowerdup(constant.name);
		lua_pushboolean(L, true);
		lua_setfield(L, -2, name);
		lua_pushinteger(L, iproto_feature_id_constants[i].value);
		lua_setfield(L, -3, name);
		free(name);
	}
	lua_setfield(L, -3, "protocol_features");
	lua_setfield(L, -2, "feature");
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
	if (luamp_encode_with_translation(L, luaL_msgpack_default, &stream, idx,
					  iproto_key_translation, NULL) != 0) {
		region_truncate(gc, used);
		return NULL;
	}
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
