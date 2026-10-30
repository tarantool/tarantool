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

#include <pthread.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct sys_stats {
	size_t used;
};

struct sys_alloc {
	/** Allocated bytes */
	uint64_t used_bytes;
	/** The source of allocations */
	struct quota *quota;
	/**
	 * List of allocations, used to free up
	 * memory, when allocator is destroyed
	 */
	struct rlist allocations;
#ifndef _NDEBUG
	/**
	 * Debug only: track that all allocations
	 * are made from a single thread.
	 */
	pthread_t thread_id;
#endif
};

static inline void
sys_stats(struct sys_alloc *alloc, struct sys_stats *totals)
{
	totals->used = alloc->used_bytes;
}

void
sys_alloc_create(struct sys_alloc *alloc, struct quota *quota);

void
sys_alloc_destroy(struct sys_alloc *alloc);

void *
sysalloc(struct sys_alloc *alloc, size_t size);

void
sysfree(struct sys_alloc *alloc, void *ptr, size_t size);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
