/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * PATH_MAX is too big and 2K is recommended
 * limit for web address.
 */
#define CRASH_FEEDBACK_HOST_MAX 2048

/**
 * Initialize crash subsystem.
 */
void
crash_init(const char *tarantool_bin);

/**
 * Configure crash parameters.
 */
void
crash_cfg(const char *host, bool is_enabled);

/**
 * Initialize crash signal handlers.
 */
void
crash_signal_init(void);

/**
 * Reset crash signal handlers.
 */
void
crash_signal_reset(void);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
