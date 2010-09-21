/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <debug.h>
#include <palloc.h>
#include <util.h>
#include <say.h>
#include <tbuf.h>
#include <stat.h>

struct chunk {
	uint32_t magic;
	size_t size;
	size_t allocated;

	struct chunk_class * restrict class;
	 SLIST_ENTRY(chunk) busy_link;
	 SLIST_ENTRY(chunk) free_link;

	unsigned char data[0];
};

SLIST_HEAD(chunk_list_head, chunk);

struct chunk_class {
	int i;
	i64 size;
	int chunks_count;
	struct chunk_list_head chunks;
	 TAILQ_ENTRY(chunk_class) link;
};

TAILQ_HEAD(class_tailq_head, chunk_class) classes;

struct palloc_pool {
	struct chunk_list_head chunks;
	 SLIST_ENTRY(palloc_pool) link;
	size_t allocated;
	const char *name;
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

size_t
palloc_greatest_size(void)
{
	return (1 << 22) - sizeof(struct chunk);
}

int
palloc_init(void)
{
	struct chunk_class *class;

	class_count = 0;
	TAILQ_INIT(&classes);

	for (size_t size = 4096; size <= 1 << 16; size *= 2) {
		class = malloc(sizeof(struct chunk_class));
		if (class == NULL)
			return 0;

		class->i = class_count++;
		class->chunks_count = 0;
		class->size = size - sizeof(struct chunk);
		SLIST_INIT(&class->chunks);
		TAILQ_INSERT_TAIL(&classes, class, link);
	}

	class = malloc(sizeof(struct chunk_class));
	class->i = class_count++;
	if (class == NULL)
		return 0;
	class->size = palloc_greatest_size();
	SLIST_INIT(&class->chunks);
	TAILQ_INSERT_TAIL(&classes, class, link);
	TAILQ_NEXT(class, link) = NULL;

	eter_pool = palloc_create_pool("eter_pool");
	return 1;
}

static void
poison_chunk(struct chunk *chunk)
{
	(void)chunk;		/* arg used */
#ifdef PALLOC_POISON
	memset(chunk->data, poison_char, chunk->size);
	VALGRIND_MAKE_MEM_NOACCESS(chunk->data, chunk->size);
#endif
}

static struct chunk *
next_chunk_for(struct palloc_pool * restrict pool, size_t size)
{
	struct chunk * restrict chunk = SLIST_FIRST(&pool->chunks);
	struct chunk_class * restrict class;

	if (chunk != NULL)
		class = chunk->class;
	else
		class = TAILQ_FIRST(&classes);

	while (class != NULL && class->size < size)
		class = TAILQ_NEXT(class, link);

	if (class == NULL)
		return NULL;

	chunk = SLIST_FIRST(&class->chunks);
	if (chunk != NULL) {
		SLIST_REMOVE_HEAD(&class->chunks, free_link);
		goto found;
	}

	chunk = malloc(class->size + sizeof(struct chunk));

	if (chunk == NULL)
		return NULL;

	class->chunks_count++;
	chunk->magic = chunk_magic;
	chunk->allocated = 0;
	chunk->size = class->size;
	chunk->class = class;
      found:
	assert(chunk != NULL && chunk->magic == chunk_magic);
	SLIST_INSERT_HEAD(&pool->chunks, chunk, busy_link);

	poison_chunk(chunk);
	return chunk;
}


static void *
alloc_from_chunk(struct chunk *chunk, size_t size)
{
	assert(chunk != NULL);
	assert(chunk->magic == chunk_magic);
	assert(chunk->size >= chunk->allocated);

	if (likely(chunk->size - chunk->allocated >= size)) {
		void *ptr = chunk->data + chunk->allocated;	// TODO: align ptr
		chunk->allocated += size;
		return ptr;
	} else {
		return NULL;
	}
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


static void * __noinline__
palloc_slow_path(struct palloc_pool * restrict pool, size_t size)
{
	struct chunk * restrict chunk;
	chunk = next_chunk_for(pool, size);
	assert(chunk != NULL);
	return alloc_from_chunk(chunk, size);
}

void *
palloc(struct palloc_pool *pool, size_t size)
{
	assert(size < palloc_greatest_size());
	size_t rz_size = size + PALLOC_REDZONE * 2;
	struct chunk * restrict chunk = SLIST_FIRST(&pool->chunks);
	void *ptr;

	pool->allocated += rz_size;
	ptr = alloc_from_chunk(chunk, rz_size);
	if (unlikely(ptr == NULL))
		ptr = palloc_slow_path(pool, rz_size);

	assert(ptr != NULL);
	assert(poisoned(ptr + PALLOC_REDZONE, size) == NULL);
	VALGRIND_MEMPOOL_ALLOC(pool, ptr + PALLOC_REDZONE, size);
	return ptr + PALLOC_REDZONE;
}

void *
p0alloc(struct palloc_pool *pool, size_t size)
{
	void * ptr;

	ptr = palloc(pool, size);
	if (ptr)
		memset(ptr, 0, size);

	return ptr;
}

void *
palloca(struct palloc_pool *pool, size_t size, size_t align)
{
	void * ptr;

	ptr = palloc(pool, size + align);
	return (void*)TYPEALIGN(align, (uintptr_t)ptr); 
}

void
prelease(struct palloc_pool *pool)
{
	struct chunk *chunk;

	for (chunk = SLIST_FIRST(&pool->chunks);
	     chunk != NULL;
	     chunk = SLIST_NEXT(chunk, busy_link))
	{
		chunk->allocated = 0;
		SLIST_INSERT_HEAD(&chunk->class->chunks, chunk, free_link);
		poison_chunk(chunk);
	}

	SLIST_INIT(&pool->chunks);
	VALGRIND_MEMPOOL_TRIM(pool, NULL, 0);
	pool->allocated = 0;
	next_chunk_for(pool, 128);
}

void
prelease_after(struct palloc_pool *pool, size_t after)
{
	if (pool->allocated > after)
		prelease(pool);
}

struct palloc_pool *
palloc_create_pool2(const char *name, size_t initial_size)
{
	struct palloc_pool *pool = malloc(sizeof(struct palloc_pool));
	assert(pool != NULL);
	memset(pool, 0, sizeof(*pool));
	pool->name = name;
	SLIST_INIT(&pool->chunks);
	SLIST_INSERT_HEAD(&pools, pool, link);
	if (initial_size < 128)
		initial_size = 128;
	next_chunk_for(pool, initial_size);
	VALGRIND_CREATE_MEMPOOL(pool, PALLOC_REDZONE, 0);
	return pool;
}

struct palloc_pool *
palloc_create_pool(const char *name)
{
	return palloc_create_pool2(name, 128);
}

void
palloc_destroy(struct palloc_pool *pool)
{
	SLIST_REMOVE(&pools, pool, palloc_pool, link);
	prelease(pool);
	VALGRIND_DESTROY_MEMPOOL(pool);
	free(pool);
}

void
palloc_stat(struct tbuf *buf)
{
	struct chunk_class *class;
	struct chunk *chunk;
	struct palloc_pool *pool;
	int chunks[class_count];

	tbuf_printf(buf, "palloc statistic:\n");
	tbuf_printf(buf, "  classes:\n");
	TAILQ_FOREACH(class, &classes, link) {
		int free_chunks = 0;
		SLIST_FOREACH(chunk, &class->chunks, free_link)
			free_chunks++;

		tbuf_printf(buf, "    - { size: %- 8"PRIi64", free_chunks: %- 6i, busy_chunks: %- 6i }\n",
			    class->size, free_chunks, class->chunks_count - free_chunks);
	}
	tbuf_printf(buf, "  pools:\n");

	SLIST_FOREACH(pool, &pools, link) {
		for (int i = 0; i < class_count; i++)
			chunks[i] = 0;

		tbuf_printf(buf, "    - name:  %s\n      alloc: %" PRI_SZ "\n",
			    pool->name, pool->allocated);

		if (pool->allocated > 0) {
			tbuf_printf(buf, "      busy chunks:\n");

			SLIST_FOREACH(chunk, &pool->chunks, busy_link)
			    chunks[chunk->class->i]++;

			int indent = 0;
			TAILQ_FOREACH(class, &classes, link) {
				if (chunks[class->i] == 0)
					continue;
				tbuf_printf(buf, "        - { size: %- 7"PRIi64", used: %i }\n",
					    class->size, chunks[class->i]);

				if (indent == 0)
					indent = 19;
			}
		}
	}
}

const char *
palloc_name(struct palloc_pool *pool, const char *new_name)
{
	const char *old_name = pool->name;
	if (new_name != NULL)
		pool->name = new_name;
	return old_name;
}

size_t
palloc_allocated(struct palloc_pool *pool)
{
	return pool->allocated;
}
