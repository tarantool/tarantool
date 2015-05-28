/*
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
#include "engine.h"
#include "txn.h"
#include "box.h"
#include "tuple.h"
#include "space.h"
#include "main.h"
#include "cluster.h"
#include "recovery.h"
#include <fiber.h>
#include "request.h" /* for request_name */
#include "session.h"
#include "port.h"
#include "iproto_constants.h"

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

void
txn_replace(struct txn *txn, struct space *space,
	    struct tuple *old_tuple, struct tuple *new_tuple,
	    enum dup_replace_mode mode)
{
	assert(old_tuple || new_tuple);

	space->handler->replace(txn, space, old_tuple, new_tuple, mode);

	/*
	 * Run on_replace triggers. For now, disallow mutation
	 * of tuples in the trigger.
	 */
	if (! rlist_empty(&space->on_replace) && space->run_triggers)
		trigger_run(&space->on_replace, txn);
}

/** Initialize a new stmt object within txn. */
static struct txn_stmt *
txn_stmt_new(struct txn *txn)
{
	assert(txn->stmt == 0);
	assert(txn->n_stmts == 0 || !txn->autocommit);
	if (txn->n_stmts++ == 1) {
		txn->stmt = &txn->first_stmt;
	} else {
		txn->stmt = (struct txn_stmt *)
			region_alloc0(&fiber()->gc, sizeof(struct txn_stmt));
	}
	rlist_add_tail_entry(&txn->stmts, txn->stmt, next);
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
	fiber_set_txn(fiber(), txn);
	return txn;
}

struct txn *
txn_begin_stmt(struct request *request, struct space *space)
{
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
	if (space_is_temporary(space) == false)
		txn_add_redo(stmt, request);
	engine->beginStatement(txn);
	return txn;
}

void
txn_commit(struct txn *txn)
{
	assert(txn == in_txn());
	struct txn_stmt *stmt;

	/* Do transaction conflict resolving */
	if (txn->engine)
		txn->engine->prepare(txn);

	rlist_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->row == NULL)
			continue;
		ev_tstamp start = ev_now(loop()), stop;
		int64_t res = wal_write(recovery, stmt->row);
		stop = ev_now(loop());
		if (stop - start > too_long_threshold && stmt->row != NULL) {
			say_warn("too long %s: %.3f sec",
				 iproto_type_name(stmt->row->type),
				 stop - start);
		}
		if (res < 0)
			tnt_raise(LoggedError, ER_WAL_IO);
		txn->signature = res;
	}

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
	txn->stmt->row = NULL;
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
boxffi_txn_begin()
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

void
boxffi_txn_rollback()
{
	txn_rollback(); /* doesn't throw */
}

} /* extern "C" */
