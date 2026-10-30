#ifndef INCLUDES_TARANTOOL_BOX_VINYL_H
#define INCLUDES_TARANTOOL_BOX_VINYL_H
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

struct info_handler;
struct engine;

struct engine *
vinyl_engine_new(const char *dir, size_t memory,
		 int read_threads, int write_threads, bool force_recovery);

/**
 * Vinyl engine statistics (box.stat.vinyl()).
 */
void
vinyl_engine_stat(struct engine *engine, struct info_handler *handler);

/**
 * Update vinyl cache size.
 */
void
vinyl_engine_set_cache(struct engine *engine, size_t quota);

/**
 * Update vinyl memory size.
 */
int
vinyl_engine_set_memory(struct engine *engine, size_t size);

/**
 * Update max tuple size.
 */
void
vinyl_engine_set_max_tuple_size(struct engine *engine, size_t max_size);

/**
 * Update query timeout.
 */
void
vinyl_engine_set_timeout(struct engine *engine, double timeout);

/**
 * Update too_long_threshold.
 */
void
vinyl_engine_set_too_long_threshold(struct engine *engine,
				    double too_long_threshold);

/**
 * Update snap_io_rate_limit.
 */
void
vinyl_engine_set_snap_io_rate_limit(struct engine *engine, double limit);

#ifdef __cplusplus
} /* extern "C" */

#include "diag.h"

static inline struct engine *
vinyl_engine_new_xc(const char *dir, size_t memory,
		    int read_threads, int write_threads, bool force_recovery)
{
	struct engine *vinyl;
	vinyl = vinyl_engine_new(dir, memory, read_threads,
				 write_threads, force_recovery);
	if (vinyl == NULL)
		diag_raise();
	return vinyl;
}

static inline void
vinyl_engine_set_memory_xc(struct engine *engine, size_t size)
{
	if (vinyl_engine_set_memory(engine, size) != 0)
		diag_raise();
}

#endif /* defined(__plusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VINYL_H */
