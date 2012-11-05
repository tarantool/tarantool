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
#include <recovery.h>
#include <fiber.h>
#include "archive.h"

static void
txn_lock(struct txn *txn __attribute__((unused)), struct tuple *tuple)
{
	say_debug("txn_lock(%p)", tuple);
	tuple->flags |= WAL_WAIT;
}

static void
txn_unlock(struct txn *txn __attribute__((unused)), struct tuple *tuple)
{
	assert(tuple->flags & WAL_WAIT);
	tuple->flags &= ~WAL_WAIT;
}

void
txn_add_redo(struct txn *txn, u16 op, struct tbuf *data)
{
	txn->op = op;
	/* Copy tbuf by-value, it will be changed by parsing. */
	txn->req = (struct tbuf) {
			.data = data->data,
			.size = data->size,
			.capacity = data->size,
			.pool = NULL };
}

void
txn_add_undo(struct txn *txn, struct space *space,
	     struct tuple *old_tuple, struct tuple *new_tuple)
{
	/* txn_add_undo() must be done after txn_add_redo() */
	assert(txn->op != 0);
	txn->new_tuple = new_tuple;
	if (new_tuple == NULL && old_tuple == NULL) {
		/*
		 * There is no subject tuple we could write to
		 * WAL, which means, to do a write, we would have
		 * to allocate one. Too complicated, for now, just
		 * do no logging for DELETEs that do nothing.
		*/
		txn->txn_flags |= BOX_NOT_STORE;
	} else if (new_tuple == NULL) {
		space_remove(space, old_tuple);
	} else {
		space_replace(space, old_tuple, new_tuple);
		txn_lock(txn, new_tuple);
	}
	/* Remember the old tuple only if we locked it
	 * successfully, to not unlock a tuple locked by another
	 * transaction in rollback().
	 */
	txn->old_tuple = old_tuple;
	txn->space = space;
}

struct txn *
txn_begin()
{
	struct txn *txn = p0alloc(fiber->gc_pool, sizeof(*txn));
	return txn;
}

void
txn_commit(struct txn *txn)
{
	if (txn->op == 0) /* Nothing to do. */
		return;
	if (! (txn->txn_flags & BOX_NOT_STORE)) {
		int64_t lsn = next_lsn(recovery_state);
		int res = wal_write(recovery_state, lsn, 0,
				    txn->op, &txn->req);
        arc_do_txn(txn);
		confirm_lsn(recovery_state, lsn, res == 0);
		if (res)
			tnt_raise(LoggedError, :ER_WAL_IO);
	}
	if (txn->old_tuple)
		tuple_ref(txn->old_tuple, -1);
	if (txn->new_tuple)
		txn_unlock(txn, txn->new_tuple);
	TRASH(txn);
}

void
txn_rollback(struct txn *txn)
{
	if (txn->op == 0) /* Nothing to do. */
		return;
	if (txn->old_tuple) {
		space_replace(txn->space, txn->new_tuple, txn->old_tuple);
	} else if (txn->new_tuple && txn->new_tuple->flags & WAL_WAIT) {
		space_remove(txn->space, txn->new_tuple);
	}
	if (txn->new_tuple)
		tuple_ref(txn->new_tuple, -1);
	TRASH(txn);
}
