/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_SECURITY)
#include "lua/security_impl.h"
#else /* !defined(ENABLE_SECURITY) */

#define SECURITY_BOX_LUA_MODULES

struct lua_State;

static inline void
box_lua_security_init(struct lua_State *L)
{
	(void)L;
}

#endif /* !defined(ENABLE_SECURITY) */
