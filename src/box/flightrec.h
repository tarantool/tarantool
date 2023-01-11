/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_FLIGHT_RECORDER)
# include "flightrec_impl.h"
#else /* !defined(ENABLE_FLIGHT_RECORDER) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <stddef.h>
#include <stdbool.h>

struct obuf;
struct obuf_svp;

/**
 * Release resources and clean-up flight recorder.
 */
static inline void
flightrec_free(void)
{
}

/** Dump request (which is already packed into msgapck) to flight recorder. */
static inline void
flightrec_write_request(const char *request_msgpack, size_t len)
{
	(void)request_msgpack;
	(void)len;
}

/**
 * Dump response to flight recorder. Given savepoint points to the start of
 * response stored into buffer.
 */
static inline void
flightrec_write_response(struct obuf *buf, struct obuf_svp *svp)
{
	(void)buf;
	(void)svp;
}

/**
 * Checks box.cfg flight recorder parameters.
 * On success, returns 0. On error, sets diag and returns -1.
 */
static inline int
box_check_flightrec(void)
{
	return 0;
}

/**
 * Applies box.cfg flight recorder parameters.
 * On success, returns 0. On error, sets diag and returns -1.
 */
static inline int
box_set_flightrec(void)
{
	return 0;
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
