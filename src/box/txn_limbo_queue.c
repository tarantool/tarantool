#include "txn_limbo_queue.h"

#include "box.h"
#include "session.h"
#include "txn.h"

/*******************************************************************************
 * Private API
 ******************************************************************************/

/**
 * Waitpoint stores information about the progress of confirmation.
 * In the case of multimaster support, it will store a bitset
 * or array instead of the boolean.
 */
struct confirm_waitpoint {
	/** Fiber that is waiting for the end of confirmation. */
	struct fiber *caller;
	/** True if confirmed. */
	bool is_confirm;
	/** True if rolled back. */
	bool is_rollback;
};

static struct txn_limbo_entry *
txn_limbo_queue_first_entry(struct txn_limbo_queue *queue)
{
	return rlist_first_entry(&queue->entries, struct txn_limbo_entry,
				 in_queue);
}

static struct txn_limbo_entry *
txn_limbo_queue_last_entry(struct txn_limbo_queue *queue)
{
	return rlist_last_entry(&queue->entries, struct txn_limbo_entry,
				in_queue);
}

static bool
txn_limbo_queue_is_full(struct txn_limbo_queue *queue)
{
	return queue->size >= queue->max_size;
}

/** Decrease queue size once write request is complete. */
static void
txn_limbo_queue_on_remove(struct txn_limbo_queue *queue,
			  const struct txn_limbo_entry *entry)
{
	queue->size -= entry->approx_len;
	assert(queue->size >= 0);
	assert(queue->len > 0);
	queue->len--;
}

/** Increase queue size on a new write request. */
static void
txn_limbo_queue_on_append(struct txn_limbo_queue *queue,
			  const struct txn_limbo_entry *entry)
{
	assert(entry->state == TXN_LIMBO_ENTRY_SUBMITTED);
	queue->size += entry->approx_len;
	queue->len++;
}

/** Pop the first entry. */
static void
txn_limbo_queue_pop_first(struct txn_limbo_queue *queue, struct txn_limbo_entry *entry)
{
	assert(!rlist_empty(&entry->in_queue));
	assert(txn_limbo_queue_first_entry(queue) == entry);
	rlist_del_entry(entry, in_queue);
	txn_limbo_queue_on_remove(queue, entry);
}

/** Complete the given transaction. */
static void
txn_limbo_queue_complete(struct txn *txn, bool is_success)
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
txn_limbo_queue_complete_fail(struct txn_limbo_queue *queue,
			      struct txn_limbo_entry *entry, int64_t signature)
{
	assert(entry->state == TXN_LIMBO_ENTRY_SUBMITTED ||
	       entry->state == TXN_LIMBO_ENTRY_VOLATILE);
	struct txn *txn = entry->txn;
	txn->signature = signature;
	txn->limbo_entry = NULL;
	txn_limbo_queue_abort(queue, entry);
	txn_clear_flags(txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
	txn_limbo_queue_complete(txn, false);
}

/** Complete the given limbo entry with a success. */
static void
txn_limbo_queue_complete_success(struct txn_limbo_queue *queue,
				 struct txn_limbo_entry *entry)
{
	assert(entry->state == TXN_LIMBO_ENTRY_SUBMITTED);
	struct txn *txn = entry->txn;
	entry->state = TXN_LIMBO_ENTRY_COMMIT;
	if (txn_has_flag(txn, TXN_WAIT_ACK))
		queue->confirm_lag = fiber_clock() - entry->insertion_time;
	txn->limbo_entry = NULL;
	txn_limbo_queue_pop_first(queue, entry);
	txn_clear_flags(txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
	/*
	 * Should be written to WAL by now. Confirm is always written after the
	 * affected transactions.
	 */
	assert(txn->signature >= 0);
	txn_limbo_queue_complete(txn, true);
}

/** Cascade-rollback all the entries from the newest to the given one. */
static void
txn_limbo_queue_rollback_volatile_up_to(struct txn_limbo_queue *queue,
					struct txn_limbo_entry *last)
{
	assert(last == NULL || last->state == TXN_LIMBO_ENTRY_VOLATILE);
	while (!txn_limbo_queue_is_empty(queue)) {
		struct txn_limbo_entry *e = txn_limbo_queue_last_entry(queue);
		if (e == last || e->state != TXN_LIMBO_ENTRY_VOLATILE)
			break;
		txn_limbo_queue_complete_fail(queue, e, TXN_SIGNATURE_CASCADE);
	}
}

/**
 * Assign a remote LSN to a limbo entry. That happens when a
 * remote transaction is added to the limbo and starts waiting for
 * a confirm.
 */
static void
txn_limbo_queue_assign_remote_lsn(struct txn_limbo_queue *queue,
				  struct txn_limbo_entry *entry, int64_t lsn)
{
	VERIFY(queue->owner_id != REPLICA_ID_NIL);
	assert(!txn_limbo_queue_is_owned_by_current_instance(queue));
	assert(entry->lsn == -1);
	assert(lsn > 0);
	/*
	 * Same as with local LSN assign, it is given after a WAL write. But for
	 * remotely received transactions it doesn't matter so far. They don't
	 * needs ACKs. They wait for explicit confirmations. That will be a
	 * problem when need acks for anything else and when local txns will
	 * become optionally non-blocking.
	 */
	entry->lsn = lsn;
}

/**
 * Assign a local LSN to a limbo entry. That happens when a local
 * transaction is written to WAL.
 */
static void
txn_limbo_queue_assign_local_lsn(struct txn_limbo_queue *queue,
				 struct txn_limbo_entry *entry, int64_t lsn)
{
	assert(queue->owner_id != REPLICA_ID_NIL);
	assert(txn_limbo_queue_is_owned_by_current_instance(queue));
	assert(entry->lsn == -1);
	assert(lsn > 0);
	entry->lsn = lsn;
	if (entry == queue->entry_to_confirm)
		queue->ack_count = vclock_count_ge(&queue->vclock, entry->lsn);
}

static int
txn_write_cb(struct trigger *trigger, void *event)
{
	(void)event;
	struct fiber *fiber = (struct fiber *)trigger->data;
	fiber_wakeup(fiber);
	return 0;
}

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

/*******************************************************************************
 * Public API
 ******************************************************************************/

double
txn_limbo_queue_age(struct txn_limbo_queue *queue)
{
	if (txn_limbo_queue_is_empty(queue))
		return 0;
	return fiber_clock() -
	       txn_limbo_queue_first_entry(queue)->insertion_time;
}

struct txn_limbo_entry *
txn_limbo_queue_last_synchro_entry(struct txn_limbo_queue *queue)
{
	struct txn_limbo_entry *entry;
	rlist_foreach_entry_reverse(entry, &queue->entries, in_queue) {
		if (txn_has_flag(entry->txn, TXN_WAIT_ACK))
			return entry;
	}
	return NULL;
}

bool
txn_limbo_queue_would_block(struct txn_limbo_queue *queue)
{
	if (txn_limbo_queue_is_full(queue)) {
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
		if (txn_limbo_queue_is_owned_by_current_instance(queue))
			return true;
	}
	if (txn_limbo_queue_is_empty(queue))
		return false;
	/*
	 * Might be not full, but still have a volatile entry in the end. Could
	 * be caused by some spurious wakeups of the entries' fibers in an
	 * unexpected order. Either way, the new submission will have to wait
	 * until the previous one gets submitted.
	 */
	return txn_limbo_queue_last_entry(queue)->state ==
	       TXN_LIMBO_ENTRY_VOLATILE;
}

int
txn_limbo_queue_submit(struct txn_limbo_queue *queue, uint32_t origin_id,
		       struct txn *txn, size_t approx_len)
{
	assert(txn_has_flag(txn, TXN_WAIT_SYNC));
	/*
	 * Transactions should be added to the limbo before WAL write. Limbo
	 * needs that to be able rollback transactions, whose WAL write is in
	 * progress.
	 */
	assert(txn->signature == TXN_SIGNATURE_UNKNOWN);
	assert(txn->status == TXN_PREPARED);
	if (queue->owner_id == REPLICA_ID_NIL) {
		diag_set(ClientError, ER_SYNC_QUEUE_UNCLAIMED);
		return -1;
	} else if (queue->owner_id != origin_id && !txn_is_fully_local(txn)) {
		if (txn_limbo_queue_is_empty(queue)) {
			diag_set(ClientError, ER_SYNC_QUEUE_FOREIGN,
				 queue->owner_id);
		} else {
			diag_set(ClientError, ER_UNCOMMITTED_FOREIGN_SYNC_TXNS,
				 queue->owner_id);
		}
		return -1;
	}
	size_t size;
	struct txn_limbo_entry *e = region_alloc_object(&txn->region,
							typeof(*e), &size);
	if (queue->entry_to_confirm == NULL &&
	    txn_has_flag(txn, TXN_WAIT_ACK)) {
		queue->entry_to_confirm = e;
		queue->ack_count = 0;
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
	bool would_block = txn_limbo_queue_would_block(queue);
	rlist_add_tail_entry(&queue->entries, e, in_queue);
	if (!would_block)
		goto success;
	e->state = TXN_LIMBO_ENTRY_VOLATILE;
	while (true) {
		bool ok = !fiber_is_cancelled() &&
			  fiber_cond_wait(&queue->cond) == 0;
		if (e->state == TXN_LIMBO_ENTRY_ROLLBACK) {
			/* Cascading rollback. */
			fiber_set_txn(fiber(), NULL);
			diag_set(ClientError, ER_SYNC_ROLLBACK);
			return -1;
		}
		if (!ok) {
			fiber_set_txn(fiber(), NULL);
			txn_limbo_queue_rollback_volatile_up_to(queue, e);
			txn_limbo_queue_complete_fail(queue, e,
						      TXN_SIGNATURE_CANCELLED);
			assert(e->state == TXN_LIMBO_ENTRY_ROLLBACK);
			diag_set(ClientError, ER_SYNC_ROLLBACK);
			return -1;
		}
		/* Could be a spurious wakeup. */
		if (txn_limbo_queue_is_full(queue))
			continue;
		if (txn_limbo_queue_first_entry(queue) == e)
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
	if (txn_limbo_queue_last_entry(queue) != e) {
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
		assert(txn_limbo_queue_first_entry(queue) == e);
		rlist_del_entry(e, in_queue);
		return 0;
	}
success:
	e->state = TXN_LIMBO_ENTRY_SUBMITTED;
	txn_limbo_queue_on_append(queue, e);
	return 0;
}

int
txn_limbo_queue_flush(struct txn_limbo_queue *queue)
{
	/* Fast path. */
	if (txn_limbo_queue_is_empty(queue))
		return 0;
	if (txn_limbo_queue_last_entry(queue)->state != TXN_LIMBO_ENTRY_VOLATILE)
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
	int rc = txn_limbo_queue_submit(queue, queue->owner_id, txn, 0);
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
			assert(txn->limbo_entry != queue->entry_to_confirm);
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
txn_limbo_queue_abort(struct txn_limbo_queue *queue, struct txn_limbo_entry *entry)
{
	assert(!rlist_empty(&entry->in_queue));
	/*
	 * The simple rule about rollback/commit order applies
	 * here as well: commit always in the order of WAL write,
	 * rollback in the reversed order. Rolled back transaction
	 * is always the last.
	 */
	assert(txn_limbo_queue_last_entry(queue) == entry);
	bool was_volatile = entry->state == TXN_LIMBO_ENTRY_VOLATILE;
	assert(was_volatile || entry->state == TXN_LIMBO_ENTRY_SUBMITTED);
	entry->state = TXN_LIMBO_ENTRY_ROLLBACK;
	if (entry == queue->entry_to_confirm)
		queue->entry_to_confirm = NULL;
	rlist_del_entry(entry, in_queue);
	if (!was_volatile)
		txn_limbo_queue_on_remove(queue, entry);
}

void
txn_limbo_queue_assign_lsn(struct txn_limbo_queue *queue,
			   struct txn_limbo_entry *entry, int64_t lsn)
{
	if (txn_limbo_queue_is_owned_by_current_instance(queue))
		txn_limbo_queue_assign_local_lsn(queue, entry, lsn);
	else
		txn_limbo_queue_assign_remote_lsn(queue, entry, lsn);
}

enum txn_limbo_wait_entry_result
txn_limbo_queue_wait_complete(struct txn_limbo_queue *queue,
			      struct txn_limbo_entry *entry)
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
		int rc = fiber_cond_wait_timeout(&queue->cond, timeout_rest);
		if (txn_limbo_entry_is_complete(entry))
			goto complete;
		if (rc != 0 && fiber_is_cancelled())
			return TXN_LIMBO_WAIT_ENTRY_FAIL_DETACH;
		if (rc != 0)
			break;
	}

	assert(!txn_limbo_queue_is_empty(queue));

	if (!replication_synchro_timeout_rollback_enabled) {
		diag_set(ClientError, ER_SYNC_TIMEOUT);
		return TXN_LIMBO_WAIT_ENTRY_FAIL_DETACH;
	}

	struct txn_limbo_entry *e;
	bool is_first_waiting_entry = true;
	rlist_foreach_entry(e, &queue->entries, in_queue) {
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
	if (entry->lsn <= queue->volatile_confirmed_lsn) {
		/*
		 * Yes, the wait timed out, but there is an on-going CONFIRM WAL
		 * write in another fiber covering this LSN. Can't rollback it
		 * already. All what can be done is waiting. The CONFIRM writer
		 * will wakeup all the confirmed txns when WAL write will be
		 * finished.
		 */
		goto wait;
	}
	return TXN_LIMBO_WAIT_ENTRY_NEED_ROLLBACK;

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
		return TXN_LIMBO_WAIT_ENTRY_FAIL_COMPLETE;
	}
	return TXN_LIMBO_WAIT_ENTRY_SUCCESS;
}

void
txn_limbo_queue_get_lsn_range(struct txn_limbo_queue *queue, int64_t *first_lsn,
			      int64_t *last_lsn)
{
	assert(!txn_limbo_queue_is_empty(queue));
	*first_lsn = txn_limbo_queue_first_entry(queue)->lsn;
	*last_lsn = txn_limbo_queue_last_synchro_entry(queue)->lsn;
}

void
txn_limbo_queue_apply_confirm(struct txn_limbo_queue *queue, int64_t lsn)
{
	assert(queue->owner_id != REPLICA_ID_NIL ||
	       txn_limbo_queue_is_empty(queue));
	assert(queue->confirmed_lsn <= lsn);

	bool queue_was_full = txn_limbo_queue_is_full(queue);
	struct txn_limbo_entry *e, *next;
	rlist_foreach_entry_safe(e, &queue->entries, in_queue, next) {
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
				if (queue->confirmed_lsn < e->lsn) {
					queue->confirmed_lsn = e->lsn;
					vclock_follow(&queue->confirmed_vclock,
						      queue->owner_id, e->lsn);
				} else {
					assert(queue->confirmed_lsn == lsn);
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
				if (e == txn_limbo_queue_last_entry(queue))
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
			txn_limbo_queue_pop_first(queue, e);
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
		txn_limbo_queue_complete_success(queue, e);
	}
	if (queue->confirmed_lsn < lsn) {
		queue->confirmed_lsn = lsn;
		vclock_follow(&queue->confirmed_vclock, queue->owner_id, lsn);
	}
	if (queue_was_full && !txn_limbo_queue_is_full(queue))
		fiber_cond_broadcast(&queue->cond);
}

void
txn_limbo_queue_apply_rollback(struct txn_limbo_queue *queue, int64_t lsn,
			       int64_t signature)
{
	assert(queue->owner_id != REPLICA_ID_NIL ||
	       txn_limbo_queue_is_empty(queue));
	struct txn_limbo_entry *e;
	struct txn_limbo_entry *last_rollback = NULL;
	rlist_foreach_entry_reverse(e, &queue->entries, in_queue) {
		if (!txn_has_flag(e->txn, TXN_WAIT_ACK))
			continue;
		if (e->lsn < lsn)
			break;
		last_rollback = e;
	}
	if (last_rollback == NULL)
		return;
	while (!txn_limbo_queue_is_empty(queue)) {
		struct txn_limbo_entry *e = txn_limbo_queue_last_entry(queue);
		txn_limbo_queue_complete_fail(queue, e, signature);
		if (e == last_rollback)
			return;
	}
	unreachable();
}

void
txn_limbo_queue_transfer_ownership(struct txn_limbo_queue *queue,
				   uint32_t new_owner_id, int64_t border_lsn)
{
	txn_limbo_queue_apply_confirm(queue, border_lsn);
	txn_limbo_queue_apply_rollback(queue, border_lsn + 1,
				       TXN_SIGNATURE_SYNC_ROLLBACK);
	assert(txn_limbo_queue_is_empty(queue));
	queue->owner_id = new_owner_id;
	queue->confirmed_lsn = vclock_get(&queue->confirmed_vclock,
					  new_owner_id);
	queue->volatile_confirmed_lsn = queue->confirmed_lsn;
	queue->entry_to_confirm = NULL;
}

bool
txn_limbo_queue_ack(struct txn_limbo_queue *queue, uint32_t replica_id,
		    int64_t lsn)
{
	if (rlist_empty(&queue->entries))
		return false;
	assert(queue->owner_id != REPLICA_ID_NIL);
	int64_t prev_lsn = vclock_get(&queue->vclock, replica_id);
	assert(lsn >= prev_lsn);
	/*
	 * One of the reasons why can happen - the remote instance is not
	 * read-only and wrote something under its own instance_id. For qsync
	 * that most likely means that the remote instance decided to take over
	 * the limbo ownership, and the current node is going to become a
	 * replica very soon.
	 */
	if (lsn == prev_lsn)
		return false;
	vclock_follow(&queue->vclock, replica_id, lsn);

	if (queue->entry_to_confirm == NULL ||
	    queue->entry_to_confirm->lsn < 0)
		return false;
	if (queue->entry_to_confirm->lsn <= prev_lsn ||
	    lsn < queue->entry_to_confirm->lsn)
		return false;
	++queue->ack_count;
	return txn_limbo_queue_bump_volatile_confirm(queue);
}

bool
txn_limbo_queue_bump_volatile_confirm(struct txn_limbo_queue *queue)
{
	assert(txn_limbo_queue_is_owned_by_current_instance(queue));
	if (queue->entry_to_confirm == NULL ||
	    queue->entry_to_confirm->lsn == -1)
		return false;
	if (queue->ack_count < replication_synchro_quorum)
		return false;
	int32_t k = (int32_t)vclock_size(&queue->vclock)
		    - replication_synchro_quorum;
	/**
	 * queue->ack_count >= replication_synchro_quorum =>
	 * vclock_size(&queue->vclock) >= replication_synchro_quorum
	 */
	assert(k >= 0);
	int64_t confirm_lsn = vclock_nth_element(&queue->vclock, k);
	assert(confirm_lsn >= queue->entry_to_confirm->lsn);
	struct txn_limbo_entry *e = queue->entry_to_confirm;
	queue->entry_to_confirm = NULL;
	int64_t max_assigned_lsn = -1;
	for (; !rlist_entry_is_head(e, &queue->entries, in_queue);
	       e = rlist_next_entry(e, in_queue)) {
		if (!txn_has_flag(e->txn, TXN_WAIT_ACK))
			continue;
		if (e->lsn == -1 || e->lsn > confirm_lsn) {
			queue->entry_to_confirm = e;
			/**
			 * It may be that a quorum has been gathered, but
			 * ack_count = 0. It's ok. CONFIRM will be written as
			 * soon as the lsn is assigned to the transaction.
			 */
			queue->ack_count = (e->lsn == -1) ? 0 :
				vclock_count_ge(&queue->vclock, e->lsn);
			break;
		} else {
			max_assigned_lsn = e->lsn;
		}
	}
	assert(max_assigned_lsn != -1);
	assert(max_assigned_lsn > queue->volatile_confirmed_lsn);
	queue->volatile_confirmed_lsn = max_assigned_lsn;
	return true;
}

int
txn_limbo_queue_wait_last_txn(struct txn_limbo_queue *queue, bool *is_rollback,
			      double timeout)
{
	struct txn_limbo_entry *tle = txn_limbo_queue_last_synchro_entry(queue);
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
		rc = fiber_cond_wait_timeout(&queue->cond, timeout);
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
txn_limbo_queue_wait_empty(struct txn_limbo_queue *queue, double timeout)
{
	if (txn_limbo_queue_is_empty(queue))
		return 0;
	bool is_rollback;
	double deadline = fiber_clock() + timeout;
	/*
	 * Retry in the loop. More transactions might be added while waiting for
	 * the last one.
	 */
	do {
		if (txn_limbo_queue_wait_last_txn(queue, &is_rollback,
						  timeout) != 0) {
			diag_set(ClientError, ER_TIMEOUT);
			return -1;
		}
		timeout = deadline - fiber_clock();
	} while (!txn_limbo_queue_is_empty(queue));
	return 0;
}

int
txn_limbo_queue_wait_persisted(struct txn_limbo_queue *queue)
{
	if (txn_limbo_queue_is_empty(queue))
		return 0;
	struct txn_limbo_entry *e = txn_limbo_queue_last_entry(queue);
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
		e = txn_limbo_queue_last_entry(queue);
	}
	return 0;
}

void
txn_limbo_queue_rollback_all_volatile(struct txn_limbo_queue *queue)
{
	txn_limbo_queue_rollback_volatile_up_to(queue, NULL);
}

void
txn_limbo_queue_sanity_check(struct txn_limbo_queue *queue)
{
	(void)queue;
	assert(queue->volatile_confirmed_lsn >= queue->confirmed_lsn);
}

void
txn_limbo_queue_create(struct txn_limbo_queue *queue)
{
	memset(queue, 0, sizeof(*queue));
	rlist_create(&queue->entries);
	queue->owner_id = REPLICA_ID_NIL;
	vclock_create(&queue->vclock);
	vclock_create(&queue->confirmed_vclock);
	fiber_cond_create(&queue->cond);
}

void
txn_limbo_queue_destroy(struct txn_limbo_queue *queue)
{
	fiber_cond_destroy(&queue->cond);
	struct txn_limbo_entry *entry;
	while (!rlist_empty(&queue->entries)) {
		entry = rlist_shift_entry(&queue->entries,
					  typeof(*entry), in_queue);
		entry->txn->limbo_entry = NULL;
		txn_free(entry->txn);
	}
	TRASH(queue);
}
