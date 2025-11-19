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
#include "raft.h"
#include "tt_static.h"
#include "session.h"
#include "trivia/config.h"

struct txn_limbo txn_limbo;

static int
txn_limbo_write_synchro(struct txn_limbo *limbo, uint16_t type, int64_t lsn,
			uint64_t term);

static void
txn_limbo_read_confirm(struct txn_limbo *limbo, int64_t lsn);

static inline struct txn_limbo_entry *
txn_limbo_first_entry(struct txn_limbo *limbo)
{
	return rlist_first_entry(&limbo->queue, struct txn_limbo_entry,
				 in_queue);
}

static inline struct txn_limbo_entry *
txn_limbo_last_entry(struct txn_limbo *limbo)
{
	return rlist_last_entry(&limbo->queue, struct txn_limbo_entry,
				in_queue);
}

static inline bool
txn_limbo_is_full(struct txn_limbo *limbo)
{
	return limbo->size >= limbo->max_size;
}

double
txn_limbo_age(struct txn_limbo *limbo)
{
	if (txn_limbo_is_empty(limbo))
		return 0;
	return fiber_clock() - txn_limbo_first_entry(limbo)->insertion_time;
}

/**
 * Write a confirmation entry to the WAL. After it's written all the
 * transactions waiting for confirmation may be finished.
 */
static int
txn_limbo_write_confirm(struct txn_limbo *limbo, int64_t lsn)
{
	assert(lsn > limbo->confirmed_lsn);
	assert(!limbo->is_in_rollback);
	return txn_limbo_write_synchro(limbo, IPROTO_RAFT_CONFIRM, lsn, 0);
}

static int
txn_limbo_worker_bump_confirmed_lsn(struct txn_limbo *limbo)
{
	assert(limbo->volatile_confirmed_lsn >= limbo->confirmed_lsn);
	while (txn_limbo_is_owned_by_current_instance(limbo) &&
	       limbo->volatile_confirmed_lsn > limbo->confirmed_lsn) {
		if (limbo->is_in_rollback)
			return -1;
		/* It can get bumped again while we are writing. */
		int64_t volatile_confirmed_lsn = limbo->volatile_confirmed_lsn;
		if (txn_limbo_write_confirm(limbo,
					    volatile_confirmed_lsn) != 0) {
			diag_log();
			return -1;
		}
		ERROR_INJECT_YIELD(ERRINJ_TXN_LIMBO_WORKER_DELAY);
		txn_limbo_read_confirm(limbo, volatile_confirmed_lsn);
	}
	assert(limbo->volatile_confirmed_lsn >= limbo->confirmed_lsn);
	return 0;
}

static int
txn_limbo_worker_f(va_list args)
{
	(void)args;
	struct txn_limbo *limbo = fiber()->f_arg;
	assert(limbo == &txn_limbo);
	while (!fiber_is_cancelled()) {
		fiber_check_gc();
		ERROR_INJECT_YIELD(ERRINJ_TXN_LIMBO_WORKER_DELAY);
		if (txn_limbo_worker_bump_confirmed_lsn(limbo) != 0)
#ifdef TEST_BUILD
			fiber_sleep(0.01);
#else
			fiber_sleep(1);
#endif
		else
			fiber_yield();
	}
	return 0;
}

static inline void
txn_limbo_create(struct txn_limbo *limbo)
{
	rlist_create(&limbo->queue);
	limbo->len = 0;
	limbo->owner_id = REPLICA_ID_NIL;
	fiber_cond_create(&limbo->wait_cond);
	vclock_create(&limbo->vclock);
	vclock_create(&limbo->promote_term_map);
	vclock_create(&limbo->confirmed_vclock);
	limbo->promote_greatest_term = 0;
	latch_create(&limbo->promote_latch);
	limbo->confirmed_lsn = 0;
	limbo->volatile_confirmed_lsn = 0;
	limbo->entry_to_confirm = NULL;
	limbo->is_in_rollback = false;
	limbo->svp_confirmed_lsn = -1;
	limbo->frozen_reasons = 0;
	limbo->is_frozen_until_promotion = true;
	limbo->do_validate = false;
	limbo->confirm_lag = 0;
	limbo->max_size = 0;
	limbo->size = 0;
	limbo->worker = fiber_new_system("txn_limbo_worker",
					 txn_limbo_worker_f);
	if (limbo->worker == NULL)
		panic("failed to allocate synchronous queue worker fiber");
	limbo->worker->f_arg = limbo;
	fiber_set_joinable(limbo->worker, true);
}

void
txn_limbo_set_max_size(struct txn_limbo *limbo, int64_t size)
{
	limbo->max_size = size;
}

static inline void
txn_limbo_destroy(struct txn_limbo *limbo)
{
	struct txn_limbo_entry *entry;
	while (!rlist_empty(&limbo->queue)) {
		entry = rlist_shift_entry(&limbo->queue,
					  typeof(*entry), in_queue);
		entry->txn->limbo_entry = NULL;
		txn_free(entry->txn);
	}
	fiber_cond_destroy(&limbo->wait_cond);
	TRASH(limbo);
}

static inline void
txn_limbo_stop(struct txn_limbo *limbo)
{
	fiber_cancel(limbo->worker);
	VERIFY(fiber_join(limbo->worker) == 0);
}

static inline bool
txn_limbo_is_frozen(const struct txn_limbo *limbo)
{
	return limbo->frozen_reasons != 0;
}

bool
txn_limbo_is_ro(struct txn_limbo *limbo)
{
	return limbo->owner_id != REPLICA_ID_NIL &&
		(!txn_limbo_is_owned_by_current_instance(limbo) ||
		txn_limbo_is_frozen(limbo));
}

/** Decrease queue size once write request is complete. */
static inline void
txn_limbo_on_remove(struct txn_limbo *limbo,
		    const struct txn_limbo_entry *entry)
{
	bool limbo_was_full = txn_limbo_is_full(limbo);
	limbo->size -= entry->approx_len;
	assert(limbo->size >= 0);
	limbo->len--;
	assert(limbo->len >= 0);
	/* Wake up all fibers waiting to add a new limbo entry. */
	if (limbo_was_full && !txn_limbo_is_full(limbo))
		fiber_cond_broadcast(&limbo->wait_cond);
}

/** Pop the first entry. */
static inline void
txn_limbo_pop_first(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	assert(!rlist_empty(&entry->in_queue));
	assert(txn_limbo_first_entry(limbo) == entry);
	rlist_del_entry(entry, in_queue);
	txn_limbo_on_remove(limbo, entry);
}

static void
txn_limbo_complete(struct txn *txn, bool is_success)
{
	/*
	 * Some rollback/commit triggers require the in_txn fiber
	 * variable to be set.
	 */
	assert(in_txn() == NULL);
	fiber_set_txn(fiber(), txn);
	/*
	 * Use session and credentials of the original fiber for
	 * commit/rollback triggers.
	 */
	struct session *orig_session = fiber_get_session(fiber());
	struct session *session = (txn->fiber != NULL ?
				   fiber_get_session(txn->fiber) : NULL);
	if (session != NULL)
		fiber_set_session(fiber(), session);
	struct credentials *orig_creds = fiber_get_user(fiber());
	struct credentials *creds = (txn->fiber != NULL ?
				     fiber_get_user(txn->fiber) : NULL);
	if (creds != NULL)
		fiber_set_user(fiber(), creds);

	if (is_success)
		txn_complete_success(txn);
	else
		txn_complete_fail(txn);

	fiber_set_txn(fiber(), NULL);
	fiber_set_user(fiber(), orig_creds);
	fiber_set_session(fiber(), orig_session);
}

/** Complete the given limbo entry with a failure and the given reason. */
static void
txn_limbo_complete_fail(struct txn_limbo *limbo, struct txn_limbo_entry *entry,
			int64_t signature)
{
	assert(entry->state == TXN_LIMBO_ENTRY_SUBMITTED ||
	       entry->state == TXN_LIMBO_ENTRY_VOLATILE);
	struct txn *txn = entry->txn;
	txn->signature = signature;
	txn->limbo_entry = NULL;
	txn_limbo_abort(limbo, entry);
	txn_clear_flags(txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
	txn_limbo_complete(txn, false);
}

/** Complete the given limbo entry with a success. */
static void
txn_limbo_complete_success(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	assert(entry->state == TXN_LIMBO_ENTRY_SUBMITTED);
	struct txn *txn = entry->txn;
	entry->state = TXN_LIMBO_ENTRY_COMMIT;
	if (txn_has_flag(txn, TXN_WAIT_ACK))
		limbo->confirm_lag = fiber_clock() - entry->insertion_time;
	txn->limbo_entry = NULL;
	txn_limbo_pop_first(limbo, entry);
	txn_clear_flags(txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
	/*
	 * Should be written to WAL by now. Confirm is always written after the
	 * affected transactions.
	 */
	assert(txn->signature >= 0);
	txn_limbo_complete(txn, true);
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

/** Increase queue size on a new write request. */
static inline void
txn_limbo_on_append(struct txn_limbo *limbo,
		    const struct txn_limbo_entry *entry)
{
	assert(entry->state == TXN_LIMBO_ENTRY_SUBMITTED);
	limbo->size += entry->approx_len;
	limbo->len++;
}

/** Cascade-rollback all the entries from the newest to the given one. */
static void
txn_limbo_rollback_volatile_up_to(struct txn_limbo *limbo,
				  struct txn_limbo_entry *last)
{
	assert(last == NULL || last->state == TXN_LIMBO_ENTRY_VOLATILE);
	while (!txn_limbo_is_empty(limbo)) {
		struct txn_limbo_entry *e = txn_limbo_last_entry(limbo);
		if (e == last || e->state != TXN_LIMBO_ENTRY_VOLATILE)
			break;
		txn_limbo_complete_fail(limbo, e, TXN_SIGNATURE_CASCADE);
	}
}

void
txn_limbo_rollback_all_volatile(struct txn_limbo *limbo)
{
	txn_limbo_rollback_volatile_up_to(limbo, NULL);
}

bool
txn_limbo_would_block(struct txn_limbo *limbo)
{
	if (txn_limbo_is_full(limbo)) {
		/*
		 * On replicas the limbo can't get blocked on max size. Because
		 * if the size is lower than on the master, the replica would
		 * become unable to read new xrows after the local max
		 * size is reached. Because the applier would be just waiting on
		 * the limbo to get some free space first. This would make the
		 * applier also unable to read CONFIRM, which in turn is
		 * necessary to make free space in the limbo. And this is a
		 * deadlock. The only way is to make the replica ignore its max
		 * size when it comes to applying new txns.
		 */
		if (txn_limbo_is_owned_by_current_instance(limbo))
			return true;
	}
	if (txn_limbo_is_empty(limbo))
		return false;
	/*
	 * Might be not full, but still have a volatile entry in the end. Could
	 * be caused by some spurious wakeups of the entries' fibers in an
	 * unexpected order. Either way, the new submission will have to wait
	 * until the previous one gets submitted.
	 */
	return txn_limbo_last_entry(limbo)->state == TXN_LIMBO_ENTRY_VOLATILE;
}

int
txn_limbo_submit(struct txn_limbo *limbo, uint32_t id, struct txn *txn,
		 size_t approx_len)
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
		return -1;
	}
	if (id == 0)
		id = instance_id;
	if  (limbo->owner_id == REPLICA_ID_NIL) {
		diag_set(ClientError, ER_SYNC_QUEUE_UNCLAIMED);
		return -1;
	} else if (limbo->owner_id != id && !txn_is_fully_local(txn)) {
		if (txn_limbo_is_empty(limbo)) {
			diag_set(ClientError, ER_SYNC_QUEUE_FOREIGN,
				 limbo->owner_id);
		} else {
			diag_set(ClientError, ER_UNCOMMITTED_FOREIGN_SYNC_TXNS,
				 limbo->owner_id);
		}
		return -1;
	}
	size_t size;
	struct txn_limbo_entry *e = region_alloc_object(&txn->region,
							typeof(*e), &size);
	if (limbo->entry_to_confirm == NULL &&
	    txn_has_flag(txn, TXN_WAIT_ACK)) {
		limbo->entry_to_confirm = e;
		limbo->ack_count = 0;
	}
	if (e == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_object", "e");
		return -1;
	}
	e->txn = txn;
	e->approx_len = approx_len;
	e->lsn = -1;
	e->insertion_time = fiber_clock();
	txn->limbo_entry = e;
	bool would_block = txn_limbo_would_block(limbo);
	rlist_add_tail_entry(&limbo->queue, e, in_queue);
	if (!would_block)
		goto success;
	e->state = TXN_LIMBO_ENTRY_VOLATILE;
	while (true) {
		bool ok = !fiber_is_cancelled() &&
			  fiber_cond_wait(&limbo->wait_cond) == 0;
		if (e->state == TXN_LIMBO_ENTRY_ROLLBACK) {
			/* Cascading rollback. */
			fiber_set_txn(fiber(), NULL);
			diag_set(ClientError, ER_SYNC_ROLLBACK);
			return -1;
		}
		if (!ok) {
			fiber_set_txn(fiber(), NULL);
			txn_limbo_rollback_volatile_up_to(limbo, e);
			txn_limbo_complete_fail(limbo, e,
						TXN_SIGNATURE_CANCELLED);
			assert(e->state == TXN_LIMBO_ENTRY_ROLLBACK);
			diag_set(ClientError, ER_SYNC_ROLLBACK);
			return -1;
		}
		/* Could be a spurious wakeup. */
		if (txn_limbo_is_full(limbo))
			continue;
		if (txn_limbo_first_entry(limbo) == e)
			break;
		struct txn_limbo_entry *prev = rlist_prev_entry(e, in_queue);
		/*
		 * Could again be a spurious wakeup, when there is space to
		 * submit more entries into the journal, but this entry isn't
		 * the first volatile one in the queue. Submission into the
		 * journal must be the same order as the addition to the queue.
		 */
		if (prev->state == TXN_LIMBO_ENTRY_VOLATILE)
			continue;
		/*
		 * The previous one can't be ROLLBACK or COMMIT. Or it wouldn't
		 * be in the limbo already.
		 */
		assert(prev->state == TXN_LIMBO_ENTRY_SUBMITTED);
		break;
	}
	assert(e->state == TXN_LIMBO_ENTRY_VOLATILE);
	if (txn_limbo_last_entry(limbo) != e) {
		/*
		 * Wake the next one up so it would check if it can also be
		 * submitted.
		 */
		struct txn_limbo_entry *next = rlist_next_entry(e, in_queue);
		assert(next->state == TXN_LIMBO_ENTRY_VOLATILE);
		fiber_wakeup(next->txn->fiber);
	}
	if (!txn_has_flag(txn, TXN_WAIT_SYNC)) {
		/*
		 * Could be an asynchronous transaction which was trying to get
		 * into the limbo only because there were some synchro txns in
		 * it. Then those got confirmed and suddenly this txn doesn't
		 * need the limbo anymore.
		 */
		txn->limbo_entry = NULL;
		e->txn = NULL;
		assert(txn_limbo_first_entry(limbo) == e);
		rlist_del_entry(e, in_queue);
		return 0;
	}
success:
	e->state = TXN_LIMBO_ENTRY_SUBMITTED;
	txn_limbo_on_append(limbo, e);
	return 0;
}

int
txn_limbo_flush(struct txn_limbo *limbo)
{
	/* Fast path. */
	if (txn_limbo_is_empty(limbo))
		return 0;
	if (txn_limbo_last_entry(limbo)->state != TXN_LIMBO_ENTRY_VOLATILE)
		return 0;
	/*
	 * Slow path.
	 * The limbo queue guarantees that if a txn is trying to be submitted
	 * into it, then the submission would return right after all the
	 * previous txns are sent to the journal and before any newer txns do
	 * the same.
	 *
	 * Which means a flush could be done as simple as just doing a txn
	 * submission. As soon as submit returns - all the older entries are
	 * sent to the journal.
	 *
	 * To conveniently reuse the submission logic the flush creates a nop
	 * txn to ride on it through the limbo queue.
	 */
	struct txn *txn = txn_begin();
	if (txn == NULL)
		return -1;
	if (txn_prepare(txn) != 0)
		unreachable();
	txn->fiber = fiber();
	txn_set_flags(txn, TXN_WAIT_SYNC);
	int rc = txn_limbo_submit(limbo, limbo->owner_id, txn, 0);
	if (rc == 0) {
		assert(!txn_has_flag(txn, TXN_IS_DONE));
		/*
		 * The limbo entry might be already removed, if all the previous
		 * txns got not just sent to WAL, but also covered by a confirm.
		 *
		 * Can happen, for example, if there was a sync txn in the
		 * limbo and an async txn waiting for limbo space. Then this
		 * flush would stand after the async txn.
		 *
		 * Then if the sync txn gets confirmed, it is committed. And all
		 * the following non-sync txns are confirmed too. Even if they
		 * aren't written to WAL yet, they just become non-synchronous
		 * anymore.
		 *
		 * Including the mentioned waiting async txn and this flush-txn.
		 */
		if (txn->limbo_entry != NULL) {
			/*
			 * The worst part of this code is that the "fake" nop
			 * txn must be removed from the middle of the limbo. It
			 * can't stay there. Such behaviour doesn't fit neither
			 * commit nor rollback, but the ability to reuse
			 * submission for flushing the limbo justifies this.
			 */
			assert(txn->limbo_entry != limbo->entry_to_confirm);
			rlist_del_entry(txn->limbo_entry, in_queue);
			txn->limbo_entry = NULL;
		}
	} else {
		assert(txn->limbo_entry == NULL);
	}
	/*
	 * Roll the nop txn back. In theory it shouldn't matter if it is
	 * committed or rolled back as it is nop anyway. But the rollback should
	 * help to catch any issues if some code would accidentally find this
	 * txn in the limbo and hang on-commit/rollback triggers on it. For
	 * instance, to wait for the "last txn to be committed". Using the nop
	 * txn for that would be wrong. The rollback would highlight such
	 * misusage.
	 */
	if (!txn_has_flag(txn, TXN_IS_DONE)) {
		txn->signature = TXN_SIGNATURE_ROLLBACK;
		txn_complete_fail(txn);
	}
	assert(in_txn() == NULL || in_txn() == txn);
	fiber_set_txn(fiber(), NULL);
	txn_free(txn);
	return rc;
}

void
txn_limbo_abort(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	assert(!rlist_empty(&entry->in_queue));
	/*
	 * The simple rule about rollback/commit order applies
	 * here as well: commit always in the order of WAL write,
	 * rollback in the reversed order. Rolled back transaction
	 * is always the last.
	 */
	assert(txn_limbo_last_entry(limbo) == entry);
	bool was_volatile = entry->state == TXN_LIMBO_ENTRY_VOLATILE;
	assert(was_volatile || entry->state == TXN_LIMBO_ENTRY_SUBMITTED);
	entry->state = TXN_LIMBO_ENTRY_ROLLBACK;
	if (entry == limbo->entry_to_confirm)
		limbo->entry_to_confirm = NULL;
	rlist_del_entry(entry, in_queue);
	if (!was_volatile)
		txn_limbo_on_remove(limbo, entry);
}

void
txn_limbo_assign_remote_lsn(struct txn_limbo *limbo,
			    struct txn_limbo_entry *entry, int64_t lsn)
{
	assert(limbo->owner_id != REPLICA_ID_NIL);
	assert(!txn_limbo_is_owned_by_current_instance(limbo));
	assert(entry->lsn == -1);
	assert(lsn > 0);
	(void) limbo;
	/*
	 * Same as with local LSN assign, it is given after a WAL write. But for
	 * remotely received transactions it doesn't matter so far. They don't
	 * needs ACKs. They wait for explicit confirmations. That will be a
	 * problem when need acks for anything else and when local txns will
	 * become optionally non-blocking.
	 */
	entry->lsn = lsn;
}

static void
txn_limbo_confirm(struct txn_limbo *limbo);

void
txn_limbo_assign_local_lsn(struct txn_limbo *limbo,
			   struct txn_limbo_entry *entry, int64_t lsn)
{
	assert(limbo->owner_id != REPLICA_ID_NIL);
	assert(txn_limbo_is_owned_by_current_instance(limbo));
	assert(entry->lsn == -1);
	assert(lsn > 0);

	entry->lsn = lsn;
	if (entry == limbo->entry_to_confirm)
		limbo->ack_count = vclock_count_ge(&limbo->vclock, entry->lsn);
}

void
txn_limbo_assign_lsn(struct txn_limbo *limbo, struct txn_limbo_entry *entry,
		     int64_t lsn)
{
	if (txn_limbo_is_owned_by_current_instance(limbo))
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

	if (txn_limbo_entry_is_complete(entry))
		goto complete;

	assert(!txn_has_flag(entry->txn, TXN_IS_DONE));
	assert(txn_has_flag(entry->txn, TXN_WAIT_SYNC));
	double start_time = fiber_clock();
	while (true) {
		double timeout = (replication_synchro_timeout_rollback_enabled ?
			replication_synchro_timeout : txn_synchro_timeout);
		double timeout_rest = start_time + timeout - fiber_clock();
		int rc = fiber_cond_wait_timeout(
			&limbo->wait_cond, timeout_rest);
		if (txn_limbo_entry_is_complete(entry))
			goto complete;
		if (rc != 0 && fiber_is_cancelled())
			return -1;
		if (txn_limbo_is_frozen(limbo))
			goto wait;
		if (rc != 0)
			break;
	}

	assert(!txn_limbo_is_empty(limbo));

	if (!replication_synchro_timeout_rollback_enabled) {
		diag_set(ClientError, ER_SYNC_TIMEOUT);
		return -1;
	}

	struct txn_limbo_entry *e;
	bool is_first_waiting_entry = true;
	rlist_foreach_entry(e, &limbo->queue, in_queue) {
		if (e == entry)
			break;
		if (txn_has_flag(e->txn, TXN_WAIT_ACK) &&
		    e->txn->fiber != NULL) {
			is_first_waiting_entry = false;
			break;
		}
	}
	if (!is_first_waiting_entry) {
		/*
		 * If this is not the first waiting entry in the limbo, it
		 * is definitely not the first timed out entry. And
		 * since it managed to time out too, it means
		 * there is currently another fiber writing
		 * rollback, or waiting for confirmation WAL
		 * write. Wait when it will finish and wake us up.
		 */
		goto wait;
	}

	/* First in the queue is always a synchronous transaction. */
	assert(entry->lsn > 0);
	if (entry->lsn <= limbo->volatile_confirmed_lsn) {
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
	struct txn_limbo_entry *tmp;
	rlist_foreach_entry_safe_reverse(e, &limbo->queue,
					 in_queue, tmp) {
		txn_limbo_complete_fail(limbo, e, TXN_SIGNATURE_QUORUM_TIMEOUT);
		if (e == entry)
			break;
	}
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
	/*
	 * The first tx to be rolled back already performed all
	 * the necessary cleanups for us.
	 */
	if (entry->state == TXN_LIMBO_ENTRY_ROLLBACK) {
		diag_set(ClientError, ER_SYNC_ROLLBACK);
		return -1;
	}
	return 0;
}

void
txn_limbo_checkpoint(const struct txn_limbo *limbo,
		     struct synchro_request *req)
{
	req->type = IPROTO_RAFT_PROMOTE;
	req->replica_id = limbo->owner_id;
	req->lsn = limbo->confirmed_lsn;
	req->term = limbo->promote_greatest_term;
	vclock_copy(&req->confirmed_vclock, &limbo->confirmed_vclock);
}

static int
synchro_request_write(const struct synchro_request *req)
{
	/*
	 * This is a synchronous commit so we can
	 * allocate everything on a stack.
	 */
	char body[XROW_BODY_LEN_MAX];
	struct xrow_header row;
	xrow_encode_synchro(&row, body, req);
	return journal_write_row(&row);
}

/** Create a request for a specific limbo and write it to WAL. */
static int
txn_limbo_write_synchro(struct txn_limbo *limbo, uint16_t type, int64_t lsn,
			uint64_t term)
{
	assert(lsn >= 0);

	struct synchro_request req = {
		.type = type,
		.replica_id = limbo->owner_id,
		.lsn = lsn,
		.term = term,
	};
	vclock_clear(&req.confirmed_vclock);
	return synchro_request_write(&req);
}

/** Write a request to WAL or panic. */
static void
synchro_request_write_or_panic(const struct synchro_request *req)
{
	if (synchro_request_write(req) == 0)
		return;
	diag_log();
	/*
	 * XXX: the stub is supposed to be removed once it is defined what to do
	 * when a synchro request WAL write fails. One of the possible
	 * solutions: log the error, keep the limbo queue as is and probably put
	 * in rollback mode. Then provide a hook to call manually when WAL
	 * problems are fixed. Or retry automatically with some period.
	 */
	panic("Could not write a synchro request to WAL: lsn = %lld, "
	      "type = %s\n", (long long)req->lsn, iproto_type_name(req->type));
}

/** Create a request for a specific limbo and write it to WAL or panic. */
static void
txn_limbo_write_synchro_or_panic(struct txn_limbo *limbo, uint16_t type,
				 int64_t lsn, uint64_t term)
{
	assert(lsn >= 0);

	struct synchro_request req = {
		.type = type,
		.replica_id = limbo->owner_id,
		.lsn = lsn,
		.term = term,
	};
	vclock_clear(&req.confirmed_vclock);
	synchro_request_write_or_panic(&req);
}

/** Confirm all the entries <= @a lsn. */
static void
txn_limbo_read_confirm(struct txn_limbo *limbo, int64_t lsn)
{
	assert(limbo->owner_id != REPLICA_ID_NIL || txn_limbo_is_empty(limbo));
	assert(limbo == &txn_limbo);
	assert(limbo->confirmed_lsn <= lsn);
	struct txn_limbo_entry *e, *next;
	rlist_foreach_entry_safe(e, &limbo->queue, in_queue, next) {
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
			if (!rlist_empty(&e->txn->on_commit)) {
				/*
				 * Bump the confirmed LSN right now, do not
				 * batch with any newer txns. So on-commit
				 * triggers would see the confirmation LSN
				 * matching this txn exactly. Making an illusion
				 * like each txn has its own confirmation.
				 */
				if (limbo->confirmed_lsn < e->lsn) {
					limbo->confirmed_lsn = e->lsn;
					vclock_follow(&limbo->confirmed_vclock,
						      limbo->owner_id, e->lsn);
				} else {
					assert(limbo->confirmed_lsn == lsn);
				}
			}
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
			if (e->state == TXN_LIMBO_ENTRY_VOLATILE) {
				if (e == txn_limbo_last_entry(limbo))
					continue;
				/*
				 * The invariant is that if found a volatile
				 * txn, then all newer txns are also volatile.
				 */
				assert(rlist_next_entry(e, in_queue) == next);
				assert(next->state == TXN_LIMBO_ENTRY_VOLATILE);
				continue;
			}
			assert(e->state == TXN_LIMBO_ENTRY_SUBMITTED);
			txn_limbo_pop_first(limbo, e);
			/*
			 * The limbo entry now should not be used by the owner
			 * transaction since it just became a plain one. Nullify
			 * the txn to get a crash on any usage attempt instead
			 * of potential undefined behaviour.
			 */
			e->txn->limbo_entry = NULL;
			e->txn = NULL;
			continue;
		}
		txn_limbo_complete_success(limbo, e);
	}
	if (limbo->confirmed_lsn < lsn) {
		limbo->confirmed_lsn = lsn;
		vclock_follow(&limbo->confirmed_vclock, limbo->owner_id, lsn);
	}
}

/** Confirm an LSN in the limbo. */
static void
txn_limbo_confirm_lsn(struct txn_limbo *limbo, int64_t confirm_lsn)
{
	assert(confirm_lsn > limbo->volatile_confirmed_lsn);
	limbo->volatile_confirmed_lsn = confirm_lsn;
	fiber_wakeup(limbo->worker);
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
	txn_limbo_write_synchro_or_panic(limbo, IPROTO_RAFT_ROLLBACK, lsn, 0);
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
		/*
		 * Should be written to WAL by now. Rollback is always written
		 * after the affected transactions.
		 */
		assert(e->txn->signature >= 0);
		txn_limbo_complete_fail(limbo, e, TXN_SIGNATURE_SYNC_ROLLBACK);
		if (e == last_rollback)
			break;
	}
}

int
txn_limbo_write_promote(struct txn_limbo *limbo, int64_t lsn, uint64_t term)
{
	assert(latch_is_locked(&limbo->promote_latch));
	/*
	 * We make sure that promote is only written once everything this
	 * instance has may be confirmed.
	 */
	struct txn_limbo_entry *e = txn_limbo_last_synchro_entry(limbo);
	assert(e == NULL || e->lsn <= lsn);
	(void) e;
	struct synchro_request req = {
		.type = IPROTO_RAFT_PROMOTE,
		.replica_id = limbo->owner_id,
		.origin_id = instance_id,
		.lsn = lsn,
		.term = term,
	};
	/*
	 * Confirmed_vclock is only persisted in checkpoints. It doesn't
	 * appear in WALs and replication.
	 */
	vclock_clear(&req.confirmed_vclock);
	if (txn_limbo_req_prepare(limbo, &req) < 0)
		return -1;
	synchro_request_write_or_panic(&req);
	txn_limbo_req_commit(limbo, &req);
	return 0;
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
	assert(txn_limbo_is_empty(limbo));
	limbo->owner_id = replica_id;
	limbo->confirmed_lsn = vclock_get(&limbo->confirmed_vclock,
					  replica_id);
	limbo->volatile_confirmed_lsn = limbo->confirmed_lsn;
	limbo->entry_to_confirm = NULL;
	box_update_ro_summary();
}

int
txn_limbo_write_demote(struct txn_limbo *limbo, int64_t lsn, uint64_t term)
{
	assert(latch_is_locked(&limbo->promote_latch));
	struct txn_limbo_entry *e = txn_limbo_last_synchro_entry(limbo);
	assert(e == NULL || e->lsn <= lsn);
	(void)e;
	struct synchro_request req = {
		.type = IPROTO_RAFT_DEMOTE,
		.replica_id = limbo->owner_id,
		.origin_id = instance_id,
		.lsn = lsn,
		.term = term,
	};
	vclock_clear(&req.confirmed_vclock);
	if (txn_limbo_req_prepare(limbo, &req) < 0)
		return -1;
	synchro_request_write_or_panic(&req);
	txn_limbo_req_commit(limbo, &req);
	return 0;
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

/**
 * Check that some synchronous transactions have gathered quorum and
 * write a confirmation entry of the last confirmed transaction.
 */
static void
txn_limbo_confirm(struct txn_limbo *limbo)
{
	assert(txn_limbo_is_owned_by_current_instance(limbo));
	if (txn_limbo_is_frozen(limbo))
		return;
	if (limbo->is_in_rollback)
		return;
	if (limbo->entry_to_confirm == NULL ||
	    limbo->entry_to_confirm->lsn == -1)
		return;
	if (limbo->ack_count < replication_synchro_quorum)
		return;
	int32_t k = (int32_t)vclock_size(&limbo->vclock)
		    - replication_synchro_quorum;
	/**
	 * limbo->ack_count >= replication_synchro_quorum =>
	 * vclock_size(&limbo->vclock) >= replication_synchro_quorum
	 */
	assert(k >= 0);
	int64_t confirm_lsn = vclock_nth_element(&limbo->vclock, k);
	assert(confirm_lsn >= limbo->entry_to_confirm->lsn);
	struct txn_limbo_entry *e = limbo->entry_to_confirm;
	limbo->entry_to_confirm = NULL;
	int64_t max_assigned_lsn = -1;
	for (; !rlist_entry_is_head(e, &limbo->queue, in_queue);
	       e = rlist_next_entry(e, in_queue)) {
		if (!txn_has_flag(e->txn, TXN_WAIT_ACK))
			continue;
		if (e->lsn == -1 || e->lsn > confirm_lsn) {
			limbo->entry_to_confirm = e;
			/**
			 * It may be that a quorum has been gathered, but
			 * ack_count = 0. It's ok. CONFIRM will be written as
			 * soon as the lsn is assigned to the transaction.
			 */
			limbo->ack_count = (e->lsn == -1) ? 0 :
				vclock_count_ge(&limbo->vclock, e->lsn);
			break;
		} else {
			max_assigned_lsn = e->lsn;
		}
	}
	assert(max_assigned_lsn != -1);
	txn_limbo_confirm_lsn(limbo, max_assigned_lsn);
}

void
txn_limbo_ack(struct txn_limbo *limbo, uint32_t replica_id, int64_t lsn)
{
	assert(txn_limbo_is_owned_by_current_instance(limbo));
	if (rlist_empty(&limbo->queue))
		return;
	if (txn_limbo_is_frozen(limbo))
		return;
	assert(!txn_limbo_is_ro(limbo));
	assert(limbo->owner_id != REPLICA_ID_NIL);
	int64_t prev_lsn = vclock_get(&limbo->vclock, replica_id);
	assert(lsn >= prev_lsn);
	/*
	 * One of the reasons why can happen - the remote instance is not
	 * read-only and wrote something under its own instance_id. For qsync
	 * that most likely means that the remote instance decided to take over
	 * the limbo ownership, and the current node is going to become a
	 * replica very soon.
	 */
	if (lsn == prev_lsn)
		return;
	vclock_follow(&limbo->vclock, replica_id, lsn);

	if (limbo->entry_to_confirm != NULL &&
	    limbo->entry_to_confirm->lsn != -1) {
		if (limbo->entry_to_confirm->lsn <= prev_lsn ||
		    lsn < limbo->entry_to_confirm->lsn)
			return;
		if (++limbo->ack_count >= replication_synchro_quorum)
			txn_limbo_confirm(limbo);
	}
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

int
txn_limbo_wait_last_txn(struct txn_limbo *limbo, bool *is_rollback,
			double timeout)
{
	struct txn_limbo_entry *tle = txn_limbo_last_synchro_entry(limbo);
	if (tle == NULL) {
		*is_rollback = false;
		return 0;
	}

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
	txn_on_commit(tle->txn, &on_complete);
	txn_on_rollback(tle->txn, &on_rollback);
	double deadline = fiber_clock() + timeout;
	int rc;
	while (true) {
		if (timeout < 0) {
			rc = -1;
			break;
		}
		rc = fiber_cond_wait_timeout(&limbo->wait_cond, timeout);
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

static int
txn_write_cb(struct trigger *trigger, void *event)
{
	(void)event;
	struct fiber *fiber = (struct fiber *)trigger->data;
	fiber_wakeup(fiber);
	return 0;
}

/**
 * Wait until all the limbo entries receive an lsn.
 */
static int
txn_limbo_wait_persisted(struct txn_limbo *limbo)
{
	if (txn_limbo_is_empty(limbo))
		return 0;
	struct txn_limbo_entry *e = txn_limbo_last_entry(limbo);
	while (e != NULL && e->lsn <= 0) {
		struct trigger on_wal_write;
		trigger_create(&on_wal_write, txn_write_cb, fiber(), NULL);
		txn_on_wal_write(e->txn, &on_wal_write);
		fiber_yield();
		trigger_clear(&on_wal_write);
		if (fiber_is_cancelled()) {
			diag_set(FiberIsCancelled);
			return -1;
		}
		e = txn_limbo_last_entry(limbo);
	}
	return 0;
}

/**
 * Fill the reject reason with request data.
 * The function is not reenterable, use with care.
 */
static const char *
reject_str(const struct synchro_request *req)
{
	const char *type_name = iproto_type_name(req->type);

	return tt_sprintf("RAFT: rejecting %s (%d) request from origin_id %u "
			  "replica_id %u term %llu",
			  type_name ? type_name : "UNKNOWN", req->type,
			  req->origin_id, req->replica_id,
			  (long long)req->term);
}

/**
 * Common filter for any incoming packet.
 */
static int
txn_limbo_filter_generic(struct txn_limbo *limbo,
			 const struct synchro_request *req)
{
	assert(latch_is_locked(&limbo->promote_latch));

	if (!limbo->do_validate)
		return 0;

	/*
	 * Zero replica_id is allowed for PROMOTE packets only.
	 */
	if (req->replica_id == REPLICA_ID_NIL) {
		if (req->type != IPROTO_RAFT_PROMOTE) {
			say_error("%s. Zero replica_id detected",
				  reject_str(req));
			diag_set(ClientError, ER_UNSUPPORTED, "Replication",
				 "synchronous requests with zero replica_id");
			return -1;
		}
	}
	if (req->replica_id != limbo->owner_id) {
		/*
		 * Incoming packets should esteem limbo owner,
		 * if it doesn't match it means the sender
		 * missed limbo owner migrations and is out of date.
		 */
		say_error("%s. Limbo owner mismatch, owner_id %u",
			  reject_str(req), limbo->owner_id);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request from a foreign synchro queue owner");
		return -1;
	}

	return 0;
}

/**
 * Filter CONFIRM and ROLLBACK packets.
 */
static int
txn_limbo_filter_confirm_rollback(struct txn_limbo *limbo,
				  const struct synchro_request *req)
{
	assert(latch_is_locked(&limbo->promote_latch));
	assert(limbo->do_validate);
	(void)limbo;
	assert(req->type == IPROTO_RAFT_CONFIRM ||
	       req->type == IPROTO_RAFT_ROLLBACK);
	/*
	 * Zero LSN is allowed for PROMOTE and DEMOTE requests only.
	 */
	if (req->lsn == 0) {
		say_error("%s. Zero lsn detected", reject_str(req));
		diag_set(ClientError, ER_UNSUPPORTED, "Replication",
			 "zero LSN for CONFIRM/ROLLBACK");
		return -1;
	}
	return 0;
}

/** A filter PROMOTE and DEMOTE packets. */
static int
txn_limbo_filter_promote_demote(struct txn_limbo *limbo,
				const struct synchro_request *req)
{
	assert(latch_is_locked(&limbo->promote_latch));
	assert(limbo->do_validate);
	assert(iproto_type_is_promote_request(req->type));
	/*
	 * PROMOTE and DEMOTE packets must not have zero
	 * term supplied, otherwise it is a broken packet.
	 */
	if (req->term == 0) {
		say_error("%s. Zero term detected", reject_str(req));
		diag_set(ClientError, ER_UNSUPPORTED,
			 "Replication", "PROMOTE/DEMOTE with a zero term");
		return -1;
	}

	/*
	 * If the term is already seen it means it comes
	 * from a node which didn't notice new elections,
	 * thus been living in subdomain and its data is
	 * no longer consistent.
	 */
	if (limbo->promote_greatest_term >= req->term) {
		say_error("%s. Max term seen is %llu", reject_str(req),
			  (long long)limbo->promote_greatest_term);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a PROMOTE/DEMOTE with an obsolete term");
		return -1;
	}
	/*
	 * Explicit split brain situation. Request comes in with an old LSN
	 * which we've already processed.
	 */
	if (limbo->confirmed_lsn > req->lsn) {
		say_error("%s. confirmed lsn %lld > request lsn %lld",
			  reject_str(req), (long long)limbo->confirmed_lsn,
			  (long long)req->lsn);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request with lsn from an already "
			 "processed range");
		return -1;
	}
	/*
	 * Easy case - processed LSN matches the new one which comes inside
	 * request, everything is consistent. This is allowed only for
	 * PROMOTE/DEMOTE.
	 */
	if (limbo->confirmed_lsn == req->lsn)
		return 0;
	/*
	 * The last case requires a few subcases.
	 */
	if (txn_limbo_is_empty(limbo)) {
		/*
		 * Transactions are rolled back already,
		 * since the limbo is empty.
		 */
		say_error("%s. confirmed lsn %lld < request lsn %lld "
			  "and empty limbo", reject_str(req),
			  (long long)limbo->confirmed_lsn,
			  (long long)req->lsn);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request mentioning future lsn");
		return -1;
	}
	/*
	 * Some entries are present in the limbo, we need to make sure that
	 * request lsn lays inside limbo [first; last] range. So that the
	 * request has some queued data to process, otherwise it means the
	 * request comes from split brained node.
	 */
	int64_t first_lsn = txn_limbo_first_entry(limbo)->lsn;
	int64_t last_lsn = txn_limbo_last_synchro_entry(limbo)->lsn;
	if (req->lsn < first_lsn || last_lsn < req->lsn) {
		say_error("%s. request lsn %lld out of range "
			  "[%lld; %lld]", reject_str(req),
			  (long long)req->lsn,
			  (long long)first_lsn,
			  (long long)last_lsn);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request lsn out of queue range");
		return -1;
	}
	return 0;
}

/** A fine-grained filter checking specific request type constraints. */
static int
txn_limbo_filter_request(struct txn_limbo *limbo,
			 const struct synchro_request *req)
{
	if (!limbo->do_validate)
		return 0;
	/*
	 * Wait until all the entries receive an lsn. The lsn will be
	 * used to determine whether filtered request is safe to apply.
	 */
	if (txn_limbo_wait_persisted(limbo) < 0)
		return -1;
	switch (req->type) {
	case IPROTO_RAFT_CONFIRM:
	case IPROTO_RAFT_ROLLBACK:
		return txn_limbo_filter_confirm_rollback(limbo, req);
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE:
		return txn_limbo_filter_promote_demote(limbo, req);
	default:
		unreachable();
	}
}

/**
 * Update the state of synchronous replication for system spaces.
 */
static void
txn_limbo_update_system_spaces_is_sync_state(struct txn_limbo *limbo,
					     const struct synchro_request *req,
					     bool is_rollback)
{
	/* Do not enable synchronous replication during bootstrap. */
	if (req->origin_id == REPLICA_ID_NIL)
		return;
	uint16_t req_type = req->type;
	assert(req_type == IPROTO_RAFT_PROMOTE ||
	       req_type == IPROTO_RAFT_DEMOTE);
	bool is_promote = req_type == IPROTO_RAFT_PROMOTE;
	/* Synchronous replication is already enabled. */
	if (is_promote && limbo->owner_id != REPLICA_ID_NIL)
		return;
	/* Synchronous replication is already disabled. */
	if (!is_promote && limbo->owner_id == REPLICA_ID_NIL) {
		assert(!is_rollback);
		return;
	}
	/* Flip operation types for a rollback. */
	if (is_rollback)
		is_promote = !is_promote;
	system_spaces_update_is_sync_state(is_promote);
}

int
txn_limbo_req_prepare(struct txn_limbo *limbo,
		      const struct synchro_request *req)
{
	assert(latch_is_locked(&limbo->promote_latch));

	if (txn_limbo_filter_generic(limbo, req) < 0)
		return -1;

	/*
	 * Guard against new transactions appearing during WAL write. It is
	 * necessary because otherwise when PROMOTE/DEMOTE would be done and it
	 * would see a txn without LSN in the limbo, it couldn't tell whether
	 * the transaction should be confirmed or rolled back. It could be
	 * delivered to the PROMOTE/DEMOTE initiator even before than to the
	 * local TX thread, or could be not.
	 *
	 * CONFIRM and ROLLBACK need this guard only during  the filter stage.
	 * Because the filter needs to see all the transactions LSNs to work
	 * correctly.
	 */
	assert(!limbo->is_in_rollback);
	limbo->is_in_rollback = true;
	if (txn_limbo_filter_request(limbo, req) < 0) {
		limbo->is_in_rollback = false;
		return -1;
	}
	/* Prepare for request execution and fine-grained filtering. */
	switch (req->type) {
	case IPROTO_RAFT_CONFIRM:
	case IPROTO_RAFT_ROLLBACK:
		limbo->is_in_rollback = false;
		break;
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE: {
		assert(limbo->svp_confirmed_lsn == -1);
		limbo->svp_confirmed_lsn = limbo->volatile_confirmed_lsn;
		limbo->volatile_confirmed_lsn = req->lsn;
		txn_limbo_update_system_spaces_is_sync_state(
			limbo, req, /*is_rollback=*/false);
		break;
	}
	/*
	 * XXX: ideally all requests should go through req_* methods. To unify
	 * their work from applier and locally.
	 */
	}
	return 0;
}

void
txn_limbo_req_rollback(struct txn_limbo *limbo,
		       const struct synchro_request *req)
{
	assert(latch_is_locked(&limbo->promote_latch));
	switch (req->type) {
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE: {
		assert(limbo->is_in_rollback);
		assert(limbo->svp_confirmed_lsn >= 0);
		limbo->volatile_confirmed_lsn = limbo->svp_confirmed_lsn;
		limbo->svp_confirmed_lsn = -1;
		txn_limbo_update_system_spaces_is_sync_state(
			limbo, req, /*is_rollback=*/true);
		limbo->is_in_rollback = false;
		break;
	}
	/*
	 * XXX: ideally all requests should go through req_* methods. To unify
	 * their work from applier and locally.
	 */
	default: {
		break;
	}
	}
}

/** Unfreeze the limbo encountering the first new PROMOTE after a restart. */
static inline void
txn_limbo_unfreeze_on_first_promote(struct txn_limbo *limbo)
{
	if (box_is_configured()) {
		limbo->is_frozen_until_promotion = false;
		box_update_ro_summary();
	}
}

void
txn_limbo_req_commit(struct txn_limbo *limbo, const struct synchro_request *req)
{
	assert(latch_is_locked(&limbo->promote_latch));
	switch (req->type) {
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE: {
		assert(limbo->svp_confirmed_lsn >= 0);
		assert(limbo->is_in_rollback);
		limbo->svp_confirmed_lsn = -1;
		limbo->is_in_rollback = false;
		break;
	}
	default: {
		break;
	}
	}

	uint64_t term = req->term;
	uint32_t origin = req->origin_id;
	if (txn_limbo_replica_term(limbo, origin) < term) {
		vclock_follow(&limbo->promote_term_map, origin, term);
		if (term > limbo->promote_greatest_term) {
			limbo->promote_greatest_term = term;
			if (iproto_type_is_promote_request(req->type)) {
				if (term >= box_raft()->volatile_term)
					txn_limbo_unfence(limbo);
				txn_limbo_unfreeze_on_first_promote(&txn_limbo);
			}
		}
	}
	if (vclock_is_set(&req->confirmed_vclock))
		vclock_copy(&limbo->confirmed_vclock, &req->confirmed_vclock);

	int64_t lsn = req->lsn;
	switch (req->type) {
	case IPROTO_RAFT_CONFIRM:
		txn_limbo_read_confirm(limbo, lsn);
		break;
	case IPROTO_RAFT_ROLLBACK:
		txn_limbo_read_rollback(limbo, lsn);
		break;
	case IPROTO_RAFT_PROMOTE:
		txn_limbo_read_promote(limbo, req->origin_id, lsn);
		break;
	case IPROTO_RAFT_DEMOTE:
		txn_limbo_read_demote(limbo, lsn);
		break;
	default:
		unreachable();
	}
	return;
}

int
txn_limbo_process(struct txn_limbo *limbo, const struct synchro_request *req)
{
	txn_limbo_begin(limbo);
	if (txn_limbo_req_prepare(limbo, req) < 0) {
		txn_limbo_rollback(limbo);
		return -1;
	}
	txn_limbo_req_commit(limbo, req);
	txn_limbo_commit(limbo);
	return 0;
}

void
txn_limbo_on_parameters_change(struct txn_limbo *limbo)
{
	if (rlist_empty(&limbo->queue))
		return;
	/* The replication_synchro_quorum value may have changed. */
	if (txn_limbo_is_owned_by_current_instance(limbo))
		txn_limbo_confirm(limbo);
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
txn_limbo_fence(struct txn_limbo *limbo)
{
	limbo->is_frozen_due_to_fencing = true;
	box_update_ro_summary();
}

void
txn_limbo_unfence(struct txn_limbo *limbo)
{
	limbo->is_frozen_due_to_fencing = false;
	box_update_ro_summary();
}

void
txn_limbo_filter_enable(struct txn_limbo *limbo)
{
	latch_lock(&limbo->promote_latch);
	limbo->do_validate = true;
	latch_unlock(&limbo->promote_latch);
}

void
txn_limbo_filter_disable(struct txn_limbo *limbo)
{
	latch_lock(&limbo->promote_latch);
	limbo->do_validate = false;
	latch_unlock(&limbo->promote_latch);
}

void
txn_limbo_init(void)
{
	txn_limbo_create(&txn_limbo);
}

void
txn_limbo_free(void)
{
	txn_limbo_destroy(&txn_limbo);
}

void
txn_limbo_shutdown(void)
{
	txn_limbo_stop(&txn_limbo);
}
