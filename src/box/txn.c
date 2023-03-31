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
#include "txn.h"
#include "memtx_tx.h"
#include "txn_limbo.h"
#include "engine.h"
#include "tuple.h"
#include "journal.h"
#include <fiber.h>
#include "xrow.h"
#include "errinj.h"
#include "iproto_constants.h"
#include "box.h"
#include "session.h"
#include "rmean.h"

double too_long_threshold;

/**
 * Incremental counter for psn (prepare sequence number) of a transaction.
 * The next prepared transaction will get psn == txn_next_psn++.
 * See also struct txn::psn.
 */
int64_t txn_next_psn = TXN_MIN_PSN;

enum txn_isolation_level txn_default_isolation = TXN_ISOLATION_BEST_EFFORT;

const char *txn_isolation_level_strs[txn_isolation_level_MAX] = {
	"DEFAULT",
	"READ_COMMITTED",
	"READ_CONFIRMED",
	"BEST_EFFORT",
};

const char *txn_isolation_level_aliases[txn_isolation_level_MAX] = {
	"default",
	"read-committed",
	"read-confirmed",
	"best-effort",
};

/* Txn cache. */
static struct stailq txn_cache = {NULL, &txn_cache.first};

static int
txn_on_stop(struct trigger *trigger, void *event);

static int
txn_on_yield(struct trigger *trigger, void *event);

/** String representation of enum tx_alloc_type. */
const char *tx_alloc_type_strs[TX_ALLOC_TYPE_MAX] = {
	"statements",
	"user",
	"system",
};

/** Objects for tx_region_alloc_object method. */
enum tx_alloc_object {
	/**
	 * Object of type struct txn_stmt.
	 */
	TX_OBJECT_STMT = 0,
	/**
	 * Object of type struct xrow_header.
	 */
	TX_OBJECT_XROW_HEADER = 1,
	/**
	 * Object of type struct trigger.
	 */
	TX_OBJECT_TRIGGER = 2,
	/**
	 * Object of type struct ev_timer.
	 */
	TX_OBJECT_EV_TIMER = 3,
	TX_OBJECT_MAX = 4,
};

/**
 * Reset txn's alloc_stats.
 */
static inline void
txn_reset_stats(struct txn *txn)
{
	memset(txn->alloc_stats, 0, sizeof(txn->alloc_stats));
}

/**
 * Collect allocation statistics.
 */
static inline void
tx_track_allocation(struct txn *txn, size_t size, enum tx_alloc_type type)
{
	assert(type < TX_ALLOC_TYPE_MAX);
	assert(txn != NULL);
	assert(txn->alloc_stats != NULL);
	/* Check if txn region is not released. */
	assert(txn->acquired_region_used == 0);

	txn->alloc_stats[type] += size;
}

/**
 * Choose tx_alloc_type for alloc_obj.
 */
static inline enum tx_alloc_type
tx_region_object_to_type(enum tx_alloc_object alloc_obj)
{
	enum tx_alloc_type alloc_type = TX_ALLOC_TYPE_MAX;
	switch (alloc_obj) {
	case TX_OBJECT_STMT:
		alloc_type = TX_ALLOC_STMT;
		break;
	case TX_OBJECT_XROW_HEADER:
	case TX_OBJECT_TRIGGER:
	case TX_OBJECT_EV_TIMER:
		alloc_type = TX_ALLOC_SYSTEM;
		break;
	default:
		unreachable();
	};
	assert(alloc_type < TX_ALLOC_TYPE_MAX);
	return alloc_type;
}

/**
 * Alloc object on region. Pass object as enum tx_alloc_object.
 * Use this method to track txn's allocations!
 */
static inline void *
tx_region_alloc_object(struct txn *txn, enum tx_alloc_object alloc_obj,
		       size_t *size)
{
	size_t alloc_size = 0;
	void *alloc = NULL;
	enum tx_alloc_type alloc_type = tx_region_object_to_type(alloc_obj);
	switch (alloc_obj) {
	case TX_OBJECT_STMT:
		alloc = region_alloc_object(&txn->region,
					    struct txn_stmt, &alloc_size);
		break;
	case TX_OBJECT_TRIGGER:
		alloc = region_alloc_object(&txn->region,
					    struct trigger, &alloc_size);
		break;
	case TX_OBJECT_XROW_HEADER:
		alloc = region_alloc_object(&txn->region,
					    struct xrow_header, &alloc_size);
		break;
	case TX_OBJECT_EV_TIMER:
		alloc = region_alloc_object(&txn->region,
					    struct ev_timer, &alloc_size);
		break;
	default:
		unreachable();
	}
	assert(alloc_type < TX_ALLOC_TYPE_MAX);
	*size = alloc_size;
	if (alloc != NULL)
		tx_track_allocation(txn, alloc_size, alloc_type);
	return alloc;
}

/**
 * Tx_region method for aligned allocations of arbitrary size.
 * You must pass allocation type explicitly to categorize an allocation.
 * Use this method to track txn's allocations!
 */
static inline void *
tx_region_aligned_alloc(struct txn *txn, size_t size, size_t alignment,
			size_t alloc_type)
{
	void *allocation = region_aligned_alloc(&txn->region, size, alignment);
	if (allocation != NULL)
		tx_track_allocation(txn, size, alloc_type);
	return allocation;
}

/**
 * Method to get txn's region to pass it outside the transaction manager.
 * Do not use txn->region, use this method to track txn's allocations!
 * Return region to txn with tx_region_release.
 */
static inline struct region *
tx_region_acquire(struct txn *txn)
{
	assert(txn != NULL);
	assert(txn->acquired_region_used == 0);

	struct region *txn_region = &txn->region;
	txn->acquired_region_used = region_used(txn_region);
	return txn_region;
}

/**
 * Method to return region to txn and account new allocations.
 */
static inline void
tx_region_release(struct txn *txn, enum tx_alloc_type alloc_type)
{
	assert(txn != NULL);
	assert(alloc_type < TX_ALLOC_TYPE_MAX);
	assert(txn->acquired_region_used != 0);

	size_t taken_region_used = region_used(&txn->region);
	assert(taken_region_used >= txn->acquired_region_used);
	size_t new_alloc_size = taken_region_used - txn->acquired_region_used;
	txn->acquired_region_used = 0;
	if (new_alloc_size > 0)
		tx_track_allocation(txn, new_alloc_size, alloc_type);
}

static inline void
txn_set_timeout(struct txn *txn, double timeout)
{
	assert(timeout > 0);
	txn->timeout = timeout;
}

static int
txn_add_redo(struct txn *txn, struct txn_stmt *stmt, struct request *request)
{
	/* Create a redo log row. */
	size_t size;
	struct xrow_header *row;
	row = tx_region_alloc_object(txn, TX_OBJECT_XROW_HEADER, &size);
	if (row == NULL) {
		diag_set(OutOfMemory, size, "tx_region_alloc_object", "row");
		return -1;
	}
	if (request->header != NULL) {
		*row = *request->header;
	} else {
		/* Initialize members explicitly to save time on memset() */
		row->type = request->type;
		row->replica_id = 0;
		row->lsn = 0;
		row->sync = 0;
		row->tm = 0;
		row->flags = 0;
	}
	/*
	 * Group ID should be set both for requests not having a
	 * header, and for the ones who have it. This is because
	 * even if a request has a header, the group id could be
	 * omitted in it, and is default - 0. Even if the space's
	 * real group id is different.
	 */
	struct space *space = stmt->space;
	row->group_id = space != NULL ? space_group_id(space) : 0;
	struct region *txn_region = tx_region_acquire(txn);
	row->bodycnt = xrow_encode_dml(request, txn_region, row->body);
	tx_region_release(txn, TX_ALLOC_SYSTEM);
	txn_region = NULL;
	if (row->bodycnt < 0)
		return -1;
	stmt->row = row;
	return 0;
}

/** Initialize a new stmt object within txn. */
static struct txn_stmt *
txn_stmt_new(struct txn *txn)
{
	size_t size;
	struct txn_stmt *stmt;
	stmt = tx_region_alloc_object(txn, TX_OBJECT_STMT, &size);
	if (stmt == NULL) {
		diag_set(OutOfMemory, size, "tx_region_alloc_object", "stmt");
		return NULL;
	}

	/* Initialize members explicitly to save time on memset() */
	stmt->txn = in_txn();
	stmt->space = NULL;
	stmt->old_tuple = NULL;
	stmt->new_tuple = NULL;
	stmt->rollback_info.old_tuple = NULL;
	stmt->rollback_info.new_tuple = NULL;
	stmt->add_story = NULL;
	stmt->del_story = NULL;
	stmt->next_in_del_list = NULL;
	stmt->engine_savepoint = NULL;
	stmt->row = NULL;
	stmt->has_triggers = false;
	stmt->is_own_change = false;
	return stmt;
}

static inline void
txn_stmt_destroy(struct txn_stmt *stmt)
{
	assert(stmt->add_story == NULL && stmt->del_story == NULL);

	if (stmt->old_tuple != NULL)
		tuple_unref(stmt->old_tuple);
	if (stmt->new_tuple != NULL)
		tuple_unref(stmt->new_tuple);
	if (stmt->rollback_info.old_tuple != NULL)
		tuple_unref(stmt->rollback_info.old_tuple);
	if (stmt->rollback_info.new_tuple != NULL)
		tuple_unref(stmt->rollback_info.new_tuple);
}

void
txn_stmt_prepare_rollback_info(struct txn_stmt *stmt, struct tuple *old_tuple,
			       struct tuple *new_tuple)
{
	stmt->rollback_info.old_tuple = old_tuple;
	if (stmt->rollback_info.old_tuple != NULL)
		tuple_ref(stmt->rollback_info.old_tuple);
	stmt->rollback_info.new_tuple = new_tuple;
	if (stmt->rollback_info.new_tuple != NULL)
		tuple_ref(stmt->rollback_info.new_tuple);
}

/*
 * Undo changes done by a statement and run the corresponding
 * rollback triggers.
 *
 * Note, a trigger set by a particular statement must be run right
 * after the statement is rolled back, because rollback triggers
 * installed by DDL statements restore the schema cache, which is
 * necessary to roll back previous statements. For example, to roll
 * back a DML statement applied to a space whose index is dropped
 * later in the same transaction, we must restore the dropped index
 * first.
 */
static void
txn_rollback_one_stmt(struct txn *txn, struct txn_stmt *stmt)
{
	if (txn->engine != NULL && stmt->space != NULL)
		engine_rollback_statement(txn->engine, txn, stmt);
	if (stmt->has_triggers && trigger_run(&stmt->on_rollback, txn) != 0) {
		diag_log();
		panic("statement rollback trigger failed");
	}
}

static void
txn_rollback_to_svp(struct txn *txn, struct stailq_entry *svp)
{
	struct txn_stmt *stmt;
	struct stailq rollback;
	stailq_cut_tail(&txn->stmts, svp, &rollback);
	stailq_reverse(&rollback);
	stailq_foreach_entry(stmt, &rollback, next) {
		txn_rollback_one_stmt(txn, stmt);
		if (stmt->row != NULL && stmt->row->replica_id == 0) {
			assert(txn->n_new_rows > 0);
			txn->n_new_rows--;
			if (stmt->row->group_id == GROUP_LOCAL)
				txn->n_local_rows--;
		}
		if (stmt->row != NULL && stmt->row->replica_id != 0) {
			assert(txn->n_applier_rows > 0);
			txn->n_applier_rows--;
		}
		txn_stmt_destroy(stmt);
		stmt->space = NULL;
		stmt->row = NULL;
	}
}

/*
 * Return a txn from cache or create a new one if cache is empty.
 */
inline static struct txn *
txn_new(void)
{
	if (!stailq_empty(&txn_cache))
		return stailq_shift_entry(&txn_cache, struct txn, in_txn_cache);

	/* Create a region. */
	struct region region;
	region_create(&region, &cord()->slabc);

	/* Place txn structure on the region. */
	int size;
	struct txn *txn = region_alloc_object(&region, struct txn, &size);
	if (txn == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_object", "txn");
		return NULL;
	}
	assert(region_used(&region) == sizeof(*txn));
	txn_reset_stats(txn);
	txn->region = region;
	rlist_create(&txn->read_set);
	rlist_create(&txn->point_holes_list);
	rlist_create(&txn->gap_list);
	rlist_create(&txn->in_read_view_txs);
	rlist_create(&txn->in_all_txs);
	txn->space_on_replace_triggers_depth = 0;
	txn->acquired_region_used = 0;
	txn->limbo_entry = NULL;
	return txn;
}

/*
 * Free txn memory and return it to a cache.
 */
inline static void
txn_free(struct txn *txn)
{
	assert(txn->limbo_entry == NULL);
	if (txn->rollback_timer != NULL)
		ev_timer_stop(loop(), txn->rollback_timer);
	memtx_tx_clean_txn(txn);
	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next)
		txn_stmt_destroy(stmt);

	/* Truncate region up to struct txn size. */
	txn_reset_stats(txn);
	region_truncate(&txn->region, sizeof(struct txn));
	stailq_add(&txn_cache, &txn->in_txn_cache);
}

void
diag_set_txn_sign_detailed(const char *file, unsigned line, int64_t signature)
{
	if (signature >= JOURNAL_ENTRY_ERR_MIN)
		return diag_set_journal_res_detailed(file, line, signature);
	switch(signature) {
	case TXN_SIGNATURE_ROLLBACK:
		diag_set_detailed(file, line, ClientError, ER_TXN_ROLLBACK);
		return;
	case TXN_SIGNATURE_QUORUM_TIMEOUT:
		diag_set_detailed(file, line, ClientError,
				  ER_SYNC_QUORUM_TIMEOUT);
		return;
	case TXN_SIGNATURE_SYNC_ROLLBACK:
		diag_set_detailed(file, line, ClientError, ER_SYNC_ROLLBACK);
		return;
	case TXN_SIGNATURE_ABORT:
		if (diag_is_empty(diag_get()))
			panic("Tried to get an absent transaction error");
		return;
	}
	panic("Transaction signature %lld can't be converted to an error "
	      "at %s:%u", (long long)signature, file, line);
}

struct txn *
txn_begin(void)
{
	static int64_t tsn = 0;
	assert(! in_txn());
	struct txn *txn = txn_new();
	if (txn == NULL)
		return NULL;

	/* Initialize members explicitly to save time on memset() */
	stailq_create(&txn->stmts);
	txn->n_new_rows = 0;
	txn->n_local_rows = 0;
	txn->n_applier_rows = 0;
	txn->flags = 0;
	txn->in_sub_stmt = 0;
	txn->id = ++tsn;
	txn->psn = 0;
	txn->rv_psn = 0;
	txn->status = TXN_INPROGRESS;
	txn->isolation = txn_default_isolation;
	txn->signature = TXN_SIGNATURE_UNKNOWN;
	txn->engine = NULL;
	txn->engine_tx = NULL;
	txn->fk_deferred_count = 0;
	txn->is_schema_changed = false;
	rlist_create(&txn->savepoints);
	txn->fiber = NULL;
	txn->timeout = TIMEOUT_INFINITY;
	txn->rollback_timer = NULL;
	fiber_set_txn(fiber(), txn);
	trigger_create(&txn->fiber_on_yield, txn_on_yield, NULL, NULL);
	trigger_add(&fiber()->on_yield, &txn->fiber_on_yield);
	trigger_create(&txn->fiber_on_stop, txn_on_stop, NULL, NULL);
	trigger_add(&fiber()->on_stop, &txn->fiber_on_stop);
	/*
	 * By default, all transactions may yield.
	 * It's a responsibility of an engine to disable yields
	 * if they are not supported.
	 */
	txn_set_flags(txn, TXN_CAN_YIELD);
	memtx_tx_register_txn(txn);
	rmean_collect(rmean_box, IPROTO_BEGIN, 1);
	return txn;
}

int
txn_begin_in_engine(struct engine *engine, struct txn *txn)
{
	if (engine->flags & ENGINE_BYPASS_TX)
		return 0;
	if (txn->engine == NULL) {
		txn->engine = engine;
		return engine_begin(engine, txn);
	} else if (txn->engine != engine) {
		/**
		 * Only one engine can be used in
		 * a multi-statement transaction currently.
		 */
		diag_set(ClientError, ER_CROSS_ENGINE_TRANSACTION);
		return -1;
	}
	return 0;
}

int
txn_begin_stmt(struct txn *txn, struct space *space, uint16_t type)
{
	assert(txn == in_txn());
	assert(txn != NULL);
	if (txn->in_sub_stmt > TXN_SUB_STMT_MAX) {
		diag_set(ClientError, ER_SUB_STMT_MAX);
		return -1;
	}

	if (txn->status == TXN_IN_READ_VIEW) {
		rlist_del(&txn->in_read_view_txs);
		txn->status = TXN_ABORTED;
		txn_set_flags(txn, TXN_IS_CONFLICTED);
	}

	if (txn_check_can_continue(txn) != 0)
		return -1;

	struct txn_stmt *stmt = txn_stmt_new(txn);
	if (stmt == NULL)
		return -1;

	/* Set the savepoint for statement rollback. */
	txn->sub_stmt_begin[txn->in_sub_stmt] = stailq_last(&txn->stmts);
	txn->in_sub_stmt++;
	stailq_add_tail_entry(&txn->stmts, stmt, next);

	if (space == NULL)
		return 0;

	struct engine *engine = space->engine;
	if (txn_begin_in_engine(engine, txn) != 0)
		goto fail;

	stmt->space = space;
	stmt->type = type;
	if (engine_begin_statement(engine, txn) != 0)
		goto fail;

	return 0;
fail:
	txn_rollback_stmt(txn);
	return -1;
}

bool
txn_is_distributed(struct txn *txn)
{
	assert(txn == in_txn());
	/**
	 * Transaction has both new and applier rows, and some of
	 * the new rows need to be replicated back to the
	 * server of transaction origin.
	 */
	return (txn->n_new_rows > 0 && txn->n_applier_rows > 0 &&
		txn->n_new_rows != txn->n_local_rows);
}

/**
 * End a statement.
 */
int
txn_commit_stmt(struct txn *txn, struct request *request)
{
	assert(txn->in_sub_stmt > 0);
	/*
	 * Run on_replace triggers. For now, disallow mutation
	 * of tuples in the trigger.
	 */
	struct txn_stmt *stmt = txn_current_stmt(txn);

	/*
	 * Create WAL record for the write requests in
	 * non-temporary spaces. stmt->space can be NULL for
	 * IRPOTO_NOP or IPROTO_RAFT_CONFIRM.
	 */
	if (stmt->space == NULL || !space_is_temporary(stmt->space)) {
		if (txn_add_redo(txn, stmt, request) != 0)
			goto fail;
		assert(stmt->row != NULL);
		if (stmt->row->replica_id == 0) {
			++txn->n_new_rows;
			if (stmt->row->group_id == GROUP_LOCAL)
				++txn->n_local_rows;

		} else {
			++txn->n_applier_rows;
		}
	}
	/*
	 * If there are triggers, and they are not disabled, and
	 * the statement found any rows, run triggers.
	 * XXX:
	 * - vinyl doesn't set old/new tuple, so triggers don't
	 *   work for it
	 * - perhaps we should run triggers even for deletes which
	 *   doesn't find any rows
	 */
	if (stmt->space != NULL && stmt->space->run_triggers &&
	    (stmt->old_tuple || stmt->new_tuple)) {
		if (!rlist_empty(&stmt->space->on_replace)) {
			txn->space_on_replace_triggers_depth++;
			int rc = trigger_run(&stmt->space->on_replace, txn);
			txn->space_on_replace_triggers_depth--;
			if (rc != 0)
				goto fail;
			assert(txn->in_sub_stmt > 0);
		}
	}
	--txn->in_sub_stmt;
	return 0;
fail:
	txn_rollback_stmt(txn);
	return -1;
}

/* A helper function to process on_wal_write triggers. */
static void
txn_run_wal_write_triggers(struct txn *txn)
{
	/* Is zero during recovery. */
	assert(txn->signature >= 0);
	if (trigger_run(&txn->on_wal_write, txn) != 0) {
		/*
		 * As transaction couldn't handle a trigger error so
		 * there is no option except panic.
		 */
		diag_log();
		unreachable();
		panic("wal_write trigger failed");
	}
	/* WAL write happens only once. */
	trigger_destroy(&txn->on_wal_write);
}

/**
 * If there is no fiber waiting for the transaction then the
 * transaction could be safely freed. In the opposite case the
 * owner fiber is in duty to free this transaction.
 */
static void
txn_free_or_wakeup(struct txn *txn)
{
	if (txn->fiber == NULL)
		txn_free(txn);
	else {
		txn_set_flags(txn, TXN_IS_DONE);
		fiber_wakeup(txn->fiber);
	}
}

void
txn_complete_fail(struct txn *txn)
{
	assert(!txn_has_flag(txn, TXN_IS_DONE));
	assert(txn->signature < 0);
	assert(txn->signature != TXN_SIGNATURE_UNKNOWN);
	assert(in_txn() == txn);
	if (txn->limbo_entry != NULL) {
		assert(txn_has_flag(txn, TXN_WAIT_SYNC));
		txn_limbo_abort(&txn_limbo, txn->limbo_entry);
		txn->limbo_entry = NULL;
	}
	txn->status = TXN_ABORTED;
	struct txn_stmt *stmt;
	stailq_reverse(&txn->stmts);
	stailq_foreach_entry(stmt, &txn->stmts, next)
		txn_rollback_one_stmt(txn, stmt);
	if (txn->engine != NULL)
		engine_rollback(txn->engine, txn);
	if (txn_has_flag(txn, TXN_HAS_TRIGGERS)) {
		if (trigger_run(&txn->on_rollback, txn) != 0) {
			diag_log();
			panic("transaction rollback trigger failed");
		}
		/* Can't rollback more than once. */
		trigger_destroy(&txn->on_rollback);
		/* Commit won't happen after rollback. */
		trigger_destroy(&txn->on_commit);
	}
	txn_free_or_wakeup(txn);
	rmean_collect(rmean_box, IPROTO_ROLLBACK, 1);
}

void
txn_complete_success(struct txn *txn)
{
	assert(!txn_has_flag(txn, TXN_IS_DONE));
	assert(!txn_has_flag(txn, TXN_WAIT_SYNC));
	assert(txn->signature >= 0);
	assert(in_txn() == txn);
	txn->status = TXN_COMMITTED;
	if (txn->engine != NULL)
		engine_commit(txn->engine, txn);
	if (txn_has_flag(txn, TXN_HAS_TRIGGERS)) {
		/*
		 * Commit triggers must be run in the same order they were added
		 * so that a trigger sees the changes done by previous triggers
		 * (this is vital for DDL).
		 */
		if (trigger_run_reverse(&txn->on_commit, txn) != 0) {
			diag_log();
			panic("transaction commit trigger failed");
		}
		/* Can't commit more than once. */
		trigger_destroy(&txn->on_commit);
		/* Rollback won't happen after commit. */
		trigger_destroy(&txn->on_rollback);
	}
	txn_free_or_wakeup(txn);
	rmean_collect(rmean_box, IPROTO_COMMIT, 1);
}

/** Callback invoked when the transaction's journal write is finished. */
static void
txn_on_journal_write(struct journal_entry *entry)
{
	struct txn *txn = entry->complete_data;
	assert(txn->signature == TXN_SIGNATURE_UNKNOWN);
	txn->signature = entry->res;
	/*
	 * Some commit/rollback triggers require for in_txn fiber
	 * variable to be set so restore it for the time triggers
	 * are in progress.
	 */
	assert(in_txn() == NULL);
	fiber_set_txn(fiber(), txn);
	/*
	 * Use session and credentials of the original fiber for
	 * commit/rollback triggers.
	 */
	struct session *orig_session = fiber_get_session(fiber());
	struct session *session = (txn->fiber != NULL ?
				   fiber_get_session(txn->fiber) : NULL);
	if (session != NULL)
		fiber_set_session(fiber(), session);
	struct credentials *orig_creds = fiber_get_user(fiber());
	struct credentials *creds = (txn->fiber != NULL ?
				     fiber_get_user(txn->fiber) : NULL);
	if (creds != NULL)
		fiber_set_user(fiber(), creds);
	if (txn->signature < 0) {
		txn_complete_fail(txn);
		goto finish;
	}
	double stop_tm = ev_monotonic_now(loop());
	double delta = stop_tm - txn->start_tm;
	if (delta > too_long_threshold) {
		int n_rows = txn->n_new_rows + txn->n_applier_rows;
		say_warn_ratelimited("too long WAL write: %d rows at LSN %lld: "
				     "%.3f sec", n_rows,
				     (long long)(txn->signature - n_rows + 1),
				     delta);
	}
	if (txn_has_flag(txn, TXN_HAS_TRIGGERS))
		txn_run_wal_write_triggers(txn);
	if (!txn_has_flag(txn, TXN_WAIT_SYNC)) {
		txn_complete_success(txn);
	} else {
		int64_t lsn;
		/*
		 * XXX: that is quite ugly. Need a more reliable way to get the
		 * synchro LSN.
		 */
		if (txn->n_applier_rows > 0)
			lsn = entry->rows[txn->n_applier_rows - 1]->lsn;
		else
			lsn = entry->rows[entry->n_rows - 1]->lsn;
		txn_limbo_assign_lsn(&txn_limbo, txn->limbo_entry, lsn);
		if (txn->fiber != NULL)
			fiber_wakeup(txn->fiber);
	}
finish:
	fiber_set_txn(fiber(), NULL);
	fiber_set_user(fiber(), orig_creds);
	fiber_set_session(fiber(), orig_session);
}

static struct journal_entry *
txn_journal_entry_new(struct txn *txn)
{
	struct journal_entry *req;
	struct txn_stmt *stmt;

	assert(txn->n_new_rows + txn->n_applier_rows > 0);

	/* Save space for an additional NOP row just in case. */
	struct region *txn_region = tx_region_acquire(txn);
	req = journal_entry_new(txn->n_new_rows + txn->n_applier_rows + 1,
				txn_region, txn_on_journal_write, txn);
	tx_region_release(txn, TX_ALLOC_SYSTEM);
	txn_region = NULL;
	if (req == NULL)
		return NULL;

	struct xrow_header **remote_row = req->rows;
	struct xrow_header **local_row = req->rows + txn->n_applier_rows;
	bool is_sync = false;
	/*
	 * A transaction which consists of NOPs solely should pass through the
	 * limbo without waiting. Even when the limbo is not empty. This is
	 * because otherwise they might fail with the limbo being not owned by
	 * the NOPs owner. But it does not matter, because they just need to
	 * bump vclock. There is nothing to confirm or rollback in them.
	 */
	bool is_fully_nop = true;

	stailq_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->has_triggers) {
			txn_init_triggers(txn);
			rlist_splice(&txn->on_commit, &stmt->on_commit);
		}

		/* A read (e.g. select) request */
		if (stmt->row == NULL)
			continue;

		if (stmt->row->type != IPROTO_NOP) {
			is_fully_nop = false;
			is_sync = is_sync || (stmt->space != NULL &&
					      stmt->space->def->opts.is_sync);
		}

		if (stmt->row->replica_id == 0)
			*local_row++ = stmt->row;
		else
			*remote_row++ = stmt->row;

		req->approx_len += xrow_approx_len(stmt->row);
	}
	/*
	 * There is no a check for all-local rows, because a local
	 * space can't be synchronous. So if there is at least one
	 * synchronous space, the transaction is not local.
	 */
	if (!txn_has_flag(txn, TXN_FORCE_ASYNC) && !is_fully_nop) {
		if (is_sync) {
			txn_set_flags(txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
		} else if (!txn_limbo_is_empty(&txn_limbo)) {
			/*
			 * There some sync entries on the
			 * fly thus wait for their completion
			 * even if this particular transaction
			 * doesn't touch sync space (each sync txn
			 * should be considered as a barrier).
			 */
			txn_set_flags(txn, TXN_WAIT_SYNC);
		}
	}

	assert(remote_row == req->rows + txn->n_applier_rows);
	assert(local_row == remote_row + txn->n_new_rows);

	/*
	 * Append a dummy NOP statement to preserve replication tx
	 * boundaries when the last tx row is a local one, and the
	 * transaction has at least one global row.
	 */
	if (txn->n_local_rows > 0 &&
	    (txn->n_local_rows != txn->n_new_rows || txn->n_applier_rows > 0) &&
	    (*(local_row - 1))->group_id == GROUP_LOCAL) {
		size_t size;
		*local_row =
			tx_region_alloc_object(txn, TX_OBJECT_XROW_HEADER,
					       &size);
		if (*local_row == NULL) {
			diag_set(OutOfMemory, size, "tx_region_alloc_object",
				 "row");
			return NULL;
		}
		memset(*local_row, 0, sizeof(**local_row));
		(*local_row)->type = IPROTO_NOP;
		(*local_row)->group_id = GROUP_DEFAULT;
	} else {
		--req->n_rows;
	}

	static const uint8_t flags_map[] = {
		[TXN_WAIT_SYNC] = IPROTO_FLAG_WAIT_SYNC,
		[TXN_WAIT_ACK] = IPROTO_FLAG_WAIT_ACK,
	};

	req->flags |= flags_map[txn->flags & TXN_WAIT_SYNC];
	req->flags |= flags_map[txn->flags & TXN_WAIT_ACK];

	return req;
}

/*
 * Prepare a transaction using engines.
 */
static int
txn_prepare(struct txn *txn)
{
	if (txn_check_can_continue(txn) != 0)
		return -1;

	if (txn->rollback_timer != NULL) {
		ev_timer_stop(loop(), txn->rollback_timer);
		txn->rollback_timer = NULL;
	}
	/*
	 * If transaction has been started in SQL, deferred
	 * foreign key constraints must not be violated.
	 * If not so, just rollback transaction.
	 */
	if (txn->fk_deferred_count != 0) {
		diag_set(ClientError, ER_FOREIGN_KEY_CONSTRAINT);
		txn->psn = 0;
		return -1;
	}

	assert(txn->psn == 0);
	/* psn must be set before calling engine handlers. */
	txn->psn = txn_next_psn++;

	/*
	 * Perform transaction conflict resolution. Engine == NULL when
	 * we have a bunch of IPROTO_NOP statements.
	 */
	if (txn->engine != NULL) {
		if (engine_prepare(txn->engine, txn) != 0) {
			txn->psn = 0;
			return -1;
		}
	}

	trigger_clear(&txn->fiber_on_stop);
	trigger_clear(&txn->fiber_on_yield);

	txn->start_tm = ev_monotonic_now(loop());
	txn->status = TXN_PREPARED;
	return 0;
}

/**
 * Complete transaction early if it is barely nop.
 */
static bool
txn_commit_nop(struct txn *txn)
{
	if (txn->n_new_rows + txn->n_applier_rows == 0) {
		txn->signature = TXN_SIGNATURE_NOP;
		txn_complete_success(txn);
		fiber_set_txn(fiber(), NULL);
		return true;
	}

	return false;
}

int
txn_commit_try_async(struct txn *txn)
{
	struct journal_entry *req;

	ERROR_INJECT(ERRINJ_TXN_COMMIT_ASYNC, {
		diag_set(ClientError, ER_INJECTION,
			 "txn commit async injection");
		goto rollback;
	});

	if (txn_prepare(txn) != 0)
		goto rollback;

	if (txn_commit_nop(txn))
		return 0;

	req = txn_journal_entry_new(txn);
	if (req == NULL)
		goto rollback;

	bool is_sync = txn_has_flag(txn, TXN_WAIT_SYNC);
	if (is_sync) {
		/* See txn_commit(). */
		uint32_t origin_id = req->rows[0]->replica_id;
		txn->limbo_entry = txn_limbo_append(&txn_limbo, origin_id, txn);
		if (txn->limbo_entry == NULL)
			goto rollback;
	}

	fiber_set_txn(fiber(), NULL);
	if (journal_write_try_async(req) != 0) {
		fiber_set_txn(fiber(), txn);
		diag_log();
		goto rollback;
	}

	return 0;

rollback:
	assert(txn->fiber == NULL);
	txn_abort(txn);
	return -1;
}

int
txn_commit(struct txn *txn)
{
	struct journal_entry *req;
	struct txn_limbo_entry *limbo_entry = NULL;

	txn->fiber = fiber();

	if (txn_prepare(txn) != 0)
		goto rollback_abort;

	if (txn_commit_nop(txn)) {
		txn_free(txn);
		return 0;
	}

	req = txn_journal_entry_new(txn);
	if (req == NULL)
		goto rollback_abort;
	/*
	 * Do not cache the flag value in a variable. The flag might be deleted
	 * during WAL write. This can happen for async transactions created
	 * during CONFIRM write, whose all blocking sync transactions get
	 * confirmed. Then they turn the async transaction into just a plain
	 * txn not waiting for anything.
	 */
	if (txn_has_flag(txn, TXN_WAIT_SYNC)) {
		/*
		 * Remote rows, if any, come before local rows, so
		 * check for originating instance id here.
		 */
		uint32_t origin_id = req->rows[0]->replica_id;

		/*
		 * Append now. Before even WAL write is done.
		 * After WAL write nothing should fail, even OOM
		 * wouldn't be acceptable.
		 */
		limbo_entry = txn_limbo_append(&txn_limbo, origin_id, txn);
		if (limbo_entry == NULL)
			goto rollback_abort;
		txn->limbo_entry = limbo_entry;
	}

	fiber_set_txn(fiber(), NULL);
	if (journal_write(req) != 0)
		goto rollback_io;
	if (req->res < 0) {
		diag_set_journal_res(req->res);
		goto rollback_io;
	}
	if (txn_has_flag(txn, TXN_WAIT_SYNC)) {
		assert(limbo_entry->lsn > 0);
		/*
		 * XXX: ACK should be done on WAL write too. But it can make
		 * another WAL write. Can't be done until it works
		 * asynchronously.
		 */
		if (txn_has_flag(txn, TXN_WAIT_ACK)) {
			txn_limbo_ack(&txn_limbo, txn_limbo.owner_id,
				      limbo_entry->lsn);
		}
		if (txn_limbo_wait_complete(&txn_limbo, limbo_entry) < 0)
			goto rollback;
	}
	assert(txn_has_flag(txn, TXN_IS_DONE));
	assert(txn->signature >= 0);

	/* Synchronous transactions are freed by the calling fiber. */
	txn_free(txn);
	return 0;

rollback_io:
	diag_log();
rollback_abort:
	txn->signature = TXN_SIGNATURE_ABORT;
rollback:
	assert(txn->signature != TXN_SIGNATURE_UNKNOWN);
	assert(txn->fiber != NULL);
	if (!txn_has_flag(txn, TXN_IS_DONE)) {
		fiber_set_txn(fiber(), txn);
		txn_rollback(txn);
	} else {
		assert(in_txn() == NULL);
	}
	txn_free(txn);
	return -1;
}

void
txn_rollback_stmt(struct txn *txn)
{
	if (txn == NULL || txn->in_sub_stmt == 0)
		return;
	assert(txn->in_sub_stmt > 0);
	txn->in_sub_stmt--;
	txn_rollback_to_svp(txn, txn->sub_stmt_begin[txn->in_sub_stmt]);
}

void
txn_rollback(struct txn *txn)
{
	assert(txn == in_txn());
	assert(txn->signature != TXN_SIGNATURE_UNKNOWN);
	txn->status = TXN_ABORTED;
	trigger_clear(&txn->fiber_on_stop);
	trigger_clear(&txn->fiber_on_yield);
	txn_complete_fail(txn);
	fiber_set_txn(fiber(), NULL);
}

void
txn_abort(struct txn *txn)
{
	assert(!diag_is_empty(diag_get()));
	assert(txn->signature == TXN_SIGNATURE_UNKNOWN);
	txn->signature = TXN_SIGNATURE_ABORT;
	txn_rollback(txn);
}

int
txn_check_singlestatement(struct txn *txn, const char *where)
{
	if (!txn_is_first_statement(txn)) {
		diag_set(ClientError, ER_MULTISTATEMENT_TRANSACTION, where);
		return -1;
	}
	return 0;
}

bool
txn_can_yield(struct txn *txn, bool set)
{
	assert(txn == in_txn());
	bool could = txn_has_flag(txn, TXN_CAN_YIELD);
	if (set) {
		txn_set_flags(txn, TXN_CAN_YIELD);
	} else {
		txn_clear_flags(txn, TXN_CAN_YIELD);
	}
	return could;
}

int64_t
box_txn_id(void)
{
	struct txn *txn = in_txn();
	if (txn != NULL)
		return txn->id;
	else
		return -1;
}

bool
box_txn(void)
{
	return in_txn() != NULL;
}

int
box_txn_begin(void)
{
	if (in_txn()) {
		diag_set(ClientError, ER_ACTIVE_TRANSACTION);
		return -1;
	}
	if (txn_begin() == NULL)
		return -1;
	txn_set_timeout(in_txn(), txn_timeout_default);
	return 0;
}

int
box_txn_commit(void)
{
	struct txn *txn = in_txn();
	/**
	 * COMMIT is like BEGIN or ROLLBACK
	 * a "transaction-initiating statement".
	 * Do nothing if transaction is not started,
	 * it's the same as BEGIN + COMMIT.
	*/
	if (! txn)
		return 0;
	if (txn->in_sub_stmt) {
		diag_set(ClientError, ER_COMMIT_IN_SUB_STMT);
		return -1;
	}
	int rc = txn_commit(txn);
	fiber_gc();
	return rc;
}

int
box_txn_rollback(void)
{
	struct txn *txn = in_txn();
	if (txn == NULL)
		return 0;
	if (txn && txn->in_sub_stmt) {
		diag_set(ClientError, ER_ROLLBACK_IN_SUB_STMT);
		return -1;
	}
	assert(txn->signature == TXN_SIGNATURE_UNKNOWN);
	txn->signature = TXN_SIGNATURE_ROLLBACK;
	txn_rollback(txn); /* doesn't throw */
	fiber_gc();
	return 0;
}

void *
box_txn_alloc(size_t size)
{
	struct txn *txn = in_txn();
	if (txn == NULL) {
		/* There are no transaction yet - return an error. */
		diag_set(ClientError, ER_NO_TRANSACTION);
		return NULL;
	}
	union natural_align {
		void *p;
		double lf;
		long l;
	};
	return tx_region_aligned_alloc(txn, size, alignof(union natural_align),
				       TX_ALLOC_USER_DATA);
}

int
box_txn_set_timeout(double timeout)
{
	if (timeout <= 0) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "timeout must be a number greater than 0");
		return -1;
	}
	struct txn *txn = in_txn();
	if (txn == NULL) {
		diag_set(ClientError, ER_NO_TRANSACTION);
		return -1;
	}
	if (txn->rollback_timer != NULL) {
		diag_set(ClientError, ER_ACTIVE_TIMER);
		return -1;
	}
	txn_set_timeout(txn, timeout);
	return 0;
}

int
box_txn_set_isolation(uint32_t level)
{
	if (level >= txn_isolation_level_MAX) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "unknown isolation level");
		return -1;
	}
	struct txn *txn = in_txn();
	if (txn == NULL) {
		diag_set(ClientError, ER_NO_TRANSACTION);
		return -1;
	}
	if (!stailq_empty(&txn->stmts)) {
		diag_set(ClientError, ER_ACTIVE_TRANSACTION);
		return -1;
	}
	if (level == TXN_ISOLATION_DEFAULT)
		level = txn_default_isolation;
	txn->isolation = level;
	return 0;
}

struct txn_savepoint *
txn_savepoint_new(struct txn *txn, const char *name)
{
	assert(txn == in_txn());
	int name_len = name != NULL ? strlen(name) : 0;
	struct txn_savepoint *svp;
	static_assert(sizeof(svp->name) == 1,
		      "name member already has 1 byte for 0 termination");
	size_t size = sizeof(*svp) + name_len;
	svp = tx_region_aligned_alloc(txn, size, alignof(*svp),
				      TX_ALLOC_SYSTEM);
	if (svp == NULL) {
		diag_set(OutOfMemory, size, "region_aligned_alloc", "svp");
		return NULL;
	}
	svp->stmt = stailq_last(&txn->stmts);
	svp->in_sub_stmt = txn->in_sub_stmt;
	svp->fk_deferred_count = txn->fk_deferred_count;
	if (name != NULL) {
		/*
		 * If savepoint with given name already exists,
		 * erase it from the list. This has to be done
		 * in accordance with ANSI SQL compliance.
		 */
		struct txn_savepoint *old_svp =
			txn_savepoint_by_name(txn, name);
		if (old_svp != NULL)
			rlist_del(&old_svp->link);
		memcpy(svp->name, name, name_len + 1);
	} else {
		svp->name[0] = 0;
	}
	rlist_add_entry(&txn->savepoints, svp, link);
	return svp;
}

struct txn_savepoint *
txn_savepoint_by_name(struct txn *txn, const char *name)
{
	assert(txn == in_txn());
	struct txn_savepoint *sv;
	rlist_foreach_entry(sv, &txn->savepoints, link) {
		if (strcmp(sv->name, name) == 0)
			return sv;
	}
	return NULL;
}

box_txn_savepoint_t *
box_txn_savepoint(void)
{
	struct txn *txn = in_txn();
	if (txn == NULL) {
		diag_set(ClientError, ER_NO_TRANSACTION);
		return NULL;
	}
	return txn_savepoint_new(txn, NULL);
}

int
box_txn_rollback_to_savepoint(box_txn_savepoint_t *svp)
{
	struct txn *txn = in_txn();
	if (txn == NULL) {
		diag_set(ClientError, ER_NO_TRANSACTION);
		return -1;
	}
	struct txn_stmt *stmt = svp->stmt == NULL ? NULL :
			stailq_entry(svp->stmt, struct txn_stmt, next);
	if (stmt != NULL && stmt->space == NULL && stmt->row == NULL) {
		/*
		 * The statement at which this savepoint was
		 * created has been rolled back.
		 */
		diag_set(ClientError, ER_NO_SUCH_SAVEPOINT);
		return -1;
	}
	if (svp->in_sub_stmt != txn->in_sub_stmt) {
		diag_set(ClientError, ER_NO_SUCH_SAVEPOINT);
		return -1;
	}
	txn_rollback_to_svp(txn, svp->stmt);
	/* Discard from list all newer savepoints. */
	RLIST_HEAD(discard);
	rlist_cut_before(&discard, &txn->savepoints, &svp->link);
	txn->fk_deferred_count = svp->fk_deferred_count;
	return 0;
}

void
txn_savepoint_release(struct txn_savepoint *svp)
{
	struct txn *txn = in_txn();
	assert(txn != NULL);
	/* Make sure that savepoint hasn't been released yet. */
	struct txn_stmt *stmt = svp->stmt == NULL ? NULL :
				stailq_entry(svp->stmt, struct txn_stmt, next);
	assert(stmt == NULL || (stmt->space != NULL && stmt->row != NULL));
	(void) stmt;
	/*
	 * Discard current savepoint alongside with all
	 * created after it savepoints.
	 */
	RLIST_HEAD(discard);
	rlist_cut_before(&discard, &txn->savepoints, rlist_next(&svp->link));
}

static int
txn_on_stop(struct trigger *trigger, void *event)
{
	(void) trigger;
	(void) event;
	struct txn *txn = in_txn();
	assert(txn->signature == TXN_SIGNATURE_UNKNOWN);
	txn->signature = TXN_SIGNATURE_ROLLBACK;
	txn_rollback(txn);
	fiber_gc();
	return 0;
}

static void
txn_on_timeout(ev_loop *loop, ev_timer *watcher, int revents)
{
	(void) loop;
	(void) revents;
	struct txn *txn = (struct txn *)watcher->data;
	txn_rollback_to_svp(txn, NULL);
	txn->status = TXN_ABORTED;
	txn_set_flags(txn, TXN_IS_ABORTED_BY_TIMEOUT);
}

/**
 * Memtx yield-in-transaction trigger callback.
 *
 * In case of a yield inside memtx multi-statement transaction
 * we must, first of all, roll back the effects of the transaction
 * so that concurrent transactions won't see dirty, uncommitted
 * data.
 *
 * Second, we must abort the transaction, since it has been rolled
 * back implicitly. The transaction can not be rolled back
 * completely from within a yield trigger, since a yield trigger
 * can't fail. Instead, we mark the transaction as aborted and
 * raise an error on attempt to commit it.
 *
 * So much hassle to be user-friendly until we have a true
 * interactive transaction support in memtx.
 */
static int
txn_on_yield(struct trigger *trigger, void *event)
{
	(void) trigger;
	(void) event;
	struct txn *txn = in_txn();
	assert(txn != NULL);
	if (txn->status != TXN_ABORTED && !txn_has_flag(txn, TXN_CAN_YIELD)) {
		txn_rollback_to_svp(txn, NULL);
		txn->status = TXN_ABORTED;
		txn_set_flags(txn, TXN_IS_ABORTED_BY_YIELD);
		say_warn("Transaction has been aborted by a fiber yield");
		return 0;
	}
	if (txn->rollback_timer == NULL && txn->timeout != TIMEOUT_INFINITY) {
		size_t size;
		txn->rollback_timer =
			tx_region_alloc_object(txn, TX_OBJECT_EV_TIMER, &size);
		if (txn->rollback_timer == NULL)
			panic("Out of memory on creation of rollback timer");
		ev_timer_init(txn->rollback_timer, txn_on_timeout,
			      txn->timeout, 0);
		txn->rollback_timer->data = txn;
		ev_timer_start(loop(), txn->rollback_timer);
	}
	return 0;
}

struct txn *
txn_detach(void)
{
	struct txn *txn = in_txn();
	if (txn == NULL)
		return NULL;
	txn_on_yield(NULL, NULL);
	trigger_clear(&txn->fiber_on_yield);
	trigger_clear(&txn->fiber_on_stop);
	fiber_set_txn(fiber(), NULL);
	return txn;
}

void
txn_attach(struct txn *txn)
{
	assert(txn != NULL);
	assert(!in_txn());
	fiber_set_txn(fiber(), txn);
	trigger_add(&fiber()->on_yield, &txn->fiber_on_yield);
	trigger_add(&fiber()->on_stop, &txn->fiber_on_stop);
}
