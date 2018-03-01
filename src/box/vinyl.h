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
struct vinyl_engine;

struct vinyl_engine *
vinyl_engine_new(const char *dir, size_t memory,
		 int read_threads, int write_threads, bool force_recovery);

/**
 * Engine introspection (box.info.vinyl())
 */
void
vinyl_engine_info(struct vinyl_engine *vinyl, struct info_handler *handler);

/**
 * Update vinyl cache size.
 */
void
vinyl_engine_set_cache(struct vinyl_engine *vinyl, size_t quota);

/**
 * Update max tuple size.
 */
void
vinyl_engine_set_max_tuple_size(struct vinyl_engine *vinyl, size_t max_size);

/**
 * Update query timeout.
 */
void
vinyl_engine_set_timeout(struct vinyl_engine *vinyl, double timeout);

/**
 * Update too_long_threshold.
 */
void
vinyl_engine_set_too_long_threshold(struct vinyl_engine *vinyl,
				    double too_long_threshold);

#ifdef __cplusplus
} /* extern "C" */

#include "diag.h"

static inline struct vinyl_engine *
vinyl_engine_new_xc(const char *dir, size_t memory,
		    int read_threads, int write_threads, bool force_recovery)
{
	struct vinyl_engine *vinyl;
	vinyl = vinyl_engine_new(dir, memory, read_threads,
				 write_threads, force_recovery);
	if (vinyl == NULL)
		diag_raise();
	return vinyl;
}

#endif /* defined(__plusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VINYL_H */
