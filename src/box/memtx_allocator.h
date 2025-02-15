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
#include "clock.h"
#include "clock_lowres.h"
#include "read_view.h"
#include "salad/stailq.h"
#include "small/rlist.h"
#include "tuple.h"

/**
 * Memtx tuple sub-class.
 */
struct PACKED memtx_tuple {
	/*
	 * Sic: The header of the tuple is used to store a link in
	 * a tuple garbage collection list. Please don't change it
	 * without understanding how tuple garbage collection and
	 * copy-on-write mechanisms work.
	 */
	union {
		struct PACKED {
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

static_assert(sizeof(struct memtx_tuple) % 4 == 2,
	      "required for tuple_has_extra");

/**
 * List of tuples owned by a read view.
 *
 * See the comment to memtx_tuple_rv for details.
 */
struct memtx_tuple_rv_list {
	/** Read view version. */
	uint32_t version;
	/** Total size of memory allocated for tuples stored in this list. */
	size_t mem_used;
	/** List of tuples, linked by memtx_tuple::in_gc. */
	struct stailq tuples;
};

/**
 * Tuple list array associated with a read view.
 *
 * When a read view is opened:
 * + We assign a unique incrementally growing version to it.
 * + We create and associate a list array with it. The array consists of one
 *   tuple list per each read view created so far, including the new read view.
 *
 * When a tuple is allocated, we store the most recent read view version in it.
 * This will allow us to check if it's visible by a read view when it's freed.
 *
 * When a tuple is freed:
 * 1. We look up the most recent open read view.
 * 2. If there's no open read views or the most recent open read view's version
 *    is <= the tuple's version, we free the tuple immediately, because it was
 *    allocated after the most recent open read view was opened.
 * 3. Otherwise, we add the tuple to the list that has the minimal version
 *    among all lists in the array such that list->version > tuple->version.
 *    In other words, we add it to the list corresponding to the oldest read
 *    view that can access the tuple.
 *
 * When a read view is closed:
 * 1. We look up the most recent read view older than the closed one.
 * 2. If there's no such read view, we free all tuples from the closed read
 *    view's lists.
 * 3. Otherwise,
 *    + We free all tuples from lists with version > the found read view's
 *      version, because those tuples were allocated after any older read
 *      view was opened and freed before any newer read view was opened.
 *    + We move tuples from all other lists to the corresponding list of
 *      the found read view.
 */
struct memtx_tuple_rv {
	/** Link in the list of all open read views. */
	struct rlist link;
	/** Reference counter. */
	int refs;
	/** Number of entries in the array. */
	int count;
	/**
	 * Array of tuple lists, one per each read view that were open at the
	 * time when this read view was created, including this read view.
	 * Ordered by read view version, ascending (the oldest read view comes
	 * first).
	 */
	struct memtx_tuple_rv_list lists[0];
};

/** Returns the read view version. */
static inline uint32_t
memtx_tuple_rv_version(struct memtx_tuple_rv *rv)
{
	/* Last list corresponds to self. */
	assert(rv->count > 0);
	return rv->lists[rv->count - 1].version;
}

/**
 * Not all read views need to access all kinds of tuples. For example, snapshot
 * isn't interested in temporary tuples. So we divide all tuples by type and
 * for each type maintain an independent list.
 */
enum memtx_tuple_rv_type {
	/** Tuples from non-data-temporary spaces. */
	memtx_tuple_rv_default,
	/** Tuples from data-temporary spaces. */
	memtx_tuple_rv_temporary,
	memtx_tuple_rv_type_MAX,
};

/**
 * Allocates a list array for a read view and initializes it using the list of
 * all open read views. Adds the new read view to the list.
 *
 * If the version of the most recent read view matches the new version,
 * the function will reuse it instead of creating a new one.
 */
struct memtx_tuple_rv *
memtx_tuple_rv_new(uint32_t version, struct rlist *list);

/**
 * Deletes a list array. Tuples that are still visible from other read views
 * are moved to the older read view's lists. Tuples that are not visible from
 * any read view are appended to the tuples_to_free list. Size of memory that
 * can be freed is stored in mem_freed.
 */
void
memtx_tuple_rv_delete(struct memtx_tuple_rv *rv, struct rlist *list,
		      struct stailq *tuples_to_free, size_t *mem_freed);

/**
 * Adds a freed tuple to a read view's list and returns true.
 *
 * The tuple must be visible from some read view, that is the tuple version
 * must be < than the most recent open read view.
 */
void
memtx_tuple_rv_add(struct memtx_tuple_rv *rv, struct memtx_tuple *tuple,
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
	 * Tuple read view.
	 *
	 * Opening a read view pins tuples that were allocated before
	 * the read view was created. See open_read_view().
	 */
	struct ReadView {
		/** Lists of tuples owned by this read view. */
		struct memtx_tuple_rv *rv[memtx_tuple_rv_type_MAX];
	};

	/** Memory usage statistics. */
	static struct memtx_allocator_stats stats;

	static void create()
	{
		memtx_allocator_stats_create(&stats);
		stailq_create(&gc);
		for (int type = 0; type < memtx_tuple_rv_type_MAX; type++)
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
	 * Opens a tuple read view: tuples visible from the read view
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
		for (int type = 0; type < memtx_tuple_rv_type_MAX; type++) {
			if (!opts->enable_data_temporary_spaces &&
			    type == memtx_tuple_rv_temporary)
				continue;
			rv->rv[type] = memtx_tuple_rv_new(read_view_version,
							  &read_views[type]);
		}
		return rv;
	}

	/**
	 * Closes a tuple read view opened with open_read_view().
	 */
	static void close_read_view(ReadView *rv)
	{
		for (int type = 0; type < memtx_tuple_rv_type_MAX; type++) {
			if (rv->rv[type] == nullptr)
				continue;
			size_t mem_freed = 0;
			memtx_tuple_rv_delete(rv->rv[type], &read_views[type],
					      &gc, &mem_freed);
			assert(stats.used_rv >= mem_freed);
			stats.used_rv -= mem_freed;
			stats.used_gc += mem_freed;
		}
		TRASH(rv);
		::free(rv);
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
		/* Use low-resolution clock, because it's hot path. */
		double now = clock_lowres_monotonic();
		if (read_view_version > 0 && read_view_reuse_interval > 0 &&
		    now - read_view_timestamp < read_view_reuse_interval) {
			/* See the comment to read_view_reuse_interval. */
			memtx_tuple->version = read_view_version - 1;
		} else {
			memtx_tuple->version = read_view_version;
			may_reuse_read_view = false;
		}
		return &memtx_tuple->base;
	}

	/**
	 * Free a tuple allocated with alloc_tuple().
	 *
	 * The tuple is freed immediately if there's no read view that may use
	 * it. Otherwise, it's put in a read view's list to be free as soon as
	 * the last read view using it is destroyed.
	 */
	static void free_tuple(struct tuple *tuple)
	{
		size_t size = tuple_size(tuple) +
			      offsetof(struct memtx_tuple, base);
		struct memtx_tuple *memtx_tuple = container_of(
			tuple, struct memtx_tuple, base);
		struct memtx_tuple_rv *rv = tuple_rv_last(tuple);
		if (rv == nullptr ||
		    memtx_tuple->version >= memtx_tuple_rv_version(rv)) {
			tuple_field_map_destroy(tuple);
			free(memtx_tuple, size);
		} else {
			stats.used_rv += size;
			memtx_tuple_rv_add(rv, memtx_tuple, size);
		}
	}

	/**
	 * Does a garbage collection step. Returns false if there's no more
	 * tuples to collect.
	 */
	static bool collect_garbage()
	{
		for (int i = 0; !stailq_empty(&gc) && i < GC_BATCH_SIZE; i++) {
			struct memtx_tuple *memtx_tuple = stailq_shift_entry(
					&gc, struct memtx_tuple, in_gc);
			size_t size = tuple_size(&memtx_tuple->base) +
				      offsetof(struct memtx_tuple, base);
			assert(stats.used_gc >= size);
			stats.used_gc -= size;
			tuple_field_map_destroy(&memtx_tuple->base);
			free(memtx_tuple, size);
		}
		return !stailq_empty(&gc);
	}

private:
	static constexpr int GC_BATCH_SIZE = 100;

	static void free(void *ptr, size_t size)
	{
		assert(stats.used_total >= size);
		stats.used_total -= size;
		Allocator::free(ptr, size);
	}

	static void *alloc(size_t size)
	{
		collect_garbage();
		void *ptr = Allocator::alloc(size);
		if (ptr != NULL)
			stats.used_total += size;
		return ptr;
	}

	/**
	 * Returns the most recent open read view that needs this tuple or null
	 * if the tuple may be freed immediately.
	 */
	static struct memtx_tuple_rv *
	tuple_rv_last(struct tuple *tuple)
	{
		struct rlist *list = tuple_has_flag(tuple, TUPLE_IS_TEMPORARY) ?
			&read_views[memtx_tuple_rv_temporary] :
			&read_views[memtx_tuple_rv_default];
		if (rlist_empty(list))
			return nullptr;
		return rlist_last_entry(list, struct memtx_tuple_rv, link);
	}

	/**
	 * List of freed tuples that were not freed immediately, because
	 * they were in use by a read view, linked in by memtx_tuple::in_gc.
	 * We collect tuples from this list on allocation.
	 */
	static struct stailq gc;
	/**
	 * Most recent read view's version.
	 *
	 * Incremented with each open read view. Not supposed to wrap around.
	 */
	static uint32_t read_view_version;
	/**
	 * List of memtx_tuple_rv objects, ordered by read view version,
	 * ascending (the oldest read view comes first).
	 */
	static struct rlist read_views[];
	/**
	 * If the last read view was created less than read_view_reuse_interval
	 * seconds ago, reuse it instead of creating a new one. Setting to 0
	 * effectively disables read view reusing.
	 *
	 * We reuse read views to ensure that read_view_version never wraps
	 * around. Here's how it works. When a tuple is allocated, we compare
	 * the current time with the time when the most recent read view was
	 * opened. If the difference is less than the reuse interval, we assign
	 * the previous read view version to it, read_view_version - 1, instead
	 * of read_view_version, like it was allocated before the last read
	 * view was created.
	 *
	 * When a read view is opened, we check if there were any tuples
	 * allocated with the current read_view_version. If such tuples exist,
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
	 * tuples were allocated with the current value of read_view_version).
	 * See also read_view_reuse_interval.
	 */
	static bool may_reuse_read_view;
};

template<class Allocator>
struct stailq MemtxAllocator<Allocator>::gc;

template<class Allocator>
uint32_t MemtxAllocator<Allocator>::read_view_version;

template<class Allocator>
struct rlist MemtxAllocator<Allocator>::read_views[memtx_tuple_rv_type_MAX];

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
