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
#include <small/matras.h>

#include "engine.h"
#include "xlog.h"
#include "salad/stailq.h"
#include "sysalloc.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct index;
struct index_read_view;
struct info_handler;
struct iterator;
struct fiber;
struct read_view_tuple;
struct tuple;
struct tuple_format;
struct memtx_tx_snapshot_cleaner;

/**
 * Recovery state of memtx engine.
 *
 * For faster recovery an optimization is used: initial recovery
 * assumes write-only flow of sorted rows from a snapshot.
 * It's followed by a final recovery state for read-write recovery
 * of rows from the write ahead log; these rows are inserted only into
 * the primary key.
 * When recovery is finished all spaces become fully functional.
 *
 * Note that this state describes only optimization state of recovery.
 * For instance in case of force recovery the state is set to MEMTX_OK
 * nearly in the start before snapshot loading.
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
#define MEMTX_ITERATOR_SIZE (176)

typedef void
(*memtx_on_indexes_built_cb)(void);

struct memtx_engine {
	struct engine base;
	/** Engine recovery state, see enum memtx_recovery_state description. */
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
	/**
	 * A callback run once memtx engine builds secondary indexes for the
	 * data.
	 */
	memtx_on_indexes_built_cb on_indexes_built_cb;
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
	/** Slab cache for allocating index extents. */
	struct slab_cache index_slab_cache;
	/** Index extent allocator. */
	struct mempool index_extent_pool;
	/** Index extent allocator statistics. */
	struct matras_stats index_extent_stats;
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
	/**
	 * Format used for allocating functional index keys.
	 */
	struct tuple_format *func_key_format;
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
		 uint64_t tuple_arena_max_size, uint32_t objsize_min,
		 bool dontdump, unsigned granularity,
		 const char *allocator, float alloc_factor,
		 memtx_on_indexes_built_cb on_indexes_built);

/**
 * Memtx engine statistics (box.stat.memtx()).
 */
void
memtx_engine_stat(struct memtx_engine *memtx, struct info_handler *h);

int
memtx_engine_recover_snapshot(struct memtx_engine *memtx,
			      const struct vclock *vclock);

void
memtx_engine_set_snap_io_rate_limit(struct memtx_engine *memtx, double limit);

int
memtx_engine_set_memory(struct memtx_engine *memtx, size_t size);

void
memtx_engine_set_max_tuple_size(struct memtx_engine *memtx, size_t max_size);

/** Tuple format vtab for memtx engine. */
extern struct tuple_format_vtab memtx_tuple_format_vtab;

enum {
	MEMTX_EXTENT_SIZE = 16 * 1024,
	MEMTX_SLAB_SIZE = 4 * 1024 * 1024
};

/**
 * Allocate and return new memtx tuple. Data validation depends
 * on @a validate value. On error returns NULL and set diag.
 */
extern struct tuple *
(*memtx_tuple_new_raw)(struct tuple_format *format, const char *data,
		       const char *end, bool validate);

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

void
memtx_set_tuple_format_vtab(const char *allocator_name);

/**
 * Converts a tuple from format in which it is stored in space
 * to format in which, it should be visible for users.
 */
int
memtx_prepare_result_tuple(struct tuple **result);

/**
 * Prepares a tuple retrieved from a consistent index read view to be returned
 * to the user.
 *
 * A pointer to the raw tuple data and its size are returned in the result
 * argument.
 *
 * This function performs two tasks:
 *
 *  1. Eliminates dirty tuples using the provided snapshot cleaner created
 *     at read_view_open. If the given tuple should be skipped, the data is
 *     set to NULL.
 *
 *  2. Decompresses tuples if required, unless the disable_decompression flag
 *     was set in the read view options. Decompressed tuple data is stored on
 *     the fiber region.
 *
 * Returns 0 on success. On error returns -1 and sets diag.
 */
int
memtx_prepare_read_view_tuple(struct tuple *tuple,
			      struct index_read_view *index,
			      struct memtx_tx_snapshot_cleaner *cleaner,
			      struct read_view_tuple *result);

/**
 * Common function for all memtx indexes. Get tuple from memtx @a index
 * and return it in @a result in format in which, it should be visible for
 * users.
 */
int
memtx_index_get(struct index *index, const char *key, uint32_t part_count,
		struct tuple **result);

/**
 * Common function for all memtx indexes. Iterate to the next tuple and
 * return it in @a ret in format in which, it should be visible for users.
 */
int
memtx_iterator_next(struct iterator *it, struct tuple **ret);

/*
 * Check tuple data correspondence to the space format.
 * Same as simple tuple_validate function, but can work
 * with compressed tuples.
 * @param format Format to which the tuple must match.
 * @param tuple  Tuple to validate.
 *
 * @retval  0 The tuple is valid.
 * @retval -1 The tuple is invalid.
 */
int
memtx_tuple_validate(struct tuple_format *format, struct tuple *tuple);

#if defined(__cplusplus)
} /* extern "C" */

#include "diag.h"

static inline struct memtx_engine *
memtx_engine_new_xc(const char *snap_dirname, bool force_recovery,
		    uint64_t tuple_arena_max_size, uint32_t objsize_min,
		    bool dontdump, unsigned granularity,
		    const char *allocator, float alloc_factor,
		    memtx_on_indexes_built_cb on_indexes_built)
{
	struct memtx_engine *memtx;
	memtx = memtx_engine_new(snap_dirname, force_recovery,
				 tuple_arena_max_size, objsize_min, dontdump,
				 granularity, allocator, alloc_factor,
				 on_indexes_built);
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
