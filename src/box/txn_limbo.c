/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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
#include "txn_limbo.h"
#include "replication.h"

struct txn_limbo txn_limbo;

static inline void
txn_limbo_create(struct txn_limbo *limbo)
{
	rlist_create(&limbo->queue);
	limbo->instance_id = REPLICA_ID_NIL;
	vclock_create(&limbo->vclock);
}

struct txn_limbo_entry *
txn_limbo_append(struct txn_limbo *limbo, uint32_t id, struct txn *txn)
{
	assert(txn_has_flag(txn, TXN_WAIT_SYNC));
	if (id == 0)
		id = instance_id;
	if (limbo->instance_id != id) {
		if (limbo->instance_id == REPLICA_ID_NIL ||
		    rlist_empty(&limbo->queue)) {
			limbo->instance_id = id;
		} else {
			diag_set(ClientError, ER_UNCOMMITTED_FOREIGN_SYNC_TXNS,
				 limbo->instance_id);
			return NULL;
		}
	}
	size_t size;
	struct txn_limbo_entry *e = region_alloc_object(&txn->region,
							typeof(*e), &size);
	if (e == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_object", "e");
		return NULL;
	}
	e->txn = txn;
	e->lsn = -1;
	e->ack_count = 0;
	e->is_commit = false;
	e->is_rollback = false;
	rlist_add_tail_entry(&limbo->queue, e, in_queue);
	return e;
}

static inline void
txn_limbo_remove(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	assert(!rlist_empty(&entry->in_queue));
	assert(txn_limbo_first_entry(limbo) == entry);
	(void) limbo;
	rlist_del_entry(entry, in_queue);
}

static inline void
txn_limbo_pop(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	assert(!rlist_empty(&entry->in_queue));
	assert(txn_limbo_last_entry(limbo) == entry);
	(void) limbo;
	rlist_del_entry(entry, in_queue);
}

void
txn_limbo_abort(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	entry->is_rollback = true;
	/*
	 * The simple rule about rollback/commit order applies
	 * here as well: commit always in the order of WAL write,
	 * rollback in the reversed order. Rolled back transaction
	 * is always the last.
	 */
	txn_limbo_pop(limbo, entry);
}

void
txn_limbo_assign_remote_lsn(struct txn_limbo *limbo,
			    struct txn_limbo_entry *entry, int64_t lsn)
{
	assert(limbo->instance_id != REPLICA_ID_NIL);
	assert(limbo->instance_id != instance_id);
	assert(entry->lsn == -1);
	assert(lsn > 0);
	assert(txn_has_flag(entry->txn, TXN_WAIT_ACK));
	(void) limbo;
	entry->lsn = lsn;
}

void
txn_limbo_assign_local_lsn(struct txn_limbo *limbo,
			   struct txn_limbo_entry *entry, int64_t lsn)
{
	assert(limbo->instance_id != REPLICA_ID_NIL);
	assert(limbo->instance_id == instance_id);
	assert(entry->lsn == -1);
	assert(lsn > 0);
	assert(txn_has_flag(entry->txn, TXN_WAIT_ACK));
	(void) limbo;
	entry->lsn = lsn;
	/*
	 * The entry just got its LSN after a WAL write. It could
	 * happen that this LSN was already ACKed by some
	 * replicas. Update the ACK counter to take them into
	 * account.
	 */
	struct vclock_iterator iter;
	vclock_iterator_init(&iter, &limbo->vclock);
	int ack_count = 0;
	vclock_foreach(&iter, vc)
		ack_count += vc.lsn >= lsn;
	assert(ack_count >= entry->ack_count);
	entry->ack_count = ack_count;
}

void
txn_limbo_wait_complete(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	struct txn *txn = entry->txn;
	assert(entry->lsn > 0 || !txn_has_flag(entry->txn, TXN_WAIT_ACK));
	if (txn_limbo_entry_is_complete(entry))
		goto complete;

	assert(!txn_has_flag(txn, TXN_IS_DONE));
	assert(txn_has_flag(txn, TXN_WAIT_SYNC));
	bool cancellable = fiber_set_cancellable(false);
	while (!txn_limbo_entry_is_complete(entry))
		fiber_yield();
	fiber_set_cancellable(cancellable);
complete:
	// TODO: implement rollback.
	assert(!entry->is_rollback);
	assert(entry->is_commit);
	/*
	 * The entry might be not the first in the limbo. It
	 * happens when there was a sync transaction and async
	 * transaction. The sync and async went to WAL. After sync
	 * WAL write is done, it may be already ACKed by the
	 * needed replica count. Now it marks self as committed
	 * and does the same for the next async txn. Then it
	 * starts writing CONFIRM. During that the async
	 * transaction finishes its WAL write, sees it is
	 * committed and ends up here. Not being the first
	 * transaction in the limbo.
	 */
	while (!rlist_empty(&entry->in_queue) &&
	       txn_limbo_first_entry(limbo) != entry) {
		bool cancellable = fiber_set_cancellable(false);
		fiber_yield();
		fiber_set_cancellable(cancellable);
	}
	if (!rlist_empty(&entry->in_queue))
		txn_limbo_remove(limbo, entry);
	txn_clear_flag(txn, TXN_WAIT_SYNC);
	txn_clear_flag(txn, TXN_WAIT_ACK);
}

/**
 * Write a confirmation entry to WAL. After it's written all the
 * transactions waiting for confirmation may be finished.
 */
static int
txn_limbo_write_confirm(struct txn_limbo *limbo, int64_t lsn)
{
	assert(lsn > 0);

	struct xrow_header row;
	struct request request = {
		.header = &row,
	};

	struct txn *txn = txn_begin();
	if (txn == NULL)
		return -1;

	if (xrow_encode_confirm(&row, &txn->region, limbo->instance_id,
				lsn) < 0)
		goto rollback;
	/*
	 * This is not really a transaction. It just uses txn API
	 * to put the data into WAL. And obviously it should not
	 * go to the limbo and block on the very same sync
	 * transaction which it tries to confirm now.
	 */
	txn_set_flag(txn, TXN_FORCE_ASYNC);

	if (txn_begin_stmt(txn, NULL) != 0)
		goto rollback;
	if (txn_commit_stmt(txn, &request) != 0)
		goto rollback;

	return txn_commit(txn);
rollback:
	txn_rollback(txn);
	return -1;
}

void
txn_limbo_read_confirm(struct txn_limbo *limbo, int64_t lsn)
{
	assert(limbo->instance_id != REPLICA_ID_NIL &&
	       limbo->instance_id != instance_id);
	struct txn_limbo_entry *e, *tmp;
	rlist_foreach_entry_safe(e, &limbo->queue, in_queue, tmp) {
		/*
		 * Confirm a transaction if
		 * - it is a sync transaction covered by the
		 *   confirmation LSN;
		 * - it is an async transaction, and it is the
		 *   last in the queue. So it does not depend on
		 *   a not finished sync transaction anymore and
		 *   can be confirmed too.
		 */
		if (e->lsn > lsn && txn_has_flag(e->txn, TXN_WAIT_ACK))
			break;
		e->is_commit = true;
		txn_limbo_remove(limbo, e);
		txn_clear_flag(e->txn, TXN_WAIT_SYNC);
		txn_clear_flag(e->txn, TXN_WAIT_ACK);
		/*
		 * If  txn_complete_async() was already called,
		 * finish tx processing. Otherwise just clear the
		 * "WAIT_ACK" flag. Tx procesing will finish once
		 * the tx is written to WAL.
		 */
		if (e->txn->signature >= 0)
			txn_complete(e->txn);
	}
}

void
txn_limbo_ack(struct txn_limbo *limbo, uint32_t replica_id, int64_t lsn)
{
	if (rlist_empty(&limbo->queue))
		return;
	assert(limbo->instance_id != REPLICA_ID_NIL);
	int64_t prev_lsn = vclock_get(&limbo->vclock, replica_id);
	vclock_follow(&limbo->vclock, replica_id, lsn);
	struct txn_limbo_entry *e, *last_quorum = NULL;
	int64_t confirm_lsn = -1;
	rlist_foreach_entry(e, &limbo->queue, in_queue) {
		assert(e->ack_count <= VCLOCK_MAX);
		if (e->lsn > lsn)
			break;
		/*
		 * Sync transactions need to collect acks. Async
		 * transactions are automatically committed right
		 * after all the previous sync transactions are.
		 */
		if (!txn_has_flag(e->txn, TXN_WAIT_ACK)) {
			assert(e->lsn == -1);
			if (last_quorum == NULL)
				continue;
		} else if (e->lsn <= prev_lsn) {
			continue;
		} else if (++e->ack_count < replication_synchro_quorum) {
			continue;
		} else {
			confirm_lsn = e->lsn;
		}
		e->is_commit = true;
		last_quorum = e;
	}
	if (last_quorum == NULL)
		return;
	if (txn_limbo_write_confirm(limbo, confirm_lsn) != 0) {
		// TODO: rollback.
		return;
	}
	/*
	 * Wakeup all the entries in direct order as soon
	 * as confirmation message is written to WAL.
	 */
	rlist_foreach_entry(e, &limbo->queue, in_queue) {
		if (e->txn->fiber != fiber())
			fiber_wakeup(e->txn->fiber);
		if (e == last_quorum)
			break;
	}
}

void
txn_limbo_init(void)
{
	txn_limbo_create(&txn_limbo);
}
