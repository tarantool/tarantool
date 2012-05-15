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
#include "request.h"
#include "tuple.h"
#include "space.h"
#include <log_io.h>

void
txn_lock_tuple(struct txn *txn, struct tuple *tuple)
{
	if (tuple->flags & WAL_WAIT)
		tnt_raise(ClientError, :ER_TUPLE_IS_RO);

	say_debug("txn_lock_tuple(%p)", tuple);
	txn->lock_tuple = tuple;
	tuple->flags |= WAL_WAIT;
}

static void
unlock_tuples(struct txn *txn)
{
	if (txn->lock_tuple) {
		txn->lock_tuple->flags &= ~WAL_WAIT;
		txn->lock_tuple = NULL;
	}
}

static void
commit_replace(struct txn *txn)
{
	if (txn->old_tuple != NULL) {
		space_replace(txn->space, txn->old_tuple, txn->new_tuple);
		tuple_ref(txn->old_tuple, -1);
	}

	if (txn->new_tuple != NULL) {
		txn->new_tuple->flags &= ~GHOST;
		tuple_ref(txn->new_tuple, +1);
	}
}

static void
rollback_replace(struct txn *txn)
{
	say_debug("rollback_replace: txn->new_tuple:%p", txn->new_tuple);

	if (txn->new_tuple && txn->new_tuple->flags & GHOST)
		space_remove(txn->space, txn->new_tuple);
}

static void
commit_delete(struct txn *txn)
{
	if (txn->old_tuple) {
		space_remove(txn->space, txn->old_tuple);
		tuple_ref(txn->old_tuple, -1);
	}
}

struct txn *
txn_begin()
{
	struct txn *txn = p0alloc(fiber->gc_pool, sizeof(*txn));
	assert(fiber->mod_data.txn == NULL);
	fiber->mod_data.txn = txn;
	return txn;
}

void
txn_commit(struct txn *txn)
{
	assert(txn == in_txn());
	assert(txn->type);

	if (!request_is_select(txn->type)) {
		say_debug("box_commit(op:%s)", requests_strs[txn->type]);

		if (txn->flags & BOX_NOT_STORE)
			;
		else {
			fiber_peer_name(fiber); /* fill the cookie */

			i64 lsn = next_lsn(recovery_state, 0);
			int res = wal_write(recovery_state, wal_tag,
					    txn->type,
					    fiber->cookie, lsn, &txn->req);
			confirm_lsn(recovery_state, lsn);
			if (res)
				tnt_raise(LoggedError, :ER_WAL_IO);
		}

		unlock_tuples(txn);

		if (txn->type == DELETE_1_3 || txn->type == DELETE)
			commit_delete(txn);
		else
			commit_replace(txn);
	}
	/*
	 * If anything above throws, we must be able to
	 * roll back. Thus clear mod_data.txn only when
	 * we know for sure the commit has succeeded.
	 */
	fiber->mod_data.txn = 0;
	TRASH(txn);
}

void
txn_rollback(struct txn *txn)
{
	assert(txn == in_txn());
	fiber->mod_data.txn = 0;
	if (txn->type == 0)
		return;

	if (!request_is_select(txn->type)) {
		say_debug("txn_rollback(request type: %s)",
			  requests_strs[txn->type]);

		unlock_tuples(txn);

		if (txn->type == REPLACE)
			rollback_replace(txn);

		if (txn->new_tuple)
			tuple_free(txn->new_tuple);
	}
	TRASH(txn);
}

