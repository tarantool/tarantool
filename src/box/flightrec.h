/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"
#include "say.h"

#if defined(ENABLE_FLIGHT_RECORDER)
# include "flightrec_impl.h"
#else /* !defined(ENABLE_FLIGHT_RECORDER) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** No-op in OS version. */
static inline void
flightrec_init(const char *fr_dirname, size_t logs_size,
	       size_t logs_max_msg_size, int logs_log_level,
	       double metrics_interval, size_t metrics_period)
{
	(void)fr_dirname;
	(void)logs_size;
	(void)logs_max_msg_size;
	(void)logs_log_level;
	(void)metrics_interval;
	(void)metrics_period;
	say_error("Flight recorder is not available in this build");
}

/** No-op in OS version. */
static inline void
flightrec_free(void)
{
}

/** No-op in OS version. */
static inline int
flightrec_check_cfg(int64_t logs_size, int64_t logs_max_msg_size,
		    int logs_log_level, double metrics_interval,
		    int64_t metrics_period)
{
	(void)logs_size;
	(void)logs_max_msg_size;
	(void)logs_log_level;
	(void)metrics_interval;
	(void)metrics_period;
	return 0;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_FLIGHT_RECORDER) */
