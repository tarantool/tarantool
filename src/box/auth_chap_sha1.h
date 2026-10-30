/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct auth_method;

/**
 * Allocates and initializes 'chap-sha1' authentication method.
 * This function never fails.
 */
struct auth_method *
auth_chap_sha1_new(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
