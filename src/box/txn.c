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
#include "engine.h"
#include "tuple.h"
#include "journal.h"
#include <fiber.h>
#include "xrow.h"

double too_long_threshold;

static inline void
fiber_set_txn(struct fiber *fiber, struct txn *txn)
{
	fiber->storage.txn = txn;
}

static int
txn_add_redo(struct txn_stmt *stmt, struct request *request)
{
	stmt->row = request->header;
	if (request->header != NULL)
		return 0;

	/* Create a redo log row for Lua requests */
	struct xrow_header *row;
	row = region_alloc_object(&fiber()->gc, struct xrow_header);
	if (row == NULL) {
		diag_set(OutOfMemory, sizeof(*row),
			 "region", "struct xrow_header");
		return -1;
	}
	/* Initialize members explicitly to save time on memset() */
	row->type = request->type;
	row->replica_id = 0;
	row->group_id = stmt->space != NULL ? space_group_id(stmt->space) : 0;
	row->lsn = 0;
	row->sync = 0;
	row->tm = 0;
	row->bodycnt = xrow_encode_dml(request, row->body);
	if (row->bodycnt < 0)
		return -1;
	stmt->row = row;
	return 0;
}

/** Initialize a new stmt object within txn. */
static struct txn_stmt *
txn_stmt_new(struct txn *txn)
{
	struct txn_stmt *stmt;
	stmt = region_alloc_object(&fiber()->gc, struct txn_stmt);
	if (stmt == NULL) {
		diag_set(OutOfMemory, sizeof(*stmt),
			 "region", "struct txn_stmt");
		return NULL;
	}

	/* Initialize members explicitly to save time on memset() */
	stmt->space = NULL;
	stmt->old_tuple = NULL;
	stmt->new_tuple = NULL;
	stmt->engine_savepoint = NULL;
	stmt->row = NULL;

	/* Set the savepoint for statement rollback. */
	txn->sub_stmt_begin[txn->in_sub_stmt] = stailq_last(&txn->stmts);
	txn->in_sub_stmt++;

	stailq_add_tail_entry(&txn->stmts, stmt, next);
	return stmt;
}

static inline void
txn_stmt_unref_tuples(struct txn_stmt *stmt)
{
	if (stmt->old_tuple != NULL)
		tuple_unref(stmt->old_tuple);
	if (stmt->new_tuple != NULL)
		tuple_unref(stmt->new_tuple);
}

static void
txn_rollback_to_svp(struct txn *txn, struct stailq_entry *svp)
{
	struct txn_stmt *stmt;
	struct stailq rollback;
	stailq_cut_tail(&txn->stmts, svp, &rollback);
	stailq_reverse(&rollback);
	stailq_foreach_entry(stmt, &rollback, next) {
		if (txn->engine != NULL && stmt->space != NULL)
			engine_rollback_statement(txn->engine, txn, stmt);
		if (stmt->row != NULL && stmt->row->replica_id == 0) {
			assert(txn->n_local_rows > 0);
			txn->n_local_rows--;
		}
		if (stmt->row != NULL && stmt->row->replica_id != 0) {
			assert(txn->n_remote_rows > 0);
			txn->n_remote_rows--;
		}
		txn_stmt_unref_tuples(stmt);
		stmt->space = NULL;
		stmt->row = NULL;
	}
}

struct txn *
txn_begin(bool is_autocommit)
{
	static int64_t tsn = 0;
	assert(! in_txn());
	struct txn *txn = region_alloc_object(&fiber()->gc, struct txn);
	if (txn == NULL) {
		diag_set(OutOfMemory, sizeof(*txn), "region", "struct txn");
		return NULL;
	}
	/* Initialize members explicitly to save time on memset() */
	stailq_create(&txn->stmts);
	txn->n_local_rows = 0;
	txn->n_remote_rows = 0;
	txn->is_autocommit = is_autocommit;
	txn->has_triggers  = false;
	txn->is_aborted = false;
	txn->in_sub_stmt = 0;
	txn->id = ++tsn;
	txn->signature = -1;
	txn->engine = NULL;
	txn->engine_tx = NULL;
	txn->psql_txn = NULL;
	/* fiber_on_yield/fiber_on_stop initialized by engine on demand */
	fiber_set_txn(fiber(), txn);
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

struct txn *
txn_begin_stmt(struct space *space)
{
	struct txn *txn = in_txn();
	if (txn == NULL) {
		txn = txn_begin(true);
		if (txn == NULL)
			return NULL;
	} else if (txn->in_sub_stmt > TXN_SUB_STMT_MAX) {
		diag_set(ClientError, ER_SUB_STMT_MAX);
		return NULL;
	}

	struct txn_stmt *stmt = txn_stmt_new(txn);
	if (stmt == NULL) {
		if (txn->is_autocommit && txn->in_sub_stmt == 0)
			txn_rollback();
		return NULL;
	}
	if (space == NULL)
		return txn;

	if (trigger_run(&space->on_stmt_begin, txn) != 0)
		goto fail;

	struct engine *engine = space->engine;
	if (txn_begin_in_engine(engine, txn) != 0)
		goto fail;

	stmt->space = space;
	if (engine_begin_statement(engine, txn) != 0)
		goto fail;

	return txn;
fail:
	txn_rollback_stmt();
	return NULL;
}

/**
 * End a statement. In autocommit mode, end
 * the current transaction as well.
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

	/* Create WAL record for the write requests in non-temporary spaces.
	 * stmt->space can be NULL for IRPOTO_NOP.
	 */
	if (stmt->space == NULL || !space_is_temporary(stmt->space)) {
		if (txn_add_redo(stmt, request) != 0)
			goto fail;
		assert(stmt->row != NULL);
		if (stmt->row->replica_id == 0)
			++txn->n_local_rows;
		else
			++txn->n_remote_rows;
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
	if (stmt->space != NULL && !rlist_empty(&stmt->space->on_replace) &&
	    stmt->space->run_triggers && (stmt->old_tuple || stmt->new_tuple)) {
		if (trigger_run(&stmt->space->on_replace, txn) != 0)
			goto fail;
	}
	--txn->in_sub_stmt;
	if (txn->is_autocommit && txn->in_sub_stmt == 0) {
		int rc = txn_commit(txn);
		fiber_gc();
		return rc;
	}
	return 0;
fail:
	txn_rollback_stmt();
	return -1;
}

static int64_t
txn_write_to_wal(struct txn *txn)
{
	assert(txn->n_local_rows + txn->n_remote_rows > 0);

	struct journal_entry *req = journal_entry_new(txn->n_local_rows +
						      txn->n_remote_rows);
	if (req == NULL)
		return -1;

	struct txn_stmt *stmt;
	struct xrow_header **remote_row = req->rows;
	struct xrow_header **local_row = req->rows + txn->n_remote_rows;
	stailq_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->row == NULL)
			continue; /* A read (e.g. select) request */
		if (stmt->row->replica_id == 0)
			*local_row++ = stmt->row;
		else
			*remote_row++ = stmt->row;
		req->approx_len += xrow_approx_len(stmt->row);
	}
	assert(remote_row == req->rows + txn->n_remote_rows);
	assert(local_row == remote_row + txn->n_local_rows);

	ev_tstamp start = ev_monotonic_now(loop());
	int64_t res = journal_write(req);
	ev_tstamp stop = ev_monotonic_now(loop());

	if (res < 0) {
		/* Cascading rollback. */
		txn_rollback(); /* Perform our part of cascading rollback. */
		/*
		 * Move fiber to end of event loop to avoid
		 * execution of any new requests before all
		 * pending rollbacks are processed.
		 */
		fiber_reschedule();
		diag_set(ClientError, ER_WAL_IO);
		diag_log();
	} else if (stop - start > too_long_threshold) {
		int n_rows = txn->n_local_rows + txn->n_remote_rows;
		say_warn_ratelimited("too long WAL write: %d rows at "
				     "LSN %lld: %.3f sec", n_rows,
				     res - n_rows + 1, stop - start);
	}
	/*
	 * Use vclock_sum() from WAL writer as transaction signature.
	 */
	return res;
}

int
txn_commit(struct txn *txn)
{
	assert(txn == in_txn());
	/*
	 * If transaction has been started in SQL, deferred
	 * foreign key constraints must not be violated.
	 * If not so, just rollback transaction.
	 */
	if (txn->psql_txn != NULL) {
		struct sql_txn *sql_txn = txn->psql_txn;
		if (sql_txn->fk_deferred_count != 0) {
			diag_set(ClientError, ER_FOREIGN_KEY_CONSTRAINT);
			goto fail;
		}
	}
	/*
	 * Perform transaction conflict resolution. Engine == NULL when
	 * we have a bunch of IPROTO_NOP statements.
	 */
	if (txn->engine != NULL) {
		if (engine_prepare(txn->engine, txn) != 0)
			goto fail;
	}

	if (txn->n_local_rows + txn->n_remote_rows > 0) {
		txn->signature = txn_write_to_wal(txn);
		if (txn->signature < 0)
			goto fail;
	}
	/*
	 * The transaction is in the binary log. No action below
	 * may throw. In case an error has happened, there is
	 * no other option but terminate.
	 */
	if (txn->has_triggers &&
	    trigger_run(&txn->on_commit, txn) != 0) {
		diag_log();
		unreachable();
		panic("commit trigger failed");
	}
	/*
	 * Engine can be NULL if transaction contains IPROTO_NOP
	 * statements only.
	 */
	if (txn->engine != NULL)
		engine_commit(txn->engine, txn);

	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next)
		txn_stmt_unref_tuples(stmt);

	TRASH(txn);
	fiber_set_txn(fiber(), NULL);
	return 0;
fail:
	txn_rollback();
	return -1;
}

void
txn_rollback_stmt()
{
	struct txn *txn = in_txn();
	if (txn == NULL || txn->in_sub_stmt == 0)
		return;
	txn->in_sub_stmt--;
	if (txn->is_autocommit && txn->in_sub_stmt == 0)
		return txn_rollback();
	txn_rollback_to_svp(txn, txn->sub_stmt_begin[txn->in_sub_stmt]);
}

void
txn_rollback()
{
	struct txn *txn = in_txn();
	if (txn == NULL)
		return;
	/* Rollback triggers must not throw. */
	if (txn->has_triggers &&
	    trigger_run(&txn->on_rollback, txn) != 0) {
		diag_log();
		unreachable();
		panic("rollback trigger failed");
	}
	if (txn->engine)
		engine_rollback(txn->engine, txn);

	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next)
		txn_stmt_unref_tuples(stmt);

	TRASH(txn);
	/** Free volatile txn memory. */
	fiber_gc();
	fiber_set_txn(fiber(), NULL);
}

void
txn_abort(struct txn *txn)
{
	txn_rollback_to_svp(txn, NULL);
	txn->is_aborted = true;
}

int
txn_check_singlestatement(struct txn *txn, const char *where)
{
	if (!txn->is_autocommit || !txn_is_first_statement(txn)) {
		diag_set(ClientError, ER_UNSUPPORTED,
			 where, "multi-statement transactions");
		return -1;
	}
	return 0;
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
box_txn()
{
	return in_txn() != NULL;
}

int
box_txn_begin()
{
	if (in_txn()) {
		diag_set(ClientError, ER_ACTIVE_TRANSACTION);
		return -1;
	}
	if (txn_begin(false) == NULL)
		return -1;
	return 0;
}

int
box_txn_commit()
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
box_txn_rollback()
{
	struct txn *txn = in_txn();
	if (txn && txn->in_sub_stmt) {
		diag_set(ClientError, ER_ROLLBACK_IN_SUB_STMT);
		return -1;
	}
	txn_rollback(); /* doesn't throw */
	return 0;
}

void *
box_txn_alloc(size_t size)
{
	union natural_align {
		void *p;
		double lf;
		long l;
	};
	return region_aligned_alloc(&fiber()->gc, size,
	                            alignof(union natural_align));
}

box_txn_savepoint_t *
box_txn_savepoint()
{
	struct txn *txn = in_txn();
	if (txn == NULL) {
		diag_set(ClientError, ER_SAVEPOINT_NO_TRANSACTION);
		return NULL;
	}
	struct txn_savepoint *svp =
		(struct txn_savepoint *) region_alloc_object(&fiber()->gc,
							struct txn_savepoint);
	if (svp == NULL) {
		diag_set(OutOfMemory, sizeof(*svp),
			 "region", "struct txn_savepoint");
		return NULL;
	}
	svp->stmt = stailq_last(&txn->stmts);
	svp->in_sub_stmt = txn->in_sub_stmt;
	if (txn->psql_txn != NULL)
		svp->fk_deferred_count = txn->psql_txn->fk_deferred_count;
	return svp;
}

int
box_txn_rollback_to_savepoint(box_txn_savepoint_t *svp)
{
	struct txn *txn = in_txn();
	if (txn == NULL) {
		diag_set(ClientError, ER_SAVEPOINT_NO_TRANSACTION);
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
	if (txn->psql_txn != NULL)
		txn->psql_txn->fk_deferred_count = svp->fk_deferred_count;
	return 0;
}

void
txn_on_stop(struct trigger *trigger, void *event)
{
	(void) trigger;
	(void) event;
	txn_rollback();                 /* doesn't yield or fail */
}

