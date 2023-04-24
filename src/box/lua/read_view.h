/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_READ_VIEW)
#include "lua/read_view_impl.h"
#else /* !defined(ENABLE_READ_VIEW) */

#define READ_VIEW_BOX_LUA_MODULES

struct lua_State;

static inline void
box_lua_read_view_init(struct lua_State *L)
{
	(void)L;
}

#endif /* !defined(ENABLE_READ_VIEW) */
