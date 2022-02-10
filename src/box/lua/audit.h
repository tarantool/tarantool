/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_AUDIT_LOG)
# include "lua/audit_impl.h"
#else /* !defined(ENABLE_AUDIT_LOG) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;

static inline int
luaopen_audit(struct lua_State *L)
{
	lua_pushnil(L);
	return 0;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_AUDIT_LOG) */
