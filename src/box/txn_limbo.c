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

/*******************************************************************************
 * Private API
 ******************************************************************************/

/** Write the request into the journal. */
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

/** Write the request into the journal. */
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

static void
txn_limbo_assert_locked(struct txn_limbo *limbo)
{
	VERIFY(latch_is_locked(&limbo->state_latch));
}

/**
 * Write a confirmation entry to the WAL. After it's written all the
 * transactions waiting for confirmation may be finished.
 */
static int
txn_limbo_write_confirm(struct txn_limbo *limbo, int64_t lsn)
{
	txn_limbo_assert_locked(limbo);
	assert(lsn > limbo->queue.confirmed_lsn);
	assert(!limbo->is_in_rollback);
	struct synchro_request req = {
		.type = IPROTO_RAFT_CONFIRM,
		.replica_id = limbo->queue.owner_id,
		.lsn = lsn,
		.term = 0,
	};
	vclock_clear(&req.confirmed_vclock);
	return synchro_request_write(&req);
}

/**
 * Write a rollback message to WAL. After it's written all the transactions
 * following the current one and waiting for confirmation must be rolled back.
 */
static void
txn_limbo_write_rollback(struct txn_limbo *limbo, int64_t lsn)
{
	txn_limbo_assert_locked(limbo);
	assert(lsn > limbo->queue.confirmed_lsn);
	assert(!limbo->is_in_rollback);
	limbo->is_in_rollback = true;
	struct synchro_request req = {
		.type = IPROTO_RAFT_ROLLBACK,
		.replica_id = limbo->queue.owner_id,
		.lsn = lsn,
		.term = 0,
	};
	vclock_clear(&req.confirmed_vclock);
	synchro_request_write_or_panic(&req);
	limbo->is_in_rollback = false;
}

static int
txn_limbo_worker_bump_confirmed_lsn(struct txn_limbo *limbo)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_queue_sanity_check(&limbo->queue);
	while (limbo->state == TXN_LIMBO_STATE_LEADER &&
	       limbo->queue.volatile_confirmed_lsn >
	       limbo->queue.confirmed_lsn) {
		if (limbo->is_in_rollback)
			return -1;
		/* It can get bumped again while we are writing. */
		int64_t lsn = limbo->queue.volatile_confirmed_lsn;
		if (txn_limbo_write_confirm(limbo, lsn) != 0) {
			diag_log();
			return -1;
		}
		ERROR_INJECT_YIELD(ERRINJ_TXN_LIMBO_WORKER_DELAY);
		txn_limbo_queue_apply_confirm(&limbo->queue, lsn);
	}
	txn_limbo_queue_sanity_check(&limbo->queue);
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

static inline void
txn_limbo_create(struct txn_limbo *limbo, struct raft *raft)
{
	memset(limbo, 0, sizeof(*limbo));
	limbo->state = TXN_LIMBO_STATE_INACTIVE;
	limbo->is_in_recovery = true;
	txn_limbo_queue_create(&limbo->queue);
	vclock_create(&limbo->promote_term_map);
	latch_create(&limbo->state_latch);
	limbo->svp_confirmed_lsn = -1;
	limbo->raft = raft;
	limbo->worker = fiber_new_system("txn_limbo_worker",
					 txn_limbo_worker_f);
	if (limbo->worker == NULL)
		panic("failed to allocate synchronous queue worker fiber");
	limbo->worker->f_arg = limbo;
	fiber_set_joinable(limbo->worker, true);
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
	 * still it is very likely not to be the leader still afterwards. So
	 * during recovery and until the next new PROMOTE the limbo can't be
	 * fully used by this instance.
	 */
	if (limbo->is_in_recovery || !limbo->saw_promote)
		goto make_replica;
	if (limbo->is_transition_in_progress)
		goto make_replica;
	/*
	 * A crutch for the "Raft-less" synchronous replication. In that mode
	 * the Raft state machine is maintained up to date, but it plays no role
	 * in the limbo's state.
	 */
	if (!raft_is_enabled(limbo->raft))
		goto make_leader;
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
	box_update_ro_summary();
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

void
txn_limbo_set_max_size(struct txn_limbo *limbo, int64_t size)
{
	limbo->queue.max_size = size;
}

static inline void
txn_limbo_destroy(struct txn_limbo *limbo)
{
	txn_limbo_queue_destroy(&limbo->queue);
	TRASH(limbo);
}

static inline void
txn_limbo_stop(struct txn_limbo *limbo)
{
	fiber_cancel(limbo->worker);
	VERIFY(fiber_join(limbo->worker) == 0);
}

bool
txn_limbo_is_ro(struct txn_limbo *limbo)
{
	return limbo->state == TXN_LIMBO_STATE_REPLICA;
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
txn_limbo_assign_lsn(struct txn_limbo *limbo, struct txn_limbo_entry *entry,
		     int64_t lsn)
{
	txn_limbo_queue_assign_lsn(&limbo->queue, entry, lsn);
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
	if (limbo->state != TXN_LIMBO_STATE_LEADER) {
		txn_limbo_unlock(limbo);
		do {
			fiber_yield();
		} while (!txn_limbo_entry_is_complete(entry));
		if (entry->state == TXN_LIMBO_ENTRY_ROLLBACK) {
			diag_set(ClientError, ER_SYNC_ROLLBACK);
			return TXN_LIMBO_WAIT_ENTRY_FAIL_COMPLETE;
		}
		return TXN_LIMBO_WAIT_ENTRY_SUCCESS;
	}
	txn_limbo_write_rollback(limbo, entry->lsn);
	txn_limbo_queue_apply_rollback(&limbo->queue, entry->lsn,
				       TXN_SIGNATURE_QUORUM_TIMEOUT);
	assert(txn_limbo_entry_is_complete(entry));
	assert(entry->state == TXN_LIMBO_ENTRY_ROLLBACK);
	txn_limbo_unlock(limbo);
	diag_set(ClientError, ER_SYNC_QUORUM_TIMEOUT);
	return TXN_LIMBO_WAIT_ENTRY_FAIL_COMPLETE;
}

void
txn_limbo_checkpoint(const struct txn_limbo *limbo,
		     struct synchro_request *req)
{
	req->type = IPROTO_RAFT_PROMOTE;
	req->replica_id = limbo->queue.owner_id;
	req->lsn = limbo->queue.confirmed_lsn;
	req->term = limbo->term;
	vclock_copy(&req->confirmed_vclock, &limbo->queue.confirmed_vclock);
}

int
txn_limbo_write_promote(struct txn_limbo *limbo, int64_t lsn, uint64_t term)
{
	txn_limbo_assert_locked(limbo);
	/*
	 * We make sure that promote is only written once everything this
	 * instance has may be confirmed.
	 */
	struct txn_limbo_entry *e = txn_limbo_last_synchro_entry(limbo);
	assert(e == NULL || e->lsn <= lsn);
	(void) e;
	struct synchro_request req = {
		.type = IPROTO_RAFT_PROMOTE,
		.replica_id = limbo->queue.owner_id,
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

int
txn_limbo_write_demote(struct txn_limbo *limbo, int64_t lsn, uint64_t term)
{
	txn_limbo_assert_locked(limbo);
	struct txn_limbo_entry *e = txn_limbo_last_synchro_entry(limbo);
	assert(e == NULL || e->lsn <= lsn);
	(void)e;
	struct synchro_request req = {
		.type = IPROTO_RAFT_DEMOTE,
		.replica_id = limbo->queue.owner_id,
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
 * Check that some synchronous transactions have gathered quorum and
 * write a confirmation entry of the last confirmed transaction.
 */
static void
txn_limbo_confirm(struct txn_limbo *limbo)
{
	assert(limbo->state == TXN_LIMBO_STATE_LEADER);
	if (limbo->is_in_rollback)
		return;
	if (txn_limbo_queue_bump_volatile_confirm(&limbo->queue))
		fiber_wakeup(limbo->worker);
}

void
txn_limbo_ack(struct txn_limbo *limbo, uint32_t replica_id, int64_t lsn)
{
	assert(limbo->state == TXN_LIMBO_STATE_LEADER);
	assert(!txn_limbo_is_ro(limbo));
	if (txn_limbo_queue_ack(&limbo->queue, replica_id, lsn))
		fiber_wakeup(limbo->worker);
}

int
txn_limbo_wait_last_txn(struct txn_limbo *limbo, bool *is_rollback,
			double timeout)
{
	return txn_limbo_queue_wait_last_txn(&limbo->queue, is_rollback,
					     timeout);
}

int
txn_limbo_wait_empty(struct txn_limbo *limbo, double timeout)
{
	return txn_limbo_queue_wait_empty(&limbo->queue, timeout);
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
	txn_limbo_assert_locked(limbo);
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
	if (req->replica_id != limbo->queue.owner_id) {
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

/**
 * Filter CONFIRM and ROLLBACK packets.
 */
static int
txn_limbo_filter_confirm_rollback(struct txn_limbo *limbo,
				  const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
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
	txn_limbo_assert_locked(limbo);
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
	if (limbo->term >= req->term) {
		say_error("%s. Max term seen is %llu", reject_str(req),
			  (long long)limbo->term);
		diag_set(ClientError, ER_SPLIT_BRAIN,
			 "got a PROMOTE/DEMOTE with an obsolete term");
		return -1;
	}
	/*
	 * Explicit split brain situation. Request comes in with an old LSN
	 * which we've already processed.
	 */
	if (limbo->queue.confirmed_lsn > req->lsn) {
		say_error("%s. confirmed lsn %lld > request lsn %lld",
			  reject_str(req),
			  (long long)limbo->queue.confirmed_lsn,
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
	if (limbo->queue.confirmed_lsn == req->lsn)
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
			  (long long)limbo->queue.confirmed_lsn,
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
	int64_t first_lsn, last_lsn;
	txn_limbo_queue_get_lsn_range(&limbo->queue, &first_lsn, &last_lsn);
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
	txn_limbo_assert_locked(limbo);
	if (!limbo->do_validate)
		return 0;
	/*
	 * Need all LSNs to be known. They will be used to determine whether
	 * filtered request is safe to apply.
	 */
	if (txn_limbo_queue_wait_persisted(&limbo->queue) < 0)
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
	txn_limbo_assert_locked(limbo);
	/* Do not enable synchronous replication during bootstrap. */
	if (req->origin_id == REPLICA_ID_NIL)
		return;
	uint16_t req_type = req->type;
	assert(req_type == IPROTO_RAFT_PROMOTE ||
	       req_type == IPROTO_RAFT_DEMOTE);
	bool is_promote = req_type == IPROTO_RAFT_PROMOTE;
	/* Synchronous replication is already enabled. */
	if (is_promote && limbo->queue.owner_id != REPLICA_ID_NIL)
		return;
	/* Synchronous replication is already disabled. */
	if (!is_promote && limbo->queue.owner_id == REPLICA_ID_NIL) {
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
	txn_limbo_assert_locked(limbo);
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
		assert(!limbo->is_transition_in_progress);
		limbo->is_transition_in_progress = true;
		limbo->svp_confirmed_lsn = limbo->queue.volatile_confirmed_lsn;
		limbo->queue.volatile_confirmed_lsn = req->lsn;
		txn_limbo_update_system_spaces_is_sync_state(
			limbo, req, /*is_rollback=*/false);
		txn_limbo_update_state(limbo);
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
	txn_limbo_assert_locked(limbo);
	switch (req->type) {
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE: {
		assert(limbo->is_in_rollback);
		assert(limbo->svp_confirmed_lsn >= 0);
		assert(limbo->is_transition_in_progress);
		limbo->is_transition_in_progress = false;
		limbo->queue.volatile_confirmed_lsn = limbo->svp_confirmed_lsn;
		limbo->svp_confirmed_lsn = -1;
		txn_limbo_update_system_spaces_is_sync_state(
			limbo, req, /*is_rollback=*/true);
		limbo->is_in_rollback = false;
		txn_limbo_update_state(limbo);
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

void
txn_limbo_req_commit(struct txn_limbo *limbo, const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	switch (req->type) {
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE: {
		assert(limbo->svp_confirmed_lsn >= 0);
		assert(limbo->is_in_rollback);
		assert(limbo->is_transition_in_progress);
		limbo->is_transition_in_progress = false;
		limbo->svp_confirmed_lsn = -1;
		limbo->is_in_rollback = false;
		txn_limbo_update_state(limbo);
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
		if (term > limbo->term)
			limbo->term = term;
	}
	if (vclock_is_set(&req->confirmed_vclock)) {
		vclock_copy(&limbo->queue.confirmed_vclock,
			    &req->confirmed_vclock);
	}

	int64_t lsn = req->lsn;
	if (req->type == IPROTO_RAFT_CONFIRM) {
		txn_limbo_queue_apply_confirm(&limbo->queue, lsn);
		return;
	}
	if (req->type == IPROTO_RAFT_ROLLBACK) {
		txn_limbo_queue_apply_rollback(&limbo->queue, lsn,
					       TXN_SIGNATURE_SYNC_ROLLBACK);
		return;
	}
	assert(req->type == IPROTO_RAFT_PROMOTE ||
	       req->type == IPROTO_RAFT_DEMOTE);
	uint32_t new_owner = REPLICA_ID_NIL;
	if (req->type == IPROTO_RAFT_PROMOTE) {
		if (!limbo->is_in_recovery)
			limbo->saw_promote = true;
		new_owner = req->origin_id;
	}
	txn_limbo_queue_transfer_ownership(&limbo->queue, new_owner, lsn);
	txn_limbo_update_state(limbo);
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
	/* The replication_synchro_quorum value may have changed. */
	if (limbo->state == TXN_LIMBO_STATE_LEADER)
		txn_limbo_confirm(limbo);
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
