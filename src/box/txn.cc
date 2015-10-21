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
	struct xrow_header *row= (struct xrow_header *)
		region_alloc0(&fiber()->gc, sizeof(struct xrow_header));
	row->type = request->type;
	row->bodycnt = request_encode(request, row->body);
	stmt->row = row;
}

/** Initialize a new stmt object within txn. */
static struct txn_stmt *
txn_stmt_new(struct txn *txn)
{
	assert(txn->stmt == 0);
	assert(txn->n_stmts == 0 || !txn->autocommit);
	if (txn->n_stmts == 0) {
		txn->stmt = &txn->first_stmt;
	} else {
		txn->stmt = (struct txn_stmt *)
			region_alloc0(&fiber()->gc, sizeof(struct txn_stmt));
	}
	rlist_add_tail_entry(&txn->stmts, txn->stmt, next);
	++txn->n_stmts;
	return txn->stmt;
}

struct txn *
txn_begin(bool autocommit)
{
	assert(! in_txn());
	struct txn *txn = (struct txn *)
		region_alloc0(&fiber()->gc, sizeof(*txn));
	rlist_create(&txn->stmts);
	rlist_create(&txn->on_commit);
	rlist_create(&txn->on_rollback);
	txn->autocommit = autocommit;
	txn->signature = -1;
	fiber_set_txn(fiber(), txn);
	return txn;
}

struct txn *
txn_begin_stmt(struct request *request, struct space *space)
{
	/* NOTE: request is NULL for the read requests (select, get, etc.) */
	struct txn *txn = in_txn();
	if (txn == NULL)
		txn = txn_begin(true);

	Engine *engine = space->handler->engine;
	if (txn->engine == NULL) {
		assert(txn->n_stmts == 0);
		txn->engine = engine;
	} else if (txn->engine != engine) {
		/**
		 * Only one engine can be used in
		 * a multi-statement transaction currently.
		 */
		tnt_raise(ClientError, ER_CROSS_ENGINE_TRANSACTION);
	}
	struct txn_stmt *stmt = txn_stmt_new(txn);
	/* Create WAL record for the write requests in non-temporary spaces */
	if (!space_is_temporary(space) && request != NULL) {
		txn_add_redo(stmt, request);
		++txn->n_rows;
	}

	engine->beginStatement(txn);
	return txn;
}

static void
txn_write_to_wal(struct txn *txn)
{
	assert(txn->n_rows > 0);
	struct wal_request *req = (struct wal_request *)
		region_alloc(&fiber()->gc, sizeof(struct wal_request) +
			     sizeof(struct xrow_header) * txn->n_rows);
	req->n_rows = 0;

	struct txn_stmt *stmt;
	rlist_foreach_entry(stmt, &txn->stmts, next) {
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
	int64_t res = wal_write(recovery, req);
	stop = ev_now(loop());
	if (stop - start > too_long_threshold)
		say_warn("too long WAL write: %.3f sec", stop - start);
	if (res < 0)
		tnt_raise(LoggedError, ER_WAL_IO);

	/*
	 * Use vclock_sum() from WAL writer as transaction signature.
	 */
	txn->signature = res;
}

void
txn_commit(struct txn *txn)
{
	assert(txn == in_txn());

	/* Do transaction conflict resolving */
	if (txn->engine)
		txn->engine->prepare(txn);

	if (txn->n_rows > 0)
		txn_write_to_wal(txn);

	/*
	 * The transaction is in the binary log. No action below
	 * may throw. In case an error has happened, there is
	 * no other option but terminate.
	 */
	trigger_run(&txn->on_commit, txn);
	if (txn->engine)
		txn->engine->commit(txn);
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
	if (txn->autocommit)
		return txn_rollback();
	if (txn->stmt == NULL)
		return;
	txn->engine->rollbackStatement(txn->stmt);
	if (txn->stmt->row != NULL) {
		txn->stmt->row = NULL;
		--txn->n_rows;
		assert(txn->n_rows >= 0);
	}
	txn->stmt = NULL;
}

void
txn_rollback()
{
	struct txn *txn = in_txn();
	if (txn == NULL)
		return;
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
	if (txn->autocommit == false) {
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  where, "multi-statement transactions");
	}
}

extern "C" {

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
	} catch (...) {
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
	return region_alloc_nothrow(&fiber()->gc, size);
}

} /* extern "C" */
