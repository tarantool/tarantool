#pragma once
/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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

#include "small/rlist.h"
#include "index.h"
#include "tuple.h"
#include "space.h"
#include "txn.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Global flag that enables mvcc engine.
 * If set, memtx starts to apply statements through txn history mechanism
 * and tx manager itself transaction reads in order to detect conflicts.
 */
extern bool memtx_tx_manager_use_mvcc_engine;

enum memtx_tx_alloc_type {
	MEMTX_TX_ALLOC_TRACKER = 0,
	MEMTX_TX_ALLOC_CONFLICT = 1,
	MEMTX_TX_ALLOC_TYPE_MAX = 2,
};

extern const char *memtx_tx_alloc_type_strs[];

/**
 * Memtx_tx allocation objects for memtx_tx_region and memtx_tx_mempool.
 */
enum memtx_tx_alloc_object {
	/**
	 * Deprecated object of type struct memtx_tx_conflict.
	 * Left for statistics compatibility.
	 * TODO: remove it considering monitoring module.
	 */
	MEMTX_TX_OBJECT_CONFLICT = 0,
	/**
	 * Object of type struct tx_conflict_tracker.
	 */
	MEMTX_TX_OBJECT_CONFLICT_TRACKER = 1,
	/**
	 * Object of type struct tx_read_tracker.
	 */
	MEMTX_TX_OBJECT_READ_TRACKER = 2,
	MEMTX_TX_OBJECT_MAX = 3,
};

/**
 * Status of story. Describes the reason why it is not deleted.
 * In the case when story fits several statuses at once, status with
 * least value is chosen.
 */
enum memtx_tx_story_status {
	/**
	 * The story is used directly by some transactions.
	 */
	MEMTX_TX_STORY_USED = 0,
	/**
	 *  The story can be used by a read view.
	 */
	MEMTX_TX_STORY_READ_VIEW = 1,
	/**
	 * The story is used for gap tracking.
	 */
	MEMTX_TX_STORY_TRACK_GAP = 2,
	MEMTX_TX_STORY_STATUS_MAX = 3,
};

extern const char *memtx_tx_story_status_strs[];

/**
 * Snapshot cleaner is a short part of history that is supposed to clarify
 * tuples in a index snapshot. It's also supposed to be used in another
 * thread while common clarify would probably crash in that case.
 */
struct memtx_tx_snapshot_cleaner {
	struct mh_snapshot_cleaner_t *ht;
};

/**
 * Cell of stats with total and count statistics.
 */
struct memtx_tx_stats {
	/* Total memory over all objects. */
	size_t total;
	/* Number of measured objects. */
	size_t count;
};

/**
 * Memory statistics of memtx mvcc engine.
 */
struct memtx_tx_statistics {
	struct memtx_tx_stats stories[MEMTX_TX_STORY_STATUS_MAX];
	struct memtx_tx_stats retained_tuples[MEMTX_TX_STORY_STATUS_MAX];
	size_t memtx_tx_total[MEMTX_TX_ALLOC_TYPE_MAX];
	size_t memtx_tx_max[MEMTX_TX_ALLOC_TYPE_MAX];
	size_t tx_total[TX_ALLOC_TYPE_MAX];
	size_t tx_max[TX_ALLOC_TYPE_MAX];
	/* Number of txns registered in memtx transaction manager. */
	size_t txn_count;
};

/**
 * Collect MVCC memory usage statics.
 */
void
memtx_tx_statistics_collect(struct memtx_tx_statistics *stats);

/**
 * Initialize MVCC part of a transaction.
 * Must be called even if MVCC engine is not enabled in config.
 */
int
memtx_tx_register_txn(struct txn *txn);

/**
 * Initialize memtx transaction manager.
 */
void
memtx_tx_manager_init();

/**
 * Free resources of memtx transaction manager.
 */
void
memtx_tx_manager_free();

/**
 * Transaction providing DDL changes is disallowed to yield after
 * modifications of internal caches (i.e. after ALTER operation finishes).
 *
 * NB: can trigger story garbage collection.
 */
void
memtx_tx_acquire_ddl(struct txn *tx);

/**
 * Mark all transactions except for a given as aborted due to conflict:
 * when DDL operation is about to be committed other transactions are
 * considered to use obsolete schema so that should be aborted.
 *
 * NB: can trigger story garbage collection.
 */
void
memtx_tx_abort_all_for_ddl(struct txn *ddl_owner);

/**
 * Notifies transaction manager about insertion of a tuple in index: just for
 * understanding, that might be:
 * REPLACE, and old_tuple is NULL because it is unknown yet.
 * INSERT, and old_tuple is NULL because there's no such tuple.
 * UPDATE, and old_tuple is not NULL and is the updated tuple.
 *
 * NB: can trigger story garbage collection.
 *
 * @param index index replace happened in.
 * @param old_tuple the tuple that was removed (can be NULL).
 * @param new_tuple the tuple that was inserted.
 * @param mode      dup_replace_mode, used only if new_tuple is not NULL and
 *                  old_tuple is NULL, and only for the primary key.
 * @param replaced - old or replaced tuple.
 * @return 0 on success, -1 on error (diag is set).
 */
int
memtx_tx_history_add_insert(struct index *index, struct tuple *old_tuple,
			    struct tuple *new_tuple, enum dup_replace_mode mode,
			    struct tuple **replaced, struct tuple *successor);

/**
 * Notifies transaction manager about deletion of a tuple in index.
 *
 * @param index index deletion happened in.
 * @param old_tuple the tuple that was removed.
 * @param replaced - old or replaced tuple.
 * @return 0 on success, -1 on error (diag is set).
 */
int
memtx_tx_history_add_delete(struct index *index, struct tuple *old_tuple,
			    struct tuple **replaced);

/**
 * @brief Rollback (undo) a statement from transaction manager's history.
 * It's just make the statement invisible to all.
 * Prepared statements could be also removed, but for consistency all latter
 * prepared statement must be also rolled back.
 *
 * NB: can trigger story garbage collection.
 *
 * @param stmt current statement.
 */
void
memtx_tx_history_rollback_stmt(struct txn_stmt *stmt);

/**
 * @brief Prepare statement in history for further commit.
 * Prepared statements are still invisible for read-only transactions
 * but are visible to all read-write transactions.
 * Prepared and in-progress transactions use the same links for creating
 * chains of stories in history. The difference is that the order of
 * prepared transactions is fixed while in-progress transactions are
 * added to the end of list in any order. Thus to switch to prepared
 * we have to reorder story in such a way that current story will be
 * between earlier prepared stories and in-progress stories. That's what
 * this function does.
 *
 * NB: can trigger story garbage collection.
 *
 * @param stmt current statement.
 */
void
memtx_tx_history_prepare_stmt(struct txn_stmt *stmt);

/**
 * Finish preparing of a transaction.
 * Must be called for entire transaction after `memtx_tx_history_rollback_stmt`
 * was called for each transaction statement.
 *
 * NB: can trigger story garbage collection.
 */
void
memtx_tx_prepare_finalize(struct txn *txn);

/**
 * @brief Commit statement in history.
 * Make the statement's changes permanent. It becomes visible to all.
 *
 * NB: can trigger story garbage collection.
 *
 * @param stmt current statement.
 * @param bsize the space bsize.
 */
void
memtx_tx_history_commit_stmt(struct txn_stmt *stmt, size_t *bsize);

/** Helper of memtx_tx_tuple_clarify */
struct tuple *
memtx_tx_tuple_clarify_slow(struct txn *txn, struct space *space,
			    struct tuple *tuples, struct index *index,
			    uint32_t mk_index);

/** Helper of memtx_tx_track_point */
int
memtx_tx_track_point_slow(struct txn *txn, struct index *index,
			  const char *key);

/**
 * Record in TX manager that a transaction @a txn have read a nothing
 * from @a space and @a index with @a key.
 * The key is expected to be full, that is has part count equal to part
 * count in unique cmp_key of the index.
 *
 * NB: can trigger story garbage collection.
 *
 * @return 0 on success, -1 on memory error.
 */
static inline int
memtx_tx_track_point(struct txn *txn, struct space *space,
		     struct index *index, const char *key)
{
	if (!memtx_tx_manager_use_mvcc_engine)
		return 0;
	if (txn == NULL)
		return 0;
	/* Skip ephemeral spaces. */
	if (space == NULL || space->def->id == 0)
		return 0;
	return memtx_tx_track_point_slow(txn, index, key);
}

/**
 * Helper of memtx_tx_track_gap.
 */
int
memtx_tx_track_gap_slow(struct txn *txn, struct space *space, struct index *index,
			struct tuple *successor, enum iterator_type type,
			const char *key, uint32_t part_count);

/**
 * Record in TX manager that a transaction @a txn have read nothing
 * from @a space and @a index with @a key, somewhere from interval between
 * some unknown predecessor and @a successor.
 * This function must be used for ordered indexes, such as TREE, for queries
 * when iteration type is not EQ or when the key is not full (otherwise
 * it's faster to use memtx_tx_track_point).
 *
 * NB: can trigger story garbage collection.
 *
 * @return 0 on success, -1 on memory error.
 */
static inline int
memtx_tx_track_gap(struct txn *txn, struct space *space, struct index *index,
		   struct tuple *successor, enum iterator_type type,
		   const char *key, uint32_t part_count)
{
	if (!memtx_tx_manager_use_mvcc_engine)
		return 0;
	if (txn == NULL)
		return 0;
	/* Skip ephemeral spaces. */
	if (space == NULL || space->def->id == 0)
		return 0;
	return memtx_tx_track_gap_slow(txn, space, index, successor,
				       type, key, part_count);
}

/**
 * Helper of memtx_tx_track_full_scan.
 */
int
memtx_tx_track_full_scan_slow(struct txn *txn, struct index *index);

/**
 * Record in TX manager that a transaction @a txn have read full @a index
 * from @a space.
 * This function must be used for unordered indexes, such as HASH, for queries
 * when iteration type is ALL.
 *
 * NB: can trigger story garbage collection.
 *
 * @return 0 on success, -1 on memory error.
 */
static inline int
memtx_tx_track_full_scan(struct txn *txn, struct space *space,
			 struct index *index)
{
	if (!memtx_tx_manager_use_mvcc_engine)
		return 0;
	if (txn == NULL)
		return 0;
	/* Skip ephemeral spaces. */
	if (space == NULL || space->def->id == 0)
		return 0;
	return memtx_tx_track_full_scan_slow(txn, index);
}

/**
 * Run several rounds of memtx_tx_story_gc_step()
 */
void
memtx_tx_story_gc();

/**
 * Clean a tuple if it's dirty - finds a visible tuple in history.
 *
 * @param txn - current transactions.
 * @param space - space in which the tuple was found.
 * @param tuple - tuple to clean.
 * @param index - index in which the tuple was found.
 * @param mk_index - multikey index (iа the index is multikey).
 * @param is_prepared_ok - allow to return prepared tuples.
 * @return clean tuple (can be NULL).
 */
static inline struct tuple *
memtx_tx_tuple_clarify(struct txn *txn, struct space *space,
		       struct tuple *tuple, struct index *index,
		       uint32_t mk_index)
{
	if (!memtx_tx_manager_use_mvcc_engine)
		return tuple;
	return memtx_tx_tuple_clarify_slow(txn, space, tuple, index, mk_index);
}

uint32_t
memtx_tx_index_invisible_count_slow(struct txn *txn,
				    struct space *space, struct index *index);

/**
 * When MVCC engine is enabled, an index can contain temporary non-committed
 * tuples that are not visible outside their transaction.
 * That's why internal index size can be greater than visible by
 * standalone observer.
 * The function calculates tje number of tuples that are physically present
 * in index, but have no visible value.
 *
 * NB: can trigger story garbage collection.
 */
static inline uint32_t
memtx_tx_index_invisible_count(struct txn *txn,
			       struct space *space, struct index *index)
{
	if (!memtx_tx_manager_use_mvcc_engine)
		return 0;
	return memtx_tx_index_invisible_count_slow(txn, space, index);
}

/**
 * Clean memtx_tx part of @a txn.
 *
 * NB: can trigger story garbage collection.
 */
void
memtx_tx_clean_txn(struct txn *txn);

/**
 * Notify manager tha an index is deleted and free data, save in index.
 *
 * NB: can trigger story garbage collection.
 */
void
memtx_tx_on_index_delete(struct index *index);

/**
 * Notify manager the a space is deleted.
 * It's necessary because there is a chance that garbage collector hasn't
 * deleted all stories of that space and in that case some actions of
 * story's destructor are not applicable.
 *
 * NB: can trigger story garbage collection.
 */
void
memtx_tx_on_space_delete(struct space *space);

/**
 * Create a snapshot cleaner.
 * @param cleaner - cleaner to create.
 * @param space - space for which the cleaner must be created.
 */
void
memtx_tx_snapshot_cleaner_create(struct memtx_tx_snapshot_cleaner *cleaner,
				 struct space *space);

/** Helper of txm_snapshot_clafify. */
struct tuple *
memtx_tx_snapshot_clarify_slow(struct memtx_tx_snapshot_cleaner *cleaner,
			       struct tuple *tuple);

/**
 * Like a common clarify that function returns proper tuple if original
 * tuple in index is dirty.
 * @param cleaner - pre-created snapshot cleaner.
 * @param tuple - tuple to clean.
 * @return cleaned tuple, can be NULL.
 */
static inline struct tuple *
memtx_tx_snapshot_clarify(struct memtx_tx_snapshot_cleaner *cleaner,
			  struct tuple *tuple)
{
	if (cleaner->ht == NULL)
		return tuple;
	return memtx_tx_snapshot_clarify_slow(cleaner, tuple);
}

/**
 * Free resources.in shapshot @cleaner.
 */
void
memtx_tx_snapshot_cleaner_destroy(struct memtx_tx_snapshot_cleaner *cleaner);

/**
 * Export step of garbage collector to builtin module
 */
API_EXPORT void
memtx_tx_story_gc_step(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
