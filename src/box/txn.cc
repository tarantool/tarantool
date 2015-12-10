/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box.h" /* global recovery */
#include "tuple.h"
#include "recovery.h"
#include <fiber.h>
#include "request.h" /* for request_name */
#include "xrow.h"

double too_long_threshold;

static inline void
fiber_set_txn(struct fiber *fiber, struct txn *txn)
{
	fiber_set_key(fiber, FIBER_KEY_TXN, (void *) txn);
}

static void
txn_add_redo(struct txn_stmt *stmt, struct request *request)
{
	stmt->row = request->header;
	if (request->header != NULL)
		return;

	/* Create a redo log row for Lua requests */
	struct xrow_header *row =
		region_alloc_object_xc(&fiber()->gc, struct xrow_header);
	/* Initialize members explicitly to save time on memset() */
	row->type = request->type;
	row->server_id = 0;
	row->lsn = 0;
	row->sync = 0;
	row->tm = 0;
	row->bodycnt = request_encode(request, row->body);
	stmt->row = row;
}

/** Initialize a new stmt object within txn. */
static struct txn_stmt *
txn_stmt_new(struct txn *txn)
{
	assert(txn->in_stmt == false);
	assert(stailq_empty(&txn->stmts) || !txn->is_autocommit);
	struct txn_stmt *stmt =
		region_alloc_object_xc(&fiber()->gc, struct txn_stmt);

	/* Initialize members explicitly to save time on memset() */
	stmt->space = NULL;
	stmt->old_tuple = NULL;
	stmt->new_tuple = NULL;
	stmt->row = NULL;

	stailq_add_tail_entry(&txn->stmts, stmt, next);

	txn->in_stmt = true;
	return stmt;
}

struct txn *
txn_begin(bool is_autocommit)
{
	assert(! in_txn());
	struct txn *txn = (struct txn *)
		region_alloc_object_xc(&fiber()->gc, struct txn);
	/* Initialize members explicitly to save time on memset() */
	stailq_create(&txn->stmts);
	txn->n_rows = 0;
	txn->is_autocommit = is_autocommit;
	txn->has_triggers  = false;
	txn->in_stmt = false;
	txn->engine = NULL;
	txn->engine_tx = NULL;
	/* fiber_on_yield/fiber_on_stop initialized by engine on demand */
	fiber_set_txn(fiber(), txn);
	return txn;
}

void
txn_begin_in_engine(struct txn *txn, struct space *space)
{
	Engine *engine = space->handler->engine;
	if (txn->engine == NULL) {
		assert(stailq_empty(&txn->stmts));
		txn->engine = engine;
		engine->begin(txn);
	} else if (txn->engine != engine) {
		/**
		 * Only one engine can be used in
		 * a multi-statement transaction currently.
		 */
		tnt_raise(ClientError, ER_CROSS_ENGINE_TRANSACTION);
	}
}

struct txn *
txn_begin_stmt(struct space *space)
{
	struct txn *txn = in_txn();
	if (txn == NULL)
		txn = txn_begin(true);

	txn_begin_in_engine(txn, space);

	struct txn_stmt *stmt = txn_stmt_new(txn);
	stmt->space = space;
	return txn;
}

/**
 * End a statement. In autocommit mode, end
 * the current transaction as well.
 */
void
txn_commit_stmt(struct txn *txn, struct request *request)
{
	assert(txn->in_stmt);
	/*
	 * Run on_replace triggers. For now, disallow mutation
	 * of tuples in the trigger.
	 */
	struct txn_stmt *stmt = stailq_last_entry(&txn->stmts,
						  struct txn_stmt, next);

	/* Create WAL record for the write requests in non-temporary spaces */
	if (!space_is_temporary(stmt->space)) {
		txn_add_redo(stmt, request);
		++txn->n_rows;
	}
	/*
	 * If there are triggers, and they are not disabled, and
	 * the statement found any rows, run triggers.
	 * XXX:
	 * - sophia doesn't set old/new tuple, so triggers don't
	 *   work for it
	 * - perhaps we should run triggers even for deletes which
	 *   doesn't find any rows
	 */
	if (!rlist_empty(&stmt->space->on_replace) &&
	    stmt->space->run_triggers && (stmt->old_tuple || stmt->new_tuple)) {

		trigger_run(&stmt->space->on_replace, txn);
	}
	txn->in_stmt = false;
	if (txn->is_autocommit)
		txn_commit(txn);
}


static int64_t
txn_write_to_wal(struct txn *txn)
{
	assert(txn->n_rows > 0);

	struct wal_request *req;
	req = (struct wal_request *)region_aligned_alloc_xc(
		&fiber()->gc,
		sizeof(struct wal_request) +
		         sizeof(req->rows[0]) * txn->n_rows,
		alignof(struct wal_request));
	/*
	 * Note: offsetof(struct wal_request, rows) is more appropriate,
	 * but compiler warns.
	 */
	req->n_rows = 0;

	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->row == NULL)
			continue; /* A read (e.g. select) request */
		/*
		 * Bump current LSN even if wal_mode = NONE, so that
		 * snapshots still works with WAL turned off.
		 */
		recovery_fill_lsn(recovery, stmt->row);
		stmt->row->tm = ev_now(loop());
		req->rows[req->n_rows++] = stmt->row;
	}
	assert(req->n_rows == txn->n_rows);

	ev_tstamp start = ev_now(loop()), stop;
	int64_t res;
	if (wal == NULL) {
		/** wal_mode = NONE or initial recovery. */
		res = vclock_sum(&recovery->vclock);
	} else {
		res = wal_write(wal, req);
	}

	stop = ev_now(loop());
	if (stop - start > too_long_threshold)
		say_warn("too long WAL write: %.3f sec", stop - start);
	if (res < 0)
		tnt_raise(LoggedError, ER_WAL_IO);
	/*
	 * Use vclock_sum() from WAL writer as transaction signature.
	 */
	return res;
}

void
txn_commit(struct txn *txn)
{
	assert(txn == in_txn());

	assert(stailq_empty(&txn->stmts) || txn->engine);

	/* Do transaction conflict resolving */
	if (txn->engine) {
		int64_t signature = -1;
		txn->engine->prepare(txn);

		if (txn->n_rows > 0)
			signature = txn_write_to_wal(txn);
		/*
		 * The transaction is in the binary log. No action below
		 * may throw. In case an error has happened, there is
		 * no other option but terminate.
		 */
		if (txn->has_triggers)
			trigger_run(&txn->on_commit, txn);

		txn->engine->commit(txn, signature);
	}
	TRASH(txn);
	/** Free volatile txn memory. */
	fiber_gc();
	fiber_set_txn(fiber(), NULL);
}

/**
 * Void all effects of the statement, but
 * keep it in the list - to maintain
 * limit on the number of statements in a
 * transaction.
 */
void
txn_rollback_stmt()
{
	struct txn *txn = in_txn();
	if (txn == NULL)
		return;
	if (txn->is_autocommit)
		return txn_rollback();
	if (txn->in_stmt == false)
		return;
	struct txn_stmt *stmt = stailq_last_entry(&txn->stmts, struct txn_stmt,
						  next);
	txn->engine->rollbackStatement(stmt);
	if (stmt->row != NULL) {
		stmt->row = NULL;
		--txn->n_rows;
		assert(txn->n_rows >= 0);
	}
	txn->in_stmt = false;
}

void
txn_rollback()
{
	struct txn *txn = in_txn();
	if (txn == NULL)
		return;
	if (txn->has_triggers)
		trigger_run(&txn->on_rollback, txn); /* must not throw. */
	if (txn->engine)
		txn->engine->rollback(txn);
	TRASH(txn);
	/** Free volatile txn memory. */
	fiber_gc();
	fiber_set_txn(fiber(), NULL);
}

void
txn_check_autocommit(struct txn *txn, const char *where)
{
	if (txn->is_autocommit == false) {
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  where, "multi-statement transactions");
	}
}

extern "C" {

bool
box_txn()
{
	return in_txn() != NULL;
}

int
box_txn_begin()
{
	try {
		if (in_txn())
			tnt_raise(ClientError, ER_ACTIVE_TRANSACTION);
		(void) txn_begin(false);
	} catch (Exception  *e) {
		return -1; /* pass exception  through FFI */
	}
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
	try {
		txn_commit(txn);
	} catch (Exception *e) {
		txn_rollback();
		return -1;
	}
	return 0;
}

void
box_txn_rollback()
{
	txn_rollback(); /* doesn't throw */
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

} /* extern "C" */
