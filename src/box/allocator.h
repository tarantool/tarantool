#pragma once
/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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
#include <small/small.h>

struct alloc_stats {
	size_t used;
	size_t total;
};

static int
small_stats_noop_cb(const struct mempool_stats *stats, void *cb_ctx)
{
	(void)stats;
	(void)cb_ctx;
	return 0;
}

class SmallAlloc
{
public:
	static inline void
	create(struct slab_cache *cache, uint32_t objsize_min,
	       unsigned granularity, float alloc_factor,
	       float *actual_alloc_factor)
	{
		small_alloc_create(&small_alloc, cache, objsize_min,
				   granularity, alloc_factor,
				   actual_alloc_factor);
	}
	static inline void
	destroy(void)
	{
		small_alloc_destroy(&small_alloc);
	}
	static inline void *
	alloc(size_t size)
	{
		return smalloc(&small_alloc, size);
	}
	static inline void
	free(void *ptr, size_t size)
	{
		return smfree(&small_alloc, ptr, size);
	}
	static inline void
	stats(struct alloc_stats *alloc_stats)
	{
		struct small_stats data_stats;
		small_stats(&small_alloc, &data_stats,
			    small_stats_noop_cb, NULL);
		alloc_stats->used = data_stats.used;
		alloc_stats->total = data_stats.total;
	}
	static inline struct small_alloc *
	get_alloc(void)
	{
		return &small_alloc;
	}
private:
	static struct small_alloc small_alloc;
};
