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
#include "small/mempool.h"
#include <stdlib.h>
#include <string.h>
#include "small/slab_cache.h"

static inline int
mslab_cmp(struct mslab *lhs, struct mslab *rhs)
{
	/* pointer arithmetics may overflow int * range. */
	return lhs > rhs ? 1 : (lhs < rhs ? -1 : 0);
}


rb_proto(, mslab_tree_, mslab_tree_t, struct mslab)

rb_gen(, mslab_tree_, mslab_tree_t, struct mslab, node, mslab_cmp)

static inline void
mslab_create(struct mslab *slab, struct mempool *pool)
{
	slab->nfree = pool->objcount;
	slab->pool = pool;
	slab->free_idx = 0;
	slab->free_list = 0;
}

/** Beginning of object data in the slab. */
static inline void *
mslab_offset(struct mslab *slab)
{
	return (char *) slab + slab->pool->objoffset;
}

/** Pointer to an object from object index. */
static inline void *
mslab_obj(struct mslab *slab, uint32_t idx)
{
	return mslab_offset(slab) + idx * slab->pool->objsize;
}

void *
mslab_alloc(struct mslab *slab)
{
	assert(slab->nfree);
	void *result;
	if (slab->free_list) {
		/* Recycle an object from the garbage pool. */
		result = slab->free_list;
		slab->free_list = *(void **)slab->free_list;
	} else {
		/* Use an object from the "untouched" area of the slab. */
		result = mslab_obj(slab, slab->free_idx++);
	}

	/* If the slab is full, remove it from the rb tree. */
	if (--slab->nfree == 0)
		mslab_tree_remove(&slab->pool->free_slabs, slab);

	return result;
}

void
mslab_free(struct mempool *pool, struct mslab *slab, void *ptr)
{
	/* put object to garbage list */
	*(void **)ptr = slab->free_list;
	slab->free_list = ptr;

	slab->nfree++;

	if (slab->nfree == 1) {
		/**
		 * Add this slab to the rbtree which contains partially
		 * populated slabs.
		 */
		mslab_tree_insert(&pool->free_slabs, slab);
	} else if (slab->nfree == pool->objcount) {
		/** Free the slab. */
		mslab_tree_remove(&pool->free_slabs, slab);
		if (pool->spare > slab) {
			slab_list_del(&pool->slabs, &pool->spare->slab,
				      next_in_list);
			slab_put(pool->cache, &pool->spare->slab);
			pool->spare = slab;
		 } else if (pool->spare) {
			 slab_list_del(&pool->slabs, &slab->slab,
				       next_in_list);
			 slab_put(pool->cache, &slab->slab);
		 } else {
			 pool->spare = slab;
		 }
	}
}

void
mempool_create_with_order(struct mempool *pool, struct slab_cache *cache,
			  uint32_t objsize, uint8_t order)
{
	assert(order <= cache->order_max);
	lifo_init(&pool->link);
	lifo_init(&pool->delayed);
	pool->cache = cache;
	slab_list_create(&pool->slabs);
	mslab_tree_new(&pool->free_slabs);
	pool->spare = NULL;
	pool->objsize = objsize;
	pool->slab_order = order;
	/* Total size of slab */
	uint32_t slab_size = slab_order_size(pool->cache, pool->slab_order);
	/* Calculate how many objects will actually fit in a slab. */
	pool->objcount = (slab_size - mslab_sizeof()) / objsize;
	assert(pool->objcount);
	pool->objoffset = slab_size - pool->objcount * pool->objsize;
}

void
mempool_destroy(struct mempool *pool)
{
	struct slab *slab, *tmp;
	rlist_foreach_entry_safe(slab, &pool->slabs.slabs,
				 next_in_list, tmp)
		slab_put(pool->cache, slab);
}

void *
mempool_alloc_nothrow(struct mempool *pool)
{
	struct mslab *slab = mslab_tree_first(&pool->free_slabs);
	if (slab == NULL) {
		if (pool->spare == NULL) {
			slab = (struct mslab *)
				slab_get_with_order(pool->cache,
						    pool->slab_order);
			if (slab == NULL)
				return NULL;
			mslab_create(slab, pool);
			slab_list_add(&pool->slabs, &slab->slab,
				      next_in_list);
		} else {
			slab = pool->spare;
			pool->spare = NULL;
		}
		mslab_tree_insert(&pool->free_slabs, slab);
	}
	assert(slab->pool == pool);
	pool->slabs.stats.used += pool->objsize;
	return mslab_alloc(slab);
}

void
mempool_stats(struct mempool *pool, struct mempool_stats *stats)
{
	/* Object size. */
	stats->objsize = pool->objsize;
	/* Number of objects. */
	stats->objcount = pool->slabs.stats.used/pool->objsize;
	/* Size of the slab. */
	stats->slabsize = slab_order_size(pool->cache, pool->slab_order);
	/* The number of slabs. */
	stats->slabcount = pool->slabs.stats.total/stats->slabsize;
	/* How much memory is used for slabs. */
	stats->totals.used = pool->slabs.stats.used;
	/*
	 * How much memory is available. Subtract the slab size,
	 * which is allocation overhead and is not available
	 * memory.
	 */
	stats->totals.total = pool->slabs.stats.total -
		mslab_sizeof() * stats->slabcount;
}
