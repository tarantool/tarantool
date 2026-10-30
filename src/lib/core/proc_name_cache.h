/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include <stdint.h>

#include "trivia/config.h"

#ifdef ENABLE_BACKTRACE
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Find procedure name and offset in hash table based on RIP register value. */
const char *
proc_name_cache_find(void *ip, uintptr_t *offs);

/* Insert procedure name and offset into hash table. */
void
proc_name_cache_insert(void *ip, const char *name, uintptr_t offs);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
#endif /* ENABLE_BACKTRACE */
