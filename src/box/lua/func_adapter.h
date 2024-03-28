/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>

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

/**
 * Returns true if func_adapter is func_adapter_lua.
 */
bool
func_adapter_is_lua(struct func_adapter *func);

/**
 * Pushes actual Lua function onto the stack. Must be func_adapter_lua.
 */
void
func_adapter_lua_get_func(struct func_adapter *func, struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
