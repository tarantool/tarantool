/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_QUIVER_ENGINE)
# include "quiver_engine_impl.h"
#else /* !defined(ENABLE_QUIVER_ENGINE) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

static inline void
quiver_engine_register(void) {}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_QUIVER_ENGINE) */
