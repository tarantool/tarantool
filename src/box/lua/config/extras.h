/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_CONFIG_EXTRAS)
#include "lua/config/extras_impl.h"
#else /* !defined(ENABLE_CONFIG_EXTRAS) */

#define CONFIG_EXTRAS_LUA_MODULES

#endif /* !defined(ENABLE_CONFIG_EXTRAS) */
