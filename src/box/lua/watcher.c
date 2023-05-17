/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/lua/watcher.h"

#include <assert.h>
#include <lua.h>
#include <lauxlib.h>
#include <stddef.h>

#include "box/watcher.h"
#include "diag.h"
#include "fiber.h"
#include "core/cord_buf.h"
#include "lua/msgpack.h"
#include "lua/utils.h"
#include "mpstream/mpstream.h"
#include "small/ibuf.h"
#include "trivia/util.h"

struct lbox_watcher {
	struct watcher base;
	/** Lua function reference. */
	int func_ref;
};

/**
 * Watcher handle pushed as userdata to Lua so that a watcher can be
 * unregistered from Lua. Garbage collection of a handle doesn't lead to
 * watcher destruction.
 */
struct lbox_watcher_handle {
	struct lbox_watcher *watcher;
};

static const char lbox_watcher_typename[] = "box.watcher";

/**
 * We keep a reference to each C function that is often called with lua_pcall
 * so as not to create a new Lua object each time we call it.
 */
static int lbox_watcher_run_lua_ref = LUA_NOREF;

/** Passed to pcall by lbox_watcher_run_f. */
static int
lbox_watcher_run_lua(struct lua_State *L)
{
	struct lbox_watcher *watcher = lua_touserdata(L, 1);
	size_t key_len;
	const char *key = watcher_key(&watcher->base, &key_len);
	const char *data_end;
	const char *data = watcher_data(&watcher->base, &data_end);
	lua_rawgeti(L, LUA_REGISTRYINDEX, watcher->func_ref);
	lua_pushlstring(L, key, key_len);
	if (data != NULL) {
		luamp_decode(L, luaL_msgpack_default, &data);
		assert(data == data_end);
		(void)data_end;
	}
	lua_call(L, data != NULL ? 2 : 1, 0);
	return 0;
}

/**
 * The callback runs a user-defined Lua function. Since the callback is invoked
 * in a newly created fiber, which doesn't have a Lua stack, we need to create
 * a temporary Lua stack for the call.
 *
 * A user-defined watcher function may throw. Even pushing arguments to the
 * stack may throw. So we wrap the callback in pcall to properly handle a Lua
 * exception.
 */
static void
lbox_watcher_run_f(struct watcher *watcher)
{
	/*
	 * Create a new coro and reference it. Remove it
	 * from tarantool_L stack, which is a) scarce
	 * b) can be used by other triggers while this
	 * trigger yields, so when it's time to clean
	 * up the coro, we wouldn't know which stack position
	 * it is on.
	 */
	void *L = luaT_newthread(tarantool_L);
	if (L == NULL) {
		diag_log();
		return;
	}
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	lua_rawgeti(L, LUA_REGISTRYINDEX, lbox_watcher_run_lua_ref);
	lua_pushlightuserdata(L, watcher);
	if (luaT_call(L, 1, 0) != 0)
		diag_log();
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
}

/**
 * Releases the Lua function reference and frees the watcher.
 */
static void
lbox_watcher_destroy_f(struct watcher *base)
{
	struct lbox_watcher *watcher = (struct lbox_watcher *)base;
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, watcher->func_ref);
	free(watcher);
}

static inline struct lbox_watcher_handle *
lbox_check_watcher(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, lbox_watcher_typename);
}

static int
lbox_watcher_tostring(struct lua_State *L)
{
	lua_pushstring(L, lbox_watcher_typename);
	return 1;
}

/**
 * Lua wrapper around box_watcher_unregister().
 */
static int
lbox_watcher_unregister(struct lua_State *L)
{
	struct lbox_watcher_handle *handle = lbox_check_watcher(L, 1);
	if (handle->watcher == NULL)
		return luaL_error(L, "Watcher is already unregistered");
	watcher_unregister(&handle->watcher->base);
	handle->watcher = NULL;
	return 0;
}

/**
 * Lua wrapper around box_watcher_register().
 */
static int
lbox_watch(struct lua_State *L)
{
	/* Check arguments. */
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Usage: box.watch(key, function)");
	size_t key_len;
	const char *key = luaL_checklstring(L, 1, &key_len);
	luaL_checktype(L, 2, LUA_TFUNCTION);

	/* Create a watcher handle. */
	struct lbox_watcher_handle *handle = lua_newuserdata(
		L, sizeof(*handle));
	luaL_getmetatable(L, lbox_watcher_typename);
	lua_setmetatable(L, -2);
	lua_replace(L, 1);

	/* Allocate and register a watcher. */
	struct lbox_watcher *watcher = xmalloc(sizeof(*watcher));
	watcher->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	box_register_watcher(key, key_len, lbox_watcher_run_f,
			     lbox_watcher_destroy_f, WATCHER_RUN_ASYNC,
			     &watcher->base);
	handle->watcher = watcher;
	return 1;
}

/**
 * Lua wrapper around box_broadcast().
 */
static int
lbox_broadcast(struct lua_State *L)
{
	int top = lua_gettop(L);
	if (top != 1 && top != 2)
		return luaL_error(L, "Usage: box.broadcast(key[, value])");
	size_t key_len;
	const char *key = luaL_checklstring(L, 1, &key_len);
	if (strncmp(key, "box.", 4) == 0)
		return luaL_error(L, "System event can't be override");
	struct ibuf *ibuf = cord_ibuf_take();
	const char *data = NULL;
	const char *data_end = NULL;
	int rc = -1;
	if (!lua_isnoneornil(L, 2)) {
		struct mpstream stream;
		mpstream_init(&stream, ibuf, ibuf_reserve_cb, ibuf_alloc_cb,
			      luamp_error, L);
		if (luamp_encode(L, luaL_msgpack_default, &stream, 2) != 0)
			goto cleanup;
		mpstream_flush(&stream);
		data = ibuf->rpos;
		data_end = data + ibuf_used(ibuf);
	}
	box_broadcast(key, key_len, data, data_end);
	rc = 0;
cleanup:
	cord_ibuf_put(ibuf);
	return rc == 0 ? 0 : luaT_error(L);
}

void
box_lua_watcher_init(struct lua_State *L)
{
	lua_pushcfunction(L, lbox_watcher_run_lua);
	lbox_watcher_run_lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	static const struct luaL_Reg lbox_watcher_meta[] = {
		{"__tostring", lbox_watcher_tostring},
		{"unregister", lbox_watcher_unregister},
		{NULL, NULL},
	};
	luaL_register_type(L, lbox_watcher_typename, lbox_watcher_meta);

	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "watch");
	lua_pushcfunction(L, lbox_watch);
	lua_settable(L, -3);
	lua_pushstring(L, "broadcast");
	lua_pushcfunction(L, lbox_broadcast);
	lua_settable(L, -3);
	lua_pop(L, 1);
}
