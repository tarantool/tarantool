#ifndef TARANTOOL_BOX_TXN_H_INCLUDED
#define TARANTOOL_BOX_TXN_H_INCLUDED
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

#include <stdbool.h>

#if defined(__cplusplus)

#include "space.h"
#include "trigger.h"
#include "fiber.h"
#include "salad/stailq.h"

extern double too_long_threshold;
struct tuple;

/**
 * A single statement of a multi-statement
 * transaction: undo and redo info.
 */
struct txn_stmt {
	/* (!) Please update txn_stmt_new() after changing members */

	/** A linked list of all statements. */
	struct stailq_entry next;
	/** Undo info. */
	struct space *space;
	struct tuple *old_tuple;
	struct tuple *new_tuple;
	/** Redo info: the binary log row */
	struct xrow_header *row;
};

struct txn {
	/** List of statements in a transaction. */
	struct stailq stmts;
	/** Total number of WAL rows in this txn. */
	int n_rows;
	/**
	 * True if this transaction is running in autocommit mode
	 * (statement end causes an automatic transaction commit).
	 */
	bool is_autocommit;
	/** True if on_commit and on_rollback lists are non-empty. */
	bool has_triggers;
	/** A statement-level transaction is active. */
	bool in_stmt;
	/** Engine involved in multi-statement transaction. */
	Engine *engine;
	/** Engine-specific transaction data */
	void *engine_tx;
	/**
	 * Triggers on fiber yield and stop to abort transaction
	 * for in-memory engine.
	 */
	struct trigger fiber_on_yield, fiber_on_stop;
	 /** Commit and rollback triggers */
	struct rlist on_commit, on_rollback;
};

/* Pointer to the current transaction (if any) */
static inline struct txn *
in_txn()
{
	return (struct txn *) fiber_get_key(fiber(), FIBER_KEY_TXN);
}

/**
 * Start a transaction explicitly.
 * @pre no transaction is active
 */
struct txn *
txn_begin(bool is_autocommit);

/**
 * Commit a transaction.
 * @pre txn == in_txn()
 */
void
txn_commit(struct txn *txn);

/** Rollback a transaction, if any. */
void
txn_rollback();

/**
 * Most txns don't have triggers, and txn objects
 * are created on every access to data, so txns
 * are partially initialized.
 */
static inline void
txn_init_triggers(struct txn *txn)
{
	if (txn->has_triggers == false) {
		rlist_create(&txn->on_commit);
		rlist_create(&txn->on_rollback);
		txn->has_triggers = true;
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

/**
 * Start a new statement. If no current transaction,
 * start a new transaction with autocommit = true.
 */
struct txn *
txn_begin_stmt(struct space *space);

void
txn_begin_in_engine(struct txn *txn, struct space *space);

/**
 * This is an optimization, which exists to speed up selects
 * in autocommit mode. For such selects, we only need to
 * manage fiber garbage heap. If autocommit mode is
 * off, however, we must start engine transaction with the first
 * select.
 */
static inline struct txn *
txn_begin_ro_stmt(struct space *space)
{
	struct txn *txn = in_txn();
	if (txn)
		txn_begin_in_engine(txn, space);
	return txn;
}

static inline void
txn_commit_ro_stmt(struct txn *txn)
{
	assert(txn == in_txn());
	if (txn) {
		assert(txn->engine);
		/* nothing to do */
	} else {
		fiber_gc();
	}
}

/**
 * End a statement. In autocommit mode, end
 * the current transaction as well.
 */
void
txn_commit_stmt(struct txn *txn, struct request *request);

/**
 * Rollback a statement. In autocommit mode,
 * rolls back the entire transaction.
 */
void
txn_rollback_stmt();

/**
 * Raise an error if this is a multi-statement
 * transaction: DDL can not be part of a multi-statement
 * transaction and must be run in autocommit mode.
 */
void
txn_check_autocommit(struct txn *txn, const char *where);

/** The current statement of the transaction. */
static inline struct txn_stmt *
txn_current_stmt(struct txn *txn)
{
	return (txn->in_stmt ?
		stailq_last_entry(&txn->stmts, struct txn_stmt, next) :
		NULL);
}

/** The last statement of the transaction. */
static inline struct txn_stmt *
txn_last_stmt(struct txn *txn)
{
	return stailq_last_entry(&txn->stmts, struct txn_stmt, next);
}

#endif /* defined(__cplusplus) */

/**
 * FFI bindings: do not throw exceptions, do not accept extra
 * arguments
 */

/** \cond public */

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
 */
API_EXPORT void
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

/** \endcond public */

#endif /* TARANTOOL_BOX_TXN_H_INCLUDED */
