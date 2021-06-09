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
#include "iproto_constants.h"
#include "journal.h"
#include "box.h"

struct txn_limbo txn_limbo;

static inline void
txn_limbo_create(struct txn_limbo *limbo)
{
	rlist_create(&limbo->queue);
	limbo->len = 0;
	limbo->owner_id = REPLICA_ID_NIL;
	fiber_cond_create(&limbo->wait_cond);
	vclock_create(&limbo->vclock);
	vclock_create(&limbo->promote_term_map);
	limbo->promote_greatest_term = 0;
	limbo->confirmed_lsn = 0;
	limbo->rollback_count = 0;
	limbo->is_in_rollback = false;
}

bool
txn_limbo_is_ro(struct txn_limbo *limbo)
{
	return limbo->owner_id != REPLICA_ID_NIL &&
	       limbo->owner_id != instance_id;
}

struct txn_limbo_entry *
txn_limbo_last_synchro_entry(struct txn_limbo *limbo)
{
	struct txn_limbo_entry *entry;
	rlist_foreach_entry_reverse(entry, &limbo->queue, in_queue) {
		if (txn_has_flag(entry->txn, TXN_WAIT_ACK))
			return entry;
	}
	return NULL;
}

struct txn_limbo_entry *
txn_limbo_append(struct txn_limbo *limbo, uint32_t id, struct txn *txn)
{
	assert(txn_has_flag(txn, TXN_WAIT_SYNC));
	assert(limbo == &txn_limbo);
	/*
	 * Transactions should be added to the limbo before WAL write. Limbo
	 * needs that to be able rollback transactions, whose WAL write is in
	 * progress.
	 */
	assert(txn->signature == TXN_SIGNATURE_UNKNOWN);
	assert(txn->status == TXN_PREPARED);
	if (limbo->is_in_rollback) {
		/*
		 * Cascading rollback. It is impossible to commit the
		 * transaction, because if there is an existing rollback in
		 * progress, it should rollback this one too for the sake of
		 * 'reversed rollback order' rule. On the other hand the
		 * rollback can't be postponed until after WAL write as well -
		 * it should be done right now. See in the limbo comments why.
		 */
		diag_set(ClientError, ER_SYNC_ROLLBACK);
		return NULL;
	}
	if (id == 0)
		id = instance_id;
	if  (limbo->owner_id == REPLICA_ID_NIL) {
		diag_set(ClientError, ER_SYNC_QUEUE_UNCLAIMED);
		return NULL;
	} else if (limbo->owner_id != id) {
		if (txn_limbo_is_empty(limbo)) {
			diag_set(ClientError, ER_SYNC_QUEUE_FOREIGN,
				 limbo->owner_id);
		} else {
			diag_set(ClientError, ER_UNCOMMITTED_FOREIGN_SYNC_TXNS,
				 limbo->owner_id);
		}
		return NULL;
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
	limbo->len++;
	return e;
}

static inline void
txn_limbo_remove(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	assert(!rlist_empty(&entry->in_queue));
	assert(txn_limbo_first_entry(limbo) == entry);
	rlist_del_entry(entry, in_queue);
	limbo->len--;
}

static inline void
txn_limbo_pop(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	assert(!rlist_empty(&entry->in_queue));
	assert(txn_limbo_last_entry(limbo) == entry);
	assert(entry->is_rollback);

	rlist_del_entry(entry, in_queue);
	limbo->len--;
	++limbo->rollback_count;
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
	assert(limbo->owner_id != REPLICA_ID_NIL);
	assert(limbo->owner_id != instance_id);
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
	assert(limbo->owner_id != REPLICA_ID_NIL);
	assert(limbo->owner_id == instance_id);
	assert(entry->lsn == -1);
	assert(lsn > 0);
	assert(txn_has_flag(entry->txn, TXN_WAIT_ACK));

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
txn_limbo_assign_lsn(struct txn_limbo *limbo, struct txn_limbo_entry *entry,
		     int64_t lsn)
{
	if (limbo->owner_id == instance_id)
		txn_limbo_assign_local_lsn(limbo, entry, lsn);
	else
		txn_limbo_assign_remote_lsn(limbo, entry, lsn);
}

static void
txn_limbo_write_rollback(struct txn_limbo *limbo, int64_t lsn);

int
txn_limbo_wait_complete(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	assert(entry->lsn > 0 || !txn_has_flag(entry->txn, TXN_WAIT_ACK));
	bool cancellable = fiber_set_cancellable(false);

	if (txn_limbo_entry_is_complete(entry))
		goto complete;

	assert(!txn_has_flag(entry->txn, TXN_IS_DONE));
	assert(txn_has_flag(entry->txn, TXN_WAIT_SYNC));
	double start_time = fiber_clock();
	while (true) {
		double deadline = start_time + replication_synchro_timeout;
		double timeout = deadline - fiber_clock();
		int rc = fiber_cond_wait_timeout(&limbo->wait_cond, timeout);
		if (txn_limbo_entry_is_complete(entry))
			goto complete;
		if (rc != 0)
			break;
	}

	assert(!txn_limbo_is_empty(limbo));
	if (txn_limbo_first_entry(limbo) != entry) {
		/*
		 * If this is not a first entry in the limbo, it
		 * is definitely not a first timed out entry. And
		 * since it managed to time out too, it means
		 * there is currently another fiber writing
		 * rollback, or waiting for confirmation WAL
		 * write. Wait when it will finish and wake us up.
		 */
		goto wait;
	}

	/* First in the queue is always a synchronous transaction. */
	assert(entry->lsn > 0);
	if (entry->lsn <= limbo->confirmed_lsn) {
		/*
		 * Yes, the wait timed out, but there is an on-going CONFIRM WAL
		 * write in another fiber covering this LSN. Can't rollback it
		 * already. All what can be done is waiting. The CONFIRM writer
		 * will wakeup all the confirmed txns when WAL write will be
		 * finished.
		 */
		goto wait;
	}

	txn_limbo_write_rollback(limbo, entry->lsn);
	struct txn_limbo_entry *e, *tmp;
	rlist_foreach_entry_safe_reverse(e, &limbo->queue,
					 in_queue, tmp) {
		e->txn->signature = TXN_SIGNATURE_QUORUM_TIMEOUT;
		txn_limbo_abort(limbo, e);
		txn_clear_flags(e->txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
		txn_complete_fail(e->txn);
		if (e == entry)
			break;
		fiber_wakeup(e->txn->fiber);
	}
	fiber_set_cancellable(cancellable);
	diag_set(ClientError, ER_SYNC_QUORUM_TIMEOUT);
	return -1;

wait:
	do {
		fiber_yield();
	} while (!txn_limbo_entry_is_complete(entry));

complete:
	assert(txn_limbo_entry_is_complete(entry));
	/*
	 * Entry is *always* removed from the limbo by the same fiber, which
	 * installed the commit/rollback flag.
	 */
	assert(rlist_empty(&entry->in_queue));
	assert(txn_has_flag(entry->txn, TXN_IS_DONE));
	fiber_set_cancellable(cancellable);
	/*
	 * The first tx to be rolled back already performed all
	 * the necessary cleanups for us.
	 */
	if (entry->is_rollback) {
		diag_set(ClientError, ER_SYNC_ROLLBACK);
		return -1;
	}
	return 0;
}

void
txn_limbo_checkpoint(const struct txn_limbo *limbo,
		     struct synchro_request *req)
{
	req->type = IPROTO_PROMOTE;
	req->replica_id = limbo->owner_id;
	req->lsn = limbo->confirmed_lsn;
	req->term = limbo->promote_greatest_term;
}

static void
txn_limbo_write_synchro(struct txn_limbo *limbo, uint16_t type, int64_t lsn,
			uint64_t term)
{
	assert(lsn >= 0);

	struct synchro_request req = {
		.type		= type,
		.replica_id	= limbo->owner_id,
		.lsn		= lsn,
		.term		= term,
	};

	/*
	 * This is a synchronous commit so we can
	 * allocate everything on a stack.
	 */
	char body[XROW_SYNCHRO_BODY_LEN_MAX];
	struct xrow_header row;
	char buf[sizeof(struct journal_entry) +
		 sizeof(struct xrow_header *)];

	struct journal_entry *entry = (struct journal_entry *)buf;
	entry->rows[0] = &row;

	xrow_encode_synchro(&row, body, &req);

	journal_entry_create(entry, 1, xrow_approx_len(&row),
			     journal_entry_fiber_wakeup_cb, fiber());

	if (journal_write(entry) != 0)
		goto fail;
	if (entry->res < 0) {
		diag_set_journal_res(entry->res);
		goto fail;
	}
	return;
fail:
	diag_log();
	/*
	 * XXX: the stub is supposed to be removed once it is defined what to do
	 * when a synchro request WAL write fails. One of the possible
	 * solutions: log the error, keep the limbo queue as is and probably put
	 * in rollback mode. Then provide a hook to call manually when WAL
	 * problems are fixed. Or retry automatically with some period.
	 */
	panic("Could not write a synchro request to WAL: lsn = %lld, "
	      "type = %s\n", (long long)lsn, iproto_type_name(type));

}

/**
 * Write a confirmation entry to WAL. After it's written all the
 * transactions waiting for confirmation may be finished.
 */
static void
txn_limbo_write_confirm(struct txn_limbo *limbo, int64_t lsn)
{
	assert(lsn > limbo->confirmed_lsn);
	assert(!limbo->is_in_rollback);
	limbo->confirmed_lsn = lsn;
	txn_limbo_write_synchro(limbo, IPROTO_CONFIRM, lsn, 0);
}

/** Confirm all the entries <= @a lsn. */
static void
txn_limbo_read_confirm(struct txn_limbo *limbo, int64_t lsn)
{
	assert(limbo->owner_id != REPLICA_ID_NIL || txn_limbo_is_empty(limbo));
	assert(limbo == &txn_limbo);
	struct txn_limbo_entry *e, *tmp;
	rlist_foreach_entry_safe(e, &limbo->queue, in_queue, tmp) {
		/*
		 * Check if it is an async transaction last in the queue. When
		 * it is last, it does not depend on a not finished sync
		 * transaction anymore and can be confirmed right away.
		 */
		if (txn_has_flag(e->txn, TXN_WAIT_ACK)) {
			/* Sync transaction not covered by the confirmation. */
			if (e->lsn > lsn)
				break;
			/*
			 * Sync transaction not yet received an LSN. Happens
			 * only to local master transactions whose WAL write is
			 * in progress.
			 */
			if (e->lsn == -1)
				break;
		} else if (e->txn->signature == TXN_SIGNATURE_UNKNOWN) {
			/*
			 * A transaction might be covered by the CONFIRM even if
			 * it is not written to WAL yet when it is an async
			 * transaction. It could be created just when the
			 * CONFIRM was being written to WAL.
			 */
			assert(e->txn->status == TXN_PREPARED);
			/*
			 * Let it complete normally as a plain transaction. It
			 * is important to remove the limbo entry, because the
			 * async transaction might be committed in a
			 * non-blocking way and won't ever wait explicitly for
			 * its completion. Therefore, won't be able to remove
			 * the limbo entry on its own. This happens for txns
			 * created in the applier.
			 */
			txn_clear_flags(e->txn, TXN_WAIT_SYNC);
			txn_limbo_remove(limbo, e);
			/*
			 * The limbo entry now should not be used by the owner
			 * transaction since it just became a plain one. Nullify
			 * the txn to get a crash on any usage attempt instead
			 * of potential undefined behaviour.
			 */
			e->txn = NULL;
			continue;
		}
		e->is_commit = true;
		txn_limbo_remove(limbo, e);
		txn_clear_flags(e->txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
		/*
		 * Should be written to WAL by now. Confirm is always written
		 * after the affected transactions.
		 */
		assert(e->txn->signature >= 0);
		txn_complete_success(e->txn);
	}
}

/**
 * Write a rollback message to WAL. After it's written all the
 * transactions following the current one and waiting for
 * confirmation must be rolled back.
 */
static void
txn_limbo_write_rollback(struct txn_limbo *limbo, int64_t lsn)
{
	assert(lsn > limbo->confirmed_lsn);
	assert(!limbo->is_in_rollback);
	limbo->is_in_rollback = true;
	txn_limbo_write_synchro(limbo, IPROTO_ROLLBACK, lsn, 0);
	limbo->is_in_rollback = false;
}

/** Rollback all the entries >= @a lsn. */
static void
txn_limbo_read_rollback(struct txn_limbo *limbo, int64_t lsn)
{
	assert(limbo->owner_id != REPLICA_ID_NIL || txn_limbo_is_empty(limbo));
	assert(limbo == &txn_limbo);
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
		txn_limbo_abort(limbo, e);
		txn_clear_flags(e->txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
		/*
		 * Should be written to WAL by now. Rollback is always written
		 * after the affected transactions.
		 */
		assert(e->txn->signature >= 0);
		e->txn->signature = TXN_SIGNATURE_SYNC_ROLLBACK;
		txn_complete_fail(e->txn);
		if (e == last_rollback)
			break;
	}
}

void
txn_limbo_write_promote(struct txn_limbo *limbo, int64_t lsn, uint64_t term)
{
	limbo->confirmed_lsn = lsn;
	limbo->is_in_rollback = true;
	/*
	 * We make sure that promote is only written once everything this
	 * instance has may be confirmed.
	 */
	struct txn_limbo_entry *e = txn_limbo_last_synchro_entry(limbo);
	assert(e == NULL || e->lsn <= lsn);
	(void) e;
	txn_limbo_write_synchro(limbo, IPROTO_PROMOTE, lsn, term);
	limbo->is_in_rollback = false;
}

/**
 * Process a PROMOTE request, i.e. confirm all entries <= @a req.lsn and
 * rollback all entries > @a req.lsn.
 */
static void
txn_limbo_read_promote(struct txn_limbo *limbo, uint32_t replica_id,
		       int64_t lsn)
{
	txn_limbo_read_confirm(limbo, lsn);
	txn_limbo_read_rollback(limbo, lsn + 1);
	assert(txn_limbo_is_empty(&txn_limbo));
	limbo->owner_id = replica_id;
	box_update_ro_summary();
	limbo->confirmed_lsn = 0;
}

void
txn_limbo_write_demote(struct txn_limbo *limbo, int64_t lsn, uint64_t term)
{
	limbo->confirmed_lsn = lsn;
	limbo->is_in_rollback = true;
	struct txn_limbo_entry *e = txn_limbo_last_synchro_entry(limbo);
	assert(e == NULL || e->lsn <= lsn);
	(void)e;
	txn_limbo_write_synchro(limbo, IPROTO_DEMOTE, lsn, term);
	limbo->is_in_rollback = false;
}

/**
 * Process a DEMOTE request, which's like PROMOTE, but clears the limbo
 * ownership.
 * @sa txn_limbo_read_promote.
 */
static void
txn_limbo_read_demote(struct txn_limbo *limbo, int64_t lsn)
{
	return txn_limbo_read_promote(limbo, REPLICA_ID_NIL, lsn);
}

void
txn_limbo_ack(struct txn_limbo *limbo, uint32_t replica_id, int64_t lsn)
{
	if (rlist_empty(&limbo->queue))
		return;
	/*
	 * If limbo is currently writing a rollback, it means that the whole
	 * queue will be rolled back. Because rollback is written only for
	 * timeout. Timeout always happens first for the oldest entry, i.e.
	 * first entry in the queue. The rollback will clear all the newer
	 * entries. So in total the whole queue is dead already. Would be
	 * strange to write CONFIRM for rolled back LSNs. Even though
	 * probably it wouldn't break anything. Would be just 2 conflicting
	 * decisions for the same LSNs.
	 */
	if (limbo->is_in_rollback)
		return;
	assert(limbo->owner_id != REPLICA_ID_NIL);
	int64_t prev_lsn = vclock_get(&limbo->vclock, replica_id);
	/*
	 * One of the reasons why can happen - the remote instance is not
	 * read-only and wrote something under its own insance_id. For qsync
	 * that most likely means that the remote instance decided to take over
	 * the limbo ownership, and the current node is going to become a
	 * replica very soon.
	 */
	if (lsn == prev_lsn)
		return;
	vclock_follow(&limbo->vclock, replica_id, lsn);
	struct txn_limbo_entry *e;
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
			continue;
		} else if (e->lsn <= prev_lsn) {
			continue;
		} else if (++e->ack_count < replication_synchro_quorum) {
			continue;
		} else {
			confirm_lsn = e->lsn;
		}
	}
	if (confirm_lsn == -1 || confirm_lsn <= limbo->confirmed_lsn)
		return;
	txn_limbo_write_confirm(limbo, confirm_lsn);
	txn_limbo_read_confirm(limbo, confirm_lsn);
}

/**
 * Waitpoint stores information about the progress of confirmation.
 * In the case of multimaster support, it will store a bitset
 * or array instead of the boolean.
 */
struct confirm_waitpoint {
	/** Fiber that is waiting for the end of confirmation. */
	struct fiber *caller;
	/**
	 * Result flag.
	 */
	bool is_confirm;
	bool is_rollback;
};

static int
txn_commit_cb(struct trigger *trigger, void *event)
{
	(void)event;
	struct confirm_waitpoint *cwp =
		(struct confirm_waitpoint *)trigger->data;
	cwp->is_confirm = true;
	fiber_wakeup(cwp->caller);
	return 0;
}

static int
txn_rollback_cb(struct trigger *trigger, void *event)
{
	(void)event;
	struct confirm_waitpoint *cwp =
		(struct confirm_waitpoint *)trigger->data;
	cwp->is_rollback = true;
	fiber_wakeup(cwp->caller);
	return 0;
}

/**
 * Wait until the last transaction in the limbo is finished and get its result.
 */
static int
txn_limbo_wait_last_txn(struct txn_limbo *limbo, bool *is_rollback,
			double timeout)
{
	assert(!txn_limbo_is_empty(limbo));

	/* initialization of a waitpoint. */
	struct confirm_waitpoint cwp;
	cwp.caller = fiber();
	cwp.is_confirm = false;
	cwp.is_rollback = false;

	/* Set triggers for the last limbo transaction. */
	struct trigger on_complete;
	trigger_create(&on_complete, txn_commit_cb, &cwp, NULL);
	struct trigger on_rollback;
	trigger_create(&on_rollback, txn_rollback_cb, &cwp, NULL);
	struct txn_limbo_entry *tle = txn_limbo_last_entry(limbo);
	txn_on_commit(tle->txn, &on_complete);
	txn_on_rollback(tle->txn, &on_rollback);
	double deadline = fiber_clock() + timeout;
	int rc;
	while (true) {
		if (timeout < 0) {
			rc = -1;
			break;
		}
		bool cancellable = fiber_set_cancellable(false);
		rc = fiber_cond_wait_timeout(&limbo->wait_cond, timeout);
		fiber_set_cancellable(cancellable);
		if (cwp.is_confirm || cwp.is_rollback) {
			*is_rollback = cwp.is_rollback;
			rc = 0;
			break;
		}
		if (rc != 0)
			break;
		timeout = deadline - fiber_clock();
	}
	trigger_clear(&on_complete);
	trigger_clear(&on_rollback);
	return rc;
}

int
txn_limbo_wait_confirm(struct txn_limbo *limbo)
{
	if (txn_limbo_is_empty(limbo))
		return 0;
	bool is_rollback;
	if (txn_limbo_wait_last_txn(limbo, &is_rollback,
				    replication_synchro_timeout) != 0) {
		diag_set(ClientError, ER_SYNC_QUORUM_TIMEOUT);
		return -1;
	}
	if (is_rollback) {
		/* The transaction has been rolled back. */
		diag_set(ClientError, ER_SYNC_ROLLBACK);
		return -1;
	}
	return 0;
}

int
txn_limbo_wait_empty(struct txn_limbo *limbo, double timeout)
{
	if (txn_limbo_is_empty(limbo))
		return 0;
	bool is_rollback;
	double deadline = fiber_clock() + timeout;
	/*
	 * Retry in the loop. More transactions might be added while waiting for
	 * the last one.
	 */
	do {
		if (txn_limbo_wait_last_txn(limbo, &is_rollback,
					    timeout) != 0) {
			diag_set(ClientError, ER_TIMEOUT);
			return -1;
		}
		timeout = deadline - fiber_clock();
	} while (!txn_limbo_is_empty(limbo));
	return 0;
}

void
txn_limbo_process(struct txn_limbo *limbo, const struct synchro_request *req)
{
	uint64_t term = req->term;
	uint32_t origin = req->origin_id;
	if (txn_limbo_replica_term(limbo, origin) < term) {
		vclock_follow(&limbo->promote_term_map, origin, term);
		if (term > limbo->promote_greatest_term)
			limbo->promote_greatest_term = term;
	} else if (iproto_type_is_promote_request(req->type) &&
		   limbo->promote_greatest_term > 1) {
		/* PROMOTE for outdated term. Ignore. */
		say_info("RAFT: ignoring %s request from instance "
			 "id %u for term %llu. Greatest term seen "
			 "before (%llu) is bigger.",
			 iproto_type_name(req->type), origin, (long long)term,
			 (long long)limbo->promote_greatest_term);
		return;
	}

	int64_t lsn = req->lsn;
	if (req->replica_id == REPLICA_ID_NIL) {
		/*
		 * The limbo was empty on the instance issuing the request.
		 * This means this instance must empty its limbo as well.
		 */
		assert(lsn == 0 && iproto_type_is_promote_request(req->type));
	} else if (req->replica_id != limbo->owner_id) {
		/*
		 * Ignore CONFIRM/ROLLBACK messages for a foreign master.
		 * These are most likely outdated messages for already confirmed
		 * data from an old leader, who has just started and written
		 * confirm right on synchronous transaction recovery.
		 */
		if (!iproto_type_is_promote_request(req->type))
			return;
		/*
		 * Promote has a bigger term, and tries to steal the limbo. It
		 * means it probably was elected with a quorum, and it makes no
		 * sense to wait here for confirmations. The other nodes already
		 * elected a new leader. Rollback all the local txns.
		 */
		lsn = 0;
	}
	switch (req->type) {
	case IPROTO_CONFIRM:
		txn_limbo_read_confirm(limbo, lsn);
		break;
	case IPROTO_ROLLBACK:
		txn_limbo_read_rollback(limbo, lsn);
		break;
	case IPROTO_PROMOTE:
		txn_limbo_read_promote(limbo, req->origin_id, lsn);
		break;
	case IPROTO_DEMOTE:
		txn_limbo_read_demote(limbo, lsn);
		break;
	default:
		unreachable();
	}
	return;
}

void
txn_limbo_on_parameters_change(struct txn_limbo *limbo)
{
	if (rlist_empty(&limbo->queue))
		return;
	struct txn_limbo_entry *e;
	int64_t confirm_lsn = -1;
	rlist_foreach_entry(e, &limbo->queue, in_queue) {
		assert(e->ack_count <= VCLOCK_MAX);
		if (!txn_has_flag(e->txn, TXN_WAIT_ACK)) {
			assert(e->lsn == -1);
			continue;
		} else if (e->ack_count < replication_synchro_quorum) {
			continue;
		} else {
			confirm_lsn = e->lsn;
			assert(confirm_lsn > 0);
		}
	}
	if (confirm_lsn > limbo->confirmed_lsn && !limbo->is_in_rollback) {
		txn_limbo_write_confirm(limbo, confirm_lsn);
		txn_limbo_read_confirm(limbo, confirm_lsn);
	}
	/*
	 * Wakeup all the others - timed out will rollback. Also
	 * there can be non-transactional waiters, such as CONFIRM
	 * waiters. They are bound to a transaction, but if they
	 * wait on replica, they won't see timeout update. Because
	 * sync transactions can live on replica infinitely.
	 */
	fiber_cond_broadcast(&limbo->wait_cond);
}

void
txn_limbo_init(void)
{
	txn_limbo_create(&txn_limbo);
}
