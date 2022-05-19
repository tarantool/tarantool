/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

/**
 * Monotonic low resolution clock, based on interval timer.
 * Not thread-safe!
 */

#if __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdbool.h>

/** Low resolution clock accumulator. */
extern double clock_lowres_monotonic_clock_value;

#ifndef NDEBUG

/**
 * Check that current thread is the thread
 * that initialized this module.
 */
bool
clock_lowres_thread_is_owner(void);

#endif /* NDEBUG */

/** Get resolution of clock_lowres clocks in double. */
double
clock_lowres_resolution(void);

/**
 * Blazingly fast low resolution monotonic time in seconds.
 */
static inline double
clock_lowres_monotonic(void)
{
	assert(clock_lowres_thread_is_owner());
	return clock_lowres_monotonic_clock_value;
}

/** Initialize signal handler and interval timer. */
void
clock_lowres_signal_init(void);

/** Reset signal handler and interval timer. */
void
clock_lowres_signal_reset(void);

#if __cplusplus
}
#endif
