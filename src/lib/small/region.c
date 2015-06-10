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
#include "small/region.h"
#include <sys/types.h> /* ssize_t */

void *
region_reserve_slow(struct region *region, size_t size)
{
	/* The new slab must have at least this many bytes available. */
	size_t slab_min_size = size + rslab_sizeof() - slab_sizeof();

	struct rslab *slab;
	slab = (struct rslab *) slab_get(region->cache, slab_min_size);
	if (slab == NULL)
		return NULL;
	slab->used = 0;
	/*
	 * Sic: add the new slab to the beginning of the
	 * region, even if it is full, otherwise,
	 * region_truncate() won't work.
	 */
	slab_list_add(&region->slabs, &slab->slab, next_in_list);
	return rslab_data(slab);
}

void
region_free(struct region *region)
{
	struct slab *slab, *tmp;
	rlist_foreach_entry_safe(slab, &region->slabs.slabs,
				 next_in_list, tmp)
		slab_put(region->cache, slab);

	slab_list_create(&region->slabs);
}

/**
 * Release all memory down to new_size; new_size has to be previously
 * obtained by calling region_used().
 */
void
region_truncate(struct region *region, size_t used)
{
	ssize_t cut_size = region_used(region) - used;
	assert(cut_size >= 0);

	while (! rlist_empty(&region->slabs.slabs)) {
		struct rslab *slab = rlist_first_entry(&region->slabs.slabs,
						       struct rslab,
						       slab.next_in_list);
		if (slab->used > cut_size) {
			/* This is the last slab to trim. */
			slab->used -= cut_size;
			cut_size = 0;
			break;
		}
		cut_size -= slab->used;
		/* Remove the entire slab. */
		slab_list_del(&region->slabs, &slab->slab, next_in_list);
		slab_put(region->cache, &slab->slab);
	}
	assert(cut_size == 0);
	region->slabs.stats.used = used;
}

void *
region_join_nothrow(struct region *region, size_t size)
{
	if (rlist_empty(&region->slabs.slabs)) {
		assert(size == 0);
		return region_alloc_nothrow(region, 0);
	}
	struct rslab *slab = rlist_first_entry(&region->slabs.slabs,
					       struct rslab,
					       slab.next_in_list);

	if (slab->used >= size) {
		/* Don't move stuff if it's in a single chunk. */
		return (char *) rslab_data(slab) + slab->used - size;
	}
	/**
	 * Use region_reserve() to ensure slab->size is not
	 * changed when the joined region is in the same slab
	 * as the final chunk.
	 */
	char *ptr = region_reserve_nothrow(region, size);
	size_t offset = size;
	if (ptr == NULL)
		return NULL;
	/*
	 * Copy data from last chunk to first, i.e. in the reverse order.
	 */
	while (offset > 0 && slab->used <= offset) {
		memcpy(ptr + offset - slab->used, rslab_data(slab), slab->used);
		offset -= slab->used;
		slab = rlist_next_entry(slab, slab.next_in_list);
	}
	if (offset > 0)
		memcpy(ptr, rslab_data(slab) + slab->used - offset, offset);
	region_alloc_nothrow(region, size);
	return ptr;
}
