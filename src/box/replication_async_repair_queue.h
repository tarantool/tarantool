/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_REPLICATION_ASYNC_REPAIR_QUEUE)
#include "replication_async_repair_queue_impl.h"
#else /* !defined(ENABLE_REPLICATION_ASYNC_REPAIR_QUEUE) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include "stdbool.h"

/**
 * A stub for the EE implementation.
 */
static inline bool
replication_async_repair_queue_is_ro(void)
{
	return false;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_FLIGHT_RECORDER) */
