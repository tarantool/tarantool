#ifndef TARANTOOL_BOX_MEMTX_ENGINE_H_INCLUDED
#define TARANTOOL_BOX_MEMTX_ENGINE_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <small/quota.h>
#include <small/small.h>
#include <small/mempool.h>

#include "engine.h"
#include "xlog.h"
#include "salad/stailq.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct index;
struct fiber;
struct tuple;
struct tuple_format;

/**
 * The state of memtx recovery process.
 * There is a global state of the entire engine state of each
 * space. The state of a space is initialized from the engine
 * state when the space is created. The exception is system
 * spaces, which are always created in the final (OK) state.
 *
 * The states exist to speed up recovery: initial state
 * assumes write-only flow of sorted rows from a snapshot.
 * It's followed by a state for read-write recovery
 * of rows from the write ahead log; these rows are
 * inserted only into the primary key. The final
 * state is for a fully functional space.
 */
enum memtx_recovery_state {
	/** The space has no indexes. */
	MEMTX_INITIALIZED,
	/**
	 * The space has only the primary index, which is in
	 * write-only bulk insert mode.
	 */
	MEMTX_INITIAL_RECOVERY,
	/**
	 * The space has the primary index, which can be
	 * used for reads and writes, but secondary indexes are
	 * empty. The will be built at the end of recovery.
	 */
	MEMTX_FINAL_RECOVERY,
	/**
	 * The space and all its indexes are fully built.
	 */
	MEMTX_OK,
};

/** Memtx extents pool, available to statistics. */
extern struct mempool memtx_index_extent_pool;

enum memtx_reserve_extents_num {
	/**
	 * This number is calculated based on the
	 * max (realistic) number of insertions
	 * a deletion from a B-tree or an R-tree
	 * can lead to, and, as a result, the max
	 * number of new block allocations.
	 */
	RESERVE_EXTENTS_BEFORE_DELETE = 8,
	RESERVE_EXTENTS_BEFORE_REPLACE = 16
};

/**
 * The size of the biggest memtx iterator. Used with
 * mempool_create. This is the size of the block that will be
 * allocated for each iterator (except rtree index iterator that
 * is significantly bigger so has own pool).
 */
#define MEMTX_ITERATOR_SIZE (152)

struct memtx_engine {
	struct engine base;
	/** Engine recovery state. */
	enum memtx_recovery_state state;
	/** Non-zero if there is a checkpoint (snapshot) in progress. */
	struct checkpoint *checkpoint;
	/** The directory where to store snapshots. */
	struct xdir snap_dir;
	/** Limit disk usage of checkpointing (bytes per second). */
	uint64_t snap_io_rate_limit;
	/** Skip invalid snapshot records if this flag is set. */
	bool force_recovery;
	/**
	 * Cord being currently used to join replica. It is only
	 * needed to be able to cancel it on shutdown.
	 */
	struct cord *replica_join_cord;
	/** Common quota for tuples and indexes. */
	struct quota quota;
	/**
	 * Common slab arena for tuples and indexes.
	 * If you decide to use it for anything other than
	 * tuple_alloc or index_extent_pool, make sure this
	 * is reflected in box.slab.info(), @sa lua/slab.c.
	 */
	struct slab_arena arena;
	/** Slab cache for allocating tuples. */
	struct slab_cache slab_cache;
	/** Tuple allocator. */
	struct small_alloc alloc;
	/** Slab cache for allocating index extents. */
	struct slab_cache index_slab_cache;
	/** Index extent allocator. */
	struct mempool index_extent_pool;
	/**
	 * To ensure proper statement-level rollback in case
	 * of out of memory conditions, we maintain a number
	 * of slack memory extents reserved before a statement
	 * is begun. If there isn't enough slack memory,
	 * we don't begin the statement.
	 */
	int num_reserved_extents;
	void *reserved_extents;
	/** Maximal allowed tuple size, box.cfg.memtx_max_tuple_size. */
	size_t max_tuple_size;
	/** Incremented with each next snapshot. */
	uint32_t snapshot_version;
	/**
	 * Unless zero, freeing of tuples allocated before the last
	 * call to memtx_enter_delayed_free_mode() is delayed until
	 * memtx_leave_delayed_free_mode() is called.
	 */
	uint32_t delayed_free_mode;
	/** Memory pool for rtree index iterator. */
	struct mempool rtree_iterator_pool;
	/**
	 * Memory pool for all index iterators except rtree.
	 * The latter is significantly larger so it has its
	 * own memory pool.
	 */
	struct mempool iterator_pool;
	/**
	 * Garbage collection fiber. Used for asynchronous
	 * destruction of dropped indexes.
	 */
	struct fiber *gc_fiber;
	/**
	 * Scheduled garbage collection tasks, linked by
	 * memtx_gc_task::link.
	 */
	struct stailq gc_queue;
};

struct memtx_gc_task;

struct memtx_gc_task_vtab {
	/**
	 * Free some objects associated with @task. If @task has
	 * no more objects to free, set flag @done.
	 */
	void (*run)(struct memtx_gc_task *task, bool *done);
	/**
	 * Destroy @task.
	 */
	void (*free)(struct memtx_gc_task *task);
};

/** Garbage collection task. */
struct memtx_gc_task {
	/** Link in memtx_engine::gc_queue. */
	struct stailq_entry link;
	/** Virtual function table. */
	const struct memtx_gc_task_vtab *vtab;
};

/**
 * Schedule a garbage collection task for execution.
 */
void
memtx_engine_schedule_gc(struct memtx_engine *memtx,
			 struct memtx_gc_task *task);

struct memtx_engine *
memtx_engine_new(const char *snap_dirname, bool force_recovery,
		 uint64_t tuple_arena_max_size,
		 uint32_t objsize_min, bool dontdump,
		 unsigned granularity, float alloc_factor);

int
memtx_engine_recover_snapshot(struct memtx_engine *memtx,
			      const struct vclock *vclock);

void
memtx_engine_set_snap_io_rate_limit(struct memtx_engine *memtx, double limit);

int
memtx_engine_set_memory(struct memtx_engine *memtx, size_t size);

void
memtx_engine_set_max_tuple_size(struct memtx_engine *memtx, size_t max_size);

/**
 * Enter tuple delayed free mode: tuple allocated before the call
 * won't be freed until memtx_leave_delayed_free_mode() is called.
 * This function is reentrant, meaning it's okay to call it multiple
 * times from the same or different fibers - one just has to leave
 * the delayed free mode the same amount of times then.
 */
void
memtx_enter_delayed_free_mode(struct memtx_engine *memtx);

/**
 * Leave tuple delayed free mode. This function undoes the effect
 * of memtx_enter_delayed_free_mode().
 */
void
memtx_leave_delayed_free_mode(struct memtx_engine *memtx);

/** Allocate a memtx tuple. @sa tuple_new(). */
struct tuple *
memtx_tuple_new(struct tuple_format *format, const char *data, const char *end);

/** Free a memtx tuple. @sa tuple_delete(). */
void
memtx_tuple_delete(struct tuple_format *format, struct tuple *tuple);

/** Tuple format vtab for memtx engine. */
extern struct tuple_format_vtab memtx_tuple_format_vtab;

enum {
	MEMTX_EXTENT_SIZE = 16 * 1024,
	MEMTX_SLAB_SIZE = 4 * 1024 * 1024
};

/**
 * Allocate a block of size MEMTX_EXTENT_SIZE for memtx index
 * @ctx must point to memtx engine
 */
void *
memtx_index_extent_alloc(void *ctx);

/**
 * Free a block previously allocated by memtx_index_extent_alloc
 * @ctx must point to memtx engine
 */
void
memtx_index_extent_free(void *ctx, void *extent);

/**
 * Reserve num extents in pool.
 * Ensure that next num extent_alloc will succeed w/o an error
 */
int
memtx_index_extent_reserve(struct memtx_engine *memtx, int num);

/**
 * Generic implementation of index_vtab::def_change_requires_rebuild,
 * common for all kinds of memtx indexes.
 */
bool
memtx_index_def_change_requires_rebuild(struct index *index,
					const struct index_def *new_def);

#if defined(__cplusplus)
} /* extern "C" */

#include "diag.h"

static inline struct memtx_engine *
memtx_engine_new_xc(const char *snap_dirname, bool force_recovery,
		    uint64_t tuple_arena_max_size,
		    uint32_t objsize_min, bool dontdump,
		    unsigned granularity, float alloc_factor)
{
	struct memtx_engine *memtx;
	memtx = memtx_engine_new(snap_dirname, force_recovery,
				 tuple_arena_max_size,
				 objsize_min, dontdump,
				 granularity, alloc_factor);
	if (memtx == NULL)
		diag_raise();
	return memtx;
}

static inline void
memtx_engine_set_memory_xc(struct memtx_engine *memtx, size_t size)
{
	if (memtx_engine_set_memory(memtx, size) != 0)
		diag_raise();
}

static inline void
memtx_engine_recover_snapshot_xc(struct memtx_engine *memtx,
				 const struct vclock *vclock)
{
	if (memtx_engine_recover_snapshot(memtx, vclock) != 0)
		diag_raise();
}

#endif /* defined(__plusplus) */

#endif /* TARANTOOL_BOX_MEMTX_ENGINE_H_INCLUDED */
