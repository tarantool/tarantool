/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_INTEGRITY)
#include "lua/integrity_impl.h"
#else /* !defined(ENABLE_INTEGRITY) */

#define INTEGRITY_BOX_LUA_MODULES

struct lua_State;

static inline void
box_lua_integrity_init(struct lua_State *L)
{
	(void)L;
}

/**
 * Placeholder function that is properly implemented inside integrity module.
 * It is available only inside Enterprise Edition builds.
 */
static inline bool
integrity_verify_file(const char *path, const char *buffer, size_t size)
{
	(void)path;
	(void)buffer;
	(void)size;
	return true;
}

#endif /* !defined(ENABLE_INTEGRITY) */
