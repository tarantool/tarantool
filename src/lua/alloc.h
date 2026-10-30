/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <stddef.h>

#include <lua.h>

/**
 * Initializes custom allocator for the LuaJIT VM.
 * The allocator supports memory limitation.
 */
void
luaT_initalloc(struct lua_State *L);

void
tarantool_lua_alloc_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
