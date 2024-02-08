/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_RETENTION_PERIOD)
#include "lua/wal_retention_period_impl.h"
#else /* !defined(ENABLE_RETENTION_PERIOD) */

struct lua_State;

static inline void
box_lua_wal_retention_period_init(struct lua_State *L)
{
	(void)L;
}

#endif /* !defined(ENABLE_RETENTION_PERIOD) */
