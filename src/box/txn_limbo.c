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

static int
txn_limbo_write_rollback(struct txn_limbo *limbo, int64_t lsn);

int
txn_limbo_wait_complete(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	struct txn *txn = entry->txn;
	assert(entry->lsn > 0 || !txn_has_flag(entry->txn, TXN_WAIT_ACK));
	if (txn_limbo_entry_is_complete(entry))
		goto complete;

	assert(!txn_has_flag(txn, TXN_IS_DONE));
	assert(txn_has_flag(txn, TXN_WAIT_SYNC));
	bool cancellable = fiber_set_cancellable(false);
	bool timed_out = fiber_yield_timeout(txn_limbo_confirm_timeout(limbo));
	fiber_set_cancellable(cancellable);
	if (timed_out) {
		assert(!txn_limbo_is_empty(limbo));
		if (txn_limbo_first_entry(limbo) != entry) {
			/*
			 * If this is not a first entry in the
			 * limbo, it is definitely not a first
			 * timed out entry. And since it managed
			 * to time out too, it means there is
			 * currently another fiber writing
			 * rollback. Wait when it will finish and
			 * wake us up.
			 */
			bool cancellable = fiber_set_cancellable(false);
			fiber_yield();
			fiber_set_cancellable(cancellable);
			assert(txn_limbo_entry_is_complete(entry));
			goto complete;
		}

		txn_limbo_write_rollback(limbo, entry->lsn);
		struct txn_limbo_entry *e, *tmp;
		rlist_foreach_entry_safe_reverse(e, &limbo->queue,
						 in_queue, tmp) {
			e->is_rollback = true;
			e->txn->signature = TXN_SIGNATURE_QUORUM_TIMEOUT;
			txn_limbo_pop(limbo, e);
			txn_clear_flag(e->txn, TXN_WAIT_SYNC);
			txn_clear_flag(e->txn, TXN_WAIT_ACK);
			txn_complete(e->txn);
			if (e == entry)
				break;
			fiber_wakeup(e->txn->fiber);
		}
		diag_set(ClientError, ER_SYNC_QUORUM_TIMEOUT);
		return -1;
	}
complete:
	assert(txn_limbo_entry_is_complete(entry));
	/*
	 * The first tx to be rolled back already performed all
	 * the necessary cleanups for us.
	 */
	if (entry->is_rollback) {
		diag_set(ClientError, ER_SYNC_ROLLBACK);
		return -1;
	}
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
	return 0;
}

static int
txn_limbo_write_confirm_rollback(struct txn_limbo *limbo, int64_t lsn,
				 bool is_confirm)
{
	assert(lsn > 0);

	struct xrow_header row;
	struct request request = {
		.header = &row,
	};

	struct txn *txn = txn_begin();
	if (txn == NULL)
		return -1;

	int res = 0;
	if (is_confirm) {
		res = xrow_encode_confirm(&row, &txn->region,
					  limbo->instance_id, lsn);
	} else {
		/*
		 * This LSN is the first to be rolled back, so
		 * the last "safe" lsn is lsn - 1.
		 */
		res = xrow_encode_rollback(&row, &txn->region,
					   limbo->instance_id, lsn);
	}
	if (res == -1)
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

/**
 * Write a confirmation entry to WAL. After it's written all the
 * transactions waiting for confirmation may be finished.
 */
static int
txn_limbo_write_confirm(struct txn_limbo *limbo, int64_t lsn)
{
	return txn_limbo_write_confirm_rollback(limbo, lsn, true);
}

void
txn_limbo_read_confirm(struct txn_limbo *limbo, int64_t lsn)
{
	assert(limbo->instance_id != REPLICA_ID_NIL);
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

/**
 * Write a rollback message to WAL. After it's written all the
 * transactions following the current one and waiting for
 * confirmation must be rolled back.
 */
static int
txn_limbo_write_rollback(struct txn_limbo *limbo, int64_t lsn)
{
	return txn_limbo_write_confirm_rollback(limbo, lsn, false);
}

void
txn_limbo_read_rollback(struct txn_limbo *limbo, int64_t lsn)
{
	assert(limbo->instance_id != REPLICA_ID_NIL);
	struct txn_limbo_entry *e, *tmp;
	struct txn_limbo_entry *last_rollback = NULL;
	rlist_foreach_entry_reverse(e, &limbo->queue, in_queue) {
		if (!txn_has_flag(e->txn, TXN_WAIT_ACK))
			continue;
		if (e->lsn < lsn)
			break;
		last_rollback = e;
	}
	if (last_rollback == NULL)
		return;
	rlist_foreach_entry_safe_reverse(e, &limbo->queue, in_queue, tmp) {
		e->is_rollback = true;
		txn_limbo_pop(limbo, e);
		txn_clear_flag(e->txn, TXN_WAIT_SYNC);
		txn_clear_flag(e->txn, TXN_WAIT_ACK);
		if (e->txn->signature >= 0) {
			/* Rollback the transaction. */
			e->txn->signature = TXN_SIGNATURE_SYNC_ROLLBACK;
			txn_complete(e->txn);
		} else {
			/*
			 * Rollback the transaction, but don't
			 * free it yet. txn_complete_async() will
			 * free it.
			 */
			e->txn->signature = TXN_SIGNATURE_SYNC_ROLLBACK;
			struct fiber *fiber = e->txn->fiber;
			e->txn->fiber = fiber();
			txn_complete(e->txn);
			e->txn->fiber = fiber;
		}
		if (e == last_rollback)
			break;
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
		// TODO: what to do here?.
		// We already failed writing the CONFIRM
		// message. What are the chances we'll be
		// able to write ROLLBACK?
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

double
txn_limbo_confirm_timeout(struct txn_limbo *limbo)
{
	(void)limbo;
	return replication_synchro_timeout;
}

/**
 * Waitpoint stores information about the progress of confirmation.
 * In the case of multimaster support, it will store a bitset
 * or array instead of the boolean.
 */
struct confirm_waitpoint {
	/**
	 * Variable for wake up the fiber that is waiting for
	 * the end of confirmation.
	 */
	struct fiber_cond confirm_cond;
	/**
	 * Result flag.
	 */
	bool is_confirm;
};

static int
txn_commit_cb(struct trigger *trigger, void *event)
{
	(void)event;
	struct confirm_waitpoint *cwp =
		(struct confirm_waitpoint *)trigger->data;
	cwp->is_confirm = true;
	fiber_cond_signal(&cwp->confirm_cond);
	return 0;
}

static int
txn_rollback_cb(struct trigger *trigger, void *event)
{
	(void)event;
	struct confirm_waitpoint *cwp =
		(struct confirm_waitpoint *)trigger->data;
	fiber_cond_signal(&cwp->confirm_cond);
	return 0;
}

int
txn_limbo_wait_confirm(struct txn_limbo *limbo)
{
	if (txn_limbo_is_empty(limbo))
		return 0;

	/* initialization of a waitpoint. */
	struct confirm_waitpoint cwp;
	fiber_cond_create(&cwp.confirm_cond);
	cwp.is_confirm = false;

	/* Set triggers for the last limbo transaction. */
	struct trigger on_complete;
	trigger_create(&on_complete, txn_commit_cb, &cwp, NULL);
	struct trigger on_rollback;
	trigger_create(&on_rollback, txn_rollback_cb, &cwp, NULL);
	struct txn_limbo_entry *tle = txn_limbo_last_entry(limbo);
	txn_on_commit(tle->txn, &on_complete);
	txn_on_rollback(tle->txn, &on_rollback);

	int rc = fiber_cond_wait_timeout(&cwp.confirm_cond,
					 txn_limbo_confirm_timeout(limbo));
	fiber_cond_destroy(&cwp.confirm_cond);
	if (rc != 0) {
		/* Clear the triggers if the timeout has been reached. */
		trigger_clear(&on_complete);
		trigger_clear(&on_rollback);
		return -1;
	}
	if (!cwp.is_confirm) {
		/* The transaction has been rolled back. */
		return -1;
	}
	return 0;
}

void
txn_limbo_init(void)
{
	txn_limbo_create(&txn_limbo);
}
