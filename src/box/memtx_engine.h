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
#include <small/mempool.h>

#include "engine.h"
#include "xlog.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct index;

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
	/** Memory pool for tree index iterator. */
	struct mempool tree_iterator_pool;
	/** Memory pool for rtree index iterator. */
	struct mempool rtree_iterator_pool;
	/** Memory pool for hash index iterator. */
	struct mempool hash_iterator_pool;
	/** Memory pool for bitset index iterator. */
	struct mempool bitset_iterator_pool;
};

struct memtx_engine *
memtx_engine_new(const char *snap_dirname, bool force_recovery,
		 uint64_t tuple_arena_max_size,
		 uint32_t objsize_min, float alloc_factor);

int
memtx_engine_recover_snapshot(struct memtx_engine *memtx,
			      const struct vclock *vclock);

void
memtx_engine_set_snap_io_rate_limit(struct memtx_engine *memtx, double limit);

void
memtx_engine_set_max_tuple_size(struct memtx_engine *memtx, size_t max_size);

enum {
	MEMTX_EXTENT_SIZE = 16 * 1024,
	MEMTX_SLAB_SIZE = 4 * 1024 * 1024
};

/**
 * Initialize arena for indexes.
 * The arena is used for memtx_index_extent_alloc
 *  and memtx_index_extent_free.
 * Can be called several times, only first call do the work.
 */
void
memtx_index_arena_init(void);

/**
 * Allocate a block of size MEMTX_EXTENT_SIZE for memtx index
 */
void *
memtx_index_extent_alloc(void *ctx);

/**
 * Free a block previously allocated by memtx_index_extent_alloc
 */
void
memtx_index_extent_free(void *ctx, void *extent);

/**
 * Reserve num extents in pool.
 * Ensure that next num extent_alloc will succeed w/o an error
 */
int
memtx_index_extent_reserve(int num);

/**
 * Free all tuples referenced by the given index.
 */
void
memtx_index_prune(struct index *index);

/*
 * The following two methods are used by all kinds of memtx indexes
 * to delete tuples stored in the space when the primary index is
 * destroyed.
 */
void
memtx_index_abort_create(struct index *index);
void
memtx_index_commit_drop(struct index *index);

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
		    uint32_t objsize_min, float alloc_factor)
{
	struct memtx_engine *memtx;
	memtx = memtx_engine_new(snap_dirname, force_recovery,
				 tuple_arena_max_size,
				 objsize_min, alloc_factor);
	if (memtx == NULL)
		diag_raise();
	return memtx;
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
