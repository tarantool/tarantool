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

static inline int
audit_log_check_cfg(void)
{
	return 0;
}

static inline void
audit_log_init(void) {}

static inline void
audit_log_free(void) {}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_AUDIT_LOG) */
