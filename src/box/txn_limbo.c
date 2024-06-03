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

static bool
txn_limbo_ack_already_seen(struct txn_limbo *limbo, uint32_t replica_id,
			   int64_t lsn, int64_t *prev_lsn);

static void
txn_limbo_apply_promote(struct txn_limbo *limbo,
			const struct synchro_request *req,
			uint32_t new_limbo_owner);

static void
txn_limbo_ack_promote(struct txn_limbo *limbo, uint32_t replica_id,
		      int64_t lsn, int64_t prev_lsn);

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
	vclock_create(&limbo->confirmed_vclock);
	limbo->promote_greatest_term = 0;
	rlist_create(&limbo->pending_promotes);
	latch_create(&limbo->promote_latch);
	limbo->confirmed_lsn = 0;
	limbo->rollback_count = 0;
	limbo->is_in_rollback = false;
	limbo->frozen_reasons = 0;
	limbo->is_frozen_until_promotion = true;
	limbo->do_validate = false;
	limbo->confirm_lag = 0;
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
	       (limbo->owner_id != instance_id || txn_limbo_is_frozen(limbo));
}

void
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

/**
 * Find a currently active PROMOTE request in a list, if any;
 * i.e. the one authored by this instance in the current term.
 */
static struct pending_promote *
txn_limbo_get_active_promote(struct rlist *reqs)
{
	if (rlist_empty(reqs))
		return NULL;

	struct pending_promote *last =
		rlist_last_entry(reqs, typeof(*last), link);

	/*
	 * We're only interested in a scenario where the current
	 * instance is the leader waiting for a quorum on its
	 * PROMOTE request (the last one in the queue).
	 *
	 * Furthermore, the quorum only matters if we
	 * reach it within the current term.
	 */
	if (last->req.origin_id != instance_id ||
	    last->req.term < box_raft()->volatile_term)
		return NULL;

	return last;
}

bool
txn_limbo_is_trying_to_promote(struct txn_limbo *limbo)
{
	struct rlist *queue = &limbo->pending_promotes;
	return txn_limbo_get_active_promote(queue);
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
	if (txn_limbo_is_trying_to_promote(limbo)) {
		diag_set(ClientError, ER_SYNC_QUEUE_UNCLAIMED);
		return NULL;
	}
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
	} else if (limbo->owner_id != id && !txn_is_fully_local(txn)) {
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
	e->insertion_time = fiber_clock();
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

void
txn_limbo_assign_local_lsn(struct txn_limbo *limbo,
			   struct txn_limbo_entry *entry, int64_t lsn)
{
	assert(limbo->owner_id != REPLICA_ID_NIL);
	assert(limbo->owner_id == instance_id);
	assert(entry->lsn == -1);
	assert(lsn > 0);

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
		if (rc != 0 && fiber_is_cancelled())
			return -1;
		if (txn_limbo_is_frozen(limbo))
			goto wait;
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
		e->txn->limbo_entry = NULL;
		txn_limbo_abort(limbo, e);
		txn_clear_flags(e->txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
		txn_limbo_complete(e->txn, false);
		if (e == entry)
			break;
		fiber_wakeup(e->txn->fiber);
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
	if (entry->is_rollback) {
		diag_set(ClientError, ER_SYNC_ROLLBACK);
		return -1;
	}
	return 0;
}

void
txn_limbo_checkpoint(const struct txn_limbo *limbo, struct synchro_request *req,
		     struct vclock *vclock)
{
	/* For simplicity we prohibit checkpoints during leadership changes. */
	if (!rlist_empty((struct rlist *)&limbo->pending_promotes))
		say_warn("limbo checkpoint: cannot capture pending PROMOTEs");

	memset(req, 0, sizeof(*req));
	req->type = IPROTO_RAFT_PROMOTE;
	req->replica_id = limbo->owner_id;
	req->lsn = limbo->confirmed_lsn;
	req->term = limbo->promote_greatest_term;
	if (vclock != NULL)
		vclock_copy(vclock, &limbo->confirmed_vclock);
	req->confirmed_vclock = vclock;
}

/** Write a request to WAL and return its own LSN. */
static int64_t
synchro_request_write(const struct synchro_request *req)
{
	/*
	 * This is a synchronous commit so we can
	 * allocate everything on a stack.
	 */
	char body[XROW_BODY_LEN_MAX];
	struct xrow_header row;
	char buf[sizeof(struct journal_entry) + sizeof(struct xrow_header *)];

	struct journal_entry *entry = (struct journal_entry *)buf;
	entry->rows[0] = &row;

	xrow_encode_synchro(&row, body, req);

	journal_entry_create(entry, 1, xrow_approx_len(&row),
			     journal_entry_fiber_wakeup_cb, fiber());

	if (journal_write(entry) != 0)
		goto fail;
	if (entry->res < 0) {
		diag_set_journal_res(entry->res);
		goto fail;
	}
	/* This is the LSN we may want to confirm. */
	return entry->rows[0]->lsn;
fail:
	diag_log();
	/*
	 * XXX: the stub is supposed to be removed once it is defined what to do
	 * when a synchro request WAL write fails. One of the possible
	 * solutions: log the error, keep the limbo queue as is and probably put
	 * in rollback mode. Then provide a hook to call manually when WAL
	 * problems are fixed. Or retry automatically with some period.
	 */
	panic("Could not write a synchro request to WAL: %s",
	      synchro_request_to_string(req));
}

/**
 * Write a confirmation entry to WAL. After it's written all the
 * transactions waiting for confirmation may be finished.
 */
static void
txn_limbo_write_confirm(struct txn_limbo *limbo, int64_t lsn)
{
	/* Prevent duplicate CONFIRMs by bumping the counters early. */
	vclock_follow(&limbo->confirmed_vclock, instance_id, lsn);
	if (limbo->owner_id == instance_id) {
		assert(lsn > limbo->confirmed_lsn);
		assert(!limbo->is_in_rollback);
		limbo->confirmed_lsn = lsn;
	}

	synchro_request_write(&(struct synchro_request) {
		.type = IPROTO_RAFT_CONFIRM,
		.replica_id = instance_id,
		.lsn = lsn,
	});
}

static void
txn_limbo_log_status(struct txn_limbo *limbo)
{
	say_info("PROMOTE: limbo owner_id: %u, "
		 "lsn: %" PRIi64 ", term: %" PRIu64,
		 limbo->owner_id, limbo->confirmed_lsn,
		 limbo->promote_greatest_term);
}

/**
 * Confirm a queue (history) of PROMOTE requests on the leader's behalf.
 * All the matching requests will be applied in order.
 */
static void
txn_limbo_confirm_promote(struct txn_limbo *limbo,
			  uint32_t origin_id, int64_t lsn)
{
	assert(latch_is_locked(&limbo->promote_latch));

	say_info("PROMOTE: read %s for lsn %" PRIi64 " from %u",
		 iproto_type_name(IPROTO_RAFT_CONFIRM), lsn, origin_id);

	struct rlist *queue = &limbo->pending_promotes;

	uint64_t prev_term = 0;
	struct pending_promote *item, *tmp, *confirmed = NULL;
	rlist_foreach_entry_safe_reverse(item, queue, link, tmp) {
		/*
		 * Find the latest matching request; Then, drop all
		 * requests which can no longer belong to the history
		 * (i.e. unreachable from the confirmed one).
		 */
		if (confirmed != NULL) {
			if (item->req.term == prev_term) {
				prev_term = item->req.prev_term;
				continue;
			}

			say_info("PROMOTE: drop obsolete %s",
				 synchro_request_to_string(&item->req));

			rlist_del_entry(item, link);
			free(item);

		} else if (item->req.origin_id == origin_id &&
			   item->req.self_lsn <= lsn) {
			say_info("PROMOTE: confirm %s",
				 synchro_request_to_string(&item->req));

			confirmed = item;
			prev_term = item->req.prev_term;
		}
	}

	/*
	 * If we couldn't find the entry, it means that the CONFIRM is
	 * obsolete. This is fine, because it implies that the PROMOTE
	 * has already been applied as a part of a longer history.
	 */
	if (confirmed != NULL) {
		/* Process all confirmed PROMOTE requests in order. */
		rlist_foreach_entry_safe(item, queue, link, tmp) {
			struct synchro_request *req = &item->req;
			txn_limbo_apply_promote(limbo, req, req->origin_id);
			rlist_del_entry(item, link);
			free(item);

			if (item == confirmed)
				break;
		}

		txn_limbo_log_status(limbo);
		if (rlist_empty(queue))
			say_info("PROMOTE: queue is now empty");
	} else {
		say_info("PROMOTE: nothing to confirm "
			 "via lsn %" PRIu64 " from %u",
			 lsn, origin_id);
	}
}

/** Confirm all the entries <= @a lsn. */
static void
txn_limbo_confirm_txn(struct txn_limbo *limbo, int64_t lsn)
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
			e->txn->limbo_entry = NULL;
			e->txn = NULL;
			continue;
		}
		e->is_commit = true;
		if (txn_has_flag(e->txn, TXN_WAIT_ACK))
			limbo->confirm_lag = fiber_clock() - e->insertion_time;
		e->txn->limbo_entry = NULL;
		txn_limbo_remove(limbo, e);
		txn_clear_flags(e->txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
		/*
		 * Should be written to WAL by now. Confirm is always written
		 * after the affected transactions.
		 */
		assert(e->txn->signature >= 0);
		txn_limbo_complete(e->txn, true);
	}
	/*
	 * Track CONFIRM lsn on replica in order to detect split-brain by
	 * comparing existing confirm_lsn with the one arriving from a remote
	 * instance.
	 */
	if (limbo->confirmed_lsn < lsn) {
		limbo->confirmed_lsn = lsn;
		vclock_follow(&limbo->confirmed_vclock, limbo->owner_id, lsn);
	}
}

static void
txn_limbo_read_confirm(struct txn_limbo *limbo,
		       const struct synchro_request *req)
{
	/*
	 * If a leadership transition is currently taking place,
	 * we should try to apply this CONFIRM to one of the queued
	 * PROMOTE requests (works for both leader & follower).
	 *
	 * The following property holds for every CONFIRM-for-PROMOTE:
	 *  - Either promote_queue is not empty;
	 *  - Or the corresponding PROMOTE has already been applied
	 *    as a part of a longer history of *another* instance,
	 *    meaning that its CONFIRM-for-PROMOTE will be nopified
	 *    upon arrival in applier_synchro_filter_tx.
	 */
	if (!rlist_empty(&limbo->pending_promotes))
		txn_limbo_confirm_promote(limbo, req->origin_id, req->lsn);
	/*
	 * Otherwise, we may confirm regular transactions.
	 */
	else if (limbo->owner_id == req->replica_id)
		txn_limbo_confirm_txn(limbo, req->lsn);
	/*
	 * Finally, this must be a bug (e.g. broken snapshots).
	 */
	else
		say_crit("BUG: read a strange %s "
			 "(possibly for a vanished PROMOTE)",
			 synchro_request_to_string(req));
}

/**
 * Write a rollback message to WAL. After it's written all the
 * transactions following the current one and waiting for
 * confirmation must be rolled back.
 */
static void
txn_limbo_write_rollback(struct txn_limbo *limbo, int64_t lsn)
{
	assert(limbo->owner_id == instance_id);
	assert(lsn > limbo->confirmed_lsn);
	assert(!limbo->is_in_rollback);

	limbo->is_in_rollback = true;
	synchro_request_write(&(struct synchro_request) {
		.type = IPROTO_RAFT_ROLLBACK,
		.replica_id = instance_id,
		.lsn = lsn,
	});
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
		txn_clear_flags(e->txn, TXN_WAIT_ACK);
		/*
		 * Should be written to WAL by now. Rollback is always written
		 * after the affected transactions.
		 */
		assert(e->txn->signature >= 0);
		e->txn->signature = TXN_SIGNATURE_SYNC_ROLLBACK;
		e->txn->limbo_entry = NULL;
		txn_limbo_complete(e->txn, false);
		if (e == last_rollback)
			break;
	}
}

static void
txn_limbo_make_promote(struct txn_limbo *limbo, int64_t lsn, uint64_t term,
		       struct synchro_request *req)
{
	/*
	 * We make sure that promote is only written once everything this
	 * instance has may be confirmed.
	 */
	struct txn_limbo_entry *e = txn_limbo_last_synchro_entry(limbo);
	assert(e == NULL || e->lsn <= lsn);
	(void) e;

	/*
	 * The PROMOTE request we're about to write depends on
	 * the previous one if it exists. It's an inductive property.
	 */
	struct rlist *queue = &limbo->pending_promotes;
	struct pending_promote *last = !rlist_empty(queue)
		? rlist_last_entry(queue, typeof(*last), link)
		: NULL;

	uint64_t greatest_term = limbo->promote_greatest_term;
	*req = (struct synchro_request) {
		.type = IPROTO_RAFT_PROMOTE,
		.origin_id = instance_id,
		.term = term,
		/* These fields require extra tinkering with: */
		.prev_term  = last ? last->req.term : greatest_term,
		.replica_id = last ? last->req.origin_id : limbo->owner_id,
		.lsn        = last ? last->req.self_lsn : lsn,
		/*
		 * Confirmed_vclock is only persisted in checkpoints. It doesn't
		 * appear in WALs and replication.
		 */
		.confirmed_vclock = NULL,
	};
}

int
txn_limbo_write_promote(struct txn_limbo *limbo, int64_t lsn, uint64_t term)
{
	assert(latch_is_locked(&limbo->promote_latch));

	struct synchro_request req;
	txn_limbo_make_promote(limbo, lsn, term, &req);

	if (txn_limbo_req_prepare(limbo, &req) < 0)
		return -1;
	req.self_lsn = synchro_request_write(&req);
	say_info("PROMOTE: write %s", synchro_request_to_string(&req));
	txn_limbo_req_commit(limbo, &req);

	/* Immediately acknowledge our own write. */
	int64_t prev_lsn, self_lsn = req.self_lsn;
	txn_limbo_ack_already_seen(limbo, instance_id, self_lsn, &prev_lsn);
	txn_limbo_ack_promote(limbo, instance_id, self_lsn, prev_lsn);

	return 0;
}

/**
 * Compatibility function for processing a single legacy PROMOTE request.
 * This is needed during a transition to the new promote logic.
 */
static bool
txn_limbo_read_promote_compat(struct txn_limbo *limbo,
			      const struct synchro_request *req)
{
	assert(latch_is_locked(&limbo->promote_latch));
	assert(req->type == IPROTO_RAFT_PROMOTE);

	/*
	 * term == 0 means it's a request from
	 * the bootstrap snapshot, so we may skip it.
	 */
	if (req->term == 0)
		return true;

	if (req->confirmed_vclock != NULL) {
		say_info("PROMOTE: restore from a snapshot %s",
			 synchro_request_to_string(req));
		txn_limbo_apply_promote(limbo, req, req->origin_id);
		txn_limbo_log_status(limbo);
		return true;
	}

	return false;
}

static void
txn_limbo_read_promote(struct txn_limbo *limbo,
		       const struct synchro_request *req)
{
	struct rlist *queue = &limbo->pending_promotes;

	/* Try finding a position for an insertion. */
	struct pending_promote *item;
	rlist_foreach_entry_reverse(item, queue, link)
		if (item->req.term <= req->term)
			break;

	uint64_t last_term = rlist_entry_is_head(item, queue, link)
		? limbo->promote_greatest_term
		: item->req.term;

	if (last_term >= req->term)
		panic("BUG: invalid term %" PRIu64 " in %s; "
		      "(should be > %" PRIu64 ")", req->term,
		      synchro_request_to_string(req), last_term);

	assert(req->confirmed_vclock == NULL);
	struct pending_promote *last = xmalloc(sizeof(*last));
	last->ack_count = 0;
	last->req = *req;

	say_info("PROMOTE: insert %s to the queue",
		 synchro_request_to_string(req));

	rlist_add_entry(&item->link, last, link);
}

/** Apply a single PROMOTE request. */
static void
txn_limbo_apply_promote(struct txn_limbo *limbo,
			const struct synchro_request *req,
			uint32_t new_limbo_owner)
{
	assert(latch_is_locked(&limbo->promote_latch));

	say_info("PROMOTE: apply %s", synchro_request_to_string(req));
	uint64_t term = req->term;
	uint32_t origin = req->origin_id;

	/*
	 * Now that we're sure this PROMOTE "has happened", we may finally
	 * apply its term as well; meaning that any row from an older
	 * term will be nopified in applier_synchro_filter_tx.
	 *
	 * Delaying the term application gives every instance a chance
	 * to catch up with a deposed leader, who may be several
	 * transactions ahead of the others.
	 */
	if (txn_limbo_replica_term(limbo, origin) < term) {
		vclock_follow(&limbo->promote_term_map, origin, term);
		if (limbo->promote_greatest_term < term) {
			limbo->promote_greatest_term = term;
			limbo->is_frozen_until_promotion = false;
			if (box_raft()->volatile_term <= term)
				txn_limbo_unfence(limbo);
		}
	}

	txn_limbo_confirm_txn(limbo, req->lsn);
	txn_limbo_read_rollback(limbo, req->lsn + 1);
	assert(txn_limbo_is_empty(limbo));

	/*
	 * Non-null confirmed_vclock means we're restoring from a snapshot.
	 * Note that the reverse isn't always true, because bootstrap snapshot
	 * doesn't contain confirmed_vclock.
	 */
	uint64_t confirmed_lsn;
	if (req->confirmed_vclock != NULL) {
		vclock_copy(&limbo->confirmed_vclock, req->confirmed_vclock);
		confirmed_lsn = vclock_get(&limbo->confirmed_vclock, origin);
	} else if (new_limbo_owner == REPLICA_ID_NIL) {
		/* txn_limbo_confirm_txn will update confirmed_vclock. */
		confirmed_lsn = 0;
	} else {
		assert(req->self_lsn > 0);
		confirmed_lsn = req->self_lsn;
		/* We use vclock_reset here due to txn_limbo_write_confirm. */
		vclock_reset(&limbo->confirmed_vclock, origin, confirmed_lsn);
	}

	limbo->confirmed_lsn = confirmed_lsn;
	limbo->owner_id = new_limbo_owner;

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
		.confirmed_vclock = NULL,
	};
	if (txn_limbo_req_prepare(limbo, &req) < 0)
		return -1;
	req.self_lsn = synchro_request_write(&req);
	txn_limbo_req_commit(limbo, &req);
	return 0;
}

/**
 * Process a DEMOTE request, which's like PROMOTE, but clears the limbo
 * ownership.
 */
static void
txn_limbo_read_demote(struct txn_limbo *limbo,
		      const struct synchro_request *req)
{
	/*
	 * We don't want to fully support DEMOTE in election_mode=off,
	 * but this impl will fix many tests for a relatively small price.
	 */
	struct rlist *queue = &limbo->pending_promotes;
	if (!rlist_empty(queue)) {
		say_warn("ignoring %s due to non-empty PROMOTE queue",
			 synchro_request_to_string(req));
		return;
	}
	txn_limbo_apply_promote(limbo, req, REPLICA_ID_NIL);
	txn_limbo_log_status(limbo);
}

/**
 * Check if we've already seen an ACK for an lsn from replica_id.
 * Return a previously ACKed lsn from replica_id via prev_lsn.
 */
static bool
txn_limbo_ack_already_seen(struct txn_limbo *limbo, uint32_t replica_id,
			   int64_t lsn, int64_t *prev_lsn)
{
	*prev_lsn = vclock_get(&limbo->vclock, replica_id);
	if (lsn <= *prev_lsn)
		return true;
	vclock_follow(&limbo->vclock, replica_id, lsn);
	return false;
}

/**
 * Check if we should CONFIRM any PROMOTE requests
 * and write a dedicated RAFT_CONFIRM entry if that's the case.
 */
static void
txn_limbo_maybe_confirm_promotes(struct txn_limbo *limbo)
{
	assert(latch_is_locked(&limbo->promote_latch));

	struct rlist *queue = &limbo->pending_promotes;
	struct pending_promote *last = txn_limbo_get_active_promote(queue);
	if (last == NULL)
		return;

	int quorum = replicaset_healthy_quorum();
	if (last->ack_count < quorum)
		return;

	int64_t confirm_lsn = last->req.self_lsn;

	txn_limbo_write_confirm(limbo, confirm_lsn);
	txn_limbo_confirm_promote(limbo, instance_id, confirm_lsn);
}

static void
txn_limbo_ack_promote(struct txn_limbo *limbo, uint32_t replica_id,
		      int64_t lsn, int64_t prev_lsn)
{
	assert(latch_is_locked(&limbo->promote_latch));

	struct rlist *queue = &limbo->pending_promotes;
	struct pending_promote *last = txn_limbo_get_active_promote(queue);
	if (last == NULL)
		return;

	assert(last->req.self_lsn > 0);
	bool is_first_ack =
		last->req.self_lsn > prev_lsn &&
		last->req.self_lsn <= lsn;

	/*
	 * We should only consider the first sufficient ACK from an instance.
	 * Otherwise bad things might happen, e.g. gaining more
	 * ACKs than theoretically possible.
	 */
	if (!is_first_ack)
		return;

	assert(last->ack_count >= 0);
	last->ack_count++;

	say_info("PROMOTE: ack (%d/%d) for %s via lsn %" PRIi64 " from %u",
		 last->ack_count, replicaset_healthy_quorum(),
		 synchro_request_to_string(&last->req), lsn, replica_id);

	txn_limbo_maybe_confirm_promotes(limbo);
}

static void
txn_limbo_ack_txn(struct txn_limbo *limbo, int64_t lsn, int64_t prev_lsn)
{
	if (rlist_empty(&limbo->queue) || txn_limbo_is_frozen(limbo))
		return;
	assert(!txn_limbo_is_ro(limbo));

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
	txn_limbo_confirm_txn(limbo, confirm_lsn);
}

void
txn_limbo_ack(struct txn_limbo *limbo, uint32_t replica_id, int64_t lsn)
{
	/*
	 * If limbo is currently writing a rollback, it means that the whole
	 * queue will be rolled back. Because rollback is written only for
	 * timeout. Timeout always happens first for the oldest entry, i.e.
	 * first entry in the queue. The rollback will clear all the newer
	 * entries. So in total the whole queue is dead already. Would be
	 * strange to write CONFIRM for rolled back LSNs. Even though
	 * probably it wouldn't break anything. Would be just 2 conflicting
	 * decisions for the same LSNs.
	 *
	 * Furthermore, this is also set during txn_limbo_write_promote;
	 * Which is why we'd like to postpone ACK accounting to prevent
	 * race conditions.
	 */
	if (limbo->is_in_rollback)
		return;

	int64_t prev_lsn;
	if (txn_limbo_ack_already_seen(limbo, replica_id, lsn, &prev_lsn))
		return;

	/*
	 * If this instance is currently trying to become a limbo owner,
	 * we should first try to apply this ACK to its active PROMOTE
	 * request (i.e. one of the queued ones).
	 *
	 * Even if it's really an ACK for a regular transaction, it
	 * doesn't matter anymore. The next limbo owner will take care of
	 * all pending transactions after its PROMOTE has been confirmed.
	 */
	if (txn_limbo_is_trying_to_promote(limbo)) {
		latch_lock(&limbo->promote_latch);
		txn_limbo_ack_promote(limbo, replica_id, lsn, prev_lsn);
		latch_unlock(&limbo->promote_latch);
	}
	/*
	 * Otherwise, we may acknowledge regular transactions
	 * provided that we're the current limbo owner. Do note
	 * that this function will check if the limbo is frozen!
	 */
	else if (limbo->owner_id == instance_id)
		txn_limbo_ack_txn(limbo, lsn, prev_lsn);
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
txn_limbo_wait_confirm(struct txn_limbo *limbo)
{
	/* Make sure we don't have any pending PROMOTE requests. */
	if (!rlist_empty(&limbo->pending_promotes)) {
		diag_set(ClientError, ER_SYNC_QUEUE_UNCLAIMED);
		return -1;
	}
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
	return tt_sprintf("RAFT: rejecting %s", synchro_request_to_string(req));
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
	if (req->replica_id == REPLICA_ID_NIL &&
	    req->type != IPROTO_RAFT_PROMOTE) {
		say_error("%s. Zero replica_id detected",
			  reject_str(req));
		diag_set(ClientError, ER_UNSUPPORTED, "Replication",
			 "synchronous requests with zero replica_id");
		return -1;
	}

	return 0;
}

/**
 * A common filter which checks that the request operates
 * over a valid lsn range.
 */
static int
txn_limbo_filter_queue_boundaries(struct txn_limbo *limbo,
				  const struct synchro_request *req)
{
	int64_t lsn = req->lsn;
	/*
	 * Easy case - processed LSN matches the new one which comes inside
	 * request, everything is consistent. This is allowed only for
	 * PROMOTE/DEMOTE.
	 */
	if (limbo->confirmed_lsn == lsn) {
		if (iproto_type_is_promote_request(req->type)) {
			return 0;
		} else {
			say_error("%s. Duplicate request with confirmed lsn "
				  "%lld = request lsn %lld", reject_str(req),
				  (long long)limbo->confirmed_lsn,
				  (long long)lsn);
			diag_set(ClientError, ER_UNSUPPORTED, "Replication",
				 "Duplicate CONFIRM/ROLLBACK request");
			return -1;
		}
	}

	/*
	 * Explicit split brain situation. Request comes in with an old LSN
	 * which we've already processed.
	 */
	if (limbo->confirmed_lsn > lsn) {
		say_error("%s. confirmed lsn %lld > request lsn %lld",
			  reject_str(req), (long long)limbo->confirmed_lsn,
			  (long long)lsn);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request with lsn from an already "
			 "processed range");
		return -1;
	}

	/*
	 * The last case requires a few subcases.
	 */
	assert(limbo->confirmed_lsn < lsn);

	if (txn_limbo_is_empty(limbo)) {
		/*
		 * Transactions are rolled back already,
		 * since the limbo is empty.
		 */
		say_error("%s. confirmed lsn %lld < request lsn %lld "
			  "and empty limbo", reject_str(req),
			  (long long)limbo->confirmed_lsn,
			  (long long)lsn);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request mentioning future lsn");
		return -1;
	} else {
		/*
		 * Some entries are present in the limbo, we need to make sure
		 * that request lsn lays inside limbo [first; last] range.
		 * So that the request has some queued data to process,
		 * otherwise it means the request comes from split brained node.
		 */
		int64_t first_lsn = txn_limbo_first_entry(limbo)->lsn;
		int64_t last_lsn = txn_limbo_last_synchro_entry(limbo)->lsn;

		if (lsn < first_lsn || last_lsn < lsn) {
			say_error("%s. request lsn %lld out of range "
				  "[%lld; %lld]", reject_str(req),
				  (long long)lsn,
				  (long long)first_lsn,
				  (long long)last_lsn);
			diag_set(ClientError, ER_SPLIT_BRAIN,
				 "got a request lsn out of queue range");
			return -1;
		}
	}

	return 0;
}

static int
txn_limbo_filter_queue_owner(struct txn_limbo *limbo,
			     const struct synchro_request *req)
{
	/*
	 * Incoming packets should esteem limbo owner,
	 * if it doesn't match it means the sender
	 * missed limbo owner migrations and is out of date.
	 */
	if (req->replica_id != limbo->owner_id) {
		say_error("%s. Limbo owner mismatch, owner_id %u",
			  reject_str(req), limbo->owner_id);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request from a foreign synchro queue owner");
		return -1;
	}

	return 0;
}

static int
txn_limbo_filter_queue(struct txn_limbo *limbo,
		       const struct synchro_request *req)
{
	if (txn_limbo_filter_queue_owner(limbo, req) < 0)
		return -1;

	if (txn_limbo_filter_queue_boundaries(limbo, req) < 0)
		return -1;

	return 0;
}

/**
 * A filter for CONFIRM and ROLLBACK packets.
 */
static int
txn_limbo_filter_confirm_rollback(struct txn_limbo *limbo,
				  const struct synchro_request *req)
{
	assert(latch_is_locked(&limbo->promote_latch));
	assert(limbo->do_validate);
	(void)limbo;

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

/** A filter for CONFIRM packets. */
static int
txn_limbo_filter_confirm(struct txn_limbo *limbo,
			 const struct synchro_request *req)
{
	if (txn_limbo_filter_confirm_rollback(limbo, req) < 0)
		return -1;

	/*
	 * If PROMOTE queue is not empty, this might be a
	 * CONFIRM-for-PROMOTE, which should bypass all ownership
	 * and bounds checks. Even if that's not the case,
	 * it will just be ignored by txn_limbo_ack_promote.
	 *
	 * On the other hand, once we've applied a certain
	 * PROMOTE request, promote_greatest_term must have
	 * been updated, too. This means that each untimely
	 * row from a previous leader will just be nopified.
	 */
	if (rlist_empty(&limbo->pending_promotes))
		return txn_limbo_filter_queue(limbo, req);

	return 0;
}

/** A filter for ROLLBACK packets. */
static int
txn_limbo_filter_rollback(struct txn_limbo *limbo,
			  const struct synchro_request *req)
{
	if (txn_limbo_filter_confirm_rollback(limbo, req) < 0)
		return -1;

	return txn_limbo_filter_queue(limbo, req);
}

/** A filter for PROMOTE and DEMOTE packets. */
static int
txn_limbo_filter_promote_demote(struct txn_limbo *limbo,
				const struct synchro_request *req)
{
	assert(latch_is_locked(&limbo->promote_latch));
	assert(limbo->do_validate);
	assert(iproto_type_is_promote_request(req->type));
	(void)limbo;

	/*
	 * PROMOTE and DEMOTE packets must not have zero
	 * term supplied, otherwise it is a broken packet.
	 */
	if (req->term == 0) {
		say_error("%s. Zero term detected", reject_str(req));
		diag_set(ClientError, ER_UNSUPPORTED,
			 "Replication",
			 "PROMOTE/DEMOTE with a zero term");
		return -1;
	}

	/* Check that prev_term < term, always. */
	if (req->prev_term >= req->term) {
		say_error("%s. prev_term >= term detected", reject_str(req));
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "Replication",
			 "PROMOTE/DEMOTE with prev_term >= term");
		return -1;
	}

	return 0;
}

/** A filter for PROMOTE packets. */
static int
txn_limbo_filter_promote(struct txn_limbo *limbo,
			 const struct synchro_request *req)
{
	if (txn_limbo_filter_promote_demote(limbo, req) < 0)
		return -1;

	bool found_prev_term = false;

	/*
	 * The first PROMOTE since promote_greatest_term
	 * will have to commit & rollback whatever is in
	 * the limbo using its req->lsn.
	 *
	 * The rest are only going to update the owner etc.
	 */
	if (req->prev_term == limbo->promote_greatest_term) {
		found_prev_term = true;
		if (txn_limbo_filter_queue(limbo, req) < 0)
			return -1;
	}

	/* Check for duplicate term or unknown prev_term. */
	struct pending_promote *item;
	struct rlist *queue = &limbo->pending_promotes;
	rlist_foreach_entry_reverse(item, queue, link) {
		if (req->term == item->req.term) {
			say_error("%s. Duplicate term detected",
				  reject_str(req));
			diag_set(ClientError, ER_SPLIT_BRAIN,
				 "Replication",
				 "PROMOTE with a duplicate term");
		}
		if (req->prev_term == item->req.term) {
			found_prev_term = true;
			break;
		}
	}

	if (!found_prev_term) {
		say_error("%s. Unknown prev_term detected", reject_str(req));
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "Replication", "PROMOTE with an unknown prev_term");
		return -1;
	}

	return 0;
}

/** A filter for DEMOTE packets. */
static int
txn_limbo_filter_demote(struct txn_limbo *limbo,
			const struct synchro_request *req)
{
	if (txn_limbo_filter_promote_demote(limbo, req) < 0)
		return -1;

	if (txn_limbo_filter_queue(limbo, req) < 0)
		return -1;

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
		return txn_limbo_filter_confirm(limbo, req);
	case IPROTO_RAFT_ROLLBACK:
		return txn_limbo_filter_rollback(limbo, req);
	case IPROTO_RAFT_PROMOTE:
		return txn_limbo_filter_promote(limbo, req);
	case IPROTO_RAFT_DEMOTE:
		return txn_limbo_filter_demote(limbo, req);
	default:
		unreachable();
	}
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
	case IPROTO_RAFT_DEMOTE:
		limbo->is_in_rollback = false;
		break;
	}
}

void
txn_limbo_req_commit(struct txn_limbo *limbo, const struct synchro_request *req)
{
	assert(latch_is_locked(&limbo->promote_latch));
	switch (req->type) {
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE:
		limbo->is_in_rollback = false;
		break;
	}

	int64_t lsn = req->lsn;
	switch (req->type) {
	case IPROTO_RAFT_CONFIRM:
		txn_limbo_read_confirm(limbo, req);
		break;
	case IPROTO_RAFT_ROLLBACK:
		txn_limbo_read_rollback(limbo, lsn);
		break;
	case IPROTO_RAFT_PROMOTE:
		if (!txn_limbo_read_promote_compat(limbo, req))
			txn_limbo_read_promote(limbo, req);
		break;
	case IPROTO_RAFT_DEMOTE:
		txn_limbo_read_demote(limbo, req);
		break;
	default:
		unreachable();
	}
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
	if (rlist_empty(&limbo->queue) || txn_limbo_is_frozen(limbo))
		return;
	struct txn_limbo_entry *e;
	int64_t confirm_lsn = -1;
	rlist_foreach_entry(e, &limbo->queue, in_queue) {
		assert(e->ack_count <= VCLOCK_MAX);
		if (!txn_has_flag(e->txn, TXN_WAIT_ACK)) {
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
		txn_limbo_confirm_txn(limbo, confirm_lsn);
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
