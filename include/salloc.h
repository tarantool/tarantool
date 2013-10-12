#ifndef TARANTOOL_SALLOC_H_INCLUDED
#define TARANTOOL_SALLOC_H_INCLUDED
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
#include <stddef.h>
#include <stdbool.h>
#include "tarantool/util.h" /* for uint64_t */

struct tbuf;

bool salloc_init(size_t size, size_t minimal, double factor);
void salloc_free(void);
void *salloc(size_t size, const char *what);
void sfree(void *ptr);
void sfree_delayed(void *ptr);
void slab_validate();
void salloc_protect(void);

/** Statistics on utilization of a single slab class. */
struct slab_cache_stats {
	int64_t item_size;
	int64_t slabs;
	int64_t items;
	int64_t bytes_used;
	int64_t bytes_free;
};

/** Statistics on utilization of the slab allocator. */
struct slab_arena_stats {
	size_t size;
	size_t used;
};

typedef int (*salloc_stat_cb)(const struct slab_cache_stats *st, void *ctx);

int
salloc_stat(salloc_stat_cb cb, struct slab_arena_stats *astat, void *cb_ctx);

/**
 * @brief Return an unique index associated with a chunk allocated by salloc.
 * This index space is more dense than pointers space, especially in the less
 * significant bits. This number is needed because some types of box's indexes
 * (e.g. BITSET) have better performance then they operate on sequencial
 * offsets (i.e. dense space) instead of memory pointers (sparse space).
 *
 * The calculation is based on SLAB number and the position of an item within
 * it. Current implementation only guarantees that adjacent chunks from one
 * SLAB will have consecutive indexes. That is, if two chunks were sequencially
 * allocated from one chunk they will have sequencial ids. If a second chunk was
 * allocated from another SLAB thеn the difference between indexes may be more
 * then one.
 *
 * @param ptr pointer to memory allocated by salloc
 * @return unique index
 */
size_t
salloc_ptr_to_index(void *ptr);

/**
 * @brief Perform the opposite action of a salloc_ptr_to_index.
 * @param index unique index
 * @see salloc_ptr_to_index
 * @return point to memory area associated with \a index.
 */
void *
salloc_ptr_from_index(size_t index);

#endif /* TARANTOOL_SALLOC_H_INCLUDED */
