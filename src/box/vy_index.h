#ifndef INCLUDES_TARANTOOL_BOX_VY_INDEX_H
#define INCLUDES_TARANTOOL_BOX_VY_INDEX_H
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <small/rlist.h>

#include "key_def.h"
#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"
#include "vy_cache.h"
#include "vy_range.h"
#include "vy_stat.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct histogram;
struct Index;
struct lsregion;
struct space;
struct tuple;
struct tuple_format;
struct vy_index;
struct vy_mem;
struct vy_recovery;
struct vy_run;

typedef void
(*vy_upsert_thresh_cb)(struct vy_index *index, struct tuple *stmt, void *arg);

/** Common index environment. */
struct vy_index_env {
	/** Path to the data directory. */
	const char *path;
	/** Memory allocator. */
	struct lsregion *allocator;
	/** Memory generation counter. */
	int64_t *p_generation;
	/** Tuple format for keys (SELECT). */
	struct tuple_format *key_format;
	/**
	 * Callback invoked when the number of upserts for
	 * the same key exceeds VY_UPSERT_THRESHOLD.
	 */
	vy_upsert_thresh_cb upsert_thresh_cb;
	/** Argument passed to upsert_thresh_cb. */
	void *upsert_thresh_arg;
};

/** Create a common index environment. */
int
vy_index_env_create(struct vy_index_env *env, const char *path,
		    struct lsregion *allocator, int64_t *p_generation,
		    vy_upsert_thresh_cb upsert_thresh_cb,
		    void *upsert_thresh_arg);

/** Destroy a common index environment. */
void
vy_index_env_destroy(struct vy_index_env *env);

/**
 * A struct for primary and secondary Vinyl indexes.
 *
 * Vinyl primary and secondary indexes work differently:
 *
 * - the primary index is fully covering (also known as
 *   "clustered" in MS SQL circles).
 *   It stores all tuple fields of the tuple coming from
 *   INSERT/REPLACE/UPDATE/DELETE operations. This index is
 *   the only place where the full tuple is stored.
 *
 * - a secondary index only stores parts participating in the
 *   secondary key, coalesced with parts of the primary key.
 *   Duplicate parts, i.e. identical parts of the primary and
 *   secondary key are only stored once. (@sa key_def_merge
 *   function). This reduces the disk and RAM space necessary to
 *   maintain a secondary index, but adds an extra look-up in the
 *   primary key for every fetched tuple.
 *
 * When a search in a secondary index is made, we first look up
 * the secondary index tuple, containing the primary key, and then
 * use this key to find the original tuple in the primary index.

 * While the primary index has only one key_def that is
 * used for validating and comparing tuples, secondary index needs
 * two:
 *
 * - the first one is defined by the user. It contains the key
 *   parts of the secondary key, as present in the original tuple.
 *   This is user_key_def.
 *
 * - the second one is used to fetch key parts of the secondary
 *   key, *augmented* with the parts of the primary key from the
 *   original tuple and compare secondary index tuples. These
 *   parts concatenated together construe the tuple of the
 *   secondary key, i.e. the tuple stored. This is key_def.
 */
struct vy_index {
	/** Common index environment. */
	struct vy_index_env *env;
	/**
	 * Reference counter. Used to postpone index deletion
	 * until all pending operations have completed.
	 */
	int refs;
	/** Index ID visible to the user. */
	uint32_t id;
	/** ID of the space this index belongs to. */
	uint32_t space_id;
	/** Index options. */
	struct index_opts opts;
	/** Key definition used to compare tuples. */
	struct key_def *key_def;
	/** Key definition passed by the user. */
	struct key_def *user_key_def;
	/** Tuple format corresponding to key_def. */
	struct tuple_format *surrogate_format;
	/** Tuple format of the space this index belongs to. */
	struct tuple_format *space_format;
	/**
	 * Format for tuples of type REPLACE or DELETE which
	 * are a part of an UPDATE operation.
	 */
	struct tuple_format *space_format_with_colmask;
	/*
	 * Format for UPSERT statements. Note, UPSERTs can only
	 * appear in spaces with a single index.
	 */
	struct tuple_format *upsert_format;
	/** Number of indexes in the space. */
	uint32_t space_index_count;
	/**
	 * Primary index of the same space or NULL if this index
	 * is primary. Referenced by each secondary index.
	 */
	struct vy_index *pk;
	/** Index statistics. */
	struct vy_index_stat stat;
	/**
	 * Merge cache of this index. Contains hottest tuples
	 * with continuation markers.
	 */
	struct vy_cache cache;
	/** Active in-memory index, i.e. the one used for insertions. */
	struct vy_mem *mem;
	/**
	 * List of sealed in-memory indexes, i.e. indexes that can't be
	 * inserted into, only read from, linked by vy_mem->in_sealed.
	 * The newer an index, the closer it to the list head.
	 */
	struct rlist sealed;
	/**
	 * Tree of all ranges of this index, linked by
	 * vy_range->tree_node, ordered by vy_range->begin.
	 */
	vy_range_tree_t *tree;
	/** Number of ranges in this index. */
	int range_count;
	/** Heap of ranges, prioritized by compact_priority. */
	heap_t range_heap;
	/**
	 * List of all runs created for this index,
	 * linked by vy_run->in_index.
	 */
	struct rlist runs;
	/** Number of entries in all ranges. */
	int run_count;
	/**
	 * Histogram accounting how many ranges of the index
	 * have a particular number of runs.
	 */
	struct histogram *run_hist;
	/**
	 * Incremented for each change of the mem list,
	 * to invalidate iterators.
	 */
	uint32_t mem_list_version;
	/**
	 * Incremented for each change of the range list,
	 * to invalidate iterators.
	 */
	uint32_t range_tree_version;
	/**
	 * LSN of the last dump or -1 if the index has not
	 * been dumped yet.
	 */
	int64_t dump_lsn;
	/**
	 * LSN of the row that committed the index or -1 if
	 * the index was not committed to the metadata log.
	 */
	int64_t commit_lsn;
	/**
	 * This flag is set if the index was dropped.
	 * It is also set on local recovery if the index
	 * will be dropped when WAL is replayed.
	 */
	bool is_dropped;
	/**
	 * Number of times the index was truncated.
	 *
	 * After recovery is complete, it equals space->truncate_count.
	 * On local recovery, it is loaded from the metadata log and may
	 * be greater than space->truncate_count, which indicates that
	 * the space is truncated in WAL.
	 */
	uint64_t truncate_count;
	/**
	 * If pin_count > 0 the index can't be scheduled for dump.
	 * Used to make sure that the primary index is dumped last.
	 */
	int pin_count;
	/** Set if the index is currently being dumped. */
	bool is_dumping;
	/** Link in vy_scheduler->dump_heap. */
	struct heap_node in_dump;
	/** Link in vy_scheduler->compact_heap. */
	struct heap_node in_compact;
};

/** Return index name. Used for logging. */
const char *
vy_index_name(struct vy_index *index);

/**
 * Extract vy_index from a VinylIndex object.
 * Defined in vinyl_index.cc
 */
struct vy_index *
vy_index(struct Index *index);

/**
 * Given a space and an index id, return vy_index.
 * If index not found, return NULL and set diag.
 */
struct vy_index *
vy_index_find(struct space *space, uint32_t id);

/**
 * Wrapper around vy_index_find() which ensures that
 * the found index is unique.
 */
struct vy_index *
vy_index_find_unique(struct space *space, uint32_t id);

/** Allocate a new index object. */
struct vy_index *
vy_index_new(struct vy_index_env *index_env, struct vy_cache_env *cache_env,
	     struct space *space, struct index_def *user_index_def);

/** Free an index object. */
void
vy_index_delete(struct vy_index *index);

/**
 * Increment the reference counter of a vinyl index.
 * An index cannot be deleted if its reference counter
 * is elevated.
 */
static inline void
vy_index_ref(struct vy_index *index)
{
	assert(index->refs >= 0);
	index->refs++;
}

/**
 * Decrement the reference counter of a vinyl index.
 * If the reference counter reaches 0, the index is
 * deleted with vy_index_delete().
 */
static inline void
vy_index_unref(struct vy_index *index)
{
	assert(index->refs > 0);
	if (--index->refs == 0)
		vy_index_delete(index);
}

/**
 * Swap disk contents (ranges, runs, and corresponding stats)
 * between two indexes. Used only on recovery, to skip reloading
 * indexes of a truncated space. The in-memory tree of the index
 * can't be populated - see vy_is_committed_one().
 */
void
vy_index_swap(struct vy_index *old_index, struct vy_index *new_index);

/** Initialize the range tree of a new index. */
int
vy_index_init_range_tree(struct vy_index *index);

/**
 * Create a new vinyl index.
 *
 * This function is called when an index is created after recovery
 * is complete or during remote recovery. It initializes the range
 * tree and makes the index directory.
 */
int
vy_index_create(struct vy_index *index);

/**
 * Load a vinyl index from disk. Called on local recovery.
 *
 * This function retrieves the index structure from the
 * metadata log, rebuilds the range tree, and opens run
 * files.
 *
 * If @snapshot_recovery is set, the index is recovered from
 * the last snapshot. In particular, this means that the index
 * must have been logged in the metadata log and so if the
 * function does not find it in the recovery context, it will
 * fail. If the flag is unset, the index is recovered from a
 * WAL, in which case a missing index is OK - it just means we
 * failed to log it before restart and have to retry during
 * WAL replay.
 *
 * @lsn is the LSN of the row that created the index.
 * If the index is recovered from a snapshot, it is set
 * to the snapshot signature.
 */
int
vy_index_recover(struct vy_index *index, struct vy_recovery *recovery,
		 int64_t lsn, bool snapshot_recovery);

/**
 * Return generation of in-memory data stored in an index
 * (min over vy_mem->generation).
 */
int64_t
vy_index_generation(struct vy_index *index);

/** Return max compact_priority among ranges of an index. */
int
vy_index_compact_priority(struct vy_index *index);

/** Add a run to the list of runs of an index. */
void
vy_index_add_run(struct vy_index *index, struct vy_run *run);

/** Remove a run from the list of runs of an index. */
void
vy_index_remove_run(struct vy_index *index, struct vy_run *run);

/**
 * Add a range to both the range tree and the range heap
 * of an index.
 */
void
vy_index_add_range(struct vy_index *index, struct vy_range *range);

/**
 * Remove a range from both the range tree and the range
 * heap of an index.
 */
void
vy_index_remove_range(struct vy_index *index, struct vy_range *range);

/** Account a range to the run histogram of an index. */
void
vy_index_acct_range(struct vy_index *index, struct vy_range *range);

/** Unaccount a range from the run histogram of an index. */
void
vy_index_unacct_range(struct vy_index *index, struct vy_range *range);

/**
 * Allocate a new active in-memory index for an index while moving
 * the old one to the sealed list. Used by the dump task in order
 * not to bother about synchronization with concurrent insertions
 * while an index is being dumped.
 */
int
vy_index_rotate_mem(struct vy_index *index);

/**
 * Remove an in-memory tree from the sealed list of a vinyl index,
 * unaccount and delete it.
 */
void
vy_index_delete_mem(struct vy_index *index, struct vy_mem *mem);

/**
 * Split a range if it has grown too big, return true if the range
 * was split. Splitting is done by making slices of the runs used
 * by the original range, adding them to new ranges, and reflecting
 * the change in the metadata log, i.e. it doesn't involve heavy
 * operations, like writing a run file, and is done immediately.
 */
bool
vy_index_split_range(struct vy_index *index, struct vy_range *range);

/**
 * Coalesce a range with one or more its neighbors if it is too small,
 * return true if the range was coalesced. We coalesce ranges by
 * splicing their lists of run slices and reflecting the change in the
 * log. No long-term operation involving a worker thread, like writing
 * a new run file, is necessary, because the merge iterator can deal
 * with runs that intersect by LSN coexisting in the same range as long
 * as they do not intersect for each particular key, which is true in
 * case of merging key ranges.
 */
bool
vy_index_coalesce_range(struct vy_index *index, struct vy_range *range);

/**
 * Insert a statement into the index's in-memory tree. If the
 * region_stmt is NULL and the statement is successfully inserted
 * then the new lsregion statement is returned via @a region_stmt.
 * Either vy_index_commit_stmt() or vy_index_rollback_stmt() must
 * be called on success.
 *
 * @param index       Index the statement is for.
 * @param mem         In-memory tree to insert the statement into.
 * @param stmt        Statement, allocated on malloc().
 * @param region_stmt NULL or the same statement, allocated on
 *                    lsregion.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
vy_index_set(struct vy_index *index, struct vy_mem *mem,
	     const struct tuple *stmt, const struct tuple **region_stmt);

/**
 * Confirm that the statement stays in the index's in-memory tree.
 *
 * @param index Index the statement is for.
 * @param mem   In-memory tree where the statement was saved.
 * @param stmt  Statement allocated from lsregion.
 */
void
vy_index_commit_stmt(struct vy_index *index, struct vy_mem *mem,
		     const struct tuple *stmt);

/**
 * Erase a statement from the index's in-memory tree.
 *
 * @param index Index to erase from.
 * @param mem   In-memory tree where the statement was saved.
 * @param stmt  Statement allocated from lsregion.
 */
void
vy_index_rollback_stmt(struct vy_index *index, struct vy_mem *mem,
		       const struct tuple *stmt);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_INDEX_H */
