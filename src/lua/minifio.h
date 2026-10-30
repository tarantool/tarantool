/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;

/**
 * Return the directory part of path and write its length to dir_len.
 * The result may point into path and need not be null-terminated.
 */
const char *
minifio_dirname(const char *path, size_t *dir_len);

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
