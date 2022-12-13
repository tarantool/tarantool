/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_SECURITY)
# include "security_impl.h"
#else /* !defined(ENABLE_SECURITY) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

static inline void
security_init(void) {}

static inline void
security_free(void) {}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_SECURITY) */
