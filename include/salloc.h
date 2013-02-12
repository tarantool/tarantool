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
#include "util.h" /* for u64 */

struct tbuf;

bool salloc_init(size_t size, size_t minimal, double factor);
void salloc_free(void);
void *salloc(size_t size, const char *what);
void sfree(void *ptr);
void slab_validate();

/** Statistics on utilization of a single slab class. */
struct slab_cache_stats {
	i64 item_size;
	i64 slabs;
	i64 items;
	i64 bytes_used;
	i64 bytes_free;
};

/** Statistics on utilization of the slab allocator. */
struct slab_arena_stats {
	size_t size;
	size_t used;
};

typedef int (*salloc_stat_cb)(const struct slab_cache_stats *st, void *ctx);

int
salloc_stat(salloc_stat_cb cb, struct slab_arena_stats *astat, void *cb_ctx);

#endif /* TARANTOOL_SALLOC_H_INCLUDED */
