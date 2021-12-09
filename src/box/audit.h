/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_AUDIT_LOG)
# include "audit_impl.h"
#else /* !defined(ENABLE_AUDIT_LOG) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

void
audit_log_init(const char *init_str, int log_nonblock);

static inline void
audit_log_free(void) {}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_AUDIT_LOG) */
