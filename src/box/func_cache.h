/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>
#include "func.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct func;

/**
 * Initialize function cache storage.
 */
void
func_cache_init(void);

/**
 * Cleanup function cache storage.
 */
void
func_cache_destroy(void);

/**
 * Insert a new function object in the function cache.
 * @param func Function object to insert.
 */
void
func_cache_insert(struct func *func);

/**
 * Delete a function object from the function cache.
 * If the function is not found by five ID - do nothing.
 * @param fid ID of function object.
 */
void
func_cache_delete(uint32_t fid);

/**
 * Find function by ID or return NULL if not found.
 * @param fid ID of function object.
 */
struct func *
func_by_id(uint32_t fid);

/**
 * Find function by name or return NULL if not found.
 * @param name Name of function object.
 * @param name_len Length of the name of function object.
 */
struct func *
func_by_name(const char *name, uint32_t name_len);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
