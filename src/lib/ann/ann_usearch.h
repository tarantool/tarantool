/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "ann_backend.h"

#if defined(__cplusplus)
extern "C" {
#endif

/** Isolated USearch implementation of the C backend contract. */
extern const struct ann_backend_ops ann_usearch_ops;

#if defined(__cplusplus)
}
#endif
