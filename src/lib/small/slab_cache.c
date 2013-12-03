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
#include "small/slab_cache.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>

static const uint32_t slab_magic = 0xeec0ffee;

#if !defined(MAP_ANONYMOUS)
/*
 * MAP_ANON is deprecated, MAP_ANONYMOUS should be used instead.
 * Unfortunately, it's not universally present (e.g. not present
 * on FreeBSD.
 */
#define MAP_ANONYMOUS MAP_ANON
#endif /* !defined(MAP_ANONYMOUS) */

/**
 * Given a pointer allocated in a slab, get the handle
 * of the slab itself.
 */
struct slab *
slab_from_ptr(void *ptr, uint8_t order)
{
	assert(order <= SLAB_ORDER_LAST);
	intptr_t addr = (intptr_t) ptr;
	/** All memory mapped slabs are slab->size aligned. */
	struct slab *slab = (struct slab *)
		(addr & ~(slab_order_size(order) - 1));
	assert(slab->magic == slab_magic && slab->order == order);
	return slab;
}

static inline void
slab_assert(struct slab *slab)
{
	(void) slab;
	assert(slab->magic == slab_magic);
	assert(slab->order <= SLAB_HUGE);
	assert(slab->order == SLAB_HUGE ||
	       (((intptr_t) slab & ~(slab_order_size(slab->order) - 1)) ==
		(intptr_t) slab &&
	       slab->size == slab_order_size(slab->order)));
}

/** Mark a slab as free. */
static inline void
slab_set_free(struct slab_cache *cache, struct slab *slab)
{
	assert(slab->in_use == slab->order + 1);		/* Sanity. */
	cache->allocated.stats.used -= slab->size;
	cache->orders[slab->order].stats.used -= slab->size;
	slab->in_use = 0;
}

static inline void
slab_set_used(struct slab_cache *cache, struct slab *slab)
{
	cache->allocated.stats.used += slab->size;
	cache->orders[slab->order].stats.used += slab->size;
	/* Not a boolean to have an extra assert. */
	slab->in_use = 1 + slab->order;
}

static inline bool
slab_is_free(struct slab *slab)
{
	return slab->in_use == 0;
}

static inline void
slab_poison(struct slab *slab)
{
	static const char poison_char = 'P';
	memset((char *) slab + slab_sizeof(), poison_char,
	       slab->size - slab_sizeof());
}

static inline void
slab_create(struct slab *slab, uint8_t order, size_t size)
{
	assert(order <= SLAB_HUGE);
	slab->magic = slab_magic;
	slab->order = order;
	slab->in_use = 0;
	slab->size = size;
}

static inline void
munmap_checked(void *addr, size_t length)
{
	if (munmap(addr, length)) {
		char buf[64];
		strerror_r(errno, buf, sizeof(buf));
		fprintf(stderr, "Error in munmap(): %s\n", buf);
		assert(false);
	}
}

static inline struct slab *
slab_mmap(uint8_t order)
{
	assert(order <= SLAB_ORDER_LAST);

	size_t size = slab_order_size(order);
	/*
	 * mmap twice the requested amount to be able to align
	 * the mapped address.
	 * @todo all mappings except the first are likely to
	 * be aligned already. Find out if trying to map
	 * optimistically exactly the requested amount and fall
	 * back to doulbe-size mapping is a viable strategy.
         */
	void *map = mmap(NULL, 2 * size,
			 PROT_READ | PROT_WRITE, MAP_PRIVATE |
			 MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED)
		return NULL;

	/* Align the mapped address around slab size. */
	size_t offset = (intptr_t) map & (size - 1);

	if (offset != 0) {
		/* Unmap unaligned prefix and postfix. */
		munmap_checked(map, size - offset);
		map += size - offset;
		munmap_checked(map + size, offset);
	} else {
		/* The address is returned aligned. */
		munmap_checked(map + size, size);
	}
	struct slab *slab = map;
	slab_create(slab, order, size);
	return slab;
}

static inline struct slab *
slab_buddy(struct slab *slab)
{
	assert(slab->order <= SLAB_ORDER_LAST);

	if (slab->order == SLAB_ORDER_LAST)
		return NULL;
	/* The buddy address has its respective bit negated. */
	return (void *) ((intptr_t) slab ^ slab_order_size(slab->order));

}

static inline struct slab *
slab_split(struct slab_cache *cache, struct slab *slab)
{
	assert(slab->order > 0);

	uint8_t new_order = slab->order - 1;
	size_t new_size = slab_order_size(new_order);

	slab_create(slab, new_order, new_size);
	struct slab *buddy = slab_buddy(slab);
	slab_create(buddy, new_order, new_size);
	slab_list_add(&cache->orders[buddy->order], buddy, next_in_list);
	return slab;
}

static inline struct slab *
slab_merge(struct slab_cache *cache, struct slab *slab, struct slab *buddy)
{
	assert(slab_buddy(slab) == buddy);
	struct slab *merged = slab > buddy ? buddy : slab;
	/** Remove the buddy from the free list. */
	slab_list_del(&cache->orders[buddy->order], buddy, next_in_list);
	merged->order++;
	merged->size = slab_order_size(merged->order);
	return merged;
}

void
slab_cache_create(struct slab_cache *cache)
{
	for (uint8_t i = 0; i <= SLAB_ORDER_LAST; i++)
		slab_list_create(&cache->orders[i]);
	slab_list_create(&cache->allocated);
}

void
slab_cache_destroy(struct slab_cache *cache)
{
	struct rlist *slabs = &cache->allocated.slabs;
	/*
	 * cache->allocated contains huge allocations and
	 * slabs of the largest order. All smaller slabs are
	 * obtained from larger slabs by splitting.
         */
	struct slab *slab, *tmp;
	rlist_foreach_entry_safe(slab, slabs, next_in_cache, tmp) {
		if (slab->order == SLAB_HUGE)
			free(slab);
		else {
			/*
			 * Don't trust slab->size or slab->order,
			 * it is wrong if the slab header was
			 * reformatted for a smaller order.
		         */
			munmap_checked(slab, slab_order_size(SLAB_ORDER_LAST));
		}
	}
}

struct slab *
slab_get_with_order(struct slab_cache *cache, uint8_t order)
{
	assert(order <= SLAB_ORDER_LAST);
	struct slab *slab;
	/* Search for the first available slab. If a slab
	 * of a bigger size is found, it can be split.
	 * If SLAB_ORDER_LAST is reached and there are no
	 * free slabs, allocate a new one.
	 */
	struct slab_list *list= &cache->orders[order];

	for ( ; rlist_empty(&list->slabs); list++) {
		if (list == cache->orders + SLAB_ORDER_LAST) {
			slab = slab_mmap(SLAB_ORDER_LAST);
			if (slab == NULL)
				return NULL;
			slab_poison(slab);
			slab_list_add(&cache->allocated, slab,
				      next_in_cache);
			slab_list_add(list, slab, next_in_list);
			break;
		}
	}
	slab = rlist_shift_entry(&list->slabs, struct slab, next_in_list);
	if (slab->order != order) {
		/*
		 * Do not "bill" the size of this slab to this
		 * order, to prevent double accounting of the
		 * same memory.
		 */
		list->stats.total -= slab->size;
		/* Get a slab of the right order. */
		do {
			slab = slab_split(cache, slab);
		} while (slab->order != order);
		/*
		 * Count the slab in this order. The buddy is
		 * already taken care of by slab_split.
		 */
		cache->orders[slab->order].stats.total += slab->size;
	}
	slab_set_used(cache, slab);
	slab_assert(slab);
	return slab;
}

/**
 * Try to find a region of the requested order
 * in the cache. On failure, mmap() a new region,
 * optionally split it into a series of half.
 * Returns a next-power-of-two(size) aligned address
 * for all sizes below SLAB_SIZE_MAX.
 */
struct slab *
slab_get(struct slab_cache *cache, size_t size)
{
	size += slab_sizeof();
	uint8_t order = slab_order(size);

	if (order == SLAB_HUGE) {
		struct slab *slab = (struct slab *) malloc(size);
		if (slab == NULL)
			return NULL;
		slab_create(slab, order, size);
		slab_list_add(&cache->allocated, slab, next_in_cache);
		cache->allocated.stats.used += size;
		return slab;
	}
	return slab_get_with_order(cache, order);
}

/** Return a slab back to the slab cache. */
void
slab_put(struct slab_cache *cache, struct slab *slab)
{
	slab_assert(slab);
	if (slab->order == SLAB_HUGE) {
		/*
		 * Free a huge slab right away, we have no
		 * further business to do with it.
		 */
		slab_list_del(&cache->allocated, slab, next_in_cache);
		cache->allocated.stats.used -= slab->size;
		free(slab);
		return;
	}
	/* An "ordered" slab is returned to the cache. */
	slab_set_free(cache, slab);
	struct slab *buddy = slab_buddy(slab);
	/*
	 * The buddy slab could also have been split into a pair
	 * of smaller slabs, the first of which happens to be
	 * free. To not merge with a slab which is in fact
	 * partially occupied, first check that slab orders match.
	 *
	 * A slab is not accounted in "used" or "total" counters
	 * if it was split into slabs of a lower order.
	 * cache->orders statistics only contains sizes of either
	 * slabs returned by slab_get, or present in the free
	 * list. This ensures that sums of cache->orders[i].stats
	 * match the totals in cache->allocated.stats.
	 */
	if (buddy && buddy->order == slab->order && slab_is_free(buddy)) {
		cache->orders[slab->order].stats.total -= slab->size;
		do {
			slab = slab_merge(cache, slab, buddy);
			buddy = slab_buddy(slab);
		} while (buddy && buddy->order == slab->order &&
			 slab_is_free(buddy));
		cache->orders[slab->order].stats.total += slab->size;
	}
	slab_poison(slab);
	rlist_add_entry(&cache->orders[slab->order].slabs, slab,
			next_in_list);
}

void
slab_cache_check(struct slab_cache *cache)
{
	size_t total = 0;
	size_t used = 0;
	size_t ordered = 0;
	size_t huge = 0;
	bool dont_panic = true;

	struct rlist *slabs = &cache->allocated.slabs;
	struct slab *slab;

	rlist_foreach_entry(slab, slabs, next_in_cache) {
		if (slab->magic != slab_magic) {
			fprintf(stderr, "%s: incorrect slab magic,"
				" expected %d, got %d", __func__,
				slab_magic, slab->magic);
			dont_panic = false;
		}
		if (slab->order == SLAB_HUGE) {
			huge += slab->size;
			used += slab->size;
			total += slab->size;
		} else {
			if (slab->size != slab_order_size(slab->order)) {
				fprintf(stderr, "%s: incorrect slab size,"
					" expected %zu, got %zu", __func__,
					slab_order_size(slab->order),
					slab->size);
				dont_panic = false;
			}
			/*
			 * The slab may have been reformatted
			 * and split into smaller slabs, don't
			 * trust slab->size.
			 */
			total += slab_order_size(SLAB_ORDER_LAST);
		}
	}

	if (total != cache->allocated.stats.total) {
		fprintf(stderr, "%s: incorrect slab statistics, total %zu,"
			" factual %zu\n", __func__,
			cache->allocated.stats.total,
			total);
		dont_panic = false;
	}

	for (struct slab_list *list = cache->orders;
	     list <= cache->orders + SLAB_ORDER_LAST;
	     list++) {

		uint8_t order = slab_order_size(list - cache->orders);
		ordered += list->stats.total;
	        used += list->stats.used;

		if (list->stats.total % slab_order_size(order)) {
			fprintf(stderr, "%s: incorrect order statistics, the"
				" total %zu is not multiple of slab size %zu\n",
				__func__, list->stats.total,
				slab_order_size(order));
			dont_panic = false;
		}
		if (list->stats.used % slab_order_size(order)) {
			fprintf(stderr, "%s: incorrect order statistics, the"
				" used %zu is not multiple of slab size %zu\n",
				__func__, list->stats.used,
				slab_order_size(order));
			dont_panic = false;
		}
	}

	if (ordered + huge != total) {
		fprintf(stderr, "%s: incorrect totals, ordered %zu, "
			" huge %zu, total %zu\n", __func__,
			ordered, huge, total);
		dont_panic = false;
	}
	if (used != cache->allocated.stats.used) {
		fprintf(stderr, "%s: incorrect used total, "
			"total %zu, sum %zu\n", __func__,
			cache->allocated.stats.used,
			used);
		dont_panic = false;
	}
	if (dont_panic)
		return;
	abort();
}
