/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct func;

/**
 * Creates func_adapter_func from a func. Never returns NULL.
 */
struct func_adapter *
func_adapter_func_create(struct func *func);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
