/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;

/**
 * Write the directory part of path to buf and return buf. The buffer must
 * be at least two bytes long and large enough for path and its terminator.
 */
char *
minifio_dirname(const char *path, char *buf);

/**
 * Set path to the main script.
 */
void
minifio_set_script(const char *script);

void
tarantool_lua_minifio_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
