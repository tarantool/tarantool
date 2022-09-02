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

/** Flight recorder config options. */
struct flight_recorder_cfg {
	/**
	 * Directory to store flight_records.ttfr file.
	 */
	const char *dir;
	/** Total size of stored logs. */
	int64_t logs_size;
	/** Max size of one log message. */
	int64_t log_max_msg_size;
	/** Flight recorder log level; may be different from say log level. */
	int logs_log_level;
	/**
	 * Time interval (in seconds) between metrics
	 * dump to flight recorder.
	 */
	double metrics_interval;
	/**
	 * Period (in seconds) of metrics storage, i.e. how long
	 * metrics are stored before being overwritten.
	 */
	int64_t metrics_period;
	/** Total size of stored requests and responses. */
	int64_t requests_size;
	/** Max size per request. */
	int64_t requests_max_req_size;
	/** Max size per response. */
	int64_t requests_max_res_size;
};

/** No-op in OS version. */
static inline void
flightrec_init(const struct flight_recorder_cfg *cfg)
{
	(void)cfg;
	say_error("Flight recorder is not available in this build");
}

/** No-op in OS version. */
static inline void
flightrec_free(void)
{
}

/** No-op in OS version. */
static inline int
flightrec_check_cfg(const struct flight_recorder_cfg *cfg)
{
	(void)cfg;
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

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_FLIGHT_RECORDER) */
