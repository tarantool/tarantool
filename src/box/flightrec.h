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
	       double metrics_interval, size_t metrics_period,
	       size_t requests_size, size_t requests_max_req_size,
	       size_t requests_max_res_size)
{
	(void)fr_dirname;
	(void)logs_size;
	(void)logs_max_msg_size;
	(void)logs_log_level;
	(void)metrics_interval;
	(void)metrics_period;
	(void)requests_size;
	(void)requests_max_req_size;
	(void)requests_max_res_size;
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
		    int64_t metrics_period, int64_t requests_size,
		    int64_t requests_max_req_size,
		    int64_t requests_max_res_size)
{
	(void)logs_size;
	(void)logs_max_msg_size;
	(void)logs_log_level;
	(void)metrics_interval;
	(void)metrics_period;
	(void)requests_size;
	(void)requests_max_req_size;
	(void)requests_max_res_size;
	return 0;
}

/** No-op in OS version. */
static inline void
flightrec_write_request(const char *request_msgpack, size_t len)
{
	(void)request_msgpack;
	(void)len;
}

struct obuf;
struct obuf_svp;

/** No-op in OS version. */
static inline void
flightrec_write_response(struct obuf *buf, struct obuf_svp *svp)
{
	(void)buf;
	(void)svp;
}

/**
 * This function is called in SIGBUS handler to check whether accessed address
 * belongs to flightrec file.
 */
static inline bool
flightrec_is_mmapped_address(void *addr)
{
	(void)addr;
	return false;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_FLIGHT_RECORDER) */
