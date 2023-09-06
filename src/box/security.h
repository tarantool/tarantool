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

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct user;

/**
 * Registers an authentication delay for the given user if authentication
 * failed and the feature is enabled.
 */
static inline void
security_on_auth(const char *user_name, size_t user_name_len,
		 bool is_authenticated)
{
	(void)user_name;
	(void)user_name_len;
	(void)is_authenticated;
}

static inline void
security_init(void) {}

static inline void
security_free(void) {}

/**
 * Sets security configuration from box.cfg option values.
 * Safe to call more than once.
 */
static inline void
security_cfg(void) {}

/**
 * Checks if it's permitted to log in as a user before authentication.
 * Note, the user may not exist.
 *
 * Returns 0 on success. On error, sets diag and returns -1.
 */
static inline int
security_check_auth_pre(const char *user_name, uint32_t user_name_len)
{
	(void)user_name;
	(void)user_name_len;
	return 0;
}

/**
 * Checks if it's permitted to log in as a user after authentication.
 *
 * Returns 0 on success. On error, sets diag and returns -1.
 */
static inline int
security_check_auth_post(struct user *user)
{
	(void)user;
	return 0;
}

/**
 * Checks if it's permitted to perform a request different from
 * auth, ping, id, or vote in the current session.
 *
 * Returns 0 on success. On error, sets diag and returns -1.
 */
static inline int
security_check_session(void)
{
	return 0;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_SECURITY) */
