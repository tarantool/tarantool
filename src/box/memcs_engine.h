/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>

#include "trivia/config.h"

#if defined(ENABLE_MEMCS_ENGINE)
# include "memcs_engine_impl.h"
#else /* !defined(ENABLE_MEMCS_ENGINE) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

static inline void
memcs_engine_register(void) {}

static inline void
memcs_engine_set_columnar_snapshot(bool enabled)
{
	(void)enabled;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_MEMCS_ENGINE) */
