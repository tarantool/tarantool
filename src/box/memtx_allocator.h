/*
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
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
#pragma once

#include "allocator.h"
#include "clock.h"
#include "clock_lowres.h"
#include "read_view.h"
#include "salad/stailq.h"
#include "small/rlist.h"
#include "tuple.h"

/**
 * Block of allocated memory, with read view support.
 */
struct PACKED memtx_block {
	/*
	 * Sic: The header of the block is used to store a link in
	 * a block garbage collection list. Please don't change it
	 * without understanding how block garbage collection and
	 * copy-on-write mechanisms work.
	 */
	union {
		struct PACKED {
			/**
			 * Most recent read view's version at the time
			 * when the block was allocated.
			 */
			uint32_t version;
			/**
			 * The block shares the header with the tuple. Fields
			 * data_offset_bsize_raw and bsize_bulky are initialized
			 * during allocation. If the block is actually used to
			 * store a tuple, one must call tuple_create_base() to
			 * initialize the rest of the tuple header.
			 */
			struct tuple header;
		};
		/** Link in garbage collection list. */
		struct stailq_entry in_gc;
	};
};

static_assert(sizeof(struct memtx_block) == 14, "Just to be sure");

/**
 * Returns total size of a block: the header (including the version) + padding
 * for alignment + data size.
 */
static inline size_t
memtx_block_size(struct memtx_block *block)
{
	return offsetof(struct memtx_block, header) +
		tuple_size(&block->header);
}

/** Returns the block that stores the given tuple. */
static inline struct memtx_block *
memtx_block_from_tuple(struct tuple *tuple)
{
	return container_of(tuple, struct memtx_block, header);
}

/** Returns the pointer to the tuple that is stored in the block. */
static inline struct tuple *
memtx_block_to_tuple(struct memtx_block *block)
{
	return &block->header;
}

/** Returns the pointer to the allocated data, not including the header. */
static inline void *
memtx_block_data(struct memtx_block *block)
{
	uint16_t data_offset = tuple_data_offset(&block->header);
	assert(data_offset >= sizeof(block->header));
	return (char *)&block->header + data_offset;
}

/**
 * List of blocks owned by a read view.
 *
 * See the comment to memtx_block_rv for details.
 */
struct memtx_block_rv_list {
	/** Read view version. */
	uint32_t version;
	/** Total size of memory allocated for blocks stored in this list. */
	size_t mem_used;
	/** List of blocks, linked by memtx_block::in_gc. */
	struct stailq blocks;
};

/**
 * Block list array associated with a read view.
 *
 * When a read view is opened:
 * + We assign a unique incrementally growing version to it.
 * + We create and associate a list array with it. The array consists of one
 *   block list per each read view created so far, including the new read view.
 *
 * When a block is allocated, we store the most recent read view version in it.
 * This will allow us to check if it's visible by a read view when it's freed.
 *
 * When a block is freed:
 * 1. We look up the most recent open read view.
 * 2. If there's no open read views or the most recent open read view's version
 *    is <= the block's version, we free the block immediately, because it was
 *    allocated after the most recent open read view was opened.
 * 3. Otherwise, we add the block to the list that has the minimal version
 *    among all lists in the array such that list->version > block->version.
 *    In other words, we add it to the list corresponding to the oldest read
 *    view that can access the block.
 *
 * When a read view is closed:
 * 1. We look up the most recent read view older than the closed one.
 * 2. If there's no such read view, we free all blocks from the closed read
 *    view's lists.
 * 3. Otherwise,
 *    + We free all blocks from lists with version > the found read view's
 *      version, because those blocks were allocated after any older read
 *      view was opened and freed before any newer read view was opened.
 *    + We move blocks from all other lists to the corresponding list of
 *      the found read view.
 */
struct memtx_block_rv {
	/** Link in the list of all open read views. */
	struct rlist link;
	/** Reference counter. */
	int refs;
	/** Number of entries in the array. */
	int count;
	/**
	 * Array of block lists, one per each read view that were open at the
	 * time when this read view was created, including this read view.
	 * Ordered by read view version, ascending (the oldest read view comes
	 * first).
	 */
	struct memtx_block_rv_list lists[0];
};

/** Returns the read view version. */
static inline uint32_t
memtx_block_rv_version(struct memtx_block_rv *rv)
{
	/* Last list corresponds to self. */
	assert(rv->count > 0);
	return rv->lists[rv->count - 1].version;
}

/**
 * Not all read views need to access all kinds of blocks. For example, snapshot
 * isn't interested in temporary blocks. So we divide all blocks by type and
 * for each type maintain an independent list.
 */
enum memtx_block_rv_type {
	/** Blocks from non-data-temporary spaces. */
	memtx_block_rv_default,
	/** Blocks from data-temporary spaces. */
	memtx_block_rv_temporary,
	memtx_block_rv_type_MAX,
};

/**
 * Allocates a list array for a read view and initializes it using the list of
 * all open read views. Adds the new read view to the list.
 *
 * If the version of the most recent read view matches the new version,
 * the function will reuse it instead of creating a new one.
 */
struct memtx_block_rv *
memtx_block_rv_new(uint32_t version, struct rlist *list);

/**
 * Deletes a list array. Blocks that are still visible from other read views
 * are moved to the older read view's lists. Blocks that are not visible from
 * any read view are appended to the blocks_to_free list. Size of memory that
 * can be freed is stored in mem_freed.
 */
void
memtx_block_rv_delete(struct memtx_block_rv *rv, struct rlist *list,
		      struct stailq *blocks_to_free, size_t *mem_freed);

/**
 * Adds a freed block to a read view's list.
 *
 * The block must be visible from some read view, that is the block version
 * must be < than the most recent open read view.
 */
void
memtx_block_rv_add(struct memtx_block_rv *rv, struct memtx_block *block,
		   size_t mem_used);

/** MemtxAllocator statistics. */
struct memtx_allocator_stats {
	/** Total size of allocated memory. */
	size_t used_total;
	/** Size of memory held for read views. */
	size_t used_rv;
	/** Size of memory freed on demand. */
	size_t used_gc;
};

static inline void
memtx_allocator_stats_create(struct memtx_allocator_stats *stats)
{
	stats->used_total = 0;
	stats->used_rv = 0;
	stats->used_gc = 0;
}

/* Adds memory allocator statistics from src to dst. */
static inline void
memtx_allocator_stats_add(struct memtx_allocator_stats *dst,
			  const struct memtx_allocator_stats *src)
{
	dst->used_total += src->used_total;
	dst->used_rv += src->used_rv;
	dst->used_gc += src->used_gc;
}

template<class Allocator>
class MemtxAllocator {
public:
	/**
	 * Block read view.
	 *
	 * Opening a read view pins blocks that were allocated before
	 * the read view was created. See open_read_view().
	 */
	struct ReadView {
		/** Lists of blocks owned by this read view. */
		struct memtx_block_rv *rv[memtx_block_rv_type_MAX];
	};

	/** Memory usage statistics. */
	static struct memtx_allocator_stats stats;

	static void create()
	{
		memtx_allocator_stats_create(&stats);
		stailq_create(&gc);
		for (int type = 0; type < memtx_block_rv_type_MAX; type++)
			rlist_create(&read_views[type]);
	}

	static void destroy()
	{
		while (collect_garbage()) {
		}
	}

	/**
	 * Sets read_view_reuse_interval. Useful for testing.
	 */
	static void set_read_view_reuse_interval(double interval)
	{
		read_view_reuse_interval = interval;
	}

	/**
	 * Opens a block read view: blocks visible from the read view
	 * (allocated before the read view was created) won't be freed
	 * until the read view is closed with close_read_view().
	 */
	static ReadView *open_read_view(const struct read_view_opts *opts)
	{
		if (!may_reuse_read_view) {
			read_view_version++;
			may_reuse_read_view = true;
			read_view_timestamp = clock_monotonic();
		}
		ReadView *rv = (ReadView *)xcalloc(1, sizeof(*rv));
		for (int type = 0; type < memtx_block_rv_type_MAX; type++) {
			if (!opts->enable_data_temporary_spaces &&
			    type == memtx_block_rv_temporary)
				continue;
			rv->rv[type] = memtx_block_rv_new(read_view_version,
							  &read_views[type]);
		}
		return rv;
	}

	/**
	 * Closes a block read view opened with open_read_view().
	 */
	static void close_read_view(ReadView *rv)
	{
		for (int type = 0; type < memtx_block_rv_type_MAX; type++) {
			if (rv->rv[type] == nullptr)
				continue;
			size_t mem_freed = 0;
			memtx_block_rv_delete(rv->rv[type], &read_views[type],
					      &gc, &mem_freed);
			assert(stats.used_rv >= mem_freed);
			stats.used_rv -= mem_freed;
			stats.used_gc += mem_freed;
		}
		TRASH(rv);
		::free(rv);
	}

	/**
	 * True if a block is visible from at least one read view.
	 * In that case it cannot be modified.
	 */
	static bool
	in_read_view(const struct memtx_block *block, bool is_temporary = false)
	{
		struct memtx_block_rv *rv = get_last_rv(is_temporary);
		if (rv == nullptr)
			return false;
		return block->version < memtx_block_rv_version(rv);
	}

	/**
	 * Allocates a block of memory of the given `data_size' and returns its
	 * handle. By default `data_offset' equals the header size, however in
	 * compact mode the data overwrites 4 bytes of a header.
	 */
	static struct memtx_block *alloc(
		uint32_t data_size,
		uint16_t data_offset = sizeof(memtx_block::header),
		bool make_compact = false)
	{
		size_t total_size = offsetof(memtx_block, header) +
				    data_offset + data_size;
		struct memtx_block *block = alloc_impl(total_size);
		if (block != nullptr)
			tuple_set_data_offset_bsize(&block->header, data_offset,
						    data_size, make_compact);
		return block;
	}

	/**
	 * Allocates `data_size' bytes of memory with an alignment specified by
	 * `alignment' and returns a handle for that block. The alignment must
	 * be a power of two and greater than zero (asserted).
	 */
	static struct memtx_block *alloc_aligned(uint32_t data_size,
						 uint32_t alignment)
	{
		assert(IS_POWER_OF_2(alignment));
		uint16_t data_offset = sizeof(memtx_block::header);
		uint32_t over_alloc = alignment - 1;
		size_t total_size = offsetof(memtx_block, header) +
				    data_offset + data_size + over_alloc;
		struct memtx_block *block = alloc_impl(total_size);
		if (block == nullptr)
			return nullptr;
		uintptr_t data = (uintptr_t)&block->header + data_offset;
		uintptr_t aligned =
			ROUND_UP_TO_POWER_OF_2(data, (uintptr_t)alignment);
		data_offset += aligned - data;
		data_size += over_alloc - (aligned - data);
		tuple_set_data_offset_bsize(
			&block->header, data_offset, data_size, false);
		return block;
	}

	/**
	 * Frees a block allocated with alloc() or alloc_aligned().
	 *
	 * The block is freed immediately if there's no read view that may use
	 * it. Otherwise, it's put in a read view's list to be free as soon as
	 * the last read view using it is destroyed.
	 */
	static void free(struct memtx_block *block, bool is_temporary = false)
	{
		if (!in_read_view(block, is_temporary)) {
			free_impl(block);
		} else {
			size_t size = memtx_block_size(block);
			stats.used_rv += size;
			struct memtx_block_rv *rv = get_last_rv(is_temporary);
			memtx_block_rv_add(rv, block, size);
		}
	}

	/**
	 * Does a garbage collection step. Returns false if there's no more
	 * blocks to collect.
	 */
	static bool collect_garbage()
	{
		for (int i = 0; !stailq_empty(&gc) && i < GC_BATCH_SIZE; i++) {
			struct memtx_block *block = stailq_shift_entry(
					&gc, struct memtx_block, in_gc);
			size_t size = memtx_block_size(block);
			assert(stats.used_gc >= size);
			stats.used_gc -= size;
			free_impl(block);
		}
		return !stailq_empty(&gc);
	}

private:
	static constexpr int GC_BATCH_SIZE = 100;

	/** Calls Allocator::alloc(), updates statistics and block version. */
	static struct memtx_block *alloc_impl(size_t size)
	{
		collect_garbage();
		void *ptr = Allocator::alloc(size);
		if (ptr == nullptr)
			return nullptr;
		struct memtx_block *block = (struct memtx_block *)ptr;
		stats.used_total += size;
		/* Use low-resolution clock, because it's hot path. */
		double now = clock_lowres_monotonic();
		if (read_view_version > 0 && read_view_reuse_interval > 0 &&
		    now - read_view_timestamp < read_view_reuse_interval) {
			/* See the comment to read_view_reuse_interval. */
			block->version = read_view_version - 1;
		} else {
			block->version = read_view_version;
			may_reuse_read_view = false;
		}
		return block;
	}

	/** Calls Allocator::free() and updates statistics. */
	static void free_impl(struct memtx_block *block)
	{
		size_t size = memtx_block_size(block);
		assert(stats.used_total >= size);
		stats.used_total -= size;
		Allocator::free(block, size);
	}

	/** Returns the most recent open read view. */
	static struct memtx_block_rv *
	get_last_rv(bool is_temporary)
	{
		struct rlist *list = is_temporary ?
			&read_views[memtx_block_rv_temporary] :
			&read_views[memtx_block_rv_default];
		if (rlist_empty(list))
			return nullptr;
		return rlist_last_entry(list, struct memtx_block_rv, link);
	}

	/**
	 * List of freed blocks that were not freed immediately, because
	 * they were in use by a read view, linked in by memtx_block::in_gc.
	 * We collect blocks from this list on allocation.
	 */
	static struct stailq gc;
	/**
	 * Most recent read view's version.
	 *
	 * Incremented with each open read view. Not supposed to wrap around.
	 */
	static uint32_t read_view_version;
	/**
	 * List of memtx_block_rv objects, ordered by read view version,
	 * ascending (the oldest read view comes first).
	 */
	static struct rlist read_views[];
	/**
	 * If the last read view was created less than read_view_reuse_interval
	 * seconds ago, reuse it instead of creating a new one. Setting to 0
	 * effectively disables read view reusing.
	 *
	 * We reuse read views to ensure that read_view_version never wraps
	 * around. Here's how it works. When a block is allocated, we compare
	 * the current time with the time when the most recent read view was
	 * opened. If the difference is less than the reuse interval, we assign
	 * the previous read view version to it, read_view_version - 1, instead
	 * of read_view_version, like it was allocated before the last read
	 * view was created.
	 *
	 * When a read view is opened, we check if there were any blocks
	 * allocated with the current read_view_version. If such blocks exist,
	 * we proceed to creation of a new read view, as usual. Otherwise, we
	 * create a new read view with the previous read view's version
	 * (read_view_version, without bumping) and reuse its garbage
	 * collection lists (with reference counting).
	 */
	static double read_view_reuse_interval;
	/**
	 * Monotonic clock time when the most recent read view was opened.
	 * See also read_view_reuse_interval.
	 */
	static double read_view_timestamp;
	/**
	 * Set if the most recent read view may be reused (that is no new
	 * blocks were allocated with the current value of read_view_version).
	 * See also read_view_reuse_interval.
	 */
	static bool may_reuse_read_view;
};

template<class Allocator>
struct stailq MemtxAllocator<Allocator>::gc;

template<class Allocator>
uint32_t MemtxAllocator<Allocator>::read_view_version;

template<class Allocator>
struct rlist MemtxAllocator<Allocator>::read_views[memtx_block_rv_type_MAX];

template<class Allocator>
double MemtxAllocator<Allocator>::read_view_reuse_interval = 0.1;

template<class Allocator>
double MemtxAllocator<Allocator>::read_view_timestamp;

template<class Allocator>
bool MemtxAllocator<Allocator>::may_reuse_read_view;

template<class Allocator>
struct memtx_allocator_stats MemtxAllocator<Allocator>::stats;

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
memtx_allocators_open_read_view(const struct read_view_opts *opts);

/** Closes a read view for each MemtxAllocator. */
void
memtx_allocators_close_read_view(memtx_allocators_read_view rv);

/** Returns allocator statistics sum over all MemtxAllocators.  */
void
memtx_allocators_stats(struct memtx_allocator_stats *stats);

template<class F, class...Arg>
static void
foreach_memtx_allocator(Arg&&...arg)
{
	F f;
	foreach_allocator_internal((memtx_allocators *) nullptr, f,
				   std::forward<Arg>(arg)...);
}
