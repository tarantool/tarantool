/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_FAILOVER)
#include "lua/failover_impl.h"
#else /* !defined(ENABLE_FAILOVER) */

#define FAILOVER_LUA_MODULES

#endif /* !defined(ENABLE_FAILOVER) */
