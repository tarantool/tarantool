/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "box/func_cache.h"

struct func;
struct func_adapter;

/**
 * Creates func adapter for persistent function, never fails.
 * Underlying function is pinned with holder of passed type,
 * so it must be in func_cache while the func_adapter is alive.
 */
struct func_adapter *
func_adapter_func_create(struct func *func, enum func_holder_type type);

#ifdef __cplusplus
} /* extern "C" */
#endif
