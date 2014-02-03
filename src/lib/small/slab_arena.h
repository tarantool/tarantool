#ifndef INCLUDES_TARANTOOL_SMALL_SLAB_ARENA_H
#define INCLUDES_TARANTOOL_SMALL_SLAB_ARENA_H
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
#include "small/lf_lifo.h"
#include <sys/mman.h>
#include <limits.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum { SLAB_MIN_SIZE = USHRT_MAX,
       SMALL_UNLIMITED = SIZE_MAX/2 + 1};

/**
 * slab_arena -- a source of large aligned blocks of memory.
 * MT-safe.
 * Uses a lock-free LIFO to maintain a cache of used slabs.
 * Never returns memory to the operating system.
 */
struct slab_arena {
	/**
	 * A lock free list of cached slabs.
	 * Initially there are no cached slabs, only arena.
	 * As slabs are used and returned to arena, the cache is
	 * used to recycle them.
	 */
	struct lf_lifo cache;
	/** A preallocated arena of size = prealloc. */
	void *arena;
	/*
	 * Each object returned by arena_map() has this size.
	 * The size is provided at arena initialization.
	 * It must be a power of 2 and large enough
	 * (at least 64kb, since the two lower bytes are
	 * used for ABA counter in the lock-free list).
	 * Returned pointers are always aligned by this size.
	 *
	 * It's important to keep this value moderate to
	 * limit the overhead of partially populated slabs.
	 * It is still necessary, however, to make it settable,
	 * to allow allocation of large objects.
	 * Typical value is 4Mb, which makes it possible to
	 * allocate objects of size up to ~1MB.
	 */
	uint32_t slab_size;
	/**
	 * How much memory is preallocated during initialization
	 * of slab_arena.
	 */
	size_t prealloc;
	/**
	 * How much above 'prealloc' size we can go after
	 * the arena is exhausted (arena_used == prealloc).
	 * Each new slab is allocated by a dedicated mmap()
	 * call. When returned, slabs allocated this way
	 * are munmap()-ed right away.
	 */
	size_t maxalloc;
	/**
	 * How much memory in the preallocated arena has
	 * already been initialized for slabs.
	 * @invariant arena_used <= prealloc.
	 */
	size_t used;
	/**
	 * mmap() flags: MAP_SHARED or MAP_PRIVATE
	 */
	int flags;
};

/** Initialize an arena.  */
int
slab_arena_create(struct slab_arena *arena, size_t prealloc,
		  size_t maxalloc, uint32_t slab_size, int flags);

/** Destroy an arena. */
void
slab_arena_destroy(struct slab_arena *arena);

/** Get a slab. */
void *
slab_map(struct slab_arena *arena);

/** Put a slab into cache. */
void
slab_unmap(struct slab_arena *arena, void *ptr);

/** mprotect() the preallocated arena. */
void
slab_arena_mprotect(struct slab_arena *arena);

/** Align a size. Alignment must be a power of 2 */
static inline size_t
small_align(size_t size, size_t alignment)
{
	return (size + alignment - 1) & ~(alignment - 1);
}

/** Round a number to the nearest power of two. */
static inline size_t
small_round(size_t size)
{
	if (size < 2)
		return size;
	assert(size <= SMALL_UNLIMITED);
	size_t r = 1;
	return r << (sizeof(unsigned long) * CHAR_BIT -
		     __builtin_clzl((unsigned long) (size - 1)));
}

/** Binary logarithm of a size. */
static inline size_t
small_lb(size_t size)
{
	assert(size <= SMALL_UNLIMITED);
	return sizeof(unsigned long) * CHAR_BIT -
		__builtin_clzl((unsigned long) size) - 1;
}


#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* INCLUDES_TARANTOOL_SMALL_SLAB_ARENA_H */
