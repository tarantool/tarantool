/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * State passed to authentication trigger.
 */
struct on_auth_trigger_ctx {
	/** Authenticated user name. Not null-terminated! */
	const char *user_name;
	/** Length of the user_name string. */
	size_t user_name_len;
	/* true if authentication was successful */
	bool is_authenticated;
};

/**
 * Authenticates a user.
 *
 * Takes the following arguments:
 * user_name: user name string, not necessarily null-terminated.
 * user_name_len: length of the user name string.
 * salt: random salt sent in the greeting message.
 * tuple: value of the IPROTO_TUPLE key sent in the IPROTO_AUTH request body.
 *
 * Returns 0 on success. On error, sets diag and returns -1.
 */
int
authenticate(const char *user_name, uint32_t user_name_len,
	     const char *salt, const char *tuple);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
