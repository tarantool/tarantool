/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "small/slab_arena.h"
#include "small/quota.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <assert.h>
#include <limits.h>
#include "third_party/pmatomic.h"

#if !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

static void
munmap_checked(void *addr, size_t size)
{
	if (munmap(addr, size)) {
		char buf[64];
		intptr_t ignore_it = (intptr_t)strerror_r(errno, buf,
							  sizeof(buf));
		(void)ignore_it;
		fprintf(stderr, "Error in munmap(%p, %zu): %s\n",
			addr, size, buf);
		assert(false);
	}
}

static void *
mmap_checked(size_t size, size_t align, int flags)
{
	/* The alignment must be a power of two. */
	assert((align & (align - 1)) == 0);
	/* The size must be a multiple of alignment */
	assert((size & (align - 1)) == 0);
	/*
	 * All mappings except the first are likely to
	 * be aligned already.  Be optimistic by trying
	 * to map exactly the requested amount.
	 */
	void *map = mmap(NULL, size, PROT_READ | PROT_WRITE,
			 flags | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED)
		return NULL;
	if (((intptr_t) map & (align - 1)) == 0)
		return map;
	munmap_checked(map, size);

	/*
	 * mmap enough amount to be able to align
	 * the mapped address.  This can lead to virtual memory
	 * fragmentation depending on the kernels allocation
	 * strategy.
	 */
	map = mmap(NULL, size + align, PROT_READ | PROT_WRITE,
		   flags | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED)
		return NULL;

	/* Align the mapped address around slab size. */
	size_t offset = (intptr_t) map & (align - 1);

	if (offset != 0) {
		/* Unmap unaligned prefix and postfix. */
		munmap_checked(map, align - offset);
		map += align - offset;
		munmap_checked(map + size, offset);
	} else {
		/* The address is returned aligned. */
		munmap_checked(map + size, align);
	}
	return map;
}

#if 0
/** This is a way to round things up without using a built-in. */
static size_t
pow2round(size_t size)
{
	int shift = 1;
	size_t res = size - 1;
	while (res & (res + 1)) {
		res |= res >> shift;
		shift <<= 1;
	}
	return res + 1;
}
#endif

#define MAX(a, b) (a) > (b) ? (a) : (b)
#define MIN(a, b) (a) < (b) ? (a) : (b)

int
slab_arena_create(struct slab_arena *arena, struct quota *quota,
		  size_t prealloc, uint32_t slab_size, int flags)
{
	assert(flags & (MAP_PRIVATE | MAP_SHARED));
	lf_lifo_init(&arena->cache);
	/*
	 * Round up the user supplied data - it can come in
	 * directly from the configuration file. Allow
	 * zero-size arena for testing purposes.
	 */
	arena->slab_size = small_round(MAX(slab_size, SLAB_MIN_SIZE));

	arena->quota = quota;
	/** Prealloc can not be greater than the quota */
	prealloc = MIN(prealloc, quota_total(quota));
	/** Extremely large sizes can not be aligned properly */
	prealloc = MIN(prealloc, SIZE_MAX - arena->slab_size);
	/* Align prealloc around a fixed number of slabs. */
	arena->prealloc = small_align(prealloc, arena->slab_size);

	arena->used = 0;

	arena->flags = flags;

	if (arena->prealloc) {
		arena->arena = mmap_checked(arena->prealloc,
					    arena->slab_size,
					    arena->flags);
	} else {
		arena->arena = NULL;
	}
	return arena->prealloc && !arena->arena ? -1 : 0;
}

void
slab_arena_destroy(struct slab_arena *arena)
{
	void *ptr;
	size_t total = 0;
	while ((ptr = lf_lifo_pop(&arena->cache))) {
		if (arena->arena == NULL || ptr < arena->arena ||
		    ptr >= arena->arena + arena->prealloc) {
			munmap_checked(ptr, arena->slab_size);
		}
		total += arena->slab_size;
	}
	if (arena->arena)
		munmap_checked(arena->arena, arena->prealloc);

	assert(total == arena->used);
}

void *
slab_map(struct slab_arena *arena)
{
	void *ptr;
	if ((ptr = lf_lifo_pop(&arena->cache)))
		return ptr;

	if (quota_use(arena->quota, arena->slab_size) < 0)
		return NULL;

	/** Need to allocate a new slab. */
	size_t used = pm_atomic_fetch_add(&arena->used, arena->slab_size);
	used += arena->slab_size;
	if (used <= arena->prealloc)
		return arena->arena + used - arena->slab_size;

	ptr = mmap_checked(arena->slab_size, arena->slab_size,
			   arena->flags);
	if (!ptr) {
		__sync_sub_and_fetch(&arena->used, arena->slab_size);
		quota_release(arena->quota, arena->slab_size);
	}
	return ptr;
}

void
slab_unmap(struct slab_arena *arena, void *ptr)
{
	if (ptr)
		lf_lifo_push(&arena->cache, ptr);
}

void
slab_arena_mprotect(struct slab_arena *arena)
{
	if (arena->arena)
		mprotect(arena->arena, arena->prealloc, PROT_READ);
}
