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
#include "palloc.h"
#include "tarantool/config.h"
#include "third_party/valgrind/memcheck.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

#include <stdlib.h>
#include <third_party/queue.h>
#include <tbuf.h>
#include "exception.h"

struct chunk {
	uint32_t magic;
	void *brk;
	size_t free;
	size_t size;

	struct chunk_class *clazz;
	SLIST_ENTRY(chunk) busy_link;
	SLIST_ENTRY(chunk) free_link;
};

SLIST_HEAD(chunk_list_head, chunk);

struct chunk_class {
	int i;
	uint32_t allocated_size;
	int chunks_count;
	struct chunk_list_head chunks;
	 TAILQ_ENTRY(chunk_class) link;
};

TAILQ_HEAD(class_tailq_head, chunk_class) classes;

struct palloc_pool {
	struct chunk_list_head chunks;
	SLIST_ENTRY(palloc_pool) link;
	size_t allocated;
	char name[PALLOC_POOL_NAME_MAXLEN];
};

SLIST_HEAD(palloc_pool_head, palloc_pool) pools;

static int class_count;
const uint32_t chunk_magic = 0xbb84fcf6;
static const char poison_char = 'P';

struct palloc_pool *eter_pool;

#ifdef REDZONE
#define PALLOC_REDZONE 4
#endif
#ifndef PALLOC_REDZONE
#define PALLOC_REDZONE 0
#endif
#ifdef POISON
#define PALLOC_POISON
#endif

static size_t
palloc_greatest_size(void)
{
	return (1 << 22) - sizeof(struct chunk);
}


static struct chunk_class *
class_init(size_t size)
{
	struct chunk_class *clazz;

	clazz = (struct chunk_class *) malloc(sizeof(struct chunk_class));
	if (clazz == NULL)
		return NULL;

	clazz->i = class_count++;
	clazz->chunks_count = 0;
	clazz->allocated_size = size;
	SLIST_INIT(&clazz->chunks);
	TAILQ_INSERT_TAIL(&classes, clazz, link);

	return clazz;
}

int
palloc_init(void)
{
	struct chunk_class *clazz;

	class_count = 0;
	TAILQ_INIT(&classes);

	/*
	 * since chunks are allocated via mmap
	 * size must by multiply of 4096 minus sizeof(struct chunk)
	 * TODO: should we use sysconf(_SC_PAGESIZE)?
	 */

	for (size_t size = 4096 * 8; size <= 1 << 16; size *= 2) {
		if (class_init(size) == NULL)
			return 0;
	}

	if (class_init(palloc_greatest_size()) == NULL)
		return 0;

	if ((clazz = class_init(-1)) == NULL)
		return 0;

	TAILQ_NEXT(clazz, link) = NULL;

	eter_pool = palloc_create_pool("eter_pool");
	return 1;
}

void
palloc_free(void)
{
	struct palloc_pool *pool, *pool_next;
	SLIST_FOREACH_SAFE(pool, &pools, link, pool_next)
		palloc_destroy_pool(pool);

	palloc_free_unused();

	struct chunk_class *clazz, *class_next;
	TAILQ_FOREACH_SAFE(clazz, &classes, link, class_next)
		free(clazz);
}

static void
poison_chunk(struct chunk *chunk)
{
	(void)chunk;		/* arg used */
#ifdef PALLOC_POISON
	memset((void *)chunk + sizeof(struct chunk), poison_char, chunk->size);
	VALGRIND_MAKE_MEM_NOACCESS(chunk->data, chunk->size);
#endif
}

static struct chunk *
next_chunk_for(struct palloc_pool *restrict pool, size_t size)
{
	struct chunk *restrict chunk = SLIST_FIRST(&pool->chunks);
	struct chunk_class *restrict clazz;
	size_t chunk_size;

	if (chunk != NULL)
		clazz = chunk->clazz;
	else
		clazz = TAILQ_FIRST(&classes);

	if (clazz->allocated_size == -1)
		clazz = TAILQ_PREV(clazz, class_tailq_head, link);

	while (clazz != NULL && clazz->allocated_size < size + sizeof(struct chunk))
		clazz = TAILQ_NEXT(clazz, link);

	assert(clazz != NULL);

	chunk = SLIST_FIRST(&clazz->chunks);
	if (chunk != NULL) {
		SLIST_REMOVE_HEAD(&clazz->chunks, free_link);
		goto found;
	}

	if (size > palloc_greatest_size()) {
		chunk_size = size + sizeof(struct chunk);
		chunk = (struct chunk *) malloc(chunk_size);
		if (chunk == NULL)
			return NULL;
	} else {
		chunk_size = clazz->allocated_size;
		chunk = (struct chunk *) mmap(NULL, chunk_size,
			     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (chunk == MAP_FAILED)
			return NULL;
	}

	clazz->chunks_count++;
	chunk->magic = chunk_magic;
	chunk->size = chunk_size;
	chunk->free = chunk_size - sizeof(struct chunk);
	chunk->brk = (char *)chunk + sizeof(struct chunk);
	chunk->clazz = clazz;
      found:
	assert(chunk != NULL && chunk->magic == chunk_magic);
	SLIST_INSERT_HEAD(&pool->chunks, chunk, busy_link);

	poison_chunk(chunk);
	return chunk;
}

#ifndef NDEBUG
static const char *
poisoned(const char *b, size_t size)
{
	(void)b;
	(void)size;		/* arg used */
#ifdef PALLOC_POISON
	for (int i = 0; i < size; i++)
		if (unlikely(b[i] != poison_char))
			return b + i;
#endif
	return NULL;
}
#endif

static void * __attribute__((noinline))
palloc_slow_path(struct palloc_pool *restrict pool, size_t size)
{
	struct chunk *chunk;
	chunk = next_chunk_for(pool, size);
	if (chunk == NULL) {
		tnt_raise(LoggedError, ER_MEMORY_ISSUE,
			  size, "palloc", "next chunk");
	}

	assert(chunk->free >= size);
	void *ptr = chunk->brk;
	chunk->brk = (char *) chunk->brk + size;
	chunk->free -= size;
	return ptr;
}

void *__attribute((regparm(2)))
palloc(struct palloc_pool *restrict pool, size_t size)
{
	const size_t rz_size = size + PALLOC_REDZONE * 2;
	struct chunk *restrict chunk = SLIST_FIRST(&pool->chunks);
	void *ptr;

	pool->allocated += rz_size;

	if (likely(chunk != NULL && chunk->free >= rz_size)) {
		ptr = chunk->brk;
		chunk->brk = (char *) chunk->brk + rz_size;
		chunk->free -= rz_size;
	} else
		ptr = palloc_slow_path(pool, rz_size);

	assert(poisoned((char *) ptr + PALLOC_REDZONE, size) == NULL);
	(void) VALGRIND_MAKE_MEM_DEFINED((char *) ptr + PALLOC_REDZONE, size);

	return (char *) ptr + PALLOC_REDZONE;
}

void *__attribute__((regparm(2)))
p0alloc(struct palloc_pool *pool, size_t size)
{
	void *ptr;

	ptr = palloc(pool, size);
	memset(ptr, 0, size);
	return ptr;
}

void *
palloca(struct palloc_pool *pool, size_t size, size_t align)
{
	void *ptr;

	ptr = palloc(pool, size + align);
	return (void *)TYPEALIGN(align, (uintptr_t)ptr);
}


static inline void
chunk_free(struct chunk *chunk)
{
	if (chunk->size <= palloc_greatest_size()) {
		chunk->free = chunk->size - sizeof(struct chunk);
		chunk->brk = (char *)chunk + sizeof(struct chunk);
		SLIST_INSERT_HEAD(&chunk->clazz->chunks, chunk, free_link);
		poison_chunk(chunk);
	} else {
		free(chunk);
	}
}

/**
 * Release all memory down to new_size; new_size has to be previously
 * obtained by calling palloc_allocated().
 */
void
ptruncate(struct palloc_pool *pool, size_t new_size)
{
	assert(new_size <= pool->allocated);

	ssize_t cut_size = pool->allocated - new_size;
	struct chunk *chunk = SLIST_FIRST(&pool->chunks);

	for (; chunk; chunk = SLIST_FIRST(&pool->chunks)) {

		size_t chunk_used = (chunk->size - chunk->free -
				     sizeof(struct chunk));
		if (chunk_used > cut_size) {
			/* This is the last chunk to trim. */
			chunk->brk = (char *) chunk->brk - cut_size;
			chunk->free += cut_size;
			(void) VALGRIND_MAKE_MEM_NOACCESS(chunk->brk, cut_size);
			cut_size = 0;
			break;
		}
		cut_size -= chunk_used;
		/* Remove the entire chunk. */
		SLIST_REMOVE_HEAD(&pool->chunks, busy_link);
		chunk_free(chunk);
	}
	assert(cut_size == 0);
	pool->allocated = new_size;
}

void
prelease(struct palloc_pool *pool)
{
	struct chunk *chunk, *next_chunk;

	for (chunk = SLIST_FIRST(&pool->chunks); chunk; chunk = next_chunk) {
		next_chunk = SLIST_NEXT(chunk, busy_link);
		chunk_free(chunk);
	}

	SLIST_INIT(&pool->chunks);
	pool->allocated = 0;
}

void
palloc_reset(struct palloc_pool *pool)
{
	struct chunk *chunk = SLIST_FIRST(&pool->chunks);
	if (chunk) {
		pool->allocated -= chunk->size - sizeof(struct chunk) -
				   chunk->free;
		chunk->free = chunk->size - sizeof(struct chunk);
		chunk->brk = (char *)chunk + sizeof(struct chunk);
	}
}

void
prelease_after(struct palloc_pool *pool, size_t after)
{
	if (pool->allocated > after)
		prelease(pool);
}

struct palloc_pool *
palloc_create_pool(const char *name)
{
	struct palloc_pool *pool = (struct palloc_pool *)
			calloc(sizeof(struct palloc_pool), 1);
	if (pool == NULL) {
		tnt_raise(LoggedError, ER_MEMORY_ISSUE,
			  sizeof(struct palloc_pool),
			  "malloc", "palloc pool");
	}
	palloc_set_name(pool, name);
	SLIST_INIT(&pool->chunks);
	SLIST_INSERT_HEAD(&pools, pool, link);
	return pool;
}

void
palloc_destroy_pool(struct palloc_pool *pool)
{
	SLIST_REMOVE(&pools, pool, palloc_pool, link);
	prelease(pool);
	free(pool);
}

void
palloc_free_unused(void)
{
	struct chunk_class *clazz;
	struct chunk *chunk, *next_chunk;

	TAILQ_FOREACH(clazz, &classes, link) {
		SLIST_FOREACH_SAFE(chunk, &clazz->chunks, free_link, next_chunk)
			munmap(chunk, clazz->allocated_size);
		SLIST_INIT(&clazz->chunks);
	}
}

void
palloc_stat(struct tbuf *buf)
{
	struct chunk_class *clazz;
	struct chunk *chunk;
	struct palloc_pool *pool;
	int chunks[class_count];

	tbuf_printf(buf, "palloc statistic:" CRLF);
	tbuf_printf(buf, "  pools:" CRLF);
	SLIST_FOREACH(pool, &pools, link) {
		for (int i = 0; i < class_count; i++)
			chunks[i] = 0;

		tbuf_printf(buf, "    - name:  %s\n      alloc: %" PRI_SZ "" CRLF,
			    pool->name, pool->allocated);

		if (pool->allocated > 0) {
			tbuf_printf(buf, "      busy chunks:" CRLF);

			SLIST_FOREACH(chunk, &pool->chunks, busy_link)
			    chunks[chunk->clazz->i]++;

			TAILQ_FOREACH(clazz, &classes, link) {
				if (chunks[clazz->i] == 0)
					continue;
				tbuf_printf(buf, "        - { size: %" PRIu32 ", used: %i }" CRLF,
					    clazz->allocated_size, chunks[clazz->i]);
			}
		}
	}
	tbuf_printf(buf, "  classes:" CRLF);
	TAILQ_FOREACH(clazz, &classes, link) {
		int free_chunks = 0;
		SLIST_FOREACH(chunk, &clazz->chunks, free_link)
			free_chunks++;

		tbuf_printf(buf,
			    "    - { size: %" PRIu32
			    ", free_chunks: %- 6i, busy_chunks: %- 6i }" CRLF, clazz->allocated_size,
			    free_chunks, clazz->chunks_count - free_chunks);
	}
	uint64_t palloc_total = 0;
	uint64_t palloc_used = 0;
	SLIST_FOREACH(pool, &pools, link) {
		SLIST_FOREACH(chunk, &pool->chunks, busy_link) {
			palloc_total += chunk->size;
			palloc_used += chunk->size - chunk->free;
		}
		SLIST_FOREACH(chunk, &pool->chunks, free_link)
			palloc_total += chunk->size;
	}
	tbuf_printf(buf, "  total:" CRLF);
	tbuf_printf(buf, "    - { occupied: %" PRIu64 ", used: %" PRIu64 " }" CRLF,
		    palloc_total, palloc_used);

}

void
palloc_set_name(struct palloc_pool *pool, const char *name)
{
	snprintf(pool->name, sizeof(pool->name), "%s", name);
}

const char *
palloc_name(struct palloc_pool *pool)
{
	return pool->name;
}

size_t
palloc_allocated(struct palloc_pool *pool)
{
	return pool->allocated;
}
