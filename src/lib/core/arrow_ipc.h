/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "arrow/abi.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct region;

/**
 * Encodes `array' and `schema' into Arrow IPC format. The memory is allocated
 * on `region', and the address is returned via `ret_data' and `ret_data_end'.
 * Returns 0 on success, -1 on failure (diag is set).
 */
int
arrow_ipc_encode(struct ArrowArray *array, struct ArrowSchema *schema,
		 struct region *region, const char **ret_data,
		 const char **ret_data_end);

/**
 * Decodes `array' and `schema' from the `data' in Arrow IPC format.
 * Returns 0 on success, -1 on failure (diag is set).
 */
int
arrow_ipc_decode(struct ArrowArray *array, struct ArrowSchema *schema,
		 const char *data, const char *data_end);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
