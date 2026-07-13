/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct key_def;

/**
 * Sets tuple comparison and hashing functions to 'raw' variants that never
 * access tuple format.
 */
void
key_def_set_func_raw(struct key_def *def);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
