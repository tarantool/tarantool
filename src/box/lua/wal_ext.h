/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_WAL_EXT)
#include "lua/wal_ext_impl.h"
#else /* !defined(ENABLE_WAL_EXT) */

struct lua_State;

static inline void
box_lua_wal_ext_init(struct lua_State *L)
{
	(void)L;
}

#endif /* !defined(ENABLE_WAL_EXT) */
