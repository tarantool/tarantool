/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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
#include "sysalloc.h"

#include <small/quota.h>

struct container {
	struct rlist rlist;
	size_t bytes;
};

void
sys_alloc_create(struct sys_alloc *alloc, struct quota *quota)
{
	alloc->used_bytes = 0;
	alloc->quota = quota;
	rlist_create(&alloc->allocations);
#ifndef _NDEBUG
	alloc->thread_id = pthread_self();
#endif
}

void
sys_alloc_destroy(struct sys_alloc *alloc)
{
#ifndef _NDEBUG
	assert(pthread_equal(alloc->thread_id, pthread_self()));
#endif
	struct container *item, *tmp;
	rlist_foreach_entry_safe(item, &alloc->allocations, rlist, tmp)
		sysfree(alloc, ((void *)item) + sizeof(struct container), item->bytes);
	assert(alloc->used_bytes == 0);
}

void *
sysalloc(struct sys_alloc *alloc, size_t bytes)
{
#ifndef _NDEBUG
	assert(pthread_equal(alloc->thread_id, pthread_self()));
#endif

	void *ptr = malloc(sizeof(struct container) + bytes);
	if (!ptr)
		return NULL;
	/*
	 * The limit on the amount of memory available to allocator
	 * is stored in struct quota. We request from the quota at least
	 * QUOTA_UNIT_SIZE bytes. Divide the requested bytes into two parts:
	 * integer number of blocks by QUOTA_UNIT_SIZE bytes and remaining
	 * bytes count.
	 */
	unsigned remaining = bytes % QUOTA_UNIT_SIZE;
	unsigned units = bytes / QUOTA_UNIT_SIZE;
	/*
	 * Check that we need one more quota block for remaining bytes
	 */
	if (small_align(alloc->used_bytes, QUOTA_UNIT_SIZE) <
	    small_align(alloc->used_bytes + remaining, QUOTA_UNIT_SIZE))
			units++;
	if (units > 0 &&
	    quota_use(alloc->quota, units * QUOTA_UNIT_SIZE) < 0) {
		free(ptr);
		return NULL;
	}
	alloc->used_bytes += bytes;
	((struct container *)ptr)->bytes = bytes;
	rlist_add_entry(&alloc->allocations, (struct container *)ptr, rlist);
	return ptr + sizeof(struct container);
}

void
sysfree(struct sys_alloc *alloc, void *ptr, size_t bytes)
{
#ifndef _NDEBUG
	assert(alloc->thread_id == pthread_self());
#endif
	ptr -= sizeof(struct container);
	unsigned remaining = bytes % QUOTA_UNIT_SIZE;
	unsigned units = bytes / QUOTA_UNIT_SIZE;
	/*
	 * Check that we can free one more quota block when we
	 * released remaining bytes
	 */
	if (small_align(alloc->used_bytes, QUOTA_UNIT_SIZE) >
	    small_align(alloc->used_bytes - remaining, QUOTA_UNIT_SIZE))
		units++;
	alloc->used_bytes -= bytes;
	if (units > 0)
		quota_release(alloc->quota, units * QUOTA_UNIT_SIZE);
	rlist_del_entry((struct container *)ptr, rlist);
	free(ptr);
}
