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

/**
* Initialize box.uri system
*/
void
tarantool_lua_uri_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
