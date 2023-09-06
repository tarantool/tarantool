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

#include <assert.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

static inline int
audit_log_check_format(const char *format)
{
	(void)format;
	return 0;
}

static inline int
audit_log_check_filter(const char *filter)
{
	(void)filter;
	return 0;
}

/**
 * Logs disconnect event if it is enabled.
 */
static inline void
audit_on_disconnect(void) {}

/**
 * Logs authenticate event if it is enabled.
 */
static inline void
audit_on_auth(const char *user_name, size_t user_name_len,
	      bool is_authenticated)
{
	(void)user_name;
	(void)user_name_len;
	(void)is_authenticated;
}

static inline void
audit_log_init(const char *init_str, int log_nonblock, const char *format,
	       const char *filter)
{
	assert(init_str == NULL);
	(void)init_str;
	(void)log_nonblock;
	(void)format;
	(void)filter;
}

static inline void
audit_log_free(void) {}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_AUDIT_LOG) */
