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
#include "txn.h"
#include "tuple.h"
#include "space.h"
#include <tarantool.h>
#include "cluster.h"
#include "recovery.h"
#include <fiber.h>
#include "request.h" /* for request_name */
#include "session.h"
#include "port.h"

double too_long_threshold;

static void
txn_add_redo(struct txn_stmt *stmt, struct request *request)
{
	stmt->row = request->header;
	if (recovery->wal_mode == WAL_NONE || request->header != NULL)
		return;

	/* Create a redo log row for Lua requests */
	struct iproto_header *row= (struct iproto_header *)
		region_alloc0(&fiber()->gc, sizeof(struct iproto_header));
	row->type = request->type;
	row->bodycnt = request_encode(request, row->body);
	stmt->row = row;
}

static void
txn_on_yield_or_stop(struct trigger * /* trigger */, void * /* event */)
{
	txn_rollback(); /* doesn't throw */
}

void
txn_replace(struct txn *txn, struct space *space,
	    struct tuple *old_tuple, struct tuple *new_tuple,
	    enum dup_replace_mode mode)
{
	struct txn_stmt *stmt;
	stmt = txn_stmt(txn);
	assert(old_tuple || new_tuple);
	/*
	 * Remember the old tuple only if we replaced it
	 * successfully, to not remove a tuple inserted by
	 * another transaction in rollback().
	 */
	stmt->old_tuple = space_replace(space, old_tuple, new_tuple, mode);
	if (new_tuple) {
		stmt->new_tuple = new_tuple;
		tuple_ref(stmt->new_tuple, 1);
	}
	stmt->space = space;
	/**
	 * Various checks of the used storage engine:
	 * - check if it supports transactions
	 * - only one engine can be used in a multi-statement
	 *   transaction
	 * - memtx doesn't allow yields between statements of
	 *   a transaction. For this engine, set a trigger which
	 *   would roll back the transaction if there is a yield.
	 */
	if (txn->autocommit == false) {
		if (txn->n_stmts == 1) {
			txn->engine = engine_id(space->engine);
			if (engine_no_yield(txn->engine)) {
				trigger_add(&fiber()->on_yield,
					    &txn->fiber_on_yield);
				trigger_add(&fiber()->on_stop,
					    &txn->fiber_on_stop);
			}
		} else if (txn->engine != engine_id(space->engine))
			tnt_raise(ClientError, ER_CROSS_ENGINE_TRANSACTION);
		if (! engine_transactional(txn->engine)) {
			tnt_raise(ClientError, ER_UNSUPPORTED,
				  space->def.engine_name, "transactions");
		}
	}
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
	assert(txn->n_stmts == 0 || !txn->autocommit);
	struct txn_stmt *stmt;
	if (txn->n_stmts++ == 1) {
		stmt = &txn->stmt;
	} else {
		stmt = (struct txn_stmt *)
			region_alloc0(&fiber()->gc, sizeof(struct txn_stmt));
	}
	rlist_add_tail_entry(&txn->stmts, stmt, next);
	return stmt;
}

struct txn *
txn_begin(bool autocommit)
{
	assert(! fiber_get_txn(fiber()));
	struct txn *txn = (struct txn *)
		region_alloc0(&fiber()->gc, sizeof(*txn));
	rlist_create(&txn->stmts);
	rlist_create(&txn->on_commit);
	rlist_create(&txn->on_rollback);
	txn->fiber_on_yield = {
		rlist_nil, txn_on_yield_or_stop, NULL, NULL
	};
	txn->fiber_on_stop = {
		rlist_nil, txn_on_yield_or_stop, NULL, NULL
	};

	txn->autocommit = autocommit;
	fiber_set_txn(fiber(), txn);
	return txn;
}

struct txn *
txn_begin_stmt(struct request *request)
{
	struct txn *txn = fiber_get_txn(fiber());
	if (txn == NULL)
		txn = txn_begin(true);
	struct txn_stmt *stmt = txn_stmt_new(txn);
	txn_add_redo(stmt, request);
	return txn;
}

void
txn_commit_stmt(struct txn *txn, struct port *port)
{
	struct txn_stmt *stmt;
	struct tuple *tuple;
	stmt = txn_stmt(txn);
	if (txn->autocommit)
		txn_commit(txn);
	/* Adding result to port must be after possible WAL write.
	 * The reason is that any yield between port_add_tuple and port_eof
	 * calls could lead to sending not finished response to iproto socket.
	 */
	if ((tuple = stmt->new_tuple) || (tuple = stmt->old_tuple))
		port_add_tuple(port, tuple);
	if (txn->autocommit)
		txn_finish(txn);
}

void
txn_commit(struct txn *txn)
{
	assert(txn == fiber_get_txn(fiber()));
	struct txn_stmt *stmt;
	/* if (!txn->autocommit && txn->n_stmts && engine_no_yield(txn->engine)) */
		trigger_clear(&txn->fiber_on_yield);
		trigger_clear(&txn->fiber_on_stop);
	rlist_foreach_entry(stmt, &txn->stmts, next) {
		if ((!stmt->old_tuple && !stmt->new_tuple) ||
		    space_is_temporary(stmt->space))
			continue;

		int res = 0;
		/* txn_commit() must be done after txn_add_redo() */
		assert(recovery->wal_mode == WAL_NONE ||
		       stmt->row != NULL);
		ev_tstamp start = ev_now(loop()), stop;
		res = wal_write(recovery, stmt->row);
		stop = ev_now(loop());

		if (stop - start > too_long_threshold && stmt->row != NULL) {
			say_warn("too long %s: %.3f sec",
				 iproto_type_name(stmt->row->type),
				 stop - start);
		}

		if (res)
			tnt_raise(LoggedError, ER_WAL_IO);
	}
	trigger_run(&txn->on_commit, txn); /* must not throw. */
}

void
txn_finish(struct txn *txn)
{
	struct txn_stmt *stmt;
	rlist_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->old_tuple)
			tuple_ref(stmt->old_tuple, -1);
		if (stmt->space)
			stmt->space->engine->factory->txnFinish(txn);
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
 * trasnaction.
 */
void
txn_rollback_stmt()
{
	struct txn *txn = fiber_get_txn(fiber());
	if (txn == NULL)
		return;
	if (txn->autocommit)
		return txn_rollback();
	struct txn_stmt *stmt = txn_stmt(txn);
	if (stmt->old_tuple || stmt->new_tuple) {
		space_replace(stmt->space, stmt->new_tuple,
			      stmt->old_tuple, DUP_INSERT);
		if (stmt->new_tuple)
			tuple_ref(stmt->new_tuple, -1);
	}
	stmt->old_tuple = stmt->new_tuple = NULL;
	stmt->space = NULL;
	stmt->row = NULL;
}

void
txn_rollback()
{
	struct txn *txn = fiber_get_txn(fiber());
	if (txn == NULL)
		return;
	struct txn_stmt *stmt;
	rlist_foreach_entry_reverse(stmt, &txn->stmts, next) {
		if (stmt->old_tuple || stmt->new_tuple) {
			space_replace(stmt->space, stmt->new_tuple,
				      stmt->old_tuple, DUP_INSERT);
		}
	}
	trigger_run(&txn->on_rollback, txn); /* must not throw. */
	rlist_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->new_tuple)
			tuple_ref(stmt->new_tuple, -1);
	}
	/* if (!txn->autocommit && txn->n_stmts && engine_no_yield(txn->engine)) */
		trigger_clear(&txn->fiber_on_yield);
		trigger_clear(&txn->fiber_on_stop);
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
		if (fiber_get_txn(fiber()))
			tnt_raise(ClientError, ER_ACTIVE_TRANSACTION);
		(void) txn_begin(false);
	} catch (Exception  *e) {
		return -1; /* pass exception  through FFI */
	}
	return 0;
}

int
boxffi_txn_commit()
{
	try {
		struct txn *txn = fiber_get_txn(fiber());
		/**
		 * COMMIT is like BEGIN or ROLLBACK
		 * a "transaction-initiating statement".
		 * Do nothing if transaction is not started,
		 * it's the same as BEGIN + COMMIT.
		 */
		if (txn) {
			txn_commit(txn);
			txn_finish(txn);
		}
	} catch (Exception  *e) {
		return -1; /* pass exception through FFI */
	}
	return 0;
}

void
boxffi_txn_rollback()
{
	txn_rollback(); /* doesn't throw */
}

} /* extern "C" */
