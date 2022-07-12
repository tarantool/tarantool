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
	static void create()
	{
		lifo_init(&lifo);
	}

	static void destroy()
	{
		void *item;
		while ((item = lifo_pop(&lifo)))
			immediate_free_tuple((struct memtx_tuple *)item);
	}

	/**
	 * Enter tuple delayed free mode: tuple allocated before the call
	 * won't be freed until leave_delayed_free_mode() is called.
	 * This function is reentrant, meaning it's okay to call it multiple
	 * times from the same or different fibers - one just has to leave
	 * the delayed free mode the same amount of times then.
	 */
	static void enter_delayed_free_mode()
	{
		snapshot_version++;
		delayed_free_mode++;
	}

	/**
	 * Leave tuple delayed free mode. This function undoes the effect
	 * of enter_delayed_free_mode().
	 */
	static void leave_delayed_free_mode()
	{
		assert(delayed_free_mode > 0);
		--delayed_free_mode;
	}

	/**
	 * Allocate a tuple of the given size.
	 */
	static struct tuple *alloc_tuple(size_t size)
	{
		size_t total = size + offsetof(struct memtx_tuple, base);
		struct memtx_tuple *memtx_tuple =
			(struct memtx_tuple *)alloc(total);
		if (memtx_tuple == NULL)
			return NULL;
		memtx_tuple->version = snapshot_version;
		return &memtx_tuple->base;
	}

	/**
	 * Free a tuple allocated with alloc_tuple().
	 *
	 * The tuple is freed immediately if there's no snapshot that may use
	 * it. Otherwise, it's put in the garbage collection list to be free as
	 * soon as the last snapshot using it is destroyed.
	 */
	static void free_tuple(struct tuple *tuple)
	{
		struct memtx_tuple *memtx_tuple = container_of(
			tuple, struct memtx_tuple, base);
		if (delayed_free_mode == 0 ||
		    memtx_tuple->version == snapshot_version ||
		    tuple_has_flag(tuple, TUPLE_IS_TEMPORARY)) {
			immediate_free_tuple(memtx_tuple);
		} else {
			delayed_free_tuple(memtx_tuple);
		}
	}

private:
	static constexpr int GC_BATCH_SIZE = 100;

	static void free(void *ptr, size_t size)
	{
		Allocator::free(ptr, size);
	}

	static void *alloc(size_t size)
	{
		collect_garbage();
		return Allocator::alloc(size);
	}

	static void immediate_free_tuple(struct memtx_tuple *memtx_tuple)
	{
		size_t size = tuple_size(&memtx_tuple->base) +
			      offsetof(struct memtx_tuple, base);
		free(memtx_tuple, size);
	}

	static void delayed_free_tuple(struct memtx_tuple *memtx_tuple)
	{
		lifo_push(&lifo, memtx_tuple);
	}

	static void collect_garbage()
	{
		if (delayed_free_mode > 0)
			return;
		if (!lifo_is_empty(&lifo)) {
			for (int i = 0; i < GC_BATCH_SIZE; i++) {
				void *item = lifo_pop(&lifo);
				if (item == NULL)
					break;
				immediate_free_tuple(
					(struct memtx_tuple *)item);
			}
		}
	}

	/**
	 * Tuple garbage collection list. Contains tuples that were not freed
	 * immediately because they are currently in use by a snapshot.
	 */
	static struct lifo lifo;
	/**
	 * Unless zero, freeing of tuples allocated before the last call to
	 * enter_delayed_free_mode() is delayed until leave_delayed_free_mode()
	 * is called.
	 */
	static uint32_t delayed_free_mode;
	/** Incremented with each next snapshot. */
	static uint32_t snapshot_version;
};

template<class Allocator>
struct lifo MemtxAllocator<Allocator>::lifo;

template<class Allocator>
uint32_t MemtxAllocator<Allocator>::delayed_free_mode;

template<class Allocator>
uint32_t MemtxAllocator<Allocator>::snapshot_version;

void
memtx_allocators_init(struct allocator_settings *settings);

void
memtx_allocators_destroy();

/** Call enter_delayed_free_mode for each MemtxAllocator. */
void
memtx_allocators_enter_delayed_free_mode();

/** Call leave_delayed_free_mode for each MemtxAllocator. */
void
memtx_allocators_leave_delayed_free_mode();

using memtx_allocators = std::tuple<MemtxAllocator<SmallAlloc>,
				    MemtxAllocator<SysAlloc>>;

template<class F, class...Arg>
static void
foreach_memtx_allocator(Arg&&...arg)
{
	F f;
	foreach_allocator_internal((memtx_allocators *) nullptr, f,
				   std::forward<Arg>(arg)...);
}
