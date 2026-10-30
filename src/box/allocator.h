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
#include <tuple>

#include <small/small.h>
#include "sysalloc.h"

typedef int (*allocator_stats_cb)(const void *, void *);

struct alloc_stat {
	size_t used;
	size_t total;
};

struct allocator_stats {
	struct alloc_stat small;
	struct alloc_stat sys;
};

int
stats_noop_cb(const void *stats, void *cb_ctx);

struct allocator_settings {
	struct small_allocator {
		struct slab_cache *cache;
		uint32_t objsize_min;
		unsigned granularity;
		float alloc_factor;
		float *actual_alloc_factor;
	} small;
	struct system_allocator {
		struct quota *quota;
	} sys;
};

static inline void
allocator_settings_init(allocator_settings *settings, struct slab_cache *cache,
			uint32_t objsize_min, unsigned granularity,
			float alloc_factor, float *actual_alloc_factor,
			struct quota *quota)
{
	settings->small.cache = cache;
	settings->small.objsize_min = objsize_min;
	settings->small.granularity = granularity;
	settings->small.alloc_factor = alloc_factor;
	settings->small.actual_alloc_factor = actual_alloc_factor;

	settings->sys.quota = quota;
}

/**
 * Each allocator class should feature at least following interfaces:
 *
 * void create(struct allocator_settings *settings);
 * void destroy(void);
 * void* alloc(size_t size);
 * void free(void* ptr, size_t size);
 * void stats(struct alloc_stats);
 */
class SmallAlloc
{
public:
	static inline void
	create(struct allocator_settings *settings)
	{
		small_alloc_create(&small_alloc,
				   settings->small.cache,
				   settings->small.objsize_min,
				   settings->small.granularity,
				   settings->small.alloc_factor,
				   settings->small.actual_alloc_factor);
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
	stats(struct allocator_stats *alloc_stats, allocator_stats_cb cb,
	      void *cb_ctx)
	{
		struct small_stats data_stats;
		small_stats(&SmallAlloc::small_alloc, &data_stats,
			    cb, cb_ctx);
		alloc_stats->small.used = data_stats.used;
		alloc_stats->small.total = data_stats.total;
	}
	static inline struct small_alloc *
	get_alloc(void)
	{
		return &small_alloc;
	}
	static inline void
	get_alloc_info(void *ptr, size_t size, struct small_alloc_info *info)
	{
		small_alloc_info(&small_alloc, ptr, size, info);
	}
private:
	static struct small_alloc small_alloc;
};

class SysAlloc
{
public:
	static inline void
	create(struct allocator_settings *settings)
	{
		sys_alloc_create(&sys_alloc, settings->sys.quota);
	}
	static inline void
	destroy(void)
	{
		sys_alloc_destroy(&sys_alloc);
	}
	static inline void *
	alloc(size_t size)
	{
		return sysalloc(&sys_alloc, size);
	}
	static inline void
	free(void *ptr, size_t size)
	{
		return sysfree(&sys_alloc, ptr, size);
	}
	static inline void
	stats(struct allocator_stats *alloc_stats, allocator_stats_cb cb,
	      void *cb_ctx)
	{
		(void) cb;
		(void) cb_ctx;
		struct sys_stats data_stats;
		sys_stats(&sys_alloc, &data_stats);
		alloc_stats->sys.used = data_stats.used;
	}
private:
	static struct sys_alloc sys_alloc;
};

using allocators = std::tuple<SmallAlloc, SysAlloc>;

template <class F, class... ARGS>
static void
foreach_allocator_internal(std::tuple<>*, F&, ARGS && ...)
{
}

template <class ALL, class... MORE, class F, class... ARGS>
static void
foreach_allocator_internal(std::tuple<ALL, MORE...>*, F& f, ARGS && ...args)
{
	f.template invoke<ALL>(std::forward<ARGS>(args)...);
	foreach_allocator_internal((std::tuple<MORE...> *) nullptr,
				   f, std::forward<ARGS>(args)...);
}

template<class F, class... ARGS>
static void
foreach_allocator(ARGS && ...args)
{
	F f;
	foreach_allocator_internal((allocators *) nullptr, f,
				   std::forward<ARGS>(args)...);
}

struct allocator_create {
	template<typename Allocator, typename...Arg>
	void
	invoke(Arg&&...settings)
	{
		Allocator::create(settings...);
	}
};

struct allocator_destroy {
	template<typename Allocator, typename...Arg>
	void
	invoke(Arg&&...)
	{
		Allocator::destroy();
	}
};

struct allocator_stat {
	template<typename Allocator, typename...Arg>
	void
	invoke(Arg&&...stats)
	{
		Allocator::stats(stats...);
	}
};

void
allocators_stats(struct allocator_stats *stats, allocator_stats_cb cb,
		 void *cb_ctx);

void
allocators_stats(struct allocator_stats *stats);
