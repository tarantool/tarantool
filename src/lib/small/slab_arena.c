/*
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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <assert.h>
#include <limits.h>

void
munmap_checked(void *addr, size_t size)
{
	if (munmap(addr, size)) {
		char buf[64];
		strerror_r(errno, buf, sizeof(buf));
		fprintf(stderr, "Error in munmap(%p, %zu): %s\n",
			addr, size, buf);
		assert(false);
	}
}

static void *
mmap_checked(size_t size, size_t align, int flags)
{
	/* The size must be a power of two. */
	assert((size & (size - 1)) == 0);
	assert((align & (align - 1)) == 0);
	/*
	 * mmap twice the requested amount to be able to align
	 * the mapped address.
	 * @todo all mappings except the first are likely to
	 * be aligned already. Find out if trying to map
	 * optimistically exactly the requested amount and fall
	 * back to double-size mapping is a viable strategy.
	 */
	void *map = mmap(NULL, size + align, PROT_READ | PROT_WRITE,
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

void
slab_arena_create(struct slab_arena *arena,
		  size_t prealloc, size_t maxalloc,
		size_t slab_size, int flags)
{
	assert(flags & (MAP_PRIVATE | MAP_SHARED));
	lf_lifo_init(&arena->cache);
	/*
	 * Round up the user supplied data - it can come in
	 * directly from the configuration file. Allow
	 * zero-size arena for testing purposes.
	 */
	arena->slab_size = small_round(MAX(slab_size, SLAB_MIN_SIZE));

	if (maxalloc) {
		arena->maxalloc = small_round(MAX(maxalloc,
						      arena->slab_size));
	} else {
		arena->maxalloc = 0;
	}

	/* Align arena around a fixed number of slabs. */
	arena->prealloc = small_align(small_round(prealloc),
				      arena->slab_size);
	if (arena->maxalloc < arena->prealloc)
		arena->prealloc = arena->maxalloc;

	arena->used = 0;

	arena->flags = flags;

	if (arena->prealloc) {
		arena->arena = mmap_checked(arena->prealloc,
					    arena->slab_size,
					    arena->flags);
	} else {
		arena->arena = NULL;
	}
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

	/** Need to allocate a new slab. */
	size_t used = __sync_add_and_fetch(&arena->used, arena->slab_size);
	if (used <= arena->prealloc)
		return arena->arena + used - arena->slab_size;

	if (used > arena->maxalloc) {
		__sync_sub_and_fetch(&arena->used, arena->slab_size);
		return NULL;
	}
	return mmap_checked(arena->slab_size, arena->slab_size,
			    arena->flags);
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
