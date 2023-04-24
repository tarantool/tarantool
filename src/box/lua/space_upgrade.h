/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_SPACE_UPGRADE)
#include "lua/space_upgrade_impl.h"
#else /* !defined(ENABLE_SPACE_UPGRADE) */

#define SPACE_UPGRADE_BOX_LUA_MODULES

struct lua_State;

static inline void
box_lua_space_upgrade_init(struct lua_State *L)
{
	(void)L;
}

#endif /* !defined(ENABLE_SPACE_UPGRADE) */
