/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;
struct tuple_format;

/**
 * Retrieves tuple format from Lua stack, and throws an error if object at
 * specified index is not a tuple format.
 */
struct tuple_format *
luaT_check_tuple_format(struct lua_State *L, int narg);

/**
 * Initialize box.tuple.format library.
 */
void
box_lua_tuple_format_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
