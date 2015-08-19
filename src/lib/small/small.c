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
#include "small/small.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum {
	/** Step size for stepped pools, in bytes */
	STEP_SIZE = 8,
	/**
	 * LB stands for logarithm with binary base, this constant
	 * is used for bit shifts, when we need to divide by
	 * STEP_SIZE.
	 */
	STEP_SIZE_LB = 3,
};

rb_proto(, factor_tree_, factor_tree_t, struct factor_pool)

/** Used for search in the tree. */
static inline int
factor_pool_cmp(struct factor_pool *a, struct factor_pool *b)
{
	return a->pool.objsize > b->pool.objsize ? 1 :
		a->pool.objsize < b->pool.objsize ? -1 : 0;
}

rb_gen(, factor_tree_, factor_tree_t, struct factor_pool, node,
       factor_pool_cmp)

static inline struct factor_pool *
factor_pool_create(struct small_alloc *alloc,
		   struct factor_pool *upper_bound,
		   size_t size)
{
	assert(size > alloc->step_pool_objsize_max);
	assert(size <= alloc->objsize_max);

	if (alloc->factor_pool_next == NULL) {
		/**
		 * Too many factored pools already, fall back
		 * to an imperfect one.
		 */
		return upper_bound;
	}
	size_t objsize = alloc->step_pool_objsize_max;
	size_t prevsize;
	do {
		prevsize = objsize;
		/*
		 * Align objsize after each multiplication to
		 * ensure that the distance between objsizes of
		 * factored pools is a multiple of STEP_SIZE.
		 */
		objsize = small_align(objsize * alloc->factor,
				      sizeof(intptr_t));
		assert(objsize > alloc->step_pool_objsize_max);
	} while (objsize < size);
	if (objsize > alloc->objsize_max)
		objsize = alloc->objsize_max;
	struct factor_pool *pool = alloc->factor_pool_next;
	alloc->factor_pool_next = pool->next;
	mempool_create(&pool->pool, alloc->cache, objsize);
	pool->objsize_min = prevsize + 1;
	factor_tree_insert(&alloc->factor_pools, pool);
	return pool;
}

/** Initialize the small allocator. */
void
small_alloc_create(struct small_alloc *alloc, struct slab_cache *cache,
		   uint32_t objsize_min, float alloc_factor)
{
	alloc->cache = cache;
	/* Align sizes. */
	objsize_min = small_align(objsize_min, STEP_SIZE);
	/* Make sure at least 4 largest objects can fit in a slab. */
	alloc->objsize_max =
		mempool_objsize_max(slab_order_size(cache, cache->order_max));
	assert(alloc->objsize_max > objsize_min + STEP_POOL_MAX * STEP_SIZE);

	struct mempool *step_pool;
	for (step_pool = alloc->step_pools;
	     step_pool < alloc->step_pools + STEP_POOL_MAX;
	     step_pool++) {
		mempool_create(step_pool, alloc->cache, objsize_min);
		objsize_min += STEP_SIZE;
	}
	alloc->step_pool_objsize_max = (step_pool - 1)->objsize;
	if (alloc_factor > 2.0)
		alloc_factor = 2.0;
	/*
	 * Correct the user-supplied alloc_factor to ensure that
	 * it actually produces growing object sizes.
	 */
	if (alloc->step_pool_objsize_max * alloc_factor <
	    alloc->step_pool_objsize_max + STEP_SIZE) {

		alloc_factor =
			(alloc->step_pool_objsize_max + STEP_SIZE + 0.5)/
			alloc->step_pool_objsize_max;
	}
	alloc->factor = alloc_factor;

	/* Initialize the factored pool cache. */
	struct factor_pool *factor_pool = alloc->factor_pool_cache;
	do {
		factor_pool->next = factor_pool + 1;
		factor_pool++;
	} while (factor_pool !=
		 alloc->factor_pool_cache + FACTOR_POOL_MAX - 1);
	factor_pool->next = NULL;
	alloc->factor_pool_next = alloc->factor_pool_cache;
	factor_tree_new(&alloc->factor_pools);
	(void) factor_pool_create(alloc, NULL, alloc->objsize_max);

	lifo_init(&alloc->delayed);
	alloc->is_delayed_free_mode = false;
}

void
small_alloc_setopt(struct small_alloc *alloc, enum small_opt opt, bool val)
{
	switch (opt) {
	case SMALL_DELAYED_FREE_MODE:
		alloc->is_delayed_free_mode = val;
		break;
	default:
		assert(false);
		break;
	}
}

static inline void
smfree_batch(struct small_alloc *alloc)
{
	if (alloc->is_delayed_free_mode || lifo_is_empty(&alloc->delayed))
		return;

	const int BATCH = 100;
	struct mempool *pool = lifo_peek(&alloc->delayed);

	for (int i = 0; i < BATCH; i++) {
		void *item = lifo_pop(&pool->delayed);
		if (item == NULL) {
			(void) lifo_pop(&alloc->delayed);
			pool = lifo_peek(&alloc->delayed);
			if (pool == NULL)
				break;
			continue;
		}
		mempool_free(pool, item);
	}
}


/**
 * Allocate a small object.
 *
 * Find or create a mempool instance of the right size,
 * and allocate the object on the pool.
 *
 * If object is small enough to fit a stepped pool,
 * finding the right pool for it is just a matter of bit
 * shifts. Otherwise, look up a pool in the red-black
 * factored pool tree.
 *
 * @retval ptr success
 * @retval NULL out of memory
 */
void *
smalloc_nothrow(struct small_alloc *alloc, size_t size)
{
	smfree_batch(alloc);

	struct mempool *pool;
	if (size <= alloc->step_pool_objsize_max) {
		/* Allocate in a stepped pool. */
		int idx;
		if (size <= alloc->step_pools[0].objsize)
			idx = 0;
		else {
			idx = (size - alloc->step_pools[0].objsize
			       + STEP_SIZE - 1) >> STEP_SIZE_LB;
		}
		pool = &alloc->step_pools[idx];
		assert(size <= pool->objsize &&
		       (size + STEP_SIZE > pool->objsize || idx == 0));
	} else {
		struct factor_pool pattern;
		pattern.pool.objsize = size;
		struct factor_pool *upper_bound =
			factor_tree_nsearch(&alloc->factor_pools, &pattern);
		if (upper_bound == NULL)
			return NULL; /* The requested size is too large. */

		if (size < upper_bound->objsize_min)
			upper_bound = factor_pool_create(alloc, upper_bound,
							 size);
		pool = &upper_bound->pool;
	}
	assert(size <= pool->objsize);
	return mempool_alloc_nothrow(pool);
}

static void
small_recycle_pool(struct small_alloc *alloc, struct mempool *pool)
{
	if (mempool_used(pool) == 0 &&
	    pool->objsize > alloc->step_pool_objsize_max &&
	    alloc->factor_pool_next == NULL) {
		struct factor_pool *factor_pool = (struct factor_pool *)
			((char *) pool - (intptr_t)
			 &((struct factor_pool *) NULL)->pool);
		factor_tree_remove(&alloc->factor_pools, factor_pool);
		mempool_destroy(pool);
		alloc->factor_pool_next = factor_pool;
	}
}

static inline struct mempool *
mempool_find(struct small_alloc *alloc, size_t size)
{
	struct mempool *pool;
	if (size <= alloc->step_pool_objsize_max) {
		/* Allocated in a stepped pool. */
		if (size <= alloc->step_pools[0].objsize) {
			pool = &alloc->step_pools[0];
		} else {
			int idx = (size - alloc->step_pools[0].objsize
				   + STEP_SIZE - 1) >> STEP_SIZE_LB;
			pool = &alloc->step_pools[idx];
			assert(size + STEP_SIZE > pool->objsize);
		}
	} else {
		/* Allocated in a factor pool. */
		struct factor_pool pattern;
		pattern.pool.objsize = size;
		struct factor_pool *upper_bound =
			factor_tree_nsearch(&alloc->factor_pools, &pattern);
		assert(upper_bound != NULL);
		assert(size >= upper_bound->objsize_min);
		pool = &upper_bound->pool;
	}
	assert(size <= pool->objsize);
	return pool;
}

/** Free memory chunk allocated by the small allocator. */
/**
 * Free a small object.
 *
 * This boils down to finding the object's mempool and delegating
 * to mempool_free().
 *
 * If the pool becomes completely empty, and it's a factored pool,
 * and the factored pool's cache is empty, put back the empty
 * factored pool into the factored pool cache.
 */
void
smfree(struct small_alloc *alloc, void *ptr, size_t size)
{
	struct mempool *pool = mempool_find(alloc, size);
	mempool_free(pool, ptr);

	if (mempool_used(pool) == 0)
		small_recycle_pool(alloc, pool);
}

/**
 * Free memory chunk allocated by the small allocator
 * if not in snapshot mode, otherwise put to the delayed
 * free list.
 */
void
smfree_delayed(struct small_alloc *alloc, void *ptr, size_t size)
{
	if (alloc->is_delayed_free_mode && ptr) {
		struct mempool *pool = mempool_find(alloc, size);
		if (lifo_is_empty(&pool->delayed))
			lifo_push(&alloc->delayed, &pool->link);
		lifo_push(&pool->delayed, ptr);
	} else {
		smfree(alloc, ptr, size);
	}
}

/** Simplify iteration over small allocator mempools. */
struct mempool_iterator
{
	struct small_alloc *alloc;
	struct mempool *step_pool;
	struct factor_pool *factor_pool;
};

void
mempool_iterator_create(struct mempool_iterator *it,
			struct small_alloc *alloc)
{
	it->alloc = alloc;
	it->step_pool = alloc->step_pools;
	it->factor_pool = factor_tree_first(&alloc->factor_pools);
}

struct mempool *
mempool_iterator_next(struct mempool_iterator *it)
{
	if (it->step_pool < it->alloc->step_pools + STEP_POOL_MAX)
		return it->step_pool++;

	if (it->factor_pool) {
		struct mempool *pool = &it->factor_pool->pool;
		it->factor_pool = factor_tree_next(&it->alloc->factor_pools,
						   it->factor_pool);
		return pool;
	}
	return NULL;
}

/** Destroy all pools. */
void
small_alloc_destroy(struct small_alloc *alloc)
{
	struct mempool_iterator it;
	mempool_iterator_create(&it, alloc);
	struct mempool *pool;

	while ((pool = mempool_iterator_next(&it)))
		mempool_destroy(pool);
}

/** Calculate allocation statistics. */
void
small_stats(struct small_alloc *alloc,
	    struct small_stats *totals,
	    mempool_stats_cb cb, void *cb_ctx)
{
	memset(totals, 0, sizeof(*totals));

	struct mempool_iterator it;
	mempool_iterator_create(&it, alloc);
	struct mempool *pool;

	while ((pool = mempool_iterator_next(&it))) {
		struct mempool_stats stats;
		mempool_stats(pool, &stats);
		totals->used += stats.totals.used;
		totals->total += stats.totals.total;
		if (cb(&stats, cb_ctx))
			break;
	}
}
