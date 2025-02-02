/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_REPLICATION_ASYNC_REPAIR_QUEUE)
#include "lua/replication_async_repair_queue_impl.h"
#else /* !defined(ENABLE_REPLICATION_ASYNC_REPAIR_QUEUE) */
#define REPLICATION_ASYNC_REPAIR_QUEUE_BOX_LUA_MODULES
#endif /* !defined(ENABLE_REPLICATION_ASYNC_REPAIR_QUEUE) */
