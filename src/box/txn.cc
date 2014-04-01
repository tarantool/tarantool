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
#include <recovery.h>
#include <fiber.h>
#include "request.h" /* for request_name */

double too_long_threshold;

void
txn_add_redo(struct txn *txn, struct request *request)
{
	if (recovery_state->wal_mode == WAL_NONE)
		return;
	if (request->packet == NULL) {
		/* Generate binary body for Lua requests */
		struct iproto_packet *packet = (struct iproto_packet *)
			region_alloc0(&fiber()->gc, sizeof(*packet));
		packet->code = request->code;
		packet->bodycnt = request_encode(request, packet->body);
		txn->packet = packet;
	} else {
		txn->packet = request->packet;
	}
}

void
txn_replace(struct txn *txn, struct space *space,
	    struct tuple *old_tuple, struct tuple *new_tuple,
	    enum dup_replace_mode mode)
{
	assert(old_tuple || new_tuple);
	/*
	 * Remember the old tuple only if we replaced it
	 * successfully, to not remove a tuple inserted by
	 * another transaction in rollback().
	 */
	txn->old_tuple = space_replace(space, old_tuple, new_tuple, mode);
	if (new_tuple) {
		txn->new_tuple = new_tuple;
		tuple_ref(txn->new_tuple, 1);
	}
	txn->space = space;
	/*
	 * Run on_replace triggers. For now, disallow mutation
	 * of tuples in the trigger.
	 */
	if (! rlist_empty(&space->on_replace) && space->run_triggers)
		trigger_run(&space->on_replace, txn);
}

struct txn *
txn_begin()
{
	struct txn *txn = (struct txn *)
		region_alloc0(&fiber()->gc, sizeof(*txn));
	rlist_create(&txn->on_commit);
	rlist_create(&txn->on_rollback);
	return txn;
}

void
txn_commit(struct txn *txn)
{
	if ((txn->old_tuple || txn->new_tuple) &&
	    !space_is_temporary(txn->space)) {
		struct iproto_packet *packet = txn->packet;
		int64_t lsn = next_lsn(recovery_state);

		int res = 0;
		if (recovery_state->wal_mode != WAL_NONE) {
			/* txn_commit() must be done after txn_add_redo() */
			assert(txn->packet != NULL);
			packet->lsn = lsn;
			ev_tstamp start = ev_now(loop()), stop;
			res = wal_write(recovery_state, packet);
			stop = ev_now(loop());

			if (stop - start > too_long_threshold) {
				say_warn("too long %s: %.3f sec",
					 iproto_request_name(packet->code),
					 stop - start);
			}
		}

		confirm_lsn(recovery_state, lsn, res == 0);

		if (res)
			tnt_raise(LoggedError, ER_WAL_IO);
	}
	trigger_run(&txn->on_commit, txn); /* must not throw. */
}

/**
 * txn_finish() follows txn_commit() on success.
 * It's moved to a separate call to be able to send
 * old tuple to the user before it's deleted.
 */
void
txn_finish(struct txn *txn)
{
	if (txn->old_tuple)
		tuple_ref(txn->old_tuple, -1);
	if (txn->space)
		txn->space->engine->factory->txn_finish(txn);
	TRASH(txn);
}

void
txn_rollback(struct txn *txn)
{
	if (txn->old_tuple || txn->new_tuple) {
		space_replace(txn->space, txn->new_tuple,
			      txn->old_tuple, DUP_INSERT);
		trigger_run(&txn->on_rollback, txn); /* must not throw. */
		if (txn->new_tuple)
			tuple_ref(txn->new_tuple, -1);
	}
	TRASH(txn);
}
