#ifndef INCLUDES_TARANTOOL_BOX_VY_TX_H
#define INCLUDES_TARANTOOL_BOX_VY_TX_H
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

#include <stdbool.h>
#include <stdint.h>

#include <small/mempool.h>
#define RB_COMPACT 1
#include <small/rb.h>
#include <small/rlist.h>

#include "iterator_type.h"
#include "salad/stailq.h"
#include "trivia/util.h"
#include "vy_lsm.h"
#include "vy_stat.h"
#include "vy_read_set.h"
#include "vy_read_view.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;
struct tx_manager;
struct vy_mem;
struct vy_tx;
struct vy_history;

/** Transaction state. */
enum tx_state {
	/** Initial state. */
	VINYL_TX_READY,
	/**
	 * Transaction is finished and validated in the engine.
	 * It may still be rolled back if there is an error
	 * writing the WAL.
	 */
	VINYL_TX_COMMIT,
	/** Transaction is aborted by a conflict. */
	VINYL_TX_ABORT,
};

/**
 * A single write operation performed by a transaction.
 */
struct txv {
	/** Transaction. */
	struct vy_tx *tx;
	/** LSM tree this operation is for. */
	struct vy_lsm *lsm;
	/** In-memory tree to insert the statement into. */
	struct vy_mem *mem;
	/** Statement of this operation. */
	struct tuple *stmt;
	/** Statement allocated on vy_mem->allocator. */
	const struct tuple *region_stmt;
	/** Next in the transaction log. */
	struct stailq_entry next_in_log;
	/** Member the transaction write set. */
	rb_node(struct txv) in_set;
	/**
	 * True if there is no tuple committed to the database
	 * matching the key this operation is for, i.e. either
	 * there is no statements for this key at all or the
	 * last committed statement is DELETE.
	 */
	bool is_first_insert;
	/**
	 * True if the txv was overwritten by another txv of
	 * the same transaction.
	 */
	bool is_overwritten;
	/** txv that was overwritten by the current txv. */
	struct txv *overwritten;
};

/**
 * Index of all modifications made by a transaction.
 * Ordered by LSM tree, then by key.
 */
struct write_set_key {
	struct vy_lsm *lsm;
	const struct tuple *stmt;
};

int
write_set_cmp(struct txv *a, struct txv *b);
int
write_set_key_cmp(struct write_set_key *a, struct txv *b);

typedef rb_tree(struct txv) write_set_t;
rb_gen_ext_key(MAYBE_UNUSED static inline, write_set_, write_set_t, struct txv,
		in_set, write_set_cmp, struct write_set_key *, write_set_key_cmp);

static inline struct txv *
write_set_search_key(write_set_t *tree, struct vy_lsm *lsm,
		     const struct tuple *stmt)
{
	struct write_set_key key = { .lsm = lsm, .stmt = stmt };
	return write_set_search(tree, &key);
}

/** Transaction object. */
struct vy_tx {
	/** Transaction manager. */
	struct tx_manager *xm;
	/**
	 * In memory transaction log. Contains both reads
	 * and writes.
	 */
	struct stailq log;
	/**
	 * Writes of the transaction segregated by the changed
	 * vy_lsm object.
	 */
	write_set_t write_set;
	/**
	 * Version of write_set state; if the state changes
	 * (insert/remove), the version is incremented.
	 */
	uint32_t write_set_version;
	/**
	 * Total size of memory occupied by statements of
	 * the write set.
	 */
	size_t write_size;
	/** Current state of the transaction.*/
	enum tx_state state;
	/**
	 * The read view of this transaction. When a transaction
	 * is started, it is set to the "read committed" state,
	 * or actually, "read prepared" state, in other words,
	 * all changes of all prepared transactions are visible
	 * to this transaction. Upon a conflict, the transaction's
	 * read view is changed: it begins to point to the
	 * last state of the database before the conflicting
	 * change.
	 */
	struct vy_read_view *read_view;
	/**
	 * Tree of all intervals read by this transaction. Linked
	 * by vy_tx_interval->in_tx. Used to merge intersecting
	 * intervals.
	 */
	vy_tx_read_set_t read_set;
	/**
	 * Prepare sequence number or -1 if the transaction
	 * is not prepared.
	 */
	int64_t psn;
	/* List of triggers invoked when this transaction ends. */
	struct rlist on_destroy;
};

static inline const struct vy_read_view **
vy_tx_read_view(struct vy_tx *tx)
{
	return (const struct vy_read_view **)&tx->read_view;
}

/** Transaction manager object. */
struct tx_manager {
	/**
	 * The last committed log sequence number known to
	 * vinyl. Updated in vy_commit().
	 */
	int64_t lsn;
	/**
	 * A global transaction prepare counter: a transaction
	 * is assigned an id at the time of vy_prepare(). Is used
	 * to order statements of prepared but not yet committed
	 * transactions in vy_mem.
	 */
	int64_t psn;
	/**
	 * The last prepared (but not committed) transaction,
	 * or NULL if there are no prepared transactions.
	 */
	struct vy_tx *last_prepared_tx;
	/**
	 * The list of TXs with a read view in order of vlsn.
	 */
	struct rlist read_views;
	/**
	 * Global read view - all prepared transactions are
	 * visible in this view. The global read view
	 * LSN is always INT64_MAX and it never changes.
	 */
	const struct vy_read_view global_read_view;
	/**
	 * It is possible to create a cursor without an active
	 * transaction, e.g. a write iterator;
	 * this pointer represents a skeleton
	 * transaction to use in such places.
	 */
	const struct vy_read_view *p_global_read_view;
	/**
	 * Committed read view - all committed transactions are
	 * visible in this view. The global read view
	 * LSN is always (MAX_LSN - 1)  and it never changes.
	 */
	const struct vy_read_view committed_read_view;
	/**
	 * It is possible to create a cursor without an active
	 * transaction, e.g. when squashing upserts;
	 * this pointer represents a skeleton
	 * transaction to use in such places.
	 */
	const struct vy_read_view *p_committed_read_view;
	/** Transaction statistics. */
	struct vy_tx_stat stat;
	/** Sum size of statements pinned by the write set. */
	size_t write_set_size;
	/** Sum size of statements pinned by the read set. */
	size_t read_set_size;
	/** Memory pool for struct vy_tx allocations. */
	struct mempool tx_mempool;
	/** Memory pool for struct txv allocations. */
	struct mempool txv_mempool;
	/** Memory pool for struct vy_read_interval allocations. */
	struct mempool read_interval_mempool;
	/** Memory pool for struct vy_read_view allocations. */
	struct mempool read_view_mempool;
};

/** Allocate a tx manager object. */
struct tx_manager *
tx_manager_new(void);

/** Delete a tx manager object. */
void
tx_manager_delete(struct tx_manager *xm);

/*
 * Determine the lowest possible vlsn, i.e. the level below
 * which the history could be compacted.
 *
 * If there are active read views, it is the first's vlsn.
 * If there is no active read view, a read view could be
 * created at any moment with vlsn = m->lsn, so m->lsn must
 * be chosen.
 */
int64_t
tx_manager_vlsn(struct tx_manager *xm);

/** Initialize a tx object. */
void
vy_tx_create(struct tx_manager *xm, struct vy_tx *tx);

/** Destroy a tx object. */
void
vy_tx_destroy(struct vy_tx *tx);

/** Begin a new transaction. */
struct vy_tx *
vy_tx_begin(struct tx_manager *xm);

/** Prepare a transaction to be committed. */
int
vy_tx_prepare(struct vy_tx *tx);

/**
 * Commit a transaction with a given LSN and destroy
 * the tx object.
 */
void
vy_tx_commit(struct vy_tx *tx, int64_t lsn);

/**
 * Rollback a transaction and destroy the tx object.
 */
void
vy_tx_rollback(struct vy_tx *tx);

/**
 * Return the save point corresponding to the current
 * transaction state. The transaction can be rolled back
 * to a save point with vy_tx_rollback_to_savepoint().
 */
static inline void *
vy_tx_savepoint(struct vy_tx *tx)
{
	assert(tx->state == VINYL_TX_READY);
	return stailq_last(&tx->log);
}

/** Rollback a transaction to a given save point. */
void
vy_tx_rollback_to_savepoint(struct vy_tx *tx, void *svp);

/**
 * Remember a read interval in the conflict manager index.
 * On success, this function guarantees that if another
 * transaction successfully commits a statement within a
 * tracked interval, the transaction the interval belongs
 * to will be aborted.
 *
 * @param tx            Transaction that invoked the read.
 * @param lsm           LSM tree that was read from.
 * @param left          Left boundary of the read interval.
 * @param left_belongs  Set if the left boundary belongs to
 *                      the interval.
 * @param right         Right boundary of the read interval.
 * @param right_belongs Set if the right boundary belongs to
 *                      the interval.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
vy_tx_track(struct vy_tx *tx, struct vy_lsm *lsm,
	    struct tuple *left, bool left_belongs,
	    struct tuple *right, bool right_belongs);

/**
 * Remember a point read in the conflict manager index.
 *
 * @param tx    Transaction that invoked the read.
 * @param lsm   LSM tree that was read from.
 * @param stmt  Key that was read.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 *
 * Note, this function isn't just a shortcut to vy_tx_track().
 * Before adding the key to the conflict manager index, it checks
 * if the key was overwritten by the transaction itself. If this
 * is the case, there is no point in tracking the key, because the
 * transaction read it from its own write set.
 */
int
vy_tx_track_point(struct vy_tx *tx, struct vy_lsm *lsm, struct tuple *stmt);

/** Add a statement to a transaction. */
int
vy_tx_set(struct vy_tx *tx, struct vy_lsm *lsm, struct tuple *stmt);

/**
 * Iterator over the write set of a transaction.
 */
struct vy_txw_iterator {
	/** Iterator statistics. */
	struct vy_txw_iterator_stat *stat;
	/** Transaction whose write set is iterated. */
	struct vy_tx *tx;
	/** LSM tree of interest. */
	struct vy_lsm *lsm;
	/**
	 * Iterator type.
	 *
	 * Note if key is NULL, GT and EQ are changed to GE
	 * while LT is changed to LE.
	 */
	enum iterator_type iterator_type;
	/** Search key. */
	const struct tuple *key;
	/* Last seen value of the write set version. */
	uint32_t version;
	/* Current position in the write set. */
	struct txv *curr_txv;
	/* Is false until first .._get ot .._next_.. method is called */
	bool search_started;
};

/**
 * Initialize a txw iterator.
 */
void
vy_txw_iterator_open(struct vy_txw_iterator *itr,
		     struct vy_txw_iterator_stat *stat,
		     struct vy_tx *tx, struct vy_lsm *lsm,
		     enum iterator_type iterator_type,
		     const struct tuple *key);

/**
 * Advance a txw iterator to the next key.
 * The key history is returned in @history (empty if EOF).
 * Returns 0 on success, -1 on memory allocation error.
 */
NODISCARD int
vy_txw_iterator_next(struct vy_txw_iterator *itr,
		     struct vy_history *history);

/**
 * Advance a txw iterator to the key following @last_stmt.
 * The key history is returned in @history (empty if EOF).
 * Returns 0 on success, -1 on memory allocation error.
 */
NODISCARD int
vy_txw_iterator_skip(struct vy_txw_iterator *itr,
		     const struct tuple *last_stmt,
		     struct vy_history *history);

/**
 * Check if a txw iterator was invalidated and needs to be restored.
 * If it does, set the iterator position to the first key following
 * @last_stmt and return 1, otherwise return 0. Returns -1 on memory
 * allocation error.
 */
int
vy_txw_iterator_restore(struct vy_txw_iterator *itr,
			const struct tuple *last_stmt,
			struct vy_history *history);

/**
 * Close a txw iterator.
 */
void
vy_txw_iterator_close(struct vy_txw_iterator *itr);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_TX_H */
