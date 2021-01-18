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

#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Global flag that enables mvcc engine.
 * If set, memtx starts to apply statements through txm history mechanism
 * and tx manager itself transaction reads in order to detect conflicts.
 */
extern bool memtx_tx_manager_use_mvcc_engine;

/**
 * Record that links two transactions, breaker and victim.
 * See memtx_tx_cause_conflict for details.
 */
struct tx_conflict_tracker {
	/** TX that aborts victim on commit. */
	struct txn *breaker;
	/** TX that will be aborted on breaker's commit. */
	struct txn *victim;
	/** Link in breaker->conflict_list. */
	struct rlist in_conflict_list;
	/** Link in victim->conflicted_by_list. */
	struct rlist in_conflicted_by_list;
};

/**
 * Record that links transaction and a story that the transaction have read.
 */
struct tx_read_tracker {
	/** The TX that read story. */
	struct txn *reader;
	/** The story that was read by reader. */
	struct memtx_story *story;
	/** Link in story->reader_list. */
	struct rlist in_reader_list;
	/** Link in reader->read_set. */
	struct rlist in_read_set;
};

/**
 * Pointer to tuple or story.
 */
struct memtx_story_or_tuple {
	/** Flag whether it's a story. */
	bool is_story;
	union {
		/** Pointer to story, it must be reverse liked. */
		struct memtx_story *story;
		/** Smart pointer to tuple: the tuple is referenced if set. */
		struct tuple *tuple;
	};
};

/**
 * Link that connects a memtx_story with older and newer stories of the same
 * key in index.
 */
struct memtx_story_link {
	/** Story that was happened after that story was ended. */
	struct memtx_story *newer_story;
	/**
	 * Older story or ancient tuple (so old that its story was lost).
	 * In case of tuple is can also be NULL.
	 */
	struct memtx_story_or_tuple older;
};

/**
 * A part of a history of a value in space.
 * It's a story about a tuple, from the point it was added to space to the
 * point when it was deleted from a space.
 * All stories are linked into a list of stories of the same key of each index.
 */
struct memtx_story {
	/** The story is about this tuple. The tuple is referenced. */

	struct tuple *tuple;
	/**
	 * Statement that told this story. Is set to NULL when the statement's
	 * transaction becomes committed. Can also be NULL if we don't know who
	 * introduced that story, the tuple was added by a transaction that
	 * was completed and destroyed some time ago.
	 */
	struct txn_stmt *add_stmt;
	/**
	 * Prepare sequence number of add_stmt's transaction. Is set when
	 * the transaction is prepared. Can be 0 if the transaction is
	 * in progress or we don't know who introduced that story.
	 */
	int64_t add_psn;
	/**
	 * Statement that ended this story. Is set to NULL when the statement's
	 * transaction becomes committed. Can also be NULL if the tuple has not
	 * been deleted yet.
	 */
	struct txn_stmt *del_stmt;
	/**
	 * Prepare sequence number of del_stmt's transaction. Is set when
	 * the transaction is prepared. Can be 0 if the transaction is
	 * in progress or if nobody has deleted the tuple.
	 */
	int64_t del_psn;
	/**
	 * List of trackers - transactions that has read this tuple.
	 */
	struct rlist reader_list;
	/**
	 * Link in tx_manager::all_stories
	 */
	struct rlist in_all_stories;
	/**
	 * Link in space::memtx_tx_stories.
	 */
	struct rlist in_space_stories;
	/**
	 * The space where the tuple is supposed to be.
	 */
	struct space *space;
	/**
	 * Number of indexes in this space - and the count of link[].
	 */
	uint32_t index_count;
	/**
	 * Link with older and newer stories (and just tuples) for each
	 * index respectively.
	 */
	struct memtx_story_link link[];
};

/**
 * Snapshot cleaner is a short part of history that is supposed to clarify
 * tuples in a index snapshot. It's also supposed to be used in another
 * thread while common clarify would probably crash in that case.
 */
struct memtx_tx_snapshot_cleaner {
	struct mh_snapshot_cleaner_t *ht;
};

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
 * Notify TX manager that if transaction @a breaker is committed then the
 * transaction @a victim must be aborted due to conflict. It is achieved
 * by adding corresponding entry (of tx_conflict_tracker type) to @a breaker
 * conflict list. In case there's already such entry, then move it to the head
 * of the list in order to optimize next invocations of this function.
 * For example: there's two rw transaction in progress, one have read
 * some value while the second is about to overwrite it. If the second
 * is committed first, the first must be aborted.
 * @return 0 on success, -1 on memory error.
 */
int
memtx_tx_cause_conflict(struct txn *breaker, struct txn *victim);

/**
 * Handle conflict when @a breaker transaction is prepared.
 * The conflict is happened if @a victim have read something that @a breaker
 * overwrites.
 * If @a victim is read-only or hasn't made any changes, it should be sent
 * to read view, in which is will not see @a breaker.
 * Otherwise @a victim must be marked as conflicted and aborted on occasion.
 */
void
memtx_tx_handle_conflict(struct txn *breaker, struct txn *victim);

/**
 * @brief Add a statement to transaction manager's history.
 * Until unlinking or releasing the space could internally contain
 * wrong tuples and must be cleaned through memtx_tx_tuple_clarify call.
 * With that clarifying the statement will be visible to current transaction,
 * but invisible to all others.
 * Follows signature of @sa memtx_space_replace_all_keys .
 *
 * @param stmt current statement.
 * @param old_tuple the tuple that should be removed (can be NULL).
 * @param new_tuple the tuple that should be inserted (can be NULL).
 * @param mode      dup_replace_mode, used only if new_tuple is not
 *                  NULL and old_tuple is NULL, and only for the
 *                  primary key.
 * @param result - old or replaced tuple.
 * @return 0 on success, -1 on error (diag is set).
 */
int
memtx_tx_history_add_stmt(struct txn_stmt *stmt, struct tuple *old_tuple,
			  struct tuple *new_tuple, enum dup_replace_mode mode,
			  struct tuple **result);

/**
 * @brief Rollback (undo) a statement from transaction manager's history.
 * It's just make the statement invisible to all.
 * Prepared statements could be also removed, but for consistency all latter
 * prepared statement must be also rolled back.
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
 * @param stmt current statement.
 */
void
memtx_tx_history_prepare_stmt(struct txn_stmt *stmt);

/**
 * @brief Commit statement in history.
 * Make the statement's changes permanent. It becomes visible to all.
 *
 * @param stmt current statement.
 * @return the change in space bsize.
 */
ssize_t
memtx_tx_history_commit_stmt(struct txn_stmt *stmt);

/** Helper of memtx_tx_tuple_clarify */
struct tuple *
memtx_tx_tuple_clarify_slow(struct txn *txn, struct space *space,
			    struct tuple *tuple, uint32_t index,
			    uint32_t mk_index, bool is_prepared_ok);

/**
 * Record in TX manager that a transaction @txn have read a @tuple in @space.
 * @return 0 on success, -1 on memory error.
 */
int
memtx_tx_track_read(struct txn *txn, struct space *space, struct tuple *tuple);

/**
 * Clean a tuple if it's dirty - finds a visible tuple in history.
 * @param txn - current transactions.
 * @param space - space in which the tuple was found.
 * @param tuple - tuple to clean.
 * @param index - index number.
 * @param mk_index - multikey index (iа the index is multikey).
 * @param is_prepared_ok - allow to return prepared tuples.
 * @return clean tuple (can be NULL).
 */
static inline struct tuple *
memtx_tx_tuple_clarify(struct txn *txn, struct space *space,
		       struct tuple *tuple, uint32_t index,
		       uint32_t mk_index, bool is_prepared_ok)
{
	if (!memtx_tx_manager_use_mvcc_engine)
		return tuple;
	if (!tuple_is_dirty(tuple)) {
		memtx_tx_track_read(txn, space, tuple);
		return tuple;
	}
	return memtx_tx_tuple_clarify_slow(txn, space, tuple, index, mk_index,
					   is_prepared_ok);
}

/**
 * Notify manager the a space is deleted.
 * It's necessary because there is a chance that garbage collector hasn't
 * deleted all stories of that space and in that case some actions of
 * story's destructor are not applicable.
 */
void
memtx_tx_on_space_delete(struct space *space);

/**
 * Create a snapshot cleaner.
 * @param cleaner - cleaner to create.
 * @param space - space for which the cleaner must be created.
 * @param index_name - name of index for diag in case of memory error.
 * @return 0 on success, -1 on memory erorr.
 */
int
memtx_tx_snapshot_cleaner_create(struct memtx_tx_snapshot_cleaner *cleaner,
				 struct space *space, const char *index_name);

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

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
