/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;
struct arrow_record_batch;

/**
 * Allocates new Arrow record batch on the Lua stack, and returns its address.
 */
struct arrow_record_batch *
luaT_new_arrow_record_batch(struct lua_State *L);

/**
 * Retrieves Arrow record batch from Lua stack, and throws an error if object at
 * specified index is not an Arrow record batch.
 */
struct arrow_record_batch *
luaT_check_arrow_record_batch(struct lua_State *L, int idx);

/**
 * Initialize `arrow_record_batch' type.
 */
void
tarantool_lua_arrow_record_batch_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
