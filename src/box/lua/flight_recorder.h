/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_FLIGHT_RECORDER)
#include "lua/flightrec_impl.h"
#else /* !defined(ENABLE_FLIGHT_RECORDER) */

#define FLIGHT_RECORDER_BOX_LUA_MODULES

struct lua_State;

static inline void
box_lua_flightrec_init(struct lua_State *L)
{
	(void)L;
}

#endif /* !defined(ENABLE_FLIGHT_RECORDER) */
