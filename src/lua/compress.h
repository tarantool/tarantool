/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_COMPRESS_MODULE)
#include "lua/compress_impl.h"
#else /* !defined(ENABLE_COMPRESS_MODULE) */

#define COMPRESS_LUA_MODULES

struct lua_State;

static inline void
tarantool_lua_compress_init(struct lua_State *L)
{
	(void)L;
}

#endif /* !defined(ENABLE_COMPRESS_MODULE) */
