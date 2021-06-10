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

double too_long_threshold;

/** Last prepare-sequence-number that was assigned to prepared TX. */
int64_t txn_last_psn = 0;

/* Txn cache. */
static struct stailq txn_cache = {NULL, &txn_cache.first};

static int
txn_on_stop(struct trigger *trigger, void *event);

static int
txn_on_yield(struct trigger *trigger, void *event);

static int
txn_add_redo(struct txn *txn, struct txn_stmt *stmt, struct request *request)
{
	/* Create a redo log row. */
	int size;
	struct xrow_header *row;
	row = region_alloc_object(&txn->region, struct xrow_header, &size);
	if (row == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_object", "row");
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
	row->bodycnt = xrow_encode_dml(request, &txn->region, row->body);
	if (row->bodycnt < 0)
		return -1;
	stmt->row = row;
	return 0;
}

/** Initialize a new stmt object within txn. */
static struct txn_stmt *
txn_stmt_new(struct region *region)
{
	int size;
	struct txn_stmt *stmt;
	stmt = region_alloc_object(region, struct txn_stmt, &size);
	if (stmt == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_object", "stmt");
		return NULL;
	}

	/* Initialize members explicitly to save time on memset() */
	stmt->txn = in_txn();
	stmt->space = NULL;
	stmt->old_tuple = NULL;
	stmt->new_tuple = NULL;
	stmt->add_story = NULL;
	stmt->del_story = NULL;
	stmt->next_in_del_list = NULL;
	stmt->engine_savepoint = NULL;
	stmt->row = NULL;
	stmt->has_triggers = false;
	stmt->does_require_old_tuple = false;
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
	txn->region = region;
	rlist_create(&txn->read_set);
	rlist_create(&txn->point_holes_list);
	rlist_create(&txn->gap_list);
	rlist_create(&txn->conflict_list);
	rlist_create(&txn->conflicted_by_list);
	rlist_create(&txn->in_read_view_txs);
	return txn;
}

/*
 * Free txn memory and return it to a cache.
 */
inline static void
txn_free(struct txn *txn)
{
	memtx_tx_clean_txn(txn);
	struct tx_read_tracker *tracker, *tmp;
	rlist_foreach_entry_safe(tracker, &txn->read_set,
				 in_read_set, tmp) {
		rlist_del(&tracker->in_reader_list);
		rlist_del(&tracker->in_read_set);
	}
	assert(rlist_empty(&txn->read_set));

	struct tx_conflict_tracker *entry, *next;
	rlist_foreach_entry_safe(entry, &txn->conflict_list,
				 in_conflict_list, next) {
		rlist_del(&entry->in_conflict_list);
		rlist_del(&entry->in_conflicted_by_list);
	}
	rlist_foreach_entry_safe(entry, &txn->conflicted_by_list,
				 in_conflicted_by_list, next) {
		rlist_del(&entry->in_conflict_list);
		rlist_del(&entry->in_conflicted_by_list);
	}
	assert(rlist_empty(&txn->conflict_list));
	assert(rlist_empty(&txn->conflicted_by_list));

	rlist_del(&txn->in_read_view_txs);

	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next)
		txn_stmt_destroy(stmt);

	/* Truncate region up to struct txn size. */
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
	assert(rlist_empty(&txn->conflict_list));
	assert(rlist_empty(&txn->conflicted_by_list));

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
	txn->signature = TXN_SIGNATURE_UNKNOWN;
	txn->engine = NULL;
	txn->engine_tx = NULL;
	txn->fk_deferred_count = 0;
	rlist_create(&txn->savepoints);
	txn->fiber = NULL;
	fiber_set_txn(fiber(), txn);
	/* fiber_on_yield is initialized by engine on demand */
	trigger_create(&txn->fiber_on_stop, txn_on_stop, NULL, NULL);
	trigger_add(&fiber()->on_stop, &txn->fiber_on_stop);
	/*
	 * By default all transactions may yield.
	 * It's a responsibility of an engine to disable yields
	 * if they are not supported.
	 */
	txn_set_flags(txn, TXN_CAN_YIELD);
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

	/*
	 * A conflict have happened; there is no reason to continue the TX.
	 */
	if (txn->status == TXN_CONFLICTED) {
		diag_set(ClientError, ER_TRANSACTION_CONFLICT);
		return -1;
	}

	struct txn_stmt *stmt = txn_stmt_new(&txn->region);
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
	 * IRPOTO_NOP or IPROTO_CONFIRM.
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
		if (!rlist_empty(&stmt->space->before_replace)) {
			/*
			 * Triggers see old_tuple and that tuple
			 * must remain the same
			 */
			stmt->does_require_old_tuple = true;
		}
		if (!rlist_empty(&stmt->space->on_replace)) {
			/*
			 * Triggers see old_tuple and that tuple
			 * must remain the same
			 */
			stmt->does_require_old_tuple = true;

			if(trigger_run(&stmt->space->on_replace, txn) != 0)
				goto fail;
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
}

void
txn_complete_success(struct txn *txn)
{
	assert(!txn_has_flag(txn, TXN_IS_DONE));
	assert(!txn_has_flag(txn, TXN_WAIT_SYNC));
	assert(txn->signature >= 0);
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
	if (!txn_has_flag(txn, TXN_WAIT_SYNC))
		txn_complete_success(txn);
	else if (txn->fiber != NULL)
		fiber_wakeup(txn->fiber);
finish:
	fiber_set_txn(fiber(), NULL);
}

static struct journal_entry *
txn_journal_entry_new(struct txn *txn)
{
	struct journal_entry *req;
	struct txn_stmt *stmt;

	assert(txn->n_new_rows + txn->n_applier_rows > 0);

	/* Save space for an additional NOP row just in case. */
	req = journal_entry_new(txn->n_new_rows + txn->n_applier_rows + 1,
				&txn->region, txn_on_journal_write, txn);
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
		*local_row = region_alloc_object(&txn->region,
						 typeof(**local_row), &size);
		if (*local_row == NULL) {
			diag_set(OutOfMemory, size, "region_alloc_object",
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
	txn->psn = ++txn_last_psn;

	if (txn_has_flag(txn, TXN_IS_ABORTED_BY_YIELD)) {
		assert(!txn_has_flag(txn, TXN_CAN_YIELD));
		diag_set(ClientError, ER_TRANSACTION_YIELD);
		diag_log();
		return -1;
	}
	/*
	 * If transaction has been started in SQL, deferred
	 * foreign key constraints must not be violated.
	 * If not so, just rollback transaction.
	 */
	if (txn->fk_deferred_count != 0) {
		diag_set(ClientError, ER_FOREIGN_KEY_CONSTRAINT);
		return -1;
	}

	/*
	 * Somebody else has written some value that we have read.
	 * The RW transaction is not possible.
	 */
	if (txn->status == TXN_CONFLICTED ||
	    (txn->status == TXN_IN_READ_VIEW && !stailq_empty(&txn->stmts))) {
		diag_set(ClientError, ER_TRANSACTION_CONFLICT);
		return -1;
	}

	/*
	 * Perform transaction conflict resolution. Engine == NULL when
	 * we have a bunch of IPROTO_NOP statements.
	 */
	if (txn->engine != NULL) {
		if (engine_prepare(txn->engine, txn) != 0)
			return -1;
	}

	struct tx_conflict_tracker *entry, *next;
	/* Handle conflicts. */
	rlist_foreach_entry_safe(entry, &txn->conflict_list,
				 in_conflict_list, next) {
		assert(entry->breaker == txn);
		memtx_tx_handle_conflict(txn, entry->victim);
		rlist_del(&entry->in_conflict_list);
		rlist_del(&entry->in_conflicted_by_list);
	}
	/* Just free conflict list - we don't need it anymore. */
	rlist_foreach_entry_safe(entry, &txn->conflicted_by_list,
				 in_conflicted_by_list, next) {
		assert(entry->victim == txn);
		rlist_del(&entry->in_conflict_list);
		rlist_del(&entry->in_conflicted_by_list);
	}
	assert(rlist_empty(&txn->conflict_list));
	assert(rlist_empty(&txn->conflicted_by_list));

	trigger_clear(&txn->fiber_on_stop);
	if (!txn_has_flag(txn, TXN_CAN_YIELD))
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

/*
 * A trigger called on tx rollback due to a failed WAL write,
 * when tx is waiting for confirmation.
 */
static int
txn_limbo_on_rollback(struct trigger *trig, void *event)
{
	(void) event;
	struct txn *txn = (struct txn *) event;
	/* Check whether limbo has performed the cleanup. */
	if (!txn_has_flag(txn, TXN_WAIT_SYNC))
		return 0;
	struct txn_limbo_entry *entry = (struct txn_limbo_entry *) trig->data;
	txn_limbo_abort(&txn_limbo, entry);
	return 0;
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
	struct txn_limbo_entry *limbo_entry;
	if (is_sync) {
		/*
		 * We'll need this trigger for sync transactions later,
		 * but allocation failure is inappropriate after the entry
		 * is sent to journal, so allocate early.
		 */
		size_t size;
		struct trigger *trig =
			region_alloc_object(&txn->region, typeof(*trig), &size);
		if (trig == NULL) {
			diag_set(OutOfMemory, size, "region_alloc_object",
				 "trig");
			goto rollback;
		}

		/* See txn_commit(). */
		uint32_t origin_id = req->rows[0]->replica_id;
		limbo_entry = txn_limbo_append(&txn_limbo, origin_id, txn);
		if (limbo_entry == NULL)
			goto rollback;

		if (txn_has_flag(txn, TXN_WAIT_ACK)) {
			int64_t lsn = req->rows[txn->n_applier_rows - 1]->lsn;
			/*
			 * Can't tell whether it is local or not -
			 * async commit is used both by applier
			 * and during recovery. Use general LSN
			 * assignment to let the limbo rule this
			 * out.
			 */
			txn_limbo_assign_lsn(&txn_limbo, limbo_entry, lsn);
		}

		/*
		 * Set a trigger to abort waiting for confirm on
		 * WAL write failure.
		 */
		trigger_create(trig, txn_limbo_on_rollback,
			       limbo_entry, NULL);
		txn_on_rollback(txn, trig);
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
	}

	fiber_set_txn(fiber(), NULL);
	if (journal_write(req) != 0)
		goto rollback_io;
	if (req->res < 0) {
		diag_set_journal_res(req->res);
		goto rollback_io;
	}
	if (txn_has_flag(txn, TXN_WAIT_SYNC)) {
		if (txn_has_flag(txn, TXN_WAIT_ACK)) {
			int64_t lsn = req->rows[req->n_rows - 1]->lsn;
			/*
			 * Use local LSN assignment. Because
			 * blocking commit is used by local
			 * transactions only.
			 */
			txn_limbo_assign_local_lsn(&txn_limbo, limbo_entry,
						   lsn);
			/* Local WAL write is a first 'ACK'. */
			txn_limbo_ack(&txn_limbo, txn_limbo.owner_id, lsn);
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
	if (txn_has_flag(txn, TXN_WAIT_SYNC))
		txn_limbo_abort(&txn_limbo, limbo_entry);
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
	if (!txn_has_flag(txn, TXN_CAN_YIELD))
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
	if (set && !could) {
		txn_set_flags(txn, TXN_CAN_YIELD);
		trigger_clear(&txn->fiber_on_yield);
	} else if (!set && could) {
		txn_clear_flags(txn, TXN_CAN_YIELD);
		trigger_create(&txn->fiber_on_yield, txn_on_yield, NULL, NULL);
		trigger_add(&fiber()->on_yield, &txn->fiber_on_yield);
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
	return region_aligned_alloc(&txn->region, size,
	                            alignof(union natural_align));
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
	svp = (struct txn_savepoint *)region_aligned_alloc(&txn->region, size,
							   alignof(*svp));
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
	assert(!txn_has_flag(txn, TXN_CAN_YIELD));
	txn_rollback_to_svp(txn, NULL);
	txn_set_flags(txn, TXN_IS_ABORTED_BY_YIELD);
	return 0;
}
