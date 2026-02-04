/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	APP_THREADS_MAX = 1000,
};

/** Number of application threads. */
extern int app_thread_count;

/**
 * Starts application threads.
 *
 * Each thread runs an event loop and has a cbus endpoint named 'app<id>'
 * connected to a fiber pool. Thread ids start with 1 because id 0 is
 * reserved for tx.
 */
void
app_threads_start(int thread_count);

/**
 * Stops application threads and waits for them to join.
 */
void
app_threads_stop(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
