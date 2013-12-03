#ifndef INCLUDES_TARANTOOL_SMALL_SLAB_CACHE_H
#define INCLUDES_TARANTOOL_SMALL_SLAB_CACHE_H
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
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <assert.h>
#include "rlist.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	/*
	 * Slabs of "order" from 0 to 8 have size which is a power
	 * of 2. They are obtained either using mmap(), or by
	 * splitting an mmapped() slab of higher order (buddy
	 * system).  Memory address of such slab is aligned to
	 * slab size.
	 */
	SLAB_ORDER_LAST = 10,
	/*
	 * The last "order" contains huge slabs, allocated with
	 * malloc(). This order is provided to make life of
	 * slab_cache user easier, so that one doesn't have to
	 * worry about allocation sizes larger than SLAB_MAX_SIZE.
	 */
	SLAB_HUGE = SLAB_ORDER_LAST + 1,
	/** Binary logarithm of SLAB_MIN_SIZE. */
	SLAB_MIN_SIZE_LB = 12,
	/** Minimal size of an ordered slab, 4096 */
	SLAB_MIN_SIZE = 1 << SLAB_MIN_SIZE_LB,
	/** Maximal size of an ordered slab, 1M */
	SLAB_MAX_SIZE = SLAB_MIN_SIZE << SLAB_ORDER_LAST
};

struct slab {
	/*
	 * Next slab in the list of allocated slabs. Unused if
	 * this slab has a buddy. Sic: if a slab is not allocated
	 * but is made by a split of a larger (allocated) slab,
	 * this member got to be left intact, to not corrupt
	 * cache->allocated list.
	 */
	struct rlist next_in_cache;
	/** Next slab in slab_list->slabs list. */
	struct rlist next_in_list;
	/**
	 * Allocated size.
	 * Is different from (SLAB_MIN_SIZE << slab->order)
	 * when requested size is bigger than SLAB_MAX_SIZE
	 * (i.e. slab->order is SLAB_CLASS_LAST).
	 */
	size_t size;
	/** Slab magic (for sanity checks). */
	uint32_t magic;
	/** Base of lb(size) for ordered slabs. */
	uint8_t order;
	/**
	 * Only used for buddy slabs. If the buddy of the current
	 * free slab is also free, both slabs are merged and
	 * a free slab of the higher order emerges.
	 */
	uint8_t in_use;
};

/** Allocation statistics. */
struct small_stats {
	size_t used;
	size_t total;
};

static inline void
small_stats_reset(struct small_stats *stats)
{
	stats->used = stats->total = 0;
}

/**
 * A general purpose list of slabs. Is used
 * to store unused slabs of a certain order in the
 * slab cache, as well as to contain allocated
 * slabs of a specialized allocator.
 */
struct slab_list {
	struct rlist slabs;
	/** Total/used bytes in this list. */
	struct small_stats stats;
};

#define slab_list_add(list, slab, member)		\
do {							\
	rlist_add_entry(&(list)->slabs, (slab), member);\
	(list)->stats.total += (slab)->size;		\
} while (0)

#define slab_list_del(list, slab, member)		\
do {							\
	rlist_del_entry((slab), member);                \
	(list)->stats.total -= (slab)->size;		\
} while (0)

static inline void
slab_list_create(struct slab_list *list)
{
	rlist_create(&list->slabs);
	small_stats_reset(&list->stats);
}

struct slab_cache {
	/**
	 * Slabs are ordered by size, which is a multiple of two.
	 * orders[0] contains slabs of size SLAB_MIN_SIZE
	 * (order 0). orders[1] contains slabs of
	 * 2 * SLAB_MIN_SIZE, and so on. The list only contains
	 * unused slabs - a used slab is removed from the
	 * slab_cache list and its next_in_list link may
	 * be reused for some other purpose.
	 * Note, that SLAB_HUGE slabs are not accounted
	 * here, since they are never reused.
         */
	struct slab_list orders[SLAB_ORDER_LAST + 1];
	/** All allocated slabs used in the cache.
	 * The stats reflect the total used/allocated
	 * memory in the cache.
	 */
	struct slab_list allocated;
};

void
slab_cache_create(struct slab_cache *cache);

void
slab_cache_destroy(struct slab_cache *cache);

struct slab *
slab_get(struct slab_cache *cache, size_t size);

struct slab *
slab_get_with_order(struct slab_cache *cache, uint8_t order);

void
slab_put(struct slab_cache *cache, struct slab *slab);

struct slab *
slab_from_ptr(void *ptr, uint8_t order);

/** Align a size. Alignment must be a power of 2 */
static inline size_t
slab_size_align(size_t size, size_t alignment)
{
	return (size + alignment - 1) & ~(alignment - 1);
}

/* Aligned size of slab meta. */
static inline size_t
slab_sizeof()
{
	return slab_size_align(sizeof(struct slab), sizeof(intptr_t));
}

static inline size_t
slab_size(struct slab *slab)
{
	return slab->size - slab_sizeof();
}

void
slab_cache_check(struct slab_cache *cache);

/**
 * Find the nearest power of 2 size capable of containing
 * a chunk of the given size. Adjust for SLAB_MIN_SIZE and
 * SLAB_MAX_SIZE.
 */
static inline uint8_t
slab_order(size_t size)
{
	assert(size <= UINT32_MAX);
	if (size <= SLAB_MIN_SIZE)
		return 0;
	if (size > SLAB_MAX_SIZE)
		return SLAB_HUGE;

	return (uint8_t) (CHAR_BIT * sizeof(uint32_t) -
			  __builtin_clz((uint32_t) size - 1) -
			  SLAB_MIN_SIZE_LB);
}

/** Convert slab order to the mmap()ed size. */
static inline intptr_t
slab_order_size(uint8_t order)
{
	assert(order <= SLAB_ORDER_LAST);
	return 1 << (order + SLAB_MIN_SIZE_LB);
}


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* INCLUDES_TARANTOOL_SMALL_SLAB_CACHE_H */
