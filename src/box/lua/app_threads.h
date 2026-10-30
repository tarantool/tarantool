/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct port;

void
app_thread_lua_init(void);

void
app_thread_lua_free(void);

/**
 * Executes a Lua function in this application thread.
 */
int
app_thread_lua_call(const char *name, uint32_t name_len,
		    struct port *args, struct port *ret);

/**
 * Executes a Lua expression in this application thread.
 */
int
app_thread_lua_eval(const char *expr, uint32_t expr_len,
		    struct port *args, struct port *ret);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
