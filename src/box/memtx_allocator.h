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
#include "salad/stailq.h"
#include "tuple.h"

/**
 * Memtx tuple sub-class.
 *
 * FIXME(gh-7422): Make this struct packed.
 */
struct memtx_tuple {
	/*
	 * Sic: The header of the tuple is used to store a link in
	 * a tuple garbage collection list. Please don't change it
	 * without understanding how tuple garbage collection and
	 * copy-on-write mechanisms work.
	 */
	union {
		struct {
			/**
			 * Most recent read view's version at the time
			 * when the tuple was allocated.
			 */
			uint32_t version;
			/** Base tuple class. */
			struct tuple base;
		};
		/** Link in garbage collection list. */
		struct stailq_entry in_gc;
	};
};

/** Memtx read view options. */
struct memtx_read_view_opts {};

template<class Allocator>
class MemtxAllocator {
public:
	/**
	 * Tuple read view.
	 *
	 * Opening a read view pins tuples that were allocated before
	 * the read view was created. See open_read_view().
	 */
	struct ReadView {};

	static void create()
	{
		stailq_create(&gc);
	}

	static void destroy()
	{
		while (!stailq_empty(&gc)) {
			struct memtx_tuple *memtx_tuple = stailq_shift_entry(
					&gc, struct memtx_tuple, in_gc);
			immediate_free_tuple(memtx_tuple);
		}
	}

	/**
	 * Opens a tuple read view: tuples visible from the read view
	 * (allocated before the read view was created) won't be freed
	 * until the read view is closed with close_read_view().
	 */
	static ReadView *open_read_view(struct memtx_read_view_opts opts)
	{
		(void)opts;
		read_view_version++;
		delayed_free_mode++;
		return nullptr;
	}

	/**
	 * Closes a tuple read view opened with open_read_view().
	 */
	static void close_read_view(ReadView *rv)
	{
		assert(rv == nullptr);
		(void)rv;
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
		memtx_tuple->version = read_view_version;
		return &memtx_tuple->base;
	}

	/**
	 * Free a tuple allocated with alloc_tuple().
	 *
	 * The tuple is freed immediately if there's no read view that may use
	 * it. Otherwise, it's put in the garbage collection list to be free as
	 * soon as the last read view using it is destroyed.
	 */
	static void free_tuple(struct tuple *tuple)
	{
		struct memtx_tuple *memtx_tuple = container_of(
			tuple, struct memtx_tuple, base);
		if (delayed_free_mode == 0 ||
		    memtx_tuple->version == read_view_version ||
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
		stailq_add_entry(&gc, memtx_tuple, in_gc);
	}

	static void collect_garbage()
	{
		if (delayed_free_mode > 0)
			return;
		for (int i = 0; !stailq_empty(&gc) && i < GC_BATCH_SIZE; i++) {
			struct memtx_tuple *memtx_tuple = stailq_shift_entry(
					&gc, struct memtx_tuple, in_gc);
			immediate_free_tuple(memtx_tuple);
		}
	}

	/**
	 * Tuple garbage collection list. Contains tuples that were not freed
	 * immediately because they are currently in use by a read view.
	 */
	static struct stailq gc;
	/**
	 * Unless zero, freeing of tuples allocated before the last call to
	 * open_read_view() is delayed until close_read_view() is called.
	 */
	static uint32_t delayed_free_mode;
	/**
	 * Most recent read view's version.
	 *
	 * Incremented with each open read view. Not supposed to wrap around.
	 */
	static uint32_t read_view_version;
};

template<class Allocator>
struct stailq MemtxAllocator<Allocator>::gc;

template<class Allocator>
uint32_t MemtxAllocator<Allocator>::delayed_free_mode;

template<class Allocator>
uint32_t MemtxAllocator<Allocator>::read_view_version;

void
memtx_allocators_init(struct allocator_settings *settings);

void
memtx_allocators_destroy();

using memtx_allocators = std::tuple<MemtxAllocator<SmallAlloc>,
				    MemtxAllocator<SysAlloc>>;

using memtx_allocators_read_view =
		std::tuple<MemtxAllocator<SmallAlloc>::ReadView *,
			   MemtxAllocator<SysAlloc>::ReadView *>;

/** Opens a read view for each MemtxAllocator. */
memtx_allocators_read_view
memtx_allocators_open_read_view(struct memtx_read_view_opts opts);

/** Closes a read view for each MemtxAllocator. */
void
memtx_allocators_close_read_view(memtx_allocators_read_view rv);

template<class F, class...Arg>
static void
foreach_memtx_allocator(Arg&&...arg)
{
	F f;
	foreach_allocator_internal((memtx_allocators *) nullptr, f,
				   std::forward<Arg>(arg)...);
}
