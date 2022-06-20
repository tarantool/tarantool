/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "trivia/config.h"

#ifdef ENABLE_BACKTRACE
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#include "libunwind.h"

/* Find procedure name and offset in hash table based on RIP register value. */
const char *
proc_name_cache_find(unw_word_t ip, unw_word_t *offs);

/* Insert procedure name and offset into hash table. */
void
proc_name_cache_insert(unw_word_t ip, const char *name, unw_word_t offs);
#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
#endif /* ENABLE_BACKTRACE */
