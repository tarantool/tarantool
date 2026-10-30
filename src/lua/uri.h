/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;
struct uri;
struct uri_set;

/**
 * Create @a uri from the value at the given valid @a idx.
 */
int
luaT_uri_create(struct lua_State *L, int idx, struct uri *uri);

/**
 * Create @a uri_set from the value at the given valid @a idx.
 */
int
luaT_uri_set_create(struct lua_State *L, int idx, struct uri_set *uri_set);

/**
* Initialize box.uri system
*/
void
tarantool_lua_uri_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
