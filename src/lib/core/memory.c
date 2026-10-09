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
#include "memory.h"

#include "fiber.h"
#include "small/quota.h"
#include "small/small.h"

struct slab_arena runtime;

void
memory_init(void)
{
	static struct quota runtime_quota;
	const size_t SLAB_SIZE = 4 * 1024 * 1024;
	/* default quota initialization */
	quota_init(&runtime_quota, QUOTA_MAX);

	/* No limit on the runtime memory. */
	slab_arena_create(&runtime, &runtime_quota, 0,
			  SLAB_SIZE, SLAB_ARENA_PRIVATE);
}

void *
runtime_memory_alloc(size_t size)
{
	return smalloc(&cord()->runtime_alloc, size);
}

void
runtime_memory_free(void *ptr, size_t size)
{
	smfree(&cord()->runtime_alloc, ptr, size);
}

static int
small_stats_noop_cb(const void *stats, void *cb_ctx)
{
	(void)stats;
	(void)cb_ctx;
	return 0;
}

size_t
runtime_memory_used(void)
{
	struct small_stats data_stats;
	small_stats(&cord()->runtime_alloc, &data_stats, small_stats_noop_cb,
		    NULL);
	return data_stats.used;
}


void
memory_free(void)
{
	/*
	 * If this is called from a fiber != sched, then
	 * %rsp is pointing at the memory that we
	 * would be trying to unmap. Don't.
	 */
#if 0
	slab_arena_destroy(&runtime);
#endif
}
