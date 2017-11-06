#ifndef TARANTOOL_BOX_VINYL_ENGINE_H_INCLUDED
#define TARANTOOL_BOX_VINYL_ENGINE_H_INCLUDED
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
#include <small/mempool.h>

#include "engine.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_env;

struct vinyl_engine {
	struct engine base;
	struct vy_env *env;
	/** Memory pool for index iterator. */
	struct mempool iterator_pool;
};

struct vinyl_engine *
vinyl_engine_new(const char *dir, size_t memory, size_t cache,
		 int read_threads, int write_threads, double timeout,
		 bool force_recovery);

void
vinyl_engine_set_max_tuple_size(struct vinyl_engine *vinyl, size_t max_size);

void
vinyl_engine_set_timeout(struct vinyl_engine *vinyl, double timeout);

#if defined(__cplusplus)
} /* extern "C" */

#include "diag.h"

static inline struct vinyl_engine *
vinyl_engine_new_xc(const char *dir, size_t memory, size_t cache,
		    int read_threads, int write_threads, double timeout,
		    bool force_recovery)
{
	struct vinyl_engine *vinyl;
	vinyl = vinyl_engine_new(dir, memory, cache, read_threads,
				 write_threads, timeout, force_recovery);
	if (vinyl == NULL)
		diag_raise();
	return vinyl;
}

#endif /* defined(__plusplus) */

#endif /* TARANTOOL_BOX_VINYL_ENGINE_H_INCLUDED */
