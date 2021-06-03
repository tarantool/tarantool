#pragma once
/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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
#include "allocator.h"
#include "memtx_engine.h"
#include "tuple.h"

struct PACKED memtx_tuple {
	/*
	 * sic: the header of the tuple is used
	 * to store a free list pointer in smfree_delayed.
	 * Please don't change it without understanding
	 * how smfree_delayed and snapshotting COW works.
	 */
	/** Snapshot generation version. */
	uint32_t version;
	struct tuple base;
};

template<class Allocator>
class MemtxAllocator {
public:
	static void free(void *item)
	{
		struct memtx_tuple *memtx_tuple = (struct memtx_tuple *) item;
		struct tuple *tuple = &memtx_tuple->base;
		size_t total = tuple_size(tuple) +
			       offsetof(struct memtx_tuple, base);
		Allocator::free((void *) memtx_tuple, total);
	}

	static void free(void *ptr, size_t size)
	{
		Allocator::free(ptr, size);
	}

	static void delayed_free(void *ptr)
	{
		lifo_push(&MemtxAllocator<Allocator>::lifo, ptr);
	}

	static void * alloc(size_t size)
	{
		collect_garbage();
		return Allocator::alloc(size);
	}

	static void create(enum memtx_engine_free_mode m)
	{
		MemtxAllocator<Allocator>::mode = m;
		lifo_init(&MemtxAllocator<Allocator>::lifo);
	}

	static void set_mode(enum memtx_engine_free_mode m)
	{
		MemtxAllocator<Allocator>::mode = m;
	}

	static void destroy()
	{
		void *item;
		while ((item = lifo_pop(&MemtxAllocator<Allocator>::lifo)))
			free(item);
	}
private:
	static constexpr int GC_BATCH_SIZE = 100;

	static void collect_garbage()
	{
		if (MemtxAllocator<Allocator>::mode !=
		    MEMTX_ENGINE_COLLECT_GARBAGE)
			return;
		if (! lifo_is_empty(&MemtxAllocator<Allocator>::lifo)) {
			for (int i = 0; i < GC_BATCH_SIZE; i++) {
				void *item = lifo_pop(&MemtxAllocator<Allocator>::lifo);
				if (item == NULL)
					break;
				free(item);
			}
		} else {
			MemtxAllocator<Allocator>::mode = MEMTX_ENGINE_FREE;
		}
	}
	static struct lifo lifo;
	static enum memtx_engine_free_mode mode;
};

template<class Allocator>
struct lifo MemtxAllocator<Allocator>::lifo;

template<class Allocator>
enum memtx_engine_free_mode MemtxAllocator<Allocator>::mode;
