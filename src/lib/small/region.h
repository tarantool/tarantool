#ifndef INCLUDES_TARANTOOL_SMALL_REGION_H
#define INCLUDES_TARANTOOL_SMALL_REGION_H
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
#include <stddef.h>
#include <inttypes.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "salad/rlist.h"
#include "slab_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Region allocator.
 *
 * Good for allocating objects of any size, as long as
 * all of them can be freed at once. Keeps a list of
 * order-of-page-size memory blocks, thus has no external
 * fragmentation. Does have a fair bit of internal fragmentation,
 * but only if average allocation size is close to the block size.
 * Therefore is ideal for a ton of small allocations of different
 * sizes.
 *
 * Under the hood, the allocator uses a page cache of
 * mmap()-allocated pages. Pages of the page cache are never
 * released back to the operating system.
 *
 * Thread-safety
 * -------------
 * @todo, not thread safe ATM
 *
 * Errors
 * ----------------
 * The only type of failure which can occur is a failure to
 * allocate memory. alloc() calls return NULL in this case.
 */

/** A memory region.
 *
 * A memory region is a list of memory blocks.
 *
 * It's possible to allocate a chunk of any size
 * from a region.
 * It's not possible, however, to free a single allocated
 * piece, all memory must be freed at once with region_reset() or
 * region_free().
 */

enum { REGION_NAME_MAX = 30 };

struct region
{
	struct slab_cache *cache;
	struct slab_list slabs;
	char name[REGION_NAME_MAX];
};

/**
 * Initialize a memory region.
 * @sa region_free().
 */
static inline void
region_create(struct region *region, struct slab_cache *cache)
{
	region->cache = cache;
	slab_list_create(&region->slabs);
	region->name[0] = '\0';
}

/**
 * Free all allocated objects and release the allocated
 * blocks.
 */
void
region_free(struct region *region);

static inline void
region_destroy(struct region *region)
{
	return region_free(region);
}

/** Internal: a single block in a region.  */
struct rslab
{
	/*
	 * slab is a wrapper around struct slab - with a few
	 * extra members.
	 */
	struct slab slab;
	uint32_t used;
};

static inline uint32_t
rslab_sizeof()
{
	return small_align(sizeof(struct rslab), sizeof(intptr_t));
}

static inline void *
rslab_data(struct rslab *slab)
{
	return (char *) slab + rslab_sizeof();
}

/** How much memory is available in a given block? */
static inline uint32_t
rslab_unused(struct rslab *slab)
{
	return slab->slab.size - rslab_sizeof() - slab->used;
}

void *
region_reserve_slow(struct region *region, size_t size);

static inline void *
region_reserve_nothrow(struct region *region, size_t size)
{
	if (! rlist_empty(&region->slabs.slabs)) {
		struct rslab *slab = rlist_first_entry(&region->slabs.slabs,
						       struct rslab,
						       slab.next_in_list);
		if (size <= rslab_unused(slab))
			return (char *) rslab_data(slab) + slab->used;
	}
	return region_reserve_slow(region, size);
}

/** Allocate size bytes from a region. */
static inline void *
region_alloc_nothrow(struct region *region, size_t size)
{
	void *ptr = region_reserve_nothrow(region, size);
	if (ptr != NULL) {
		struct rslab *slab = rlist_first_entry(&region->slabs.slabs,
						       struct rslab,
						       slab.next_in_list);
		assert(size <= rslab_unused(slab));

		region->slabs.stats.used += size;
		slab->used += size;
	}
	return ptr;
}

/**
 * Mark region as empty, but keep the blocks.
 */
static inline void
region_reset(struct region *region)
{
	if (! rlist_empty(&region->slabs.slabs)) {
		struct rslab *slab = rlist_first_entry(&region->slabs.slabs,
						       struct rslab,
						       slab.next_in_list);
		region->slabs.stats.used -= slab->used;
		slab->used = 0;
	}
}

/** How much memory is used by this region. */
static inline size_t
region_used(struct region *region)
{
	return region->slabs.stats.used;
}

/** Return size bytes allocated last as a single chunk. */
void *
region_join_nothrow(struct region *region, size_t size);

/** How much memory is held by this region. */
static inline size_t
region_total(struct region *region)
{
	return region->slabs.stats.total;
}

static inline void
region_free_after(struct region *region, size_t after)
{
	if (region_used(region) > after)
		region_free(region);
}

/** Truncate the region to the given size */
void
region_truncate(struct region *pool, size_t size);

static inline void
region_set_name(struct region *region, const char *name)
{
	snprintf(region->name, sizeof(region->name), "%s", name);
}

static inline const char *
region_name(struct region *region)
{
	return region->name;
}

#if defined(__cplusplus)
} /* extern "C" */
#include "exception.h"

static inline void *
region_alloc(struct region *region, size_t size)
{
	void *ptr = region_alloc_nothrow(region, size);
	if (ptr == NULL)
		tnt_raise(OutOfMemory, size, "region", "new slab");
	return ptr;
}

static inline void *
region_reserve(struct region *region, size_t size)
{
	void *ptr = region_reserve_nothrow(region, size);
	if (ptr == NULL)
		tnt_raise(OutOfMemory, size, "region", "new slab");
	return ptr;
}

static inline void *
region_join(struct region *region, size_t size)
{
	void *ptr = region_join_nothrow(region, size);
	if (ptr == NULL)
		tnt_raise(OutOfMemory, size, "region", "join");
	return ptr;
}

static inline void *
region_alloc0(struct region *region, size_t size)
{
	return memset(region_alloc(region, size), 0, size);
}

static inline void
region_dup(struct region *region, const void *ptr, size_t size)
{
	(void) memcpy(region_alloc(region, size), ptr, size);
}

extern "C" {
static inline void *
region_alloc_cb(void *ctx, size_t size)
{
	return region_alloc((struct region *) ctx, size);
}

static inline void *
region_reserve_cb(void *ctx, size_t *size)
{
	struct region *region = (struct region *) ctx;
	void *ptr = region_reserve(region, *size);
	struct rslab *slab = rlist_first_entry(&region->slabs.slabs,
					       struct rslab,
					       slab.next_in_list);
	*size = rslab_unused(slab);
	return ptr;
}

} /* extern "C" */

struct RegionGuard {
	struct region *region;
	size_t used;

	RegionGuard(struct region *region_arg)
		: region(region_arg),
		  used(region_used(region_arg))
        {
		/* nothing */
	}

	~RegionGuard() {
		region_truncate(region, used);
	}
};
#endif

#endif /* INCLUDES_TARANTOOL_SMALL_REGION_H */
