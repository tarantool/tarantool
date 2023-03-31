#ifndef TARANTOOL_BOX_TXN_H_INCLUDED
#define TARANTOOL_BOX_TXN_H_INCLUDED
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
#include "salad/stailq.h"
#include "trigger.h"
#include "fiber.h"
#include "space.h"
#include "journal.h"
#include "tt_static.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** box statistics */
extern struct rmean *rmean_box;

/**
 * Incremental counter for psn (prepare sequence number) of a transaction.
 * The next prepared transaction will get psn == txn_next_psn++.
 * See also struct txn::psn.
 */
extern int64_t txn_next_psn;

struct journal_entry;
struct engine;
struct space;
struct tuple;
struct xrow_header;
struct Vdbe;

enum txn_flag {
	/** Transaction has been processed. */
	TXN_IS_DONE = 0x1,
	/**
	 * Transaction has been aborted by fiber yield so
	 * should be rolled back at commit.
	 */
	TXN_IS_ABORTED_BY_YIELD = 0x2,
	/**
	 * fiber_yield() is allowed inside the transaction.
	 * See txn_can_yield() for more details.
	 */
	TXN_CAN_YIELD = 0x4,
	/** on_commit and/or on_rollback list is not empty. */
	TXN_HAS_TRIGGERS = 0x8,
	/**
	 * Synchronous transaction touched sync spaces, or an
	 * asynchronous transaction blocked by a sync one until it
	 * is confirmed.
	 */
	TXN_WAIT_SYNC = 0x10,
	/**
	 * Synchronous transaction 'waiting for ACKs' state before
	 * commit. In this state it waits until it is replicated
	 * onto a quorum of replicas, and only then finishes
	 * commit and returns success to a user.
	 * TXN_WAIT_SYNC is always set, if TXN_WAIT_ACK is set.
	 */
	TXN_WAIT_ACK = 0x20,
	/**
	 * A transaction may be forced to be asynchronous, not
	 * wait for any ACKs, and not depend on prepending sync
	 * transactions. This happens in a few special cases. For
	 * example, when applier receives snapshot from master.
	 */
	TXN_FORCE_ASYNC = 0x40,
	/**
	 * Transaction was aborted when other transaction was
	 * committed due to conflict.
	 */
	TXN_IS_CONFLICTED = 0x80,
	/*
	 * Transaction has been aborted by timeout so should be
	 * rolled back at commit.
	 */
	TXN_IS_ABORTED_BY_TIMEOUT = 0x100,
};

enum {
	/**
	 * Maximum recursion depth for on_replace triggers.
	 * Large numbers may corrupt C stack.
	 */
	TXN_SUB_STMT_MAX = 3,
	/**
	 * The minimal PSN (prepare sequence number) that can be assigned to a
	 * prepared transaction (see struct txn::psn). All values below this
	 * threshold can be used by a transaction manager as values with
	 * special meaning that no real transaction can have.
	 */
	TXN_MIN_PSN = 2,
};

enum {
	/** Signature set for empty transactions. */
	TXN_SIGNATURE_NOP = 0,
	/**
	 * Aliases for journal errors to make all signature codes have the same
	 * prefix.
	 */
	TXN_SIGNATURE_UNKNOWN = JOURNAL_ENTRY_ERR_UNKNOWN,
	TXN_SIGNATURE_IO = JOURNAL_ENTRY_ERR_IO,
	TXN_SIGNATURE_CASCADE = JOURNAL_ENTRY_ERR_CASCADE,
	/**
	 * The default signature value for failed transactions.
	 * Indicates either write failure or any other failure
	 * not caused by synchronous transaction processing.
	 */
	TXN_SIGNATURE_ROLLBACK = JOURNAL_ENTRY_ERR_MIN - 1,
	/**
	 * A value set for failed synchronous transactions
	 * on master, when not enough acks were collected.
	 */
	TXN_SIGNATURE_QUORUM_TIMEOUT = JOURNAL_ENTRY_ERR_MIN - 2,
	/**
	 * A value set for failed synchronous transactions
	 * on replica (or any instance during recovery), when a
	 * transaction is rolled back because ROLLBACK message was
	 * read.
	 */
	TXN_SIGNATURE_SYNC_ROLLBACK = JOURNAL_ENTRY_ERR_MIN - 3,
	/**
	 * Aborted before it could be written due an error which is already
	 * installed into the global diag.
	 */
	TXN_SIGNATURE_ABORT = JOURNAL_ENTRY_ERR_MIN - 4,
};

/** \cond public */
/**
 * When a transaction calls `commit`, this action can last for some time until
 * redo data is written to WAL. While such a `commit` call is in progress we
 * call changes of such a transaction as 'committed', and when the process is
 * finished - we call the changes as 'confirmed'. One of the main options of
 * a transaction is to see or not to see 'committed' changes.
 * Note that now there are different terminologies in different places. This
 * enum uses new 'committed' and 'confirmed' states of transactions. Meanwhile
 * in engined the first state is usually called as 'prepared', and the second
 * as 'committed' or 'completed'.
 * Warning: this enum is exposed in lua via ffi, and thus any change in items
 * must be correspondingly modified on ffi.cdef(), see schema.lua.
 */
enum txn_isolation_level {
	/** Take isolation level from global default_isolation_level. */
	TXN_ISOLATION_DEFAULT,
	/** Allow to read committed, but not confirmed changes. */
	TXN_ISOLATION_READ_COMMITTED,
	/** Allow to read only confirmed changes. */
	TXN_ISOLATION_READ_CONFIRMED,
	/** Determine isolation level automatically. */
	TXN_ISOLATION_BEST_EFFORT,
	/** Allow to read only the changes confirmed on any cluster node. */
	TXN_ISOLATION_LINEARIZABLE,
	/** Upper bound of valid values. */
	txn_isolation_level_MAX,
};

/** \endcond public */

/**
 * Common enum strings: uppercase letters, underscores.
 */
extern const char *txn_isolation_level_strs[txn_isolation_level_MAX];

/**
 * Aliases: lowercase letters, hyphens.
 */
extern const char *txn_isolation_level_aliases[txn_isolation_level_MAX];

/**
 * The level that is set for a transaction by default.
 * Cannot be TXN_ISOLATION_DEFAULT since it senseless.
 */
extern enum txn_isolation_level txn_default_isolation;

/**
 * Convert a result of a transaction execution to an error installed into the
 * current diag.
 */
void
diag_set_txn_sign_detailed(const char *file, unsigned line, int64_t signature);

#define diag_set_txn_sign(signature)						\
	diag_set_txn_sign_detailed(__FILE__, __LINE__, signature)

/**
 * Status of a transaction.
 */
enum txn_status {
	/**
	 * Initial state of TX. The only state of a TX that allowed to do
	 * read or write actions.
	 */
	TXN_INPROGRESS,
	/**
	 * The TX have passed conflict checks and is ready to be committed.
	 */
	TXN_PREPARED,
	/**
	 * The TX was read_only, has a conflict and was sent to read view.
	 * Read-only and does not participate in conflict resolution ever more.
	 * This transaction can only see a state of the database at some fixed
	 * point in the past.
	 */
	TXN_IN_READ_VIEW,
	/**
	 * The TX was committed.
	 */
	TXN_COMMITTED,
	/**
	 * The TX was aborted, either explicitly, by box.rollback(), or
	 * automatically, by conflict or timeout.
	 */
	TXN_ABORTED,
};


/**
 * Structure which contains pointers to the tuples,
 * that are used in rollback. Currently used only in
 * memxt engine.
 */
struct txn_stmt_rollback_info {
	struct tuple *old_tuple;
	struct tuple *new_tuple;
};

/**
 * A single statement of a multi-statement
 * transaction: undo and redo info.
 */
struct txn_stmt {
	/* (!) Please update txn_stmt_new() after changing members */

	/** A linked list of all statements. */
	struct stailq_entry next;
	/** Owner of that statement. */
	struct txn *txn;
	/** Undo info. */
	struct space *space;
	struct tuple *old_tuple;
	struct tuple *new_tuple;
	/** Structure, which contains tuples for rollback. */
	struct txn_stmt_rollback_info rollback_info;
	/**
	 * If new_tuple != NULL and this transaction was not prepared,
	 * this member holds added story of the new_tuple.
	 */
	struct memtx_story *add_story;
	/**
	 * If new_tuple == NULL and this transaction was not prepared,
	 * this member holds added story of the old_tuple.
	 */
	struct memtx_story *del_story;
	/**
	 * Link in memtx_story::del_stmt linked list.
	 * Only one prepared TX can delete a tuple and a story. But
	 * when there are several in-progress transactions and they delete
	 * the same tuple we have to store several delete statements in one
	 * story. It's implemented in that way: story has a pointer to the first
	 * deleting statement, that statement has a pointer to the next etc,
	 * with NULL in the end.
	 * That member is that the pointer to next deleting statement.
	 */
	struct txn_stmt *next_in_del_list;
	/** Engine savepoint for the start of this statement. */
	void *engine_savepoint;
	/** Redo info: the binary log row */
	struct xrow_header *row;
	/** on_commit and/or on_rollback list is not empty. */
	bool has_triggers;
	/*
	 * Flag that shows whether this statement overwrites own transaction
	 * statement. For example if a transaction makes two replaces of the
	 * same key, the second statement will be with is_own_change = true.
	 * Or if a transaction deletes some key and then inserts that key,
	 * the insertion statement will be with is_own_change = true.
	 */
	bool is_own_change;
	/**
	* Request type - IPROTO type code
	*/
	uint16_t type;
	/** Commit/rollback triggers associated with this statement. */
	struct rlist on_commit;
	struct rlist on_rollback;
};

/**
 * Transaction savepoint object. Allocated on a transaction
 * region and becames invalid after the transaction's end.
 * Allows to rollback a transaction partially.
 */
struct txn_savepoint {
	/**
	 * Saved substatement level at the time of a savepoint
	 * creation.
	 */
	int in_sub_stmt;
	/**
	 * Statement, on which a savepoint is created. On rollback
	 * to this savepoint all newer statements are rolled back.
	 * Initialized to NULL in case a savepoint is created in
	 * an empty transaction.
	 */
	struct stailq_entry *stmt;
	/** Organize savepoints into linked list. */
	struct rlist link;
	/**
	 * Optional name of savepoint. If savepoint lacks
	 * name (i.e. anonymous savepoint available only by
	 * reference to the object), name[0] == ''. Otherwise,
	 * memory for name is reserved in the same memory chunk
	 * as struct txn_savepoint itself - name is placed
	 * right after structure (see txn_savepoint_new()).
	 */
	char name[1];
};

/**
 * Read-only transaction savepoint object. After completing a read-only
 * transaction, we must clear the region. However, if we just reset the region,
 * we may corrupt the data that was placed in the region before the read-only
 * transaction began. To avoid this, we should use truncation. This structure
 * contains the information required for truncation.
 */
struct txn_ro_savepoint {
	/** Region used during this transaction. */
	struct region *region;
	/** Savepoint for region. */
	size_t region_used;
};

extern double too_long_threshold;

/**
 * An element of list of autogenerated ids, being returned as SQL
 * response metadata.
 */
struct autoinc_id_entry {
	struct stailq_entry link;
	int64_t id;
};

/** Type of allocation, used to collect statistics. */
enum tx_alloc_type {
	/**
	 * Allocation of txn_stmt.
	 */
	TX_ALLOC_STMT = 0,
	/**
	 * Allocation made with box_txn_alloc().
	 */
	TX_ALLOC_USER_DATA = 1,
	/**
	 * Allocation made by txns for internal purposes
	 * (xrow_header, ev_timer, trigger, etc).
	 */
	TX_ALLOC_SYSTEM = 2,
	TX_ALLOC_TYPE_MAX = 3,
};

extern const char *tx_alloc_type_strs[];

struct txn {
	/** A stailq_entry to hold a txn in a cache. */
	struct stailq_entry in_txn_cache;
	/**
	 * A memory region to put all transaction relative data in.
	 * Detaching transaction data from a fiber temporary storage
	 * is required to allow an applier fiber to manage multiple
	 * transactions simultaneously. Also interactive and autonomous
	 * transactions will require this. To avoid missing memory consumption
	 * monitoring, do not use directly, instead use tx_region methods.
	 */
	struct region region;
	/**
	 * Used memory of region before it was acquired, 0 if was not.
	 */
	uint32_t acquired_region_used;
	/**
	 * Allocation statistics.
	 */
	uint32_t alloc_stats[TX_ALLOC_TYPE_MAX];
	/**
	 * Memtx tx allocation statistics.
	 */
	uint32_t *memtx_tx_alloc_stats;
	/**
	 * A sequentially growing transaction id, assigned when
	 * a transaction is initiated. Used to identify
	 * a transaction after it has possibly been destroyed.
	 *
	 * Valid IDs start from 1.
	 */
	int64_t id;
	/**
	 * A sequential ID that is assigned when the TX becomes prepared.
	 * Transactions are committed in that order.
	 */
	int64_t psn;
	/**
	 * Read view of that TX. The TX can see only changes with ps < rv_psn.
	 * Is nonzero if and only if status = TXN_IN_READ_VIEW.
	 */
	int64_t rv_psn;
	/** Status of the TX */
	enum txn_status status;
	/**
	 * Isolation level of TX. Can't be TXN_ISOLATION_DEFAULT since setting
	 * this value actually uses txn_default_isolation
	 */
	enum txn_isolation_level isolation;
	/** List of statements in a transaction. */
	struct stailq stmts;
	/** Number of new rows without an assigned LSN. */
	int n_new_rows;
	/**
	 * Number of local new rows, no assigned LSN and
	 * replication group_id=local (not replicated anywhere).
	 */
	int n_local_rows;
	/**
	 * Number of rows coming from the applier, with an
	 * already assigned LSN.
	 */
	int n_applier_rows;
	/** Bit mask of transaction flags, see txn_flag. */
	unsigned flags;
	/** The number of active nested statement-level transactions. */
	int8_t in_sub_stmt;
	/**
	 * First statement at each statement-level.
	 * Needed to rollback sub statements.
	 */
	struct stailq_entry *sub_stmt_begin[TXN_SUB_STMT_MAX + 1];
	/** LSN of this transaction when written to WAL. */
	int64_t signature;
	/** Engine involved in multi-statement transaction. */
	struct engine *engine;
	/** Engine-specific transaction data */
	void *engine_tx;
	/* A fiber to wake up when transaction is finished. */
	struct fiber *fiber;
	/** Timestampt of entry write start. */
	double start_tm;
	/**
	 * Triggers on fiber yield to abort transaction for
	 * for in-memory engine.
	 */
	struct trigger fiber_on_yield;
	/**
	 * Trigger on fiber stop, to rollback transaction
	 * in case a fiber stops (all engines).
	 */
	struct trigger fiber_on_stop;
	/** Commit and rollback triggers. */
	struct rlist on_commit, on_rollback, on_wal_write;
	/** List of savepoints to find savepoint by name. */
	struct rlist savepoints;
	/**
	 * Link in tx_manager::read_view_txs.
	 */
	struct rlist in_read_view_txs;
	/** List of tx_read_trackers with stories that the TX have read. */
	struct rlist read_set;
	/** List of point hole reads. @sa struct point_hole_item. */
	struct rlist point_holes_list;
	/** List of gap reads. @sa struct inplace_gap_item / nearby_gap_item. */
	struct rlist gap_list;
	/** Link in tx_manager::all_txs. */
	struct rlist in_all_txs;
	/** True in case transaction provides any DDL change. */
	bool is_schema_changed;
	/** Timeout for transaction, or TIMEOUT_INFINITY if not set. */
	double timeout;
	/**
	 * Timer that is alarmed if the transaction did not have time
	 * to complete within the timeout specified when it was created.
	 */
	struct ev_timer *rollback_timer;
	/**
	 * For synchronous transactions - their context in the synchro queue.
	 */
	struct txn_limbo_entry *limbo_entry;
	/**
	 * Nesting level of space on_replace triggers for current txn.
	 */
	int space_on_replace_triggers_depth;
};

static inline bool
txn_has_flag(const struct txn *txn, enum txn_flag flag)
{
	assert((flag & (flag - 1)) == 0);
	return (txn->flags & flag) != 0;
}

static inline void
txn_set_flags(struct txn *txn, unsigned int flags)
{
	txn->flags |= flags;
}

static inline void
txn_clear_flags(struct txn *txn, unsigned int flags)
{
	txn->flags &= ~flags;
}

/**
 * Returns the code of the error that caused abort of the given transaction.
 */
static inline enum box_error_code
txn_flags_to_error_code(struct txn *txn)
{
	if (txn_has_flag(txn, TXN_IS_CONFLICTED))
		return ER_TRANSACTION_CONFLICT;
	else if (txn_has_flag(txn, TXN_IS_ABORTED_BY_YIELD))
		return ER_TRANSACTION_YIELD;
	else if (txn_has_flag(txn, TXN_IS_ABORTED_BY_TIMEOUT))
		return ER_TRANSACTION_TIMEOUT;
	return ER_UNKNOWN;
}

/**
 * Checks if new statements can be executed in the given transaction.
 * Returns 0 if true. Otherwise, sets diag and returns -1.
 */
static inline int
txn_check_can_continue(struct txn *txn)
{
	if (txn->status == TXN_ABORTED) {
		diag_set(ClientError, txn_flags_to_error_code(txn));
		return -1;
	}
	return 0;
}

/* Pointer to the current transaction (if any) */
static inline struct txn *
in_txn(void)
{
	return fiber()->storage.txn;
}

/* Set to the current transaction (if any) */
static inline void
fiber_set_txn(struct fiber *fiber, struct txn *txn)
{
	fiber->storage.txn = txn;
}

/**
 * Detach transaction from fiber.
 * By default if the fiber is stopped the transaction started
 * in this fiber is rollback. This function detaches transaction
 * from fiber - detached transaction does not rollback in case
 * when fiber stopped, but can be aborted in case it does not
 * support yeild.
 */
struct txn *
txn_detach(void);

/**
 * Attach transaction to fiber.
 * Attach @a txn that has been detached previously and saved
 * somewhere to a new fiber.
 */
void
txn_attach(struct txn *txn);

/**
 * Start a transaction explicitly.
 * @pre no transaction is active
 */
struct txn *
txn_begin(void);

/**
 * Complete transaction processing with an error. All the changes are going to
 * be rolled back. The transaction's signature should be set to the rollback
 * reason.
 */
void
txn_complete_fail(struct txn *txn);

/**
 * Complete transaction processing successfully. All the changes are going to
 * become committed and visible.
 */
void
txn_complete_success(struct txn *txn);

/**
 * Commit a transaction.
 * @pre txn == in_txn()
 *
 * Return 0 on success. On error, rollback
 * the transaction and return -1.
 */
int
txn_commit(struct txn *txn);

/**
 * Rollback a transaction.
 * @pre txn == in_txn()
 */
void
txn_rollback(struct txn *txn);

/**
 * Rollback a transaction due to an error which is already installed into the
 * global diag. This is preferable over the plain rollback when there are
 * already triggers installed and they might need to know the exact reason for
 * the rollback.
 */
void
txn_abort(struct txn *txn);

/**
 * Submit a transaction to the journal.
 * @pre txn == in_txn()
 *
 * On success 0 is returned, and the transaction will be freed upon
 * journal write completion. Note, the journal write may still fail.
 * To track transaction status, one is supposed to use on_commit and
 * on_rollback triggers.
 * Note, this may yield occasionally, once journal queue gets full.
 *
 * On failure -1 is returned and the transaction is rolled back and
 * freed.
 */
int
txn_commit_try_async(struct txn *txn);

/**
 * Most txns don't have triggers, and txn objects
 * are created on every access to data, so txns
 * are partially initialized.
 */
static inline void
txn_init_triggers(struct txn *txn)
{
	if (!txn_has_flag(txn, TXN_HAS_TRIGGERS)) {
		rlist_create(&txn->on_commit);
		rlist_create(&txn->on_rollback);
		rlist_create(&txn->on_wal_write);
		txn_set_flags(txn, TXN_HAS_TRIGGERS);
	}
}

static inline void
txn_on_commit(struct txn *txn, struct trigger *trigger)
{
	txn_init_triggers(txn);
	trigger_add(&txn->on_commit, trigger);
}

static inline void
txn_on_rollback(struct txn *txn, struct trigger *trigger)
{
	txn_init_triggers(txn);
	trigger_add(&txn->on_rollback, trigger);
}

static inline void
txn_on_wal_write(struct txn *txn, struct trigger *trigger)
{
	txn_init_triggers(txn);
	trigger_add(&txn->on_wal_write, trigger);
}

/**
 * Most statements don't have triggers, and txn objects
 * are created on every access to data, so statements
 * are partially initialized.
 */
static inline void
txn_stmt_init_triggers(struct txn_stmt *stmt)
{
	if (!stmt->has_triggers) {
		rlist_create(&stmt->on_commit);
		rlist_create(&stmt->on_rollback);
		stmt->has_triggers = true;
	}
}

static inline void
txn_stmt_on_commit(struct txn_stmt *stmt, struct trigger *trigger)
{
	txn_stmt_init_triggers(stmt);
	/* Statement triggers are private and never have anything to free. */
	assert(trigger->destroy == NULL);
	trigger_add(&stmt->on_commit, trigger);
}

static inline void
txn_stmt_on_rollback(struct txn_stmt *stmt, struct trigger *trigger)
{
	txn_stmt_init_triggers(stmt);
	/* Statement triggers are private and never have anything to free. */
	assert(trigger->destroy == NULL);
	trigger_add(&stmt->on_rollback, trigger);
}

/**
 * Save and ref @a old_tuple and @a new_tuple in special structure
 * inside @a stmt. Later this tuples will be used in case of rollback.
 */
void
txn_stmt_prepare_rollback_info(struct txn_stmt *stmt, struct tuple *old_tuple,
			       struct tuple *new_tuple);

/*
 * Return the total number of rows committed in the txn.
 */
static inline int
txn_n_rows(struct txn *txn)
{
	return txn->n_new_rows + txn->n_applier_rows;
}

/**
 * Start a new statement in @a space with requst @a type (IPROTO_ constant).
 */
int
txn_begin_stmt(struct txn *txn, struct space *space, uint16_t type);

int
txn_begin_in_engine(struct engine *engine, struct txn *txn);

/**
 * Check if a space supports linearizable access, i.e. it is either synchronous
 * or not replicated at all, and its engine prevents dirty reads.
 */
int
txn_check_space_linearizability(const struct txn *txn,
				const struct space *space);

static inline int
txn_check_space(const struct txn *txn,
		const struct space *space)
{
	if (txn->isolation == TXN_ISOLATION_LINEARIZABLE)
		return txn_check_space_linearizability(txn, space);
	return 0;
}

/**
 * This is an optimization, which exists to speed up selects
 * in autocommit mode. For such selects, we only need to
 * manage fiber garbage heap. If autocommit mode is
 * off, however, we must start engine transaction with the first
 * select.
 */
static inline int
txn_begin_ro_stmt(struct space *space, struct txn **txn,
		  struct txn_ro_savepoint *svp)
{
	svp->region = &fiber()->gc;
	svp->region_used = region_used(svp->region);
	*txn = in_txn();
	if (*txn != NULL) {
		if (txn_check_can_continue(*txn) != 0)
			return -1;
		if (txn_check_space(*txn, space) != 0)
			return -1;
		struct engine *engine = space->engine;
		return txn_begin_in_engine(engine, *txn);
	}
	return 0;
}

/**
 * End a read-only statement.
 */
static inline void
txn_end_ro_stmt(struct txn *txn, struct txn_ro_savepoint *svp)
{
	assert(txn == in_txn());
	if (txn) {
		/* nothing to do */
	} else {
		region_truncate(svp->region, svp->region_used);
	}
}

/**
 * Check whether a transaction which is used to apply
 * remote master rows generated some local changes.
 * Such transaction must be aborted, since we wouldn't
 * be able to *consistently* apply the local changes
 * to the remote master.
 */
bool
txn_is_distributed(struct txn *txn);

static inline bool
txn_is_fully_local(const struct txn *txn)
{
	return txn->n_new_rows == txn->n_local_rows &&
	       txn->n_applier_rows == 0;
}

/**
 * End a statement. In autocommit mode, end
 * the current transaction as well.
 *
 * Return 0 on success. On error, rollback
 * the statement and return -1.
 */
int
txn_commit_stmt(struct txn *txn, struct request *request);

/**
 * Rollback a statement. In autocommit mode,
 * rolls back the entire transaction.
 */
void
txn_rollback_stmt(struct txn *txn);

/**
 * Raise an error if this is a multi-statement transaction:
 * a yielding DDL operation, such as index build or space format
 * check, can not be part of a multi-statement transaction,
 * because there may be uncommitted objects in the schema cache,
 * which would be revealed to other fibers on yield.
 */
int
txn_check_singlestatement(struct txn *txn, const char *where);

/**
 * Enables or disables fiber yields inside the current transaction
 * depending on the value of the given flag. Yields are disabled
 * by installing a fiber-on-yield trigger that marks the transaction
 * as aborted, which results in rolling back the transaction on
 * commit.
 *
 * This function is used only by memtx engine because there are cases
 * when it doesn't support yields inside transactions. It is also
 * used to temporarily enable yields for long DDL operations such as
 * building an index or checking a space format.
 *
 * Return previous state of the flag: true - yields were enabled,
 * false - yields were disabled before the function call.
 */
bool
txn_can_yield(struct txn *txn, bool set);

/**
 * Returns true if the transaction has a single statement.
 * Supposed to be used from a space on_replace trigger to
 * detect transaction boundaries.
 */
static inline bool
txn_is_first_statement(struct txn *txn)
{
	return stailq_last(&txn->stmts) == stailq_first(&txn->stmts);
}

/** The current statement of the transaction. */
static inline struct txn_stmt *
txn_current_stmt(struct txn *txn)
{
	if (txn->in_sub_stmt == 0)
		return NULL;
	struct stailq_entry *stmt = txn->sub_stmt_begin[txn->in_sub_stmt - 1];
	stmt = stmt != NULL ? stailq_next(stmt) : stailq_first(&txn->stmts);
	return stailq_entry(stmt, struct txn_stmt, next);
}

/**
 * Allocate new savepoint object using region allocator.
 * Savepoint is allowed to be anonymous (i.e. without
 * name).
 */
struct txn_savepoint *
txn_savepoint_new(struct txn *txn, const char *name);

/** Find savepoint by its name in savepoint list. */
struct txn_savepoint *
txn_savepoint_by_name(struct txn *txn, const char *name);

/** Remove given and all newer entries from savepoint list. */
void
txn_savepoint_release(struct txn_savepoint *svp);

/**
 * Method to get txn's region to pass it outside the transaction manager.
 * Do not use txn->region, use this method to track txn's allocations!
 * Return region to txn with tx_region_release.
 */
struct region *
tx_region_acquire(struct txn *txn);

/**
 * Method to return region to txn and account new allocations.
 */
void
tx_region_release(struct txn *txn, enum tx_alloc_type alloc_type);

/**
 * FFI bindings: do not throw exceptions, do not accept extra
 * arguments
 */

/** \cond public */

/**
 * Transaction id - a non-persistent unique identifier
 * of the current transaction. -1 if there is no current
 * transaction.
 */
API_EXPORT int64_t
box_txn_id(void);

/**
 * Get isolation level of current transaction, one of enum txn_isolation_level
 * values (but cannot be TXN_ISOLATION_DEFAULT (which is zero) by design).
 * -1 if there is no current transaction.
 */
API_EXPORT int
box_txn_isolation(void);

/**
 * Return true if there is an active transaction.
 */
API_EXPORT bool
box_txn(void);

/**
 * Begin a transaction in the current fiber.
 *
 * A transaction is attached to caller fiber, therefore one fiber can have
 * only one active transaction.
 *
 * @retval 0 - success
 * @retval -1 - failed, perhaps a transaction has already been
 * started
 */
API_EXPORT int
box_txn_begin(void);

/**
 * Commit the current transaction.
 * @retval 0 - success
 * @retval -1 - failed, perhaps a disk write failure.
 * started
 */
API_EXPORT int
box_txn_commit(void);

/**
 * Rollback the current transaction.
 * May fail if called from a nested
 * statement.
 */
API_EXPORT int
box_txn_rollback(void);

/**
 * Allocate memory on txn memory pool.
 * The memory is automatically deallocated when the transaction
 * is committed or rolled back.
 *
 * @retval NULL out of memory
 */
API_EXPORT void *
box_txn_alloc(size_t size);

/**
 * Set @a timeout for transaction, when it expires, transaction
 * will be rolled back.
 *
 * @retval 0 if success
 * @retval -1 if timeout is less than or equal to 0, there is
 *            no current transaction or rollback timer for
 *            current transaction is already started.
 */
API_EXPORT int
box_txn_set_timeout(double timeout);

/**
 * Set an isolation @a level for a transaction.
 * Must be called before the first DML.
 * The level must be of enun txn_isolation_level values.
 * @retval 0 if success
 * @retval -1 if failed, diag is set.
 *
 */
API_EXPORT int
box_txn_set_isolation(uint32_t level);

/** \endcond public */

typedef struct txn_savepoint box_txn_savepoint_t;

/**
 * Create a new savepoint.
 * @retval not NULL Savepoint object.
 * @retval     NULL Client or memory error.
 */
API_EXPORT box_txn_savepoint_t *
box_txn_savepoint(void);

/**
 * Rollback to @a savepoint. Rollback all statements newer than a
 * saved statement. @A savepoint can be rolled back multiple
 * times. All existing savepoints, newer than @a savepoint, are
 * deleted and can not be used.
 * @A savepoint must be from a current transaction, else the
 * rollback crashes. To validate savepoints store transaction id
 * together with @a savepoint.
 * @retval  0 Success.
 * @retval -1 Client error.
 */
API_EXPORT int
box_txn_rollback_to_savepoint(box_txn_savepoint_t *savepoint);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_TXN_H_INCLUDED */
