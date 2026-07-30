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
#include "raft/raft.h"
#include "tt_static.h"
#include "trivia/config.h"

struct txn_limbo txn_limbo;

/**
 * Stringify the synchro request into the given buffer. Same semantics as
 * snprintf().
 */
static int
synchro_request_snprint(char *buf, int size, const struct synchro_request *req)
{
	int total = 0;
	if (req->type == IPROTO_RAFT_CONFIRM) {
		SNPRINT(total, snprintf, buf, size,
			"CONFIRM{owner: %u, origin: %u, lsn: %lld}",
			req->queue_owner_id, req->origin_id,
			(long long)req->confirm.lsn);
		return total;
	}
	if (req->type == IPROTO_RAFT_ROLLBACK) {
		SNPRINT(total, snprintf, buf, size,
			"ROLLBACK{owner: %u, origin: %u, lsn: %lld}",
			req->queue_owner_id, req->origin_id,
			(long long)req->rollback.lsn);
		return total;
	}
	assert(req->type == IPROTO_RAFT_PROMOTE ||
	       req->type == IPROTO_RAFT_DEMOTE);
	SNPRINT(total, snprintf, buf, size,
		"%s{owner: %u, origin: %u, lsn: %lld, term: %llu",
		req->type == IPROTO_RAFT_PROMOTE ? "PROMOTE" : "DEMOTE",
		req->queue_owner_id, req->origin_id,
		(long long)req->promote.lsn, (long long)req->promote.term);
	if (vclock_calc_sum(&req->promote.confirmed_vclock) > 0) {
		SNPRINT(total, snprintf, buf, size, ", vclock: ");
		SNPRINT(total, vclock_snprint, buf, size,
			&req->promote.confirmed_vclock);
	}
	SNPRINT(total, snprintf, buf, size, "}");
	return total;
}

/**
 * Stringify the synchro request into the static buffer for error logging.
 * May crop at TT_STATIC_BUF_LEN.
 */
static const char *
synchro_request_str(const struct synchro_request *req)
{
	return TOSTR(synchro_request_snprint, req);
}

/** Write the request into the journal and get its LSN. */
static int64_t
synchro_request_write(const struct synchro_request *req)
{
	/*
	 * This is a synchronous commit so we can
	 * allocate everything on a stack.
	 */
	char body[XROW_BODY_LEN_MAX];
	struct xrow_header row;
	xrow_encode_synchro(&row, body, req);
	if (journal_write_row(&row) != 0)
		return -1;
	return row.lsn;
}

/** Write the request into the journal and get its LSN. */
static int64_t
synchro_request_write_or_panic(const struct synchro_request *req)
{
	int64_t lsn = synchro_request_write(req);
	if (lsn >= 0)
		return lsn;
	diag_log();
	/*
	 * XXX: the stub is supposed to be removed once it is defined what to do
	 * when a synchro request WAL write fails. One of the possible
	 * solutions: log the error, keep the limbo queue as is and probably put
	 * in rollback mode. Then provide a hook to call manually when WAL
	 * problems are fixed. Or retry automatically with some period.
	 */
	panic("Could not write a synchro request to WAL: %s",
	      synchro_request_str(req));
	return -1;
}

/**
 * Prepare a request which is never supposed to fail its preparation. These are
 * supposed to be locally made requests originating from limbo's own state.
 */
static void
txn_limbo_req_prepare_or_panic(struct txn_limbo *limbo,
			       const struct synchro_request *req)
{
	if (txn_limbo_req_prepare(limbo, req) == 0)
		return;
	diag_log();
	panic("Could not prepare a synchro request: %s",
	      synchro_request_str(req));
}

/**
 * Build a PROMOTE entry from a PROMOTE/DEMOTE request. A request might be
 * missing the confirmed vclock. Rows of one origin are strictly ordered, so
 * the request can't be telling anything about the other nodes which the local
 * instance doesn't already know. The missing part is completed from the local
 * state - the same values the sender would have built into the request.
 */
static void
txn_limbo_promote_entry_create(const struct txn_limbo *limbo,
			       struct txn_limbo_promote_entry *e,
			       const struct synchro_request *req)
{
	assert(iproto_type_is_promote_request(req->type));
	e->raft_term = req->promote.term;
	e->queue_owner_id = req->queue_owner_id;
	e->confirm_lsn = req->promote.lsn;
	vclock_copy(&e->confirmed_vclock, &req->promote.confirmed_vclock);
	if (!vclock_is_set(&e->confirmed_vclock)) {
		vclock_copy(&e->confirmed_vclock,
			    &limbo->queue.confirmed_vclock);
		if (e->queue_owner_id != REPLICA_ID_NIL) {
			vclock_reset(&e->confirmed_vclock, e->queue_owner_id,
				     e->confirm_lsn);
		}
	}
}

static bool
txn_limbo_request_is_promote_bootstrap(const struct synchro_request *e)
{
	if (e->type != IPROTO_RAFT_PROMOTE)
		return false;
	if (e->queue_owner_id != REPLICA_ID_NIL)
		return false;
	if (e->origin_id != 0)
		return false;
	if (e->promote.lsn != 0)
		return false;
	if (e->promote.term != 1)
		return false;
	if (vclock_calc_sum(&e->promote.confirmed_vclock) > 0)
		return false;
	return true;
}

static void
txn_limbo_assert_locked(struct txn_limbo *limbo)
{
	VERIFY(latch_is_locked(&limbo->state_latch));
}

/**
 * Validate correctness of the limbo state. The function helps to catch
 * state-breaking changes early, instead of continuing execution and either
 * leaving the broken state unnoticed or crashing later in some distantly
 * related place which usually complicates debug a lot.
 */
static void
txn_limbo_assert_consistent(struct txn_limbo *limbo)
{
#ifndef NDEBUG
	struct txn_limbo_queue *queue = &limbo->queue;
	uint32_t owner_id = limbo->queue.owner_id;
	if (owner_id != REPLICA_ID_NIL)
		VERIFY(limbo->term == limbo->nodes[owner_id].latest_term);
	/*
	 * The promote entries are transient so far - built and applied right
	 * away, under the same latch lock.
	 */
	int entry_count = 0;
	for (size_t i = 0; i < lengthof(limbo->nodes); ++i) {
		const struct txn_limbo_node *n = &limbo->nodes[i];
		VERIFY(n->latest_term <= limbo->term);
		const struct txn_limbo_promote_entry *e = &n->pending;
		if (e->raft_term == 0)
			continue;
		++entry_count;
		/*
		 * The confirm boundary is in the LSN space of the queue owner
		 * being replaced. An entry claiming an unowned limbo has
		 * nothing to confirm.
		 */
		if (e->queue_owner_id != REPLICA_ID_NIL) {
			VERIFY(e->confirm_lsn == vclock_get(
				&e->confirmed_vclock, e->queue_owner_id));
		} else {
			VERIFY(e->confirm_lsn == 0);
		}
	}
	VERIFY(entry_count <= 1);
	VERIFY(limbo->nodes[REPLICA_ID_NIL].latest_term == 0);
	/*
	 * Queue confirmed LSNs are valid.
	 */
	VERIFY(queue->confirmed_lsn ==
	       vclock_get(&queue->confirmed_vclock, queue->owner_id));
	VERIFY(queue->volatile_confirmed_lsn >= queue->confirmed_lsn);
#else
	(void)limbo;
#endif
}

static int64_t
txn_limbo_replica_confirmed_lsn(const struct txn_limbo *limbo,
				uint32_t replica_id)
{
	return vclock_get(&limbo->queue.confirmed_vclock, replica_id);
}

/** Check limbo's term is unchanged. */
static int
txn_limbo_check_own_term_intact(const struct txn_limbo *limbo, uint64_t term)
{
	if (limbo->term == term)
		return 0;
	diag_set(ClientError, ER_INTERFERING_PROMOTE, limbo->queue.owner_id);
	return -1;
}

/** Check Raft's term is unchanged. */
static int
txn_limbo_check_raft_term_intact(const struct txn_limbo *limbo, uint64_t term)
{
	if (limbo->raft->volatile_term == term)
		return 0;
	diag_set(ClientError, ER_INTERFERING_ELECTIONS);
	return -1;
}

static bool
txn_limbo_has_quorum_for(struct txn_limbo *limbo, int64_t lsn)
{
	assert(lsn > 0);
	return vclock_count_ge(&limbo->queue.vclock, lsn) >=
	       replication_synchro_quorum;
}

static void
txn_limbo_ack_queue(struct txn_limbo *limbo, uint32_t replica_id, int64_t lsn)
{
	if (txn_limbo_queue_ack(&limbo->queue, replica_id, lsn))
		fiber_wakeup(limbo->worker);
}

static void
txn_limbo_fence(struct txn_limbo *limbo)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_queue_fence(&limbo->queue);
}

static void
txn_limbo_unfence(struct txn_limbo *limbo)
{
	txn_limbo_assert_locked(limbo);
	if (txn_limbo_queue_unfence(&limbo->queue))
		fiber_wakeup(limbo->worker);
}

/**
 * Update the state of synchronous replication for system spaces to match the
 * limbo state: they are synchronous while the queue has an owner.
 *
 * The request, when not NULL, is an in-progress PROMOTE/DEMOTE whose outcome
 * is applied optimistically, before its WAL write: a PROMOTE is about to
 * claim the ownership, a DEMOTE is about to drop it. A WAL failure restores
 * the actual state via the rollback.
 */
static void
txn_limbo_update_system_spaces_is_sync_state(struct txn_limbo *limbo,
					     const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	bool is_sync;
	if (req != NULL) {
		assert(req->type == IPROTO_RAFT_PROMOTE ||
		       req->type == IPROTO_RAFT_DEMOTE);
		/* Bootstrap entries do not enable synchronous replication. */
		if (req->origin_id == REPLICA_ID_NIL)
			return;
		is_sync = req->type == IPROTO_RAFT_PROMOTE;
	} else {
		is_sync = limbo->queue.owner_id != REPLICA_ID_NIL;
	}
	system_spaces_update_is_sync_state(is_sync);
}

static int
txn_limbo_worker_bump_confirmed_lsn(struct txn_limbo *limbo)
{
	txn_limbo_assert_locked(limbo);
	struct txn_limbo_queue *queue = &limbo->queue;
	assert(queue->volatile_confirmed_lsn >= queue->confirmed_lsn);
	while (limbo->state == TXN_LIMBO_STATE_LEADER &&
	       queue->volatile_confirmed_lsn > queue->confirmed_lsn) {
		if (queue->is_fenced)
			return -1;
		/* It can get bumped again while we are writing. */
		struct synchro_request req = {
			.type = IPROTO_RAFT_CONFIRM,
			.origin_id = instance_id,
			.queue_owner_id = queue->owner_id,
			.confirm = {
				.lsn = queue->volatile_confirmed_lsn,
			},
		};
		txn_limbo_req_prepare_or_panic(limbo, &req);
		if (synchro_request_write(&req) < 0) {
			diag_log();
			txn_limbo_req_rollback(limbo, &req);
			return -1;
		}
		ERROR_INJECT_YIELD(ERRINJ_TXN_LIMBO_WORKER_DELAY);
		txn_limbo_req_commit(limbo, &req);
	}
	assert(queue->volatile_confirmed_lsn >= queue->confirmed_lsn);
	return 0;
}

/**
 * Apply a PROMOTE entry: transfer queue ownership to the promote's origin,
 * catch up the terms, and inherit the confirmed_vclock.
 */
static void
txn_limbo_apply_promote(struct txn_limbo *limbo, uint16_t type, uint32_t origin)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_assert_consistent(limbo);
	assert(type == IPROTO_RAFT_PROMOTE || type == IPROTO_RAFT_DEMOTE);
	struct txn_limbo_queue *queue = &limbo->queue;
	struct txn_limbo_node *origin_node = &limbo->nodes[origin];
	struct txn_limbo_promote_entry *p = &origin_node->pending;
	/* 0 is never allowed, and 1 is the special bootstrap promotion. */
	assert(p->raft_term > 1);
	uint64_t applied_term = p->raft_term;
	if (applied_term > origin_node->latest_term && origin != REPLICA_ID_NIL)
		origin_node->latest_term = applied_term;
	if (applied_term > limbo->term)
		limbo->term = applied_term;
	if (!limbo->is_in_recovery)
		limbo->saw_promote = true;
	/*
	 * The PROMOTE confirms its prev owner's txns up to confirm_lsn. The
	 * owner might legitimately mismatch - a checkpoint PROMOTE from a
	 * JOIN stream or recovery describes an already established
	 * ownership, while the local queue isn't claimed yet. There is
	 * nothing to confirm then. With the filter disabled the request can
	 * also carry a completely foreign ownership history - then nothing is
	 * confirmed either, and the queued transactions get rolled back by
	 * the transfer.
	 */
	assert(!limbo->do_validate || queue->owner_id == p->queue_owner_id ||
	       queue->owner_id == REPLICA_ID_NIL);
	int64_t border_lsn = 0;
	if (queue->owner_id == p->queue_owner_id)
		border_lsn = p->confirm_lsn;
	uint32_t new_owner = type == IPROTO_RAFT_PROMOTE ?
		origin : REPLICA_ID_NIL;
	txn_limbo_queue_transfer_ownership(queue, new_owner, border_lsn,
					   &p->confirmed_vclock);
	for (uint32_t i = 0; i < VCLOCK_MAX; i++) {
		struct txn_limbo_promote_entry *other =
			&limbo->nodes[i].pending;
		if (other->raft_term == 0)
			continue;
		assert(i == origin);
		assert(other->raft_term <= applied_term);
		memset(other, 0, sizeof(*other));
	}
	txn_limbo_update_system_spaces_is_sync_state(limbo, NULL);
	txn_limbo_update_state(limbo);
	txn_limbo_assert_consistent(limbo);
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
		txn_limbo_lock(limbo);
		int rc = txn_limbo_worker_bump_confirmed_lsn(limbo);
		txn_limbo_unlock(limbo);
		if (rc != 0)
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

static int
txn_limbo_on_ack_f(struct trigger *t, void *event)
{
	struct txn_limbo *limbo = t->data;
	assert(t == &limbo->on_ack);
	const struct replication_ack *ack = event;
	/*
	 * If the limbo has no owner, this will be ack for 0 LSN (since acks
	 * have vclock[0] decoded as 0 on the receiving side, regardless what
	 * was send). 0 LSN won't bump quorum and will be just nop.
	 */
	int64_t lsn = vclock_get(ack->vclock, limbo->queue.owner_id);
	assert(limbo->queue.owner_id != REPLICA_ID_NIL || lsn == 0);
	txn_limbo_ack_queue(limbo, ack->source, lsn);
	return 0;
}

static inline void
txn_limbo_create(struct txn_limbo *limbo, struct raft *raft)
{
	memset(limbo, 0, sizeof(*limbo));
	limbo->state = TXN_LIMBO_STATE_INACTIVE;
	rlist_create(&limbo->on_state_update);
	limbo->is_in_recovery = true;
	txn_limbo_queue_create(&limbo->queue);
	latch_create(&limbo->state_latch);
	limbo->raft = raft;
	limbo->term = 1;
	limbo->worker = fiber_new_system("txn_limbo_worker",
					 txn_limbo_worker_f);
	limbo->worker->f_arg = limbo;
	fiber_set_joinable(limbo->worker, true);
	trigger_create(&limbo->on_ack, txn_limbo_on_ack_f, limbo, NULL);
	trigger_add(&replicaset.on_ack, &limbo->on_ack);
}

void
txn_limbo_update_state(struct txn_limbo *limbo)
{
	if (limbo->queue.owner_id == REPLICA_ID_NIL)
		goto make_inactive;
	if (limbo->queue.owner_id != instance_id)
		goto make_replica;
	/*
	 * Even if the node owns the limbo and was the leader before restart,
	 * it is very likely not to be the leader still afterwards. So during
	 * recovery and until the next new PROMOTE the limbo can't be fully used
	 * by this instance.
	 */
	if (limbo->is_in_recovery || !limbo->saw_promote)
		goto make_replica;
	if (limbo->is_transition_in_progress)
		goto make_replica;
	if (limbo->raft->state != RAFT_STATE_LEADER)
		goto make_replica;
	/*
	 * Even if the limbo's term is higher than of the Raft state machine,
	 * still the limbo isn't the source of truth. The limbo can't be fully
	 * used unless both states are in sync.
	 */
	if (limbo->raft->volatile_term == limbo->term)
		goto make_leader;
make_replica:
	limbo->state = TXN_LIMBO_STATE_REPLICA;
	goto end;
make_leader:
	limbo->state = TXN_LIMBO_STATE_LEADER;
	goto end;
make_inactive:
	limbo->state = TXN_LIMBO_STATE_INACTIVE;
end:
	/*
	 * Run before the ro summary update. A trigger might need to finish
	 * setting things up before the new state's effects, such as the box
	 * becoming writable, can be observed.
	 */
	trigger_run(&limbo->on_state_update, limbo);
	box_update_ro_summary();
}

/**
 * A helper to wait until all limbo entries are ready to be confirmed, i.e.
 * written to WAL and have gathered a quorum of ACKs from replicas.
 * Return lsn of the last quorum-acked limbo entry on success.
 */
static int64_t
txn_limbo_wait_acked(struct txn_limbo *limbo, double timeout)
{
	if (txn_limbo_is_empty(limbo))
		return limbo->queue.confirmed_lsn;
#ifndef NDEBUG
	++errinj(ERRINJ_WAIT_QUORUM_COUNT, ERRINJ_INT)->iparam;
#endif
	uint64_t term = limbo->term;
	double deadline = fiber_clock() + timeout;
	while (!fiber_is_cancelled()) {
		int64_t lsn = txn_limbo_last_synchro_entry(limbo)->lsn;
		if (lsn > 0 && txn_limbo_has_quorum_for(limbo, lsn))
			return lsn;
		struct trigger on_ack;
		trigger_create(&on_ack, fiber_wakeup_trigger_cb,
			       fiber(), NULL);
		trigger_add(&replicaset.on_ack, &on_ack);
		int rc = fiber_cond_wait_deadline(&limbo->queue.cond, deadline);
		trigger_clear(&on_ack);
		if (rc != 0)
			return -1;
		if (txn_limbo_check_own_term_intact(limbo, term) != 0)
			return -1;
		if (txn_limbo_is_empty(limbo))
			return limbo->queue.confirmed_lsn;
	}
	diag_set(FiberIsCancelled);
	return -1;
}

/** Execute an ownership change request (PROMOTE/DEMOTE). */
static int
txn_limbo_req_promote(struct txn_limbo *limbo, uint16_t type, int64_t lsn,
		      uint64_t term)
{
	txn_limbo_assert_locked(limbo);
	/*
	 * We make sure that promote is only written once everything this
	 * instance has may be confirmed.
	 */
	struct txn_limbo_entry *e = txn_limbo_last_synchro_entry(limbo);
	VERIFY(e == NULL || e->lsn <= lsn);
	struct synchro_request req = {
		.type = type,
		.queue_owner_id = limbo->queue.owner_id,
		.origin_id = instance_id,
		.promote = {
			.lsn = lsn,
			.term = term,
		},
	};
	/*
	 * Confirmed_vclock is only persisted in checkpoints. It doesn't
	 * appear in WALs and replication.
	 */
	vclock_clear(&req.promote.confirmed_vclock);
	if (txn_limbo_req_prepare(limbo, &req) < 0)
		return -1;
	synchro_request_write_or_panic(&req);
	txn_limbo_req_commit(limbo, &req);
	return 0;
}

void
txn_limbo_set_max_size(struct txn_limbo *limbo, int64_t size)
{
	limbo->queue.max_size = size;
}

static inline void
txn_limbo_destroy(struct txn_limbo *limbo)
{
	trigger_clear(&limbo->on_ack);
	trigger_destroy(&limbo->on_state_update);
	txn_limbo_queue_destroy(&limbo->queue);
	TRASH(limbo);
}

static inline void
txn_limbo_stop(struct txn_limbo *limbo)
{
	fiber_cancel(limbo->worker);
	VERIFY(fiber_join(limbo->worker) == 0);
}

struct txn_limbo_entry *
txn_limbo_last_synchro_entry(struct txn_limbo *limbo)
{
	return txn_limbo_queue_last_synchro_entry(&limbo->queue);
}

void
txn_limbo_rollback_all_volatile(struct txn_limbo *limbo)
{
	txn_limbo_queue_rollback_all_volatile(&limbo->queue);
}

bool
txn_limbo_would_block(struct txn_limbo *limbo)
{
	return txn_limbo_queue_would_block(&limbo->queue);
}

int
txn_limbo_submit(struct txn_limbo *limbo, uint32_t id, struct txn *txn,
		 size_t approx_len)
{
	return txn_limbo_queue_submit(&limbo->queue, id == 0 ? instance_id : id,
				      txn, approx_len);
}

int
txn_limbo_flush(struct txn_limbo *limbo)
{
	return txn_limbo_queue_flush(&limbo->queue);
}

void
txn_limbo_abort(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	txn_limbo_queue_abort(&limbo->queue, entry);
}

void
txn_limbo_assign_lsn(struct txn_limbo *limbo, uint32_t origin_id,
		     struct txn_limbo_entry *entry, int64_t lsn)
{
	txn_limbo_queue_assign_lsn(&limbo->queue, entry, lsn);
	txn_limbo_ack_queue(limbo, origin_id, lsn);
}

enum txn_limbo_wait_entry_result
txn_limbo_wait_complete(struct txn_limbo *limbo, struct txn_limbo_entry *entry)
{
	enum txn_limbo_wait_entry_result rc =
		txn_limbo_queue_wait_complete(&limbo->queue, entry);
	if (rc != TXN_LIMBO_WAIT_ENTRY_NEED_ROLLBACK)
		return rc;
	/*
	 * XXX: this whole thing is a bug. Neither infinite waiting nor the
	 * concept of a "rollback by timeout" should exist. Especially the
	 * latter since it breaks Raft guarantees. This code below should be
	 * removed in the closest major version (at the moment of writing it was
	 * upcoming 4.x).
	 */
	assert(!txn_limbo_entry_is_complete(entry));
	assert(entry->lsn >= 0);
	txn_limbo_lock(limbo);
	/*
	 * The latch might have been taken over from a PROMOTE/DEMOTE WAL write
	 * covering this entry. Then the entry is already completed by now.
	 */
	if (txn_limbo_entry_is_complete(entry) ||
	    limbo->state != TXN_LIMBO_STATE_LEADER) {
		txn_limbo_unlock(limbo);
		while (!txn_limbo_entry_is_complete(entry))
			fiber_yield();
		if (entry->state == TXN_LIMBO_ENTRY_ROLLBACK) {
			diag_set(ClientError, ER_SYNC_ROLLBACK);
			return TXN_LIMBO_WAIT_ENTRY_FAIL_COMPLETE;
		}
		return TXN_LIMBO_WAIT_ENTRY_SUCCESS;
	}
	txn_limbo_fence(limbo);
	struct synchro_request req = {
		.type = IPROTO_RAFT_ROLLBACK,
		.origin_id = instance_id,
		.queue_owner_id = limbo->queue.owner_id,
		.rollback = {
			.lsn = entry->lsn,
		},
	};
	txn_limbo_req_prepare_or_panic(limbo, &req);
	synchro_request_write_or_panic(&req);
	txn_limbo_req_commit(limbo, &req);
	assert(txn_limbo_entry_is_complete(entry));
	assert(entry->state == TXN_LIMBO_ENTRY_ROLLBACK);
	txn_limbo_unfence(limbo);
	txn_limbo_unlock(limbo);
	diag_set(ClientError, ER_SYNC_QUORUM_TIMEOUT);
	return TXN_LIMBO_WAIT_ENTRY_FAIL_COMPLETE;
}

void
txn_limbo_checkpoint(const struct txn_limbo *limbo,
		     struct synchro_request *req)
{
	req->type = IPROTO_RAFT_PROMOTE;
	req->queue_owner_id = limbo->queue.owner_id;
	req->promote.lsn = limbo->queue.confirmed_lsn;
	req->promote.term = limbo->term;
	vclock_copy(&req->promote.confirmed_vclock,
		    &limbo->queue.confirmed_vclock);
}

int
txn_limbo_promote(struct txn_limbo *limbo, uint16_t type, double timeout)
{
	struct raft *raft = limbo->raft;
	uint64_t term = raft->term;
	uint64_t limbo_term = limbo->term;
	if (txn_limbo_replica_term(limbo, instance_id) == term)
		return 0;
	int64_t wait_lsn = txn_limbo_wait_acked(limbo, timeout);
	if (wait_lsn < 0)
		return -1;
	if (type == IPROTO_RAFT_PROMOTE && raft->state != RAFT_STATE_LEADER) {
		diag_set(ClientError, ER_NOT_LEADER, raft->leader);
		return -1;
	}
	int rc = txn_limbo_check_raft_term_intact(limbo, term);
	if (rc != 0)
		return rc;
	/*
	 * Fully ready to execute the promotion now.
	 */
	txn_limbo_begin(limbo);
	rc = txn_limbo_check_raft_term_intact(limbo, term);
	if (rc != 0)
		goto tx_end;
	rc = txn_limbo_check_own_term_intact(limbo, limbo_term);
	if (rc != 0)
		goto tx_end;
	rc = txn_limbo_req_promote(limbo, type, wait_lsn, term);
tx_end:
	if (rc == 0) {
		txn_limbo_commit(limbo);
		assert(txn_limbo_is_empty(limbo));
	} else {
		txn_limbo_rollback(limbo);
	}
	return rc;
}

int
txn_limbo_wait_last_txn(struct txn_limbo *limbo, bool *is_rollback,
			double timeout)
{
	return txn_limbo_queue_wait_last_txn(&limbo->queue, is_rollback,
					     timeout);
}

/**
 * Fill the reject reason with request data.
 * The function is not reenterable, use with care.
 * May crop at TT_STATIC_BUF_LEN.
 */
static const char *
reject_str(const struct synchro_request *req)
{
	return tt_sprintf("RAFT: rejecting %s", synchro_request_str(req));
}

/** Ensure request sees the correct limbo owner. */
static int
txn_limbo_filter_owner_match(struct txn_limbo *limbo,
			     const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	if (!limbo->do_validate)
		return 0;
	if (req->queue_owner_id != limbo->queue.owner_id) {
		/*
		 * Incoming packets should esteem limbo owner,
		 * if it doesn't match it means the sender
		 * missed limbo owner migrations and is out of date.
		 */
		say_error("%s. Limbo owner mismatch, owner_id %u",
			  reject_str(req), limbo->queue.owner_id);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request from a foreign synchro queue owner");
		return -1;
	}
	return 0;
}

/** Ensure request is expecting a specific limbo owner. */
static int
txn_limbo_filter_owner_set(struct txn_limbo *limbo,
			   const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	if (!limbo->do_validate)
		return 0;
	if (req->queue_owner_id == REPLICA_ID_NIL) {
		say_error("%s. Zero replica_id detected",
			  reject_str(req));
		diag_set(ClientError, ER_UNSUPPORTED, "Replication",
			 "synchronous requests with zero replica_id");
		return -1;
	}
	return 0;
}

/** Ensure the request has a non-zero LSN whatever it is needed for. */
static int
txn_limbo_filter_non_zero_lsn(struct txn_limbo *limbo,
			      const struct synchro_request *req,
			      int64_t lsn)
{
	VERIFY(limbo->do_validate);
	if (lsn > 0)
		return 0;
	say_error("%s. Zero lsn detected", reject_str(req));
	diag_set(ClientError, ER_UNSUPPORTED, "Replication",
		 "zero LSN for CONFIRM/ROLLBACK");
	return -1;
}

/** Validate correctness of a PROMOTE/DEMOTE entry to confirm. */
static int
txn_limbo_filter_promote_pre_commit(struct txn_limbo *limbo,
				    const struct synchro_request *req,
				    const struct txn_limbo_promote_entry *e)
{
	txn_limbo_assert_locked(limbo);
	struct txn_limbo_queue *queue = &limbo->queue;
	/*
	 * The entry is self-consistent, otherwise it would have been filtered
	 * out even earlier.
	 */
	assert(e->confirm_lsn ==
	       vclock_get(&e->confirmed_vclock, e->queue_owner_id));
	assert(e->queue_owner_id != REPLICA_ID_NIL || e->confirm_lsn == 0);
	int64_t confirmed_lsn = queue->confirmed_lsn;
	int64_t to_confirm_lsn = vclock_get(&e->confirmed_vclock,
					    queue->owner_id);
	if (confirmed_lsn > to_confirm_lsn) {
		say_error("RAFT: rejecting a PROMOTE in term %llu: its confirm "
			  "lsn %lld is below the already confirmed lsn %lld",
			  (long long)e->raft_term, (long long)e->confirm_lsn,
			  (long long)confirmed_lsn);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request with lsn from an already "
			 "processed range");
		return -1;
	}
	/*
	 * Ahead in some components or incomparable (VCLOCK_ORDER_UNDEFINED
	 * is a positive value too).
	 */
	if (vclock_compare_ignore0(&queue->confirmed_vclock,
				   &e->confirmed_vclock) > 0) {
		say_error("%s. The confirmed vclock of the PROMOTE is behind "
			  "the locally confirmed vclock", reject_str(req));
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a PROMOTE with a confirmed vclock from an "
			 "already processed range");
		return -1;
	}
	if (e->raft_term <= limbo->term) {
		say_error("%s. The committing PROMOTE tries to revert "
			  "the latest confirmed term", reject_str(req));
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "trying to commit a PROMOTE reverting the "
			 "latest confirmed term");
		return -1;
	}
	if (confirmed_lsn == to_confirm_lsn)
		return 0;
	if (txn_limbo_is_empty(limbo)) {
		say_error("RAFT: rejecting a PROMOTE in term %llu: its confirm "
			  "lsn %lld is ahead of the confirmed lsn %lld and the "
			  "limbo is empty", (long long)e->raft_term,
			  (long long)to_confirm_lsn, (long long)confirmed_lsn);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request mentioning future lsn");
		return -1;
	}
	/*
	 * Some entries are present in the limbo, we need to make sure that
	 * request lsn lays inside limbo [first; last] range. So that the
	 * request has some queued data to process, otherwise it means the
	 * request comes from split brained node.
	 *
	 * XXX: this case of split brain though is only possible (excluding
	 * cases of potential stray broken requests) if this node did the
	 * rollback-by-timeout on some txns, and later a PROMOTE/DEMOTE tries to
	 * confirm them.
	 *
	 * This is one of the reasons why the rollback-by-timeout is broken by
	 * design and needs to be eliminated in future versions entirely. It can
	 * produce the split-brain even in a perfectly working cluster if the
	 * old leader would slightly lag and decided to rollback its pending
	 * synchro txns, while they are actually confirmed by a newer leader.
	 *
	 * The given check along with all the helper functions like "waiting for
	 * LSNs of the limbo entries" or "getting LSN range of the queue" can be
	 * fully deleted as soon as rollback-by-timeout nonsense is completely
	 * deprecated.
	 */
	int64_t first_lsn, last_lsn;
	txn_limbo_queue_get_lsn_range(queue, &first_lsn, &last_lsn);
	if (to_confirm_lsn < first_lsn || last_lsn < to_confirm_lsn) {
		say_error("RAFT: rejecting a PROMOTE in term %llu: its confirm "
			  "lsn %lld is out of the queue range [%lld; %lld]",
			  (long long)e->raft_term, (long long)to_confirm_lsn,
			  (long long)first_lsn, (long long)last_lsn);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a request lsn out of queue range");
		return -1;
	}
	return 0;
}

/** Validate CONFIRM request. */
static int
txn_limbo_filter_confirm(struct txn_limbo *limbo,
			 const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	assert(req->type == IPROTO_RAFT_CONFIRM);
	assert(limbo->do_validate);
	if (txn_limbo_filter_owner_set(limbo, req) != 0)
		return -1;
	if (txn_limbo_filter_non_zero_lsn(limbo, req, req->confirm.lsn) != 0)
		return -1;
	int64_t confirmed_lsn = txn_limbo_replica_confirmed_lsn(
		limbo, req->queue_owner_id);
	/*
	 * Want to confirm something new? - need to own the limbo right now, in
	 * the latest known term.
	 */
	if (req->confirm.lsn > confirmed_lsn)
		return txn_limbo_filter_owner_match(limbo, req);
	/*
	 * A CONFIRM with lsn <= known confirm lsn for this replica may be
	 * ignored without a second thought. The transactions it's going to
	 * confirm were already confirmed by one of the PROMOTE/DEMOTE requests
	 * in a new term.
	 *
	 * See that the CONFIRM can be ignored even in the current term if it
	 * wants to commit already committed txns. This is a niche case which
	 * might happen when a replica joins a master and receives a valid fully
	 * confirmed read-view from it, but some CONFIRM WAL entries might have
	 * been written by the master after the read-view is sent. Then the
	 * replica would receive those "already known" CONFIRMs during xlogs
	 * catch up.
	 *
	 * Besides, logically a confirmation of already confirmed txns doesn't
	 * contradict anything.
	 */
	return 0;
}

/** Validate ROLLBACK request. */
static int
txn_limbo_filter_rollback(struct txn_limbo *limbo,
			  const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	assert(req->type == IPROTO_RAFT_ROLLBACK);
	assert(limbo->do_validate);
	if (txn_limbo_filter_owner_set(limbo, req) != 0)
		return -1;
	if (txn_limbo_filter_non_zero_lsn(limbo, req, req->rollback.lsn) != 0)
		return -1;
	int64_t confirmed_lsn = txn_limbo_replica_confirmed_lsn(
		limbo, req->queue_owner_id);
	if (req->rollback.lsn <= confirmed_lsn)
		return txn_limbo_filter_owner_match(limbo, req);
	uint64_t origin_term = txn_limbo_replica_term(limbo,
						      req->origin_id);
	assert(origin_term <= limbo->term);
	/*
	 * Rollback in the current term wants to roll some currently waiting
	 * transactions back. No case when it can be considered outdated.
	 */
	if (origin_term == limbo->term)
		return txn_limbo_filter_owner_match(limbo, req);
	/*
	 * In older terms though this is fine to nopify it. Those txns must have
	 * already been cancelled by the new leader anyway.
	 */
	return 0;
}

/** A filter PROMOTE and DEMOTE packets. */
static int
txn_limbo_filter_promote_demote(struct txn_limbo *limbo,
				const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	assert(limbo->do_validate);
	assert(iproto_type_is_promote_request(req->type));
	/*
	 * Need all LSNs to be known. They are used to determine whether the
	 * request is safe to apply, below in the filtering logic.
	 *
	 * The queue is fenced off during the wait, so no new txns appearing
	 * in parallel would prolong the waiting.
	 */
	txn_limbo_fence(limbo);
	int rc = txn_limbo_queue_wait_writes_finished(&limbo->queue);
	txn_limbo_unfence(limbo);
	if (rc != 0)
		return -1;
	/*
	 * PROMOTE might be claiming an unclaimed limbo. But DEMOTE can't be
	 * unclaiming a nobody-owned limbo.
	 */
	if (req->type == IPROTO_RAFT_DEMOTE &&
	    txn_limbo_filter_owner_set(limbo, req) != 0)
		return -1;
	/*
	 * PROMOTE and DEMOTE packets must not have zero
	 * term supplied, otherwise it is a broken packet.
	 */
	if (req->promote.term == 0) {
		say_error("%s. Zero term detected", reject_str(req));
		diag_set(ClientError, ER_UNSUPPORTED,
			 "Replication", "PROMOTE/DEMOTE with a zero term");
		return -1;
	}
	/*
	 * Zero origin appears only in the bootstrap and checkpoint entries,
	 * which are never filtered - they only come from the initial snapshot
	 * data, when the filtering is disabled.
	 */
	if (req->origin_id == REPLICA_ID_NIL) {
		say_error("%s. Zero origin_id detected", reject_str(req));
		diag_set(ClientError, ER_UNSUPPORTED, "Replication",
			 "PROMOTE/DEMOTE with a zero origin_id");
		return -1;
	}
	/*
	 * A PROMOTE of an unowned limbo confirms nothing - a non-zero confirm
	 * lsn can't be attributed to any queue owner.
	 */
	if (req->queue_owner_id == REPLICA_ID_NIL && req->promote.lsn != 0) {
		say_error("%s. Non-zero confirm lsn without a queue owner",
			  reject_str(req));
		diag_set(ClientError, ER_UNSUPPORTED, "Replication",
			 "PROMOTE/DEMOTE with a confirm lsn but no queue "
			 "owner");
		return -1;
	}
	/*
	 * The request must be self-consistent. A request might be missing the
	 * confirmed vclock - it gets completed from the local state when the
	 * entry is built. But a vclock which is present must match the scalar
	 * fields, whoever built it.
	 */
	if (vclock_is_set(&req->promote.confirmed_vclock) &&
	    vclock_get(&req->promote.confirmed_vclock, req->queue_owner_id) !=
	    req->promote.lsn) {
		say_error("%s. The confirm lsn doesn't match its component "
			  "in the confirmed vclock", reject_str(req));
		diag_set(ClientError, ER_UNSUPPORTED, "Replication",
			 "PROMOTE/DEMOTE confirm lsn being not equal to its "
			 "component in the confirmed vclock");
		return -1;
	}
	/*
	 * If the term is already seen it means it comes from a node which
	 * didn't notice new elections, thus been living in subdomain and its
	 * data is no longer consistent.
	 */
	if (limbo->term >= req->promote.term) {
		say_error("%s. Max term seen is %llu", reject_str(req),
			  (long long)limbo->term);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a PROMOTE/DEMOTE with an obsolete term");
		return -1;
	}
	/*
	 * The request is applied at once. Hence must validate it right on the
	 * spot.
	 */
	struct txn_limbo_promote_entry e;
	txn_limbo_promote_entry_create(limbo, &e, req);
	return txn_limbo_filter_promote_pre_commit(limbo, req, &e);
}

/** A fine-grained filter checking specific request type constraints. */
static int
txn_limbo_filter_request(struct txn_limbo *limbo,
			 const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	if (!limbo->do_validate)
		return 0;
	switch (req->type) {
	case IPROTO_RAFT_CONFIRM:
		return txn_limbo_filter_confirm(limbo, req);
	case IPROTO_RAFT_ROLLBACK:
		return txn_limbo_filter_rollback(limbo, req);
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE:
		return txn_limbo_filter_promote_demote(limbo, req);
	default:
		unreachable();
	}
}

int
txn_limbo_req_prepare(struct txn_limbo *limbo,
		      const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_assert_consistent(limbo);
	if (txn_limbo_filter_request(limbo, req) < 0)
		return -1;
	/* Prepare for request execution and fine-grained filtering. */
	switch (req->type) {
	case IPROTO_RAFT_CONFIRM:
	case IPROTO_RAFT_ROLLBACK:
		break;
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE: {
		assert(!limbo->is_transition_in_progress);
		limbo->is_transition_in_progress = true;
		/*
		 * Guard against new transactions appearing during the WAL
		 * write. Otherwise a txn written right after the PROMOTE
		 * must be rolled back by this PROMOTE, but it can't be, because
		 * it would be written after it.
		 */
		txn_limbo_fence(limbo);
		txn_limbo_update_system_spaces_is_sync_state(limbo, req);
		txn_limbo_update_state(limbo);
		break;
	}
	}
	txn_limbo_assert_consistent(limbo);
	return 0;
}

void
txn_limbo_req_rollback(struct txn_limbo *limbo,
		       const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_assert_consistent(limbo);
	switch (req->type) {
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE: {
		assert(limbo->is_transition_in_progress);
		limbo->is_transition_in_progress = false;
		txn_limbo_update_system_spaces_is_sync_state(limbo, NULL);
		txn_limbo_unfence(limbo);
		txn_limbo_update_state(limbo);
		break;
	}
	default: {
		break;
	}
	}
	txn_limbo_assert_consistent(limbo);
}

/** Commit IPROTO_RAFT_CONFIRM request. */
static void
txn_limbo_req_commit_confirm(struct txn_limbo *limbo, const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_assert_consistent(limbo);
	assert(req->type == IPROTO_RAFT_CONFIRM);
	/*
	 * Check if outdated and its effects are nop / already applied before.
	 */
	if (req->queue_owner_id != limbo->queue.owner_id)
		return;
	txn_limbo_queue_apply_confirm(&limbo->queue, req->confirm.lsn);
	txn_limbo_assert_consistent(limbo);
}

/** Commit IPROTO_RAFT_ROLLBACK request. */
static void
txn_limbo_req_commit_rollback(struct txn_limbo *limbo, const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	assert(req->type == IPROTO_RAFT_ROLLBACK);
	/*
	 * Check if outdated and its effects are nop / already applied before.
	 */
	if (req->queue_owner_id != limbo->queue.owner_id)
		return;
	/*
	 * A locally created rollback is a quorum timeout. A received one means
	 * the same has happened on the queue owner.
	 */
	int64_t signature = req->origin_id == instance_id ?
		TXN_SIGNATURE_QUORUM_TIMEOUT : TXN_SIGNATURE_SYNC_ROLLBACK;
	txn_limbo_queue_apply_rollback(&limbo->queue, req->rollback.lsn,
				       signature);
	txn_limbo_assert_consistent(limbo);
}

/** Commit IPROTO_RAFT_PROMOTE/DEMOTE request. */
static void
txn_limbo_req_commit_promote_demote(struct txn_limbo *limbo,
				    const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	assert(req->type == IPROTO_RAFT_PROMOTE ||
	       req->type == IPROTO_RAFT_DEMOTE);
	assert(limbo->is_transition_in_progress);
	limbo->is_transition_in_progress = false;
	/* Nop from the bootstrap snapshot. */
	if (txn_limbo_request_is_promote_bootstrap(req)) {
		txn_limbo_unfence(limbo);
		txn_limbo_assert_consistent(limbo);
		return;
	}
	uint32_t origin = req->origin_id;
	struct txn_limbo_promote_entry *entry = &limbo->nodes[origin].pending;
	assert(entry->raft_term == 0);
	txn_limbo_promote_entry_create(limbo, entry, req);
	if (entry->raft_term == 0) {
		assert(limbo->is_in_recovery);
		txn_limbo_unfence(limbo);
		txn_limbo_assert_consistent(limbo);
		return;
	}
	txn_limbo_apply_promote(limbo, req->type, origin);
	txn_limbo_unfence(limbo);
}

void
txn_limbo_req_commit(struct txn_limbo *limbo, const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_assert_consistent(limbo);
	switch (req->type) {
	case IPROTO_RAFT_CONFIRM:
		txn_limbo_req_commit_confirm(limbo, req);
		return;
	case IPROTO_RAFT_ROLLBACK:
		txn_limbo_req_commit_rollback(limbo, req);
		return;
	}
	txn_limbo_req_commit_promote_demote(limbo, req);
}

int
txn_limbo_process(struct txn_limbo *limbo, const struct synchro_request *req)
{
	txn_limbo_begin(limbo);
	txn_limbo_assert_consistent(limbo);
	if (txn_limbo_req_prepare(limbo, req) < 0) {
		txn_limbo_rollback(limbo);
		txn_limbo_assert_consistent(limbo);
		return -1;
	}
	txn_limbo_req_commit(limbo, req);
	txn_limbo_commit(limbo);
	txn_limbo_assert_consistent(limbo);
	return 0;
}

void
txn_limbo_on_parameters_change(struct txn_limbo *limbo)
{
	txn_limbo_assert_consistent(limbo);
	/* The replication_synchro_quorum value may have changed. */
	txn_limbo_queue_bump_volatile_confirm(&limbo->queue);
	txn_limbo_assert_consistent(limbo);
	fiber_wakeup(limbo->worker);
	/*
	 * Wakeup all the others - timed out will rollback. Also
	 * there can be non-transactional waiters, such as CONFIRM
	 * waiters. They are bound to a transaction, but if they
	 * wait on replica, they won't see timeout update. Because
	 * sync transactions can live on replica infinitely.
	 */
	fiber_cond_broadcast(&limbo->queue.cond);
}

void
txn_limbo_filter_enable(struct txn_limbo *limbo)
{
	txn_limbo_lock(limbo);
	limbo->do_validate = true;
	txn_limbo_unlock(limbo);
}

void
txn_limbo_filter_disable(struct txn_limbo *limbo)
{
	txn_limbo_lock(limbo);
	limbo->do_validate = false;
	txn_limbo_unlock(limbo);
}

void
txn_limbo_finish_recovery(struct txn_limbo *limbo)
{
	assert(limbo->is_in_recovery);
	limbo->is_in_recovery = false;
	txn_limbo_update_state(limbo);
}

void
txn_limbo_init(struct raft *raft)
{
	txn_limbo_create(&txn_limbo, raft);
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
