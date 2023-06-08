/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;
struct func_adapter;

/**
 * Creates func_adapter_lua from Lua callable object. Never returns NULL.
 */
struct func_adapter *
func_adapter_lua_create(struct lua_State *L, int idx);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
