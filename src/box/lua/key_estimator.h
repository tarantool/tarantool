/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#ifndef TARANTOOL_BOX_LUA_KEY_ESTIMATOR_H_INCLUDED
#define TARANTOOL_BOX_LUA_KEY_ESTIMATOR_H_INCLUDED

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;
struct key_estimator;

/**
 * Push a value representing a key_estimator object onto the Lua stack.
 */
void
luaT_push_key_estimator(struct lua_State *L, struct key_estimator *estimator);

/**
 * Check a key_estimator object in LUA stack by specified index.
 * Return not NULL key_estimator pointer on success, NULL otherwise.
 */
struct key_estimator *
luaT_is_key_estimator(struct lua_State *L, int idx);

/**
 * Register the module.
 */
void
luaopen_key_estimator(struct lua_State *L);

#if defined(__cplusplus)
}  /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_LUA_KEY_ESTIMATOR_H_INCLUDED */
