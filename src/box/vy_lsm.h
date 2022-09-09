#ifndef INCLUDES_TARANTOOL_BOX_VY_LSM_H
#define INCLUDES_TARANTOOL_BOX_VY_LSM_H
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
#include <small/mempool.h>
#include <small/rlist.h>

#include "index.h"
#include "index_def.h"
#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"
#include "vy_entry.h"
#include "vy_cache.h"
#include "vy_range.h"
#include "vy_stat.h"
#include "vy_read_set.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct histogram;
struct tuple;
struct tuple_format;
struct vy_lsm;
struct vy_mem;
struct vy_mem_env;
struct vy_recovery;
struct vy_run;
struct vy_run_env;

typedef void
(*vy_upsert_thresh_cb)(struct vy_lsm *lsm, struct vy_entry entry, void *arg);

/** Common LSM tree environment. */
struct vy_lsm_env {
	/** Path to the data directory. */
	const char *path;
	/** Memory generation counter. */
	int64_t *p_generation;
	/** Tuple format for keys (SELECT). */
	struct tuple_format *key_format;
	/** Key (SELECT) with no parts. */
	struct vy_entry empty_key;
	/**
	 * If read of a single statement takes longer than
	 * the given value, warn about it in the log.
	 */
	double too_long_threshold;
	/**
	 * Callback invoked when the number of upserts for
	 * the same key exceeds VY_UPSERT_THRESHOLD.
	 */
	vy_upsert_thresh_cb upsert_thresh_cb;
	/** Argument passed to upsert_thresh_cb. */
	void *upsert_thresh_arg;
	/** Number of LSM trees in this environment. */
	int lsm_count;
	/** Size of memory used for bloom filters. */
	size_t bloom_size;
	/** Size of memory used for page index. */
	size_t page_index_size;
	/**
	 * Size of disk space used for storing data of all spaces,
	 * in bytes, without taking into account disk compression.
	 * By 'data' we mean statements stored in primary indexes
	 * only, which is consistent with space.bsize().
	 */
	int64_t disk_data_size;
	/**
	 * Size of disk space used for indexing data in all spaces,
	 * in bytes, without taking into account disk compression.
	 * This consists of page indexes and bloom filters, which
	 * are stored in .index files, as well as the total size of
	 * statements stored in secondary index .run files, which
	 * is consistent with index.bsize().
	 */
	int64_t disk_index_size;
	/**
	 * Min size of disk space required to store data of all
	 * spaces of the database. In other words, the size of
	 * disk space the database would occupy if all spaces were
	 * compacted and there were no indexes. Accounted in bytes,
	 * without taking into account disk compression. Estimated
	 * as the size of data stored in the last level of primary
	 * LSM trees. Along with disk_data_size and disk_index_size,
	 * it can be used for evaluating space amplification.
	 */
	int64_t compacted_data_size;
	/**
	 * Size of data of all spaces that need to be compacted,
	 * in bytes, without taking into account disk compression.
	 */
	int64_t compaction_queue_size;
	/** Memory pool for vy_history_node allocations. */
	struct mempool history_node_pool;
};

/** Create a common LSM tree environment. */
int
vy_lsm_env_create(struct vy_lsm_env *env, const char *path,
		  int64_t *p_generation, struct tuple_format *key_format,
		  vy_upsert_thresh_cb upsert_thresh_cb,
		  void *upsert_thresh_arg);

/** Destroy a common LSM tree environment. */
void
vy_lsm_env_destroy(struct vy_lsm_env *env);

/**
 * A struct for primary and secondary Vinyl indexes.
 * Named after the data structure used for organizing
 * data on disk - log-structured merge-tree (LSM tree).
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
 *
 * While the primary index has only one key_def that is
 * used for validating and comparing tuples, secondary index needs
 * two:
 *
 * - the first one is defined by the user. It contains the key
 *   parts of the secondary key, as present in the original tuple.
 *   This is key_def.
 *
 * - the second one is used to fetch key parts of the secondary
 *   key, *augmented* with the parts of the primary key from the
 *   original tuple and compare secondary index tuples. These
 *   parts concatenated together construe the tuple of the
 *   secondary key, i.e. the tuple stored. This is key_def.
 */
struct vy_lsm {
	struct index base;
	/** Common LSM tree environment. */
	struct vy_lsm_env *env;
	/** Unique ID of this LSM tree. */
	int64_t id;
	/** ID of the index this LSM tree is for. */
	uint32_t index_id;
	/** ID of the space this LSM tree is for. */
	uint32_t space_id;
	/** Replication group ID. */
	uint32_t group_id;
	/** Index options. */
	struct index_opts opts;
	/** Key definition used to compare tuples. */
	struct key_def *cmp_def;
	/** Key definition passed by the user. */
	struct key_def *key_def;
	/**
	 * Key definition to extract primary key parts from
	 * a secondary key. NULL if this LSM tree corresponds
	 * to a primary index.
	 */
	struct key_def *pk_in_cmp_def;
	/**
	 * Tuple format for tuples of this LSM tree created when
	 * reading pages from disk.
	 * Is distinct from mem_format only for secondary keys,
	 * whose tuples have MP_NIL in all "gap" positions between
	 * positions of the secondary and primary key fields.
	 * These gaps are necessary to make such tuples comparable
	 * with tuples from vy_mem, while using the same cmp_def.
	 * Since upserts are never present in secondary keys, is
	 * used only for REPLACE and DELETE
	 * tuples.
	 */
	struct tuple_format *disk_format;
	/** Tuple format of the space this LSM tree belongs to. */
	struct tuple_format *mem_format;
	/**
	 * If this LSM tree is for a secondary index, the following
	 * variable points to the LSM tree of the primary index of
	 * the same space, otherwise it is set to NULL. Referenced
	 * by each secondary index.
	 */
	struct vy_lsm *pk;
	/** LSM tree statistics. */
	struct vy_lsm_stat stat;
	/**
	 * Merge cache of this LSM tree. Contains hottest tuples
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
	 * Tree of all ranges of this LSM tree, linked by
	 * vy_range->tree_node, ordered by vy_range->begin.
	 */
	vy_range_tree_t range_tree;
	/** Number of ranges in this LSM tree. */
	int range_count;
	/** Sum dumps_per_compaction across all ranges. */
	int sum_dumps_per_compaction;
	/** Heap of ranges, prioritized by compaction_priority. */
	heap_t range_heap;
	/**
	 * List of all runs created for this LSM tree,
	 * linked by vy_run->in_lsm.
	 */
	struct rlist runs;
	/** Number of entries in all ranges. */
	int run_count;
	/**
	 * Histogram accounting how many ranges of the LSM tree
	 * have a particular number of runs.
	 */
	struct histogram *run_hist;
	/** Size of memory used for bloom filters. */
	size_t bloom_size;
	/** Size of memory used for page index. */
	size_t page_index_size;
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
	 * Max LSN stored on disk or -1 if the LSM tree has not
	 * been dumped yet.
	 */
	int64_t dump_lsn;
	/**
	 * LSN of the WAL row that created or last modified
	 * this LSM tree. We store it in vylog so that during
	 * local recovery we can replay vylog records we failed
	 * to log before restart.
	 */
	int64_t commit_lsn;
	/**
	 * This flag is set if the LSM tree was dropped.
	 * It is also set on local recovery if the LSM tree
	 * will be dropped when WAL is replayed.
	 */
	bool is_dropped;
	/**
	 * If pin_count > 0 the LSM tree can't be scheduled for dump.
	 * Used to make sure that the primary index is dumped last.
	 */
	int pin_count;
	/** Set if the LSM tree is currently being dumped. */
	bool is_dumping;
	/** Link in vy_scheduler->dump_heap. */
	struct heap_node in_dump;
	/** Link in vy_scheduler->compaction_heap. */
	struct heap_node in_compaction;
	/**
	 * Interval tree containing reads from this LSM tree done by
	 * all active transactions. Linked by vy_tx_interval->in_lsm.
	 * Used to abort transactions that conflict with a write to
	 * this LSM tree.
	 */
	vy_lsm_read_set_t read_set;
	/**
	 * Triggers run when the last reference to this LSM tree
	 * is dropped and the LSM tree is about to be destroyed.
	 * A pointer to this LSM tree is passed to the trigger
	 * callback in the 'event' argument.
	 *
	 * For instance, this trigger is used to remove a dropped
	 * LSM tree from the scheduler before it gets destroyed.
	 * Since each dump/compaction task takes a reference to
	 * the target index, this means that a dropped index will
	 * not get destroyed until all tasks scheduled for it have
	 * been completed.
	 */
	struct rlist on_destroy;
};

/** Extract vy_lsm from an index object. */
struct vy_lsm *
vy_lsm(struct index *index);

/** Return LSM tree name. Used for logging. */
const char *
vy_lsm_name(struct vy_lsm *lsm);

/** Return sum size of memory tree extents. */
size_t
vy_lsm_mem_tree_size(struct vy_lsm *lsm);

/** Allocate a new LSM tree object. */
struct vy_lsm *
vy_lsm_new(struct vy_lsm_env *lsm_env, struct vy_cache_env *cache_env,
	   struct vy_mem_env *mem_env, struct index_def *index_def,
	   struct tuple_format *format, struct vy_lsm *pk, uint32_t group_id);

/** Free an LSM tree object. */
void
vy_lsm_delete(struct vy_lsm *lsm);

/**
 * Return true if the LSM tree has no statements, neither on disk
 * nor in memory.
 */
static inline bool
vy_lsm_is_empty(struct vy_lsm *lsm)
{
	return (lsm->stat.disk.count.rows == 0 &&
		lsm->stat.memory.count.rows == 0);
}

/**
 * Return true if LSM tree is currently being built
 * (i.e. index_commit_create() hasn't been called yet).
 */
static inline bool
vy_lsm_is_being_constructed(struct vy_lsm *lsm)
{
	return lsm->commit_lsn < 0;
}

/**
 * Return the averange number of dumps it takes to trigger major
 * compaction of a range in this LSM tree.
 */
static inline int
vy_lsm_dumps_per_compaction(struct vy_lsm *lsm)
{
	return lsm->sum_dumps_per_compaction / lsm->range_count;
}

/**
 * Increment the reference counter of an LSM tree.
 * An LSM tree cannot be deleted if its reference
 * counter is elevated.
 */
static inline void
vy_lsm_ref(struct vy_lsm *lsm)
{
	index_ref(&lsm->base);
}

/**
 * Decrement the reference counter of an LSM tree.
 * If the reference counter reaches 0, the LSM tree
 * is deleted with vy_lsm_delete().
 */
static inline void
vy_lsm_unref(struct vy_lsm *lsm)
{
	index_unref(&lsm->base);
}

/**
 * Update pointer to the primary key for an LSM tree.
 * If called for an LSM tree corresponding to a primary
 * index, this function does nothing.
 */
static inline void
vy_lsm_update_pk(struct vy_lsm *lsm, struct vy_lsm *pk)
{
	if (lsm->index_id == 0) {
		assert(lsm->pk == NULL);
		return;
	}
	vy_lsm_unref(lsm->pk);
	vy_lsm_ref(pk);
	lsm->pk = pk;
}

/**
 * Create a new LSM tree.
 *
 * This function is called when an LSM tree is created
 * after recovery is complete or during remote recovery.
 * It initializes the range tree, makes the LSM tree
 * directory, and writes the LSM tree record to vylog.
 */
int
vy_lsm_create(struct vy_lsm *lsm);

/**
 * Load an LSM tree from disk. Called on local recovery.
 *
 * This function retrieves the LSM tree structure from the
 * metadata log, rebuilds the range tree, and opens run files.
 *
 * If @is_checkpoint_recovery is set, the LSM tree is recovered
 * from the last snapshot. In particular, this means that the LSM
 * tree must have been logged in the metadata log and so if the
 * function does not find it in the recovery context, it will
 * fail. If the flag is unset, the LSM tree is recovered from a
 * WAL, in which case a missing LSM tree is OK - it just means we
 * failed to log it before restart and have to retry during
 * WAL replay.
 *
 * @lsn is the LSN of the WAL row that created the LSM tree.
 * If the LSM tree is recovered from a snapshot, it is set
 * to the snapshot signature.
 */
int
vy_lsm_recover(struct vy_lsm *lsm, struct vy_recovery *recovery,
		 struct vy_run_env *run_env, int64_t lsn,
		 bool is_checkpoint_recovery, bool force_recovery);

/**
 * Return generation of in-memory data stored in an LSM tree
 * (min over vy_mem->generation).
 */
int64_t
vy_lsm_generation(struct vy_lsm *lsm);

/** Return max compaction_priority among ranges of an LSM tree. */
int
vy_lsm_compaction_priority(struct vy_lsm *lsm);

/** Return the target size of a range in an LSM tree. */
int64_t
vy_lsm_range_size(struct vy_lsm *lsm);

/** Add a run to the list of runs of an LSM tree. */
void
vy_lsm_add_run(struct vy_lsm *lsm, struct vy_run *run);

/** Remove a run from the list of runs of an LSM tree. */
void
vy_lsm_remove_run(struct vy_lsm *lsm, struct vy_run *run);

/**
 * Add a range to both the range tree and the range heap
 * of an LSM tree.
 */
void
vy_lsm_add_range(struct vy_lsm *lsm, struct vy_range *range);

/**
 * Remove a range from both the range tree and the range
 * heap of an LSM tree.
 */
void
vy_lsm_remove_range(struct vy_lsm *lsm, struct vy_range *range);

/**
 * Account a range in an LSM tree.
 *
 * This function updates the following LSM tree statistics:
 *  - vy_lsm::run_hist and vy_lsm::sum_dumps_per_compaction after
 *    a slice is added to or removed from a range of the LSM tree.
 *  - vy_lsm::stat::disk::compaction::queue after compaction priority
 *    of a range is updated.
 *  - vy_lsm::stat::disk::last_level_count after a range is compacted.
 */
void
vy_lsm_acct_range(struct vy_lsm *lsm, struct vy_range *range);

/**
 * Unaccount a range in an LSM tree.
 *
 * This function undoes the effect of vy_lsm_acct_range().
 */
void
vy_lsm_unacct_range(struct vy_lsm *lsm, struct vy_range *range);

/**
 * Account dump in LSM tree statistics.
 */
void
vy_lsm_acct_dump(struct vy_lsm *lsm, double time,
		 const struct vy_stmt_counter *input,
		 const struct vy_disk_stmt_counter *output);

/**
 * Account compaction in LSM tree statistics.
 */
void
vy_lsm_acct_compaction(struct vy_lsm *lsm, double time,
		       const struct vy_disk_stmt_counter *input,
		       const struct vy_disk_stmt_counter *output);

/**
 * Allocate a new active in-memory index for an LSM tree while
 * moving the old one to the sealed list. Used by the dump task
 * in order not to bother about synchronization with concurrent
 * insertions while an LSM tree is being dumped.
 */
int
vy_lsm_rotate_mem(struct vy_lsm *lsm);

/**
 * Allocate a new in-memory tree if either of the following
 * conditions is true:
 *
 * - Generation has increased after the tree was created.
 *   In this case we need to dump the tree as is in order to
 *   guarantee dump consistency.
 *
 * - Schema state has increased after the tree was created.
 *   We have to seal the tree, because we don't support mixing
 *   statements of different formats in the same tree.
 */
int
vy_lsm_rotate_mem_if_required(struct vy_lsm *lsm);

/**
 * Remove an in-memory tree from the sealed list of an LSM tree,
 * unaccount and delete it.
 */
void
vy_lsm_delete_mem(struct vy_lsm *lsm, struct vy_mem *mem);

/**
 * Lookup ranges intersecting [min_key, max_key] interval in
 * the given LSM tree.
 *
 * On success returns 0 and sets @begin to the first range
 * and @end to the one following the last range instersecting
 * the given interval (NULL if max_key lays in the rightmost
 * range).
 *
 * On memory allocation error returns -1 and sets diag.
 */
int
vy_lsm_find_range_intersection(struct vy_lsm *lsm,
		const char *min_key, const char *max_key,
		struct vy_range **begin, struct vy_range **end);

/**
 * Split a range if it has grown too big, return true if the range
 * was split. Splitting is done by making slices of the runs used
 * by the original range, adding them to new ranges, and reflecting
 * the change in the metadata log, i.e. it doesn't involve heavy
 * operations, like writing a run file, and is done immediately.
 */
bool
vy_lsm_split_range(struct vy_lsm *lsm, struct vy_range *range);

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
vy_lsm_coalesce_range(struct vy_lsm *lsm, struct vy_range *range);

/**
 * Mark all ranges of an LSM tree for major compaction.
 */
void
vy_lsm_force_compaction(struct vy_lsm *lsm);

/**
 * Insert a statement into the in-memory index of an LSM tree. If
 * the region_stmt is NULL and the statement is successfully inserted
 * then the new lsregion statement is returned via @a region_stmt.
 * Either vy_lsm_commit_stmt() or vy_lsm_rollback_stmt() must
 * be called on success.
 *
 * @param lsm         LSM tree the statement is for.
 * @param mem         In-memory tree to insert the statement into.
 * @param entry       Statement, allocated on malloc().
 * @param region_stmt NULL or the same statement, allocated on
 *                    lsregion.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
vy_lsm_set(struct vy_lsm *lsm, struct vy_mem *mem,
	   struct vy_entry entry, struct tuple **region_stmt);

/**
 * Confirm that the statement stays in the in-memory index of
 * an LSM tree.
 *
 * @param lsm   LSM tree the statement is for.
 * @param mem   In-memory tree where the statement was saved.
 * @param entry Statement allocated from lsregion.
 */
void
vy_lsm_commit_stmt(struct vy_lsm *lsm, struct vy_mem *mem,
		   struct vy_entry entry);

/**
 * Erase a statement from the in-memory index of an LSM tree.
 *
 * @param lsm   LSM tree to erase from.
 * @param mem   In-memory tree where the statement was saved.
 * @param entry Statement allocated from lsregion.
 */
void
vy_lsm_rollback_stmt(struct vy_lsm *lsm, struct vy_mem *mem,
		     struct vy_entry entry);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_LSM_H */
