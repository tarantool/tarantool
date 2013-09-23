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
#include "salloc.h"

#include "tarantool/config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "third_party/valgrind/memcheck.h"
#include <third_party/queue.h>
#include "tarantool/util.h"
#include <tbuf.h>
#include <say.h>
#include "exception.h"

#define SLAB_ALIGN_PTR(ptr) (void *)((uintptr_t)(ptr) & ~(SLAB_SIZE - 1))

#ifdef SLAB_DEBUG
#undef NDEBUG
uint8_t red_zone[4] = { 0xfa, 0xfa, 0xfa, 0xfa };
#else
uint8_t red_zone[0] = { };
#endif

static const uint32_t SLAB_MAGIC = 0x51abface;
static const size_t SLAB_SIZE = 1 << 22;
static const size_t MAX_SLAB_ITEM = 1 << 20;

/* maximum number of items in one slab */
/* updated in slab_classes_init, depends on salloc_init params */
size_t MAX_SLAB_ITEM_COUNT;

/*
 *  slab delayed free queue.
*/
#define SLAB_Q_WATERMARK (512 * sizeof(void*))

struct slab_q {
	char *buf;
	size_t bottom; /* advanced by batch free */
	size_t top;
	size_t size;   /* total buffer size */
};

static inline int
slab_qinit(struct slab_q *q, size_t size) {
	q->size = size;
	q->bottom = 0;
	q->top = 0;
	q->buf = (char*)malloc(size);
	return (q->buf == NULL ? -1 : 0);
}

static inline void
slab_qfree(struct slab_q *q) {
	if (q->buf) {
		free(q->buf);
		q->buf = NULL;
	}
}

#ifndef unlikely
# define unlikely __builtin_expect(!! (EXPR), 0)
#endif

static inline int
slab_qpush(struct slab_q *q, void *ptr)
{
	/* reduce memory allocation and memmove
	 * effect by reusing free pointers buffer space only after the
	 * watermark frees reached. */
	if (unlikely(q->bottom >= SLAB_Q_WATERMARK)) {
		memmove(q->buf, q->buf + q->bottom, q->bottom);
		q->top -= q->bottom;
		q->bottom = 0;
	}
	if (unlikely((q->top + sizeof(void*)) > q->size)) {
		size_t newsize = q->size * 2;
		char *ptr = (char*)realloc((void*)q->buf, newsize);
		if (unlikely(ptr == NULL))
			return -1;
		q->buf = ptr;
		q->size = newsize;
	}
	memcpy(q->buf + q->top, (char*)&ptr, sizeof(ptr));
	q->top += sizeof(void*);
	return 0;
}

static inline int
slab_qn(struct slab_q *q) {
	return (q->top - q->bottom) / sizeof(void*);
}

static inline void*
slab_qpop(struct slab_q *q) {
	if (unlikely(q->bottom == q->top))
		return NULL;
	void *ret = *(void**)(q->buf + q->bottom);
	q->bottom += sizeof(void*);
	return ret;
}

struct slab_item {
	struct slab_item *next;
};

struct slab {
	uint32_t magic;
	size_t used;
	size_t items;
	struct slab_item *free;
	struct slab_cache *cache;
	void *brk;
	SLIST_ENTRY(slab) link;
	SLIST_ENTRY(slab) free_link;
	TAILQ_ENTRY(slab) cache_free_link;
	TAILQ_ENTRY(slab) cache_link;
};

SLIST_HEAD(slab_slist_head, slab);
TAILQ_HEAD(slab_tailq_head, slab);

struct slab_cache {
	size_t item_size;
	struct slab_tailq_head slabs, free_slabs;
};

struct arena {
	void *mmap_base;
	size_t mmap_size;
	bool delayed_free_mode;
	size_t delayed_free_batch;
	struct slab_q delayed_q;
	void *base;
	size_t size;
	size_t used;
	struct slab_slist_head slabs, free_slabs;
};

static uint32_t slab_active_caches;
static struct slab_cache slab_caches[256];
static struct arena arena;

static struct slab *
slab_header(void *ptr)
{
	struct slab *slab = (struct slab *) SLAB_ALIGN_PTR(ptr);
	assert(slab->magic == SLAB_MAGIC);
	return slab;
}

static void
slab_caches_init(size_t minimal, double factor)
{
	uint32_t i;
	size_t size;
	const size_t ptr_size = sizeof(void *);

	for (i = 0, size = minimal; i < nelem(slab_caches) && size <= MAX_SLAB_ITEM; i++) {
		slab_caches[i].item_size = size - sizeof(red_zone);
		TAILQ_INIT(&slab_caches[i].free_slabs);

		size = MAX((size_t)(size * factor) & ~(ptr_size - 1),
			   (size + ptr_size) & ~(ptr_size - 1));
	}

	slab_active_caches = i;

	MAX_SLAB_ITEM_COUNT = (size_t) (SLAB_SIZE - sizeof(struct slab)) /
			slab_caches[0].item_size;
}

static bool
arena_init(struct arena *arena, size_t size)
{
	arena->delayed_free_mode = false;
	arena->delayed_free_batch = 100;

	int rc = slab_qinit(&arena->delayed_q, 4096);
	if (rc == -1)
		return false;

	arena->used = 0;
	arena->size = size - size % SLAB_SIZE;
	arena->mmap_size = size - size % SLAB_SIZE + SLAB_SIZE;	/* spend SLAB_SIZE bytes on align :-( */

	arena->mmap_base = mmap(NULL, arena->mmap_size,
				PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (arena->mmap_base == MAP_FAILED) {
		say_syserror("mmap");
		slab_qfree(&arena->delayed_q);
		return false;
	}

	arena->base = (char *)SLAB_ALIGN_PTR(arena->mmap_base) + SLAB_SIZE;
	SLIST_INIT(&arena->slabs);
	SLIST_INIT(&arena->free_slabs);

	return true;
}

void salloc_reattach(void) {
	mprotect(arena.mmap_base, arena.mmap_size, PROT_READ);
}

void salloc_batch_mode(bool mode) {
	arena.delayed_free_mode = mode;
}

static void *
arena_alloc(struct arena *arena)
{
	void *ptr;
	const size_t size = SLAB_SIZE;

	if (arena->size - arena->used < size)
		return NULL;

	ptr = (char *)arena->base + arena->used;
	arena->used += size;

	return ptr;
}

bool
salloc_init(size_t size, size_t minimal, double factor)
{
	if (size < SLAB_SIZE * 2)
		return false;

	if (!arena_init(&arena, size))
		return false;

	slab_caches_init(MAX(sizeof(void *), minimal), factor);
	return true;
}

void
salloc_free(void)
{
	if (arena.mmap_base != NULL)
		munmap(arena.mmap_base, arena.mmap_size);
	slab_qfree(&arena.delayed_q);
	memset(&arena, 0, sizeof(struct arena));
}

static void
format_slab(struct slab_cache *cache, struct slab *slab)
{
	assert(cache->item_size <= MAX_SLAB_ITEM);

	slab->magic = SLAB_MAGIC;
	slab->free = NULL;
	slab->cache = cache;
	slab->items = 0;
	slab->used = 0;
	slab->brk = (char *)CACHEALIGN((char *)slab + sizeof(struct slab));

	TAILQ_INSERT_HEAD(&cache->slabs, slab, cache_link);
	TAILQ_INSERT_HEAD(&cache->free_slabs, slab, cache_free_link);
}

static bool
fully_formatted(struct slab *slab)
{
	return (char *) slab->brk + slab->cache->item_size >= (char *)slab + SLAB_SIZE;
}

void
slab_validate(void)
{
	struct slab *slab;

	SLIST_FOREACH(slab, &arena.slabs, link) {
		for (char *p = (char *)slab + sizeof(struct slab);
		     p + slab->cache->item_size < (char *)slab + SLAB_SIZE;
		     p += slab->cache->item_size + sizeof(red_zone)) {
			assert(memcmp(p + slab->cache->item_size, red_zone, sizeof(red_zone)) == 0);
		}
	}
}

static struct slab_cache *
cache_for(size_t size)
{
	for (uint32_t i = 0; i < slab_active_caches; i++)
		if (slab_caches[i].item_size >= size)
			return &slab_caches[i];

	return NULL;
}

static struct slab *
slab_of(struct slab_cache *cache)
{
	struct slab *slab;

	if (!TAILQ_EMPTY(&cache->free_slabs)) {
		slab = TAILQ_FIRST(&cache->free_slabs);
		assert(slab->magic == SLAB_MAGIC);
		return slab;
	}

	if (!SLIST_EMPTY(&arena.free_slabs)) {
		slab = SLIST_FIRST(&arena.free_slabs);
		assert(slab->magic == SLAB_MAGIC);
		SLIST_REMOVE_HEAD(&arena.free_slabs, free_link);
		format_slab(cache, slab);
		return slab;
	}

	if ((slab = (struct slab *) arena_alloc(&arena)) != NULL) {
		format_slab(cache, slab);
		SLIST_INSERT_HEAD(&arena.slabs, slab, link);
		return slab;
	}

	return NULL;
}

#ifndef NDEBUG
static bool
valid_item(struct slab *slab, void *item)
{
	return (char *)item >= (char *)(slab) + sizeof(struct slab) &&
	    (char *)item < (char *)(slab) + sizeof(struct slab) + SLAB_SIZE;
}
#endif

static void
sfree_do(void *ptr)
{
	struct slab *slab = slab_header(ptr);
	struct slab_cache *cache = slab->cache;
	struct slab_item *item = (struct slab_item *) ptr;

	if (fully_formatted(slab) && slab->free == NULL)
		TAILQ_INSERT_TAIL(&cache->free_slabs, slab, cache_free_link);

	assert(valid_item(slab, item));
	assert(slab->free == NULL || valid_item(slab, slab->free));

	item->next = slab->free;
	slab->free = item;
	slab->used -= cache->item_size + sizeof(red_zone);
	slab->items -= 1;

	if (slab->items == 0) {
		TAILQ_REMOVE(&cache->free_slabs, slab, cache_free_link);
		TAILQ_REMOVE(&cache->slabs, slab, cache_link);
		SLIST_INSERT_HEAD(&arena.free_slabs, slab, free_link);
	}

	VALGRIND_FREELIKE_BLOCK(item, sizeof(red_zone));
}

static void
sfree_batch(void)
{
	ssize_t batch = arena.delayed_free_batch;
	size_t n = slab_qn(&arena.delayed_q);
	while (batch-- > 0 && n-- > 0) {
		void *ptr = slab_qpop(&arena.delayed_q);
		assert(ptr != NULL);
		sfree_do(ptr);
	}
}

void
sfree(void *ptr, const char *what)
{
	if (ptr == NULL)
		return;
	if (arena.delayed_free_mode) {
		if (slab_qpush(&arena.delayed_q, ptr) == -1)
			tnt_raise(LoggedError, ER_MEMORY_ISSUE, arena.delayed_q.size * 2,
				  "slab allocator", what);
		return;
	}
	sfree_batch();
	return sfree_do(ptr);
}

void *
salloc(size_t size, const char *what)
{
	struct slab_cache *cache;
	struct slab *slab;
	struct slab_item *item;

	if (! arena.delayed_free_mode)
		sfree_batch();

	if ((cache = cache_for(size)) == NULL ||
	    (slab = slab_of(cache)) == NULL) {

		tnt_raise(LoggedError, ER_MEMORY_ISSUE, size,
			  "slab allocator", what);
	}

	if (slab->free == NULL) {
		assert(valid_item(slab, slab->brk));
		item = (struct slab_item *) slab->brk;
		memcpy((char *)item + cache->item_size, red_zone, sizeof(red_zone));
		slab->brk = (char *) slab->brk + cache->item_size + sizeof(red_zone);
	} else {
		assert(valid_item(slab, slab->free));
		item = slab->free;

		(void) VALGRIND_MAKE_MEM_DEFINED(item, sizeof(void *));
		slab->free = item->next;
		(void) VALGRIND_MAKE_MEM_UNDEFINED(item, sizeof(void *));
	}

	if (fully_formatted(slab) && slab->free == NULL)
		TAILQ_REMOVE(&cache->free_slabs, slab, cache_free_link);

	slab->used += cache->item_size + sizeof(red_zone);
	slab->items += 1;

	VALGRIND_MALLOCLIKE_BLOCK(item, cache->item_size, sizeof(red_zone), 0);
	return (void *)item;
}

size_t
salloc_ptr_to_index(void *ptr)
{
	struct slab *slab = slab_header(ptr);
	struct slab_item *item = (struct slab_item *) ptr;
	struct slab_cache *clazz = slab->cache;

	(void) item;
	assert(valid_item(slab, item));

	void *brk_start = (char *)CACHEALIGN((char *)slab+sizeof(struct slab));
	ptrdiff_t item_no = ((const char *) ptr - (const char *) brk_start) / clazz->item_size;
	assert(item_no >= 0);

	ptrdiff_t slab_no = ((const char *) slab - (const char *) arena.base) / SLAB_SIZE;
	assert(slab_no >= 0);

	size_t index = (size_t)slab_no * MAX_SLAB_ITEM_COUNT + (size_t) item_no;

	assert(salloc_ptr_from_index(index) == ptr);

	return index;
}

void *
salloc_ptr_from_index(size_t index)
{
	size_t slab_no = index / MAX_SLAB_ITEM_COUNT;
	size_t item_no = index % MAX_SLAB_ITEM_COUNT;

	struct slab *slab = slab_header(
		(void *) ((size_t) arena.base + SLAB_SIZE * slab_no));
	struct slab_cache *clazz = slab->cache;

	void *brk_start = (char *)CACHEALIGN((char *)slab+sizeof(struct slab));
	struct slab_item *item = (struct slab_item *)((char *) brk_start + item_no * clazz->item_size);
	assert(valid_item(slab, item));

	return (void *) item;
}

/**
 * Collect slab allocator statistics.
 *
 * @param cb - a callback to receive statistic item
 * @param astat - a structure to fill with of arena
 * @user_data - user's data that will be sent to cb
 *
 */
int
salloc_stat(salloc_stat_cb cb, struct slab_arena_stats *astat, void *cb_ctx)
{
	if (astat) {
		astat->used = arena.used;
		astat->size = arena.size;
	}

	if (cb) {
		struct slab *slab;
		struct slab_cache_stats st;

		for (int i = 0; i < slab_active_caches; i++) {
			memset(&st, 0, sizeof(st));
			TAILQ_FOREACH(slab, &slab_caches[i].slabs, cache_link)
			{
				st.slabs++;
				st.items += slab->items;
				st.bytes_free += SLAB_SIZE;
				st.bytes_free -= slab->used;
				st.bytes_free -= sizeof(struct slab);
				st.bytes_used += sizeof(struct slab);
				st.bytes_used += slab->used;
			}
			st.item_size = slab_caches[i].item_size;

			if (st.slabs == 0)
				continue;
			int res = cb(&st, cb_ctx);
			if (res != 0)
				return res;
		}
	}
	return 0;
}
