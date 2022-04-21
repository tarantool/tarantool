/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "lua/utils.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct lua_State;

int
tarantool_lua_compat_init(struct lua_State *L);

#if defined(__cplusplus)
}
#endif
