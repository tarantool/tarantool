/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Inititalize builtin modules cache.
 */
void
builtin_modcache_init(void);

/**
 * Destruct builtin modules cache.
 */
void
builtin_modcache_free(void);

/**
 * Add a new element to the builtin modules cache.
 * @param modname short name of a module.
 * @param code Lua code to be saved for the given module.
 */
void
builtin_modcache_put(const char *modname, const char *code);

/**
 * Return saved Lua code for a builtin module.
 * @param modname short name of a module.
 */
const char*
builtin_modcache_find(const char *modname);

#ifdef __cplusplus
}
#endif /* defined(__cplusplus) */
