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

#define limbo_debug say_info

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
		"%s{owner: %u, origin: %u, lsn: %lld, term: %llu, wait_ack: %d",
		req->type == IPROTO_RAFT_PROMOTE ? "PROMOTE" : "DEMOTE",
		req->queue_owner_id, req->origin_id,
		(long long)req->promote.lsn, (long long)req->promote.term,
		req->promote.wait_ack);
	if (vclock_calc_sum(&req->promote.confirmed_vclock) > 0) {
		SNPRINT(total, snprintf, buf, size, ", vclock: ");
		SNPRINT(total, vclock_snprint, buf, size,
			&req->promote.confirmed_vclock);
	}
	if (vclock_calc_sum(&req->promote.term_map) > 0) {
		SNPRINT(total, snprintf, buf, size, ", term_map: ");
		SNPRINT(total, vclock_snprint, buf, size,
			&req->promote.term_map);
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

/**
 * Log the rejection of the given synchro request with the error installed in
 * the diag, and yield -1, so the callers could reject a request in a single
 * 'return' statement.
 */
#define synchro_request_error(req) ({					\
	say_error("RAFT: rejecting %s. %s", synchro_request_str(req),	\
		  diag_last_error(diag_get())->errmsg);			\
	-1;								\
})

/** Reject the given request as a split-brain with the given reason. */
#define synchro_request_error_split_brain(req, ...) ({			\
	diag_set(ClientError, ER_SPLIT_BRAIN, tt_sprintf(__VA_ARGS__));	\
	synchro_request_error(req);					\
})

/** Reject the given request as an unsupported replication feature. */
#define synchro_request_error_unsupported(req, ...) ({			\
	diag_set(ClientError, ER_UNSUPPORTED, "Replication",		\
		 tt_sprintf(__VA_ARGS__));				\
	synchro_request_error(req);					\
})

static int
txn_limbo_promote_entry_snprintf(char *buf, int size,
				 const struct txn_limbo_promote_entry *entry)
{
	int total = 0;
	SNPRINT(total, snprintf, buf, size, "ENTRY{term: %lld, owner: %u, "
		"lsn: %lld", (long long)entry->raft_term, entry->queue_owner_id,
		(long long)entry->confirm_lsn);
	SNPRINT(total, snprintf, buf, size, ", vclock: ");
	SNPRINT(total, vclock_snprint, buf, size, &entry->confirmed_vclock);
	SNPRINT(total, snprintf, buf, size, ", term_map: ");
	SNPRINT(total, vclock_snprint, buf, size, &entry->term_map);
	SNPRINT(total, snprintf, buf, size, "}");
	return total;
}

static const char *
txn_limbo_promote_entry_str(const struct txn_limbo_promote_entry *entry)
{
	char *buf = tt_static_buf();
	if (txn_limbo_promote_entry_snprintf(buf, TT_STATIC_BUF_LEN, entry) < 0)
		panic("couldn't stringify a promote entry");
	return buf;
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

static const char *
txn_limbo_state_str(enum txn_limbo_state state)
{
	if (state == TXN_LIMBO_STATE_INACTIVE)
		return "inactive";
	if (state == TXN_LIMBO_STATE_LEADER)
		return "leader";
	if (state == TXN_LIMBO_STATE_REPLICA)
		return "replica";
	panic("unknown state");
	return NULL;
}

/** Fill the map with the terms of the latest PROMOTEs of all the nodes. */
static void
txn_limbo_fill_term_map(const struct txn_limbo *limbo, struct vclock *map)
{
	for (uint32_t i = 0; i < VCLOCK_MAX; i++) {
		uint64_t term = limbo->nodes[i].latest_term;
		if (term != 0)
			vclock_reset(map, i, term);
	}
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
 * Build a pending PROMOTE entry from a PROMOTE/DEMOTE request. A legacy
 * request, which doesn't wait for acks, might be missing the term map or the
 * confirmed vclock. Such a request is always a single promotion, never a
 * merge of several ones, and rows of one origin are strictly ordered, so it
 * can't be telling anything about the other nodes which the local instance
 * doesn't already know. The missing parts are completed from the local state
 * - the same values the sender would have built into the request.
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
	vclock_copy(&e->term_map, &req->promote.term_map);
	if (req->promote.wait_ack)
		return;
	if (!vclock_is_set(&e->term_map) &&
	    req->origin_id != REPLICA_ID_NIL) {
		txn_limbo_fill_term_map(limbo, &e->term_map);
		vclock_reset(&e->term_map, req->origin_id, e->raft_term);
	}
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
	if (vclock_calc_sum(&e->promote.term_map) > 0)
		return false;
	return !e->promote.wait_ack;
}

static void
txn_limbo_assert_locked(struct txn_limbo *limbo)
{
	VERIFY(latch_is_locked(&limbo->state_latch));
}

/**
 * Find the node holding the given term as its latest applied or pending
 * PROMOTE term. Return its replica ID or -1 when the term is not occupied.
 */
static int
txn_limbo_find_exact_term(const struct txn_limbo *limbo, uint64_t term)
{
	assert(term != 0);
	for (size_t i = 0; i < lengthof(limbo->nodes); ++i) {
		const struct txn_limbo_node *n = &limbo->nodes[i];
		if (n->latest_term == term || n->pending.raft_term == term)
			return i;
	}
	return -1;
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
	struct txn_limbo_promote_entry *entry =
		&limbo->nodes[instance_id].pending;
	struct txn_limbo_promote_state *state = &limbo->own_promote;
	struct raft *raft = limbo->raft;
	uint32_t owner_id = limbo->queue.owner_id;
	if (owner_id != REPLICA_ID_NIL)
		VERIFY(limbo->term == limbo->nodes[owner_id].latest_term);
	/*
	 * If the own promote is actually pending, then there can be no newer
	 * promotions or term bumps or other state disruptions, or they would
	 * have reset this node's promote.
	 */
	if (entry->raft_term != 0 && state->journal_lsn != 0) {
		for (size_t i = 0; i < lengthof(limbo->nodes); ++i) {
			struct txn_limbo_node *n = &limbo->nodes[i];
			VERIFY(i == instance_id ||
			       n->pending.raft_term < entry->raft_term);
		}
		VERIFY(raft->state == RAFT_STATE_LEADER);
		VERIFY(raft->volatile_term == entry->raft_term);
	}
	/*
	 * A term can host at most one PROMOTE, so each node's terms can be
	 * found only in that very node.
	 */
	for (size_t i = 0; i < lengthof(limbo->nodes); ++i) {
		struct txn_limbo_node *n = &limbo->nodes[i];
		VERIFY(n->latest_term <= limbo->term);
		if (n->pending.raft_term != 0) {
			VERIFY(n->pending.raft_term != n->latest_term);
			VERIFY(txn_limbo_find_exact_term(
				limbo, n->pending.raft_term) == (int)i);
		}
		if (n->latest_term != 0) {
			VERIFY(txn_limbo_find_exact_term(
				limbo, n->latest_term) == (int)i);
		}
	}
	/*
	 * The pending promotes are not trying to revert any states back.
	 */
	for (size_t i = 0; i < lengthof(limbo->nodes); ++i) {
		struct txn_limbo_promote_entry *e = &limbo->nodes[i].pending;
		if (e->raft_term == 0)
			continue;
		VERIFY(e->raft_term > limbo->term);
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
		/*
		 * Origin might be 0 when this is a checkpoint received during
		 * master snapshot fetch.
		 */
		if (i != REPLICA_ID_NIL) {
			VERIFY((int64_t)e->raft_term ==
			       vclock_get(&e->term_map, i));
		}
		for (size_t id = 0; id < lengthof(limbo->nodes); ++id) {
			VERIFY(vclock_get(&e->term_map, id) >=
			       (int64_t)limbo->nodes[id].latest_term);
		}
	}
	VERIFY(limbo->nodes[REPLICA_ID_NIL].latest_term == 0);
	/*
	 * Queue confirmed LSNs are valid.
	 */
	VERIFY(queue->confirmed_lsn ==
	       vclock_get(&queue->confirmed_vclock, owner_id));
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

/** Check the instance is the Raft leader in the given unchanged term. */
static int
txn_limbo_check_raft_leadership_intact(const struct txn_limbo *limbo,
				       uint64_t raft_term)
{
	const struct raft *raft = limbo->raft;
	if (raft->state == RAFT_STATE_LEADER &&
	    raft->volatile_term == raft_term)
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
 * limbo state: they are synchronous while the queue has an owner or any
 * promotion is pending.
 *
 * The request, when not NULL, is an in-progress PROMOTE/DEMOTE whose outcome
 * is applied optimistically, before its WAL write: a PROMOTE is about to
 * become a pending promotion, a DEMOTE is about to drop the ownership and
 * sweep the pending promotions up to its term - only ones with newer terms
 * would keep the spaces synchronous then. A WAL failure restores the actual
 * state via the rollback.
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
		for (size_t i = 0; !is_sync && i < lengthof(limbo->nodes);
		     ++i) {
			is_sync = limbo->nodes[i].pending.raft_term >
				  req->promote.term;
		}
	} else {
		is_sync = limbo->queue.owner_id != REPLICA_ID_NIL;
		for (size_t i = 0; !is_sync && i < lengthof(limbo->nodes);
		     ++i)
			is_sync = limbo->nodes[i].pending.raft_term != 0;
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
		limbo_debug("limbo: confirming queue at %lld",
			    (long long)req.confirm.lsn);
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
 * Stop tracking the own pending PROMOTE. Called when the pending is cleared
 * by either being applied via CONFIRM or being superseded by a higher-term
 * PROMOTE from another instance, and when the Raft leadership in the
 * PROMOTE's term is lost.
 */
static void
txn_limbo_drop_own_promote(struct txn_limbo *limbo)
{
	struct txn_limbo_promote_state *state = &limbo->own_promote;
	trigger_clear(&state->on_ack);
	state->journal_lsn = 0;
	vclock_clear(&state->acks);
}

/**
 * Apply a confirmed pending PROMOTE: transfer queue ownership to the
 * promote's origin, catch up per-instance terms, inherit the chained
 * confirmed_vclock, and drop pending PROMOTEs superseded by this term.
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
	limbo_debug("limbo: apply promote: from %u: %s", origin,
		    txn_limbo_promote_entry_str(p));
	/* 0 is never allowed, and 1 is the special bootstrap promotion. */
	assert(p->raft_term > 1);
	uint64_t applied_term = p->raft_term;
	for (uint32_t i = 0; i < VCLOCK_MAX; i++) {
		uint64_t t = vclock_get(&p->term_map, i);
		if (t > limbo->nodes[i].latest_term)
			limbo->nodes[i].latest_term = t;
	}
	if (applied_term > origin_node->latest_term && origin != REPLICA_ID_NIL)
		origin_node->latest_term = applied_term;
	if (applied_term > limbo->term)
		limbo->term = applied_term;
	if (!limbo->is_in_recovery)
		limbo->saw_promote = true;
	/*
	 * The PROMOTE confirms its prev owner's txns up to confirm_lsn. The
	 * owner might legitimately mismatch:
	 *
	 * - An older pending PROMOTE got confirmed after this one was
	 *   written. None of the queued txns are covered then - the lsn is
	 *   not even comparable with their lsns. They are all rolled back,
	 *   like the spec does.
	 * - A checkpoint PROMOTE from a JOIN stream or recovery describes an
	 *   already established ownership, while the local queue isn't
	 *   claimed yet. There is nothing to confirm then.
	 * - With the filter disabled the request can also carry a completely
	 *   foreign ownership history - nothing is confirmed either.
	 */
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
		if (other->raft_term != 0 && other->raft_term <= applied_term)
			memset(other, 0, sizeof(*other));
	}
	if (limbo->nodes[instance_id].pending.raft_term == 0)
		txn_limbo_drop_own_promote(limbo);
	txn_limbo_update_system_spaces_is_sync_state(limbo, NULL);
	txn_limbo_update_state(limbo);
	txn_limbo_assert_consistent(limbo);
	limbo_debug("limbo: apply promote: finished");
}

/**
 * If our own pending PROMOTE has gathered quorum and we are still leader in
 * its term, write the CONFIRM that applies it and apply locally.
 */
static int
txn_limbo_worker_bump_pending_promote(struct txn_limbo *limbo)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_assert_consistent(limbo);
	struct txn_limbo_promote_entry *entry =
		&limbo->nodes[instance_id].pending;
	/* No pending promote. */
	if (entry->raft_term == 0)
		return 0;
	/*
	 * Not written into the journal OR it is from an older term, chained to
	 * a newer promote already.
	 */
	struct txn_limbo_promote_state *state = &limbo->own_promote;
	if (state->journal_lsn == 0) {
		limbo_debug("limbo: bump pending promote: waiting journal "
			    "write");
		return 0;
	}
	/*
	 * The PROMOTE is the transaction of the election itself, so it is
	 * committed by the quorum which elects leaders, not by the synchro
	 * quorum. With elections enabled they match, except that the election
	 * quorum is clamped to the count of registered replicas - otherwise a
	 * bootstrap leader could never claim the queue, being the only member
	 * of the new replicaset. With elections off it is 1, keeping the
	 * legacy promotion working, which never collected any acks at all.
	 */
	if (vclock_count_ge(&state->acks, state->journal_lsn) <
	    limbo->raft->election_quorum)
		return 0;
	limbo_debug("limbo: bump pending promote: writing confirm for owner "
		    "%u, LSN %lld", entry->queue_owner_id,
		    (long long)entry->confirm_lsn);
	struct synchro_request req = {
		.type = IPROTO_RAFT_CONFIRM,
		.origin_id = instance_id,
		.queue_owner_id = entry->queue_owner_id,
		.confirm = {
			.lsn = entry->confirm_lsn,
		},
	};
	if (txn_limbo_req_prepare(limbo, &req) != 0) {
		diag_log();
		return -1;
	}
	if (synchro_request_write(&req) < 0) {
		diag_log();
		txn_limbo_req_rollback(limbo, &req);
		return -1;
	}
	/*
	 * The leadership might have been lost during the CONFIRM WAL write -
	 * then the own promote tracking is already dropped. The CONFIRM is
	 * durable regardless, so the promotion is applied anyway.
	 */
	assert(entry->raft_term != 0);
	txn_limbo_req_commit(limbo, &req);
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
		int rc = txn_limbo_worker_bump_pending_promote(limbo);
		if (rc == 0)
			rc = txn_limbo_worker_bump_confirmed_lsn(limbo);
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

/**
 * Ack trigger for the own pending PROMOTE. Updates the per-replica acks
 * vclock and wakes up the worker once a quorum on the PROMOTE's journal LSN
 * is reached. Installed on the replicaset's ack list right before the own
 * PROMOTE's WAL write starts, so the acks arriving while the write is in
 * progress are not lost. The pending entry itself appears only when the
 * write ends.
 */
static int
txn_limbo_on_promote_ack_f(struct trigger *t, void *event)
{
	struct txn_limbo *limbo = t->data;
	struct txn_limbo_promote_state *state = &limbo->own_promote;
	assert(t == &state->on_ack);
	txn_limbo_assert_consistent(limbo);
	const struct replication_ack *ack = event;
	int64_t self_lsn = vclock_get(ack->vclock, instance_id);
	int64_t prev = vclock_get(&state->acks, ack->source);
	if (self_lsn <= prev)
		return 0;
	vclock_follow(&state->acks, ack->source, self_lsn);
	int64_t journal_lsn = state->journal_lsn;
	if (journal_lsn == 0)
		return 0;
	if (vclock_count_ge(&state->acks, journal_lsn) >=
	    limbo->raft->election_quorum) {
	    	limbo_debug("limbo: promote ack: got quorum");
		fiber_wakeup(limbo->worker);
		trigger_clear(t);
	}
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
	trigger_create(&limbo->own_promote.on_ack, txn_limbo_on_promote_ack_f,
		       limbo, NULL);
	vclock_create(&limbo->own_promote.acks);
}

void
txn_limbo_update_state(struct txn_limbo *limbo)
{
	enum txn_limbo_state state = limbo->state;
	struct raft *raft = limbo->raft;
	/*
	 * The own pending PROMOTE can be confirmed by this instance only
	 * while it remains the Raft leader in that PROMOTE's term. On
	 * leadership loss the pending entry stays, like on all the other
	 * instances, until a newer promotion supersedes it. But its
	 * confirmation is not this instance's job anymore.
	 */
	if (limbo->own_promote.journal_lsn != 0) {
		uint64_t own_term =
			limbo->nodes[instance_id].pending.raft_term;
		assert(own_term != 0);
		if (raft->state != RAFT_STATE_LEADER ||
		    raft->volatile_term != own_term)
			txn_limbo_drop_own_promote(limbo);
	}
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
	if (raft->state != RAFT_STATE_LEADER)
		goto make_replica;
	/*
	 * Even if the limbo's term is higher than of the Raft state machine,
	 * still the limbo isn't the source of truth. The limbo can't be fully
	 * used unless both states are in sync.
	 */
	if (raft->volatile_term == limbo->term)
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
	if (limbo->state != state) {
		limbo_debug("limbo: state change %s -> %s",
			    txn_limbo_state_str(state),
			    txn_limbo_state_str(limbo->state));
	}
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

/** Execute a DEMOTE request. */
static int
txn_limbo_req_demote(struct txn_limbo *limbo, int64_t lsn, uint64_t raft_term,
		     uint64_t limbo_term)
{
	/*
	 * It can't wait for a quorum on itself before being applied, because it
	 * would mean this instance would have to write a CONFIRM while not
	 * being a Raft or limbo leader. This in turn is because DEMOTE has to
	 * be done in a new leader-less term, making it the last one before the
	 * limbo is turned off.
	 */
	txn_limbo_assert_locked(limbo);
	if (txn_limbo_check_raft_term_intact(limbo, raft_term) != 0)
		return -1;
	if (txn_limbo_check_own_term_intact(limbo, limbo_term) != 0)
		return -1;
	/*
	 * We make sure that demote is only written once everything this
	 * instance has may be confirmed.
	 */
	struct txn_limbo_entry *e = txn_limbo_last_synchro_entry(limbo);
	VERIFY(e == NULL || e->lsn <= lsn);
	struct synchro_request req = {
		.type = IPROTO_RAFT_DEMOTE,
		.queue_owner_id = limbo->queue.owner_id,
		.origin_id = instance_id,
		.promote = {
			.lsn = lsn,
			.term = raft_term,
		},
	};
	vclock_copy(&req.promote.confirmed_vclock,
		    &limbo->queue.confirmed_vclock);
	vclock_create(&req.promote.term_map);
	assert(req.queue_owner_id != REPLICA_ID_NIL);
	vclock_reset(&req.promote.confirmed_vclock,
		     req.queue_owner_id, req.promote.lsn);
	txn_limbo_fill_term_map(limbo, &req.promote.term_map);
	vclock_follow(&req.promote.term_map, instance_id, raft_term);
	limbo_debug("limbo: req demote: prepare");
	if (txn_limbo_req_prepare(limbo, &req) < 0) {
		limbo_debug("limbo: req demote: prepare - fail");
		return -1;
	}
	limbo_debug("limbo: req demote: write");
	synchro_request_write_or_panic(&req);
	limbo_debug("limbo: req demote: commit");
	txn_limbo_req_commit(limbo, &req);
	return 0;
}

/** Execute a PROMOTE request. */
static int
txn_limbo_req_promote(struct txn_limbo *limbo, uint64_t raft_term,
		      uint64_t limbo_term)
{
	/*
	 * It is an actually synchronous transaction of its own. Which needs to
	 * wait for a quorum and be committed via a CONFIRM. This guarantees
	 * that no newer PROMOTE would quickly appear and get executed somewhere
	 * else, conflicting the results of this PROMOTE.
	 *
	 * In vanilla Raft terms it is the first transaction in a new term via
	 * which the new leader can commit the still pending transactions of the
	 * previous leader.
	 */
	txn_limbo_assert_locked(limbo);
	txn_limbo_assert_consistent(limbo);
	if (txn_limbo_check_raft_leadership_intact(limbo, raft_term) != 0)
		return -1;
	if (txn_limbo_check_own_term_intact(limbo, limbo_term) != 0)
		return -1;
	/*
	 * The PROMOTE is built out of the LSNs of the queued transactions, so
	 * they all need to be known. The queue is fenced off during the wait,
	 * so no new txns appearing in parallel would prolong the waiting.
	 * After the wait there are no yields until the PROMOTE's own journal
	 * write - the built request can't go stale.
	 */
	txn_limbo_fence(limbo);
	int rc = txn_limbo_queue_wait_writes_finished(&limbo->queue);
	txn_limbo_unfence(limbo);
	if (rc != 0)
		return -1;
	if (txn_limbo_check_raft_leadership_intact(limbo, raft_term) != 0)
		return -1;
	if (txn_limbo_check_own_term_intact(limbo, limbo_term) != 0)
		return -1;
	struct synchro_request req = {
		.type = IPROTO_RAFT_PROMOTE,
		.origin_id = instance_id,
		.promote = {
			.term = raft_term,
			.wait_ack = true,
		},
	};
	vclock_create(&req.promote.term_map);
	limbo_debug("limbo: req promote: build for term %lld",
		    (long long)raft_term);
	/*
	 * Chained promotion. An older pending PROMOTE, even one from another
	 * instance, can still get confirmed after this new PROMOTE is
	 * written. The new PROMOTE must then lead to the exact same state
	 * when its own CONFIRM is applied. Hence it inherits the previous
	 * owner, the confirm boundary, and the confirmed vclock from the
	 * latest pending PROMOTE instead of the local limbo state, which that
	 * PROMOTE would supersede.
	 */
	const struct txn_limbo_promote_entry *latest = NULL;
	uint32_t latest_origin = 0;
	for (uint32_t i = 0; i < lengthof(limbo->nodes); i++) {
		const struct txn_limbo_promote_entry *p =
			&limbo->nodes[i].pending;
		if (p->raft_term == 0)
			continue;
		if (latest == NULL || p->raft_term > latest->raft_term) {
			latest_origin = i;
			latest = p;
		}
	}
	if (latest != NULL) {
		limbo_debug("limbo: req promote: found older from %u: %s",
			    latest_origin, txn_limbo_promote_entry_str(latest));
		req.queue_owner_id = latest->queue_owner_id;
		req.promote.lsn = latest->confirm_lsn;
		vclock_copy(&req.promote.confirmed_vclock,
			    &latest->confirmed_vclock);
		vclock_copy(&req.promote.term_map, &latest->term_map);
	} else {
		limbo_debug("limbo: req promote: making new");
		/*
		 * Nothing is pending - the PROMOTE confirms everything this
		 * instance has in its journal from the current owner. The own
		 * quorum of the PROMOTE will certify all of it, being
		 * replicated strictly after these transactions.
		 *
		 * Volatile entries are not sent to the journal yet. The
		 * PROMOTE is going to enter the journal before them and can't
		 * cover them. They get rolled back when the promotion takes
		 * effect.
		 */
		struct txn_limbo_entry *e =
			txn_limbo_queue_last_written_synchro_entry(
				&limbo->queue);
		assert(e == NULL || e->lsn > 0);
		int64_t last_lsn = e != NULL ?
			e->lsn : limbo->queue.confirmed_lsn;
		req.queue_owner_id = limbo->queue.owner_id;
		req.promote.lsn = last_lsn;
		vclock_copy(&req.promote.confirmed_vclock,
			    &limbo->queue.confirmed_vclock);
		txn_limbo_fill_term_map(limbo, &req.promote.term_map);
	}
	if (req.queue_owner_id != REPLICA_ID_NIL) {
		vclock_reset(&req.promote.confirmed_vclock,
			     req.queue_owner_id, req.promote.lsn);
	}
	vclock_follow(&req.promote.term_map, instance_id, raft_term);
	if (txn_limbo_req_prepare(limbo, &req) < 0) {
		limbo_debug("limbo: req promote: prepare fail");
		return -1;
	}
	/*
	 * Start collecting the acks before the WAL write, so the ones
	 * arriving while the write is in progress are not lost.
	 */
	txn_limbo_drop_own_promote(limbo);
	struct txn_limbo_promote_state *state = &limbo->own_promote;
	trigger_add(&replicaset.on_ack, &state->on_ack);
	limbo_debug("limbo: req promote: write");
	int64_t journal_lsn = synchro_request_write_or_panic(&req);
	/*
	 * No new promotions could be started or any other state changes
	 * committed, because the limbo lock is held.
	 */
	txn_limbo_assert_locked(limbo);
	txn_limbo_req_commit(limbo, &req);
	/*
	 * The leadership could be lost while the PROMOTE was being written.
	 * The entry then stays pending, like on all the other instances, until
	 * a newer promotion supersedes it. But its confirmation is not this
	 * instance's job anymore.
	 */
	if (txn_limbo_check_raft_leadership_intact(limbo, raft_term) != 0) {
		txn_limbo_drop_own_promote(limbo);
		txn_limbo_assert_consistent(limbo);
		return -1;
	}
	state->journal_lsn = journal_lsn;
	txn_limbo_assert_consistent(limbo);
	latest = &limbo->nodes[instance_id].pending;
	assert(state->journal_lsn != 0 && latest->raft_term != 0);
	limbo_debug("limbo: req promote: committed %s, journal LSN %lld",
		    txn_limbo_promote_entry_str(latest),
		    (long long)state->journal_lsn);
	/* The local WAL write is the own ack. */
	vclock_follow(&state->acks, instance_id, state->journal_lsn);
	fiber_wakeup(limbo->worker);
	return 0;
}

/**
 * Wait until the own pending PROMOTE written in the given term is gone:
 * either applied via its CONFIRM, which makes this instance the limbo owner,
 * or superseded by a newer-term promotion. Give up at the deadline, on a fiber
 * cancel, and when this instance is no longer the raft leader in that term -
 * then the CONFIRM can't ever be written.
 */
static int
txn_limbo_wait_own_promote(struct txn_limbo *limbo, uint64_t term,
			   double deadline)
{
	const struct txn_limbo_promote_entry *entry =
		&limbo->nodes[instance_id].pending;
	while (true) {
		if (entry->raft_term == 0) {
			if (limbo->term != term) {
				diag_set(ClientError, ER_INTERFERING_PROMOTE,
					 limbo->queue.owner_id);
				return -1;
			}
			assert(txn_limbo_is_owned_by_current_instance(limbo));
			return 0;
		}
		assert(entry->raft_term == term);
		if (txn_limbo_check_raft_leadership_intact(limbo, term) != 0)
			return -1;
		if (fiber_is_cancelled()) {
			diag_set(FiberIsCancelled);
			return -1;
		}
		struct trigger on_state_update;
		trigger_create(&on_state_update, fiber_wakeup_trigger_cb,
			       fiber(), NULL);
		trigger_add(&limbo->on_state_update, &on_state_update);
		bool is_timed_out = fiber_yield_deadline(deadline);
		trigger_clear(&on_state_update);
		if (is_timed_out) {
			diag_set(TimedOut);
			return -1;
		}
	}
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
	trigger_clear(&limbo->own_promote.on_ack);
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
	vclock_create(&req->promote.term_map);
	txn_limbo_fill_term_map(limbo, &req->promote.term_map);
	req->promote.wait_ack = false;
}

int
txn_limbo_promote(struct txn_limbo *limbo, double timeout)
{
	txn_limbo_assert_consistent(limbo);
	struct raft *raft = limbo->raft;
	uint64_t term = raft->term;
	uint64_t limbo_term = limbo->term;
	double deadline = fiber_clock() + timeout;
	if (txn_limbo_replica_term(limbo, instance_id) == term)
		return 0;
	/*
	 * The own PROMOTE for this term might be already written and still
	 * waiting for its CONFIRM, when the previous promotion attempt was
	 * interrupted, for example, by a fiber cancellation. Then just keep
	 * waiting for the existing PROMOTE instead of writing a same-term
	 * duplicate.
	 */
	if (limbo->nodes[instance_id].pending.raft_term == term)
		return txn_limbo_wait_own_promote(limbo, term, deadline);
	/*
	 * The queued transactions don't need to gather their quorums before
	 * the PROMOTE is written. The PROMOTE confirms everything this
	 * instance has in its journal from the current owner, and gets
	 * replicated strictly after all of it. The PROMOTE takes effect only
	 * when it gathers a quorum of its own - which then certifies the
	 * whole journal prefix, including the covered transactions.
	 */
	if (raft->state != RAFT_STATE_LEADER) {
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
	limbo_debug("limbo: promote: started");
	rc = txn_limbo_req_promote(limbo, term, limbo_term);
	if (rc != 0) {
		txn_limbo_rollback(limbo);
		txn_limbo_assert_consistent(limbo);
		limbo_debug("limbo: promote: error");
		return rc;
	}
	txn_limbo_commit(limbo);
	txn_limbo_assert_consistent(limbo);
	limbo_debug("limbo: promote: committed and waiting for quorum %d",
		    limbo->raft->election_quorum);
	return txn_limbo_wait_own_promote(limbo, term, deadline);
}

int
txn_limbo_demote(struct txn_limbo *limbo, double timeout)
{
	txn_limbo_assert_consistent(limbo);
	struct raft *raft = limbo->raft;
	uint64_t term = raft->term;
	uint64_t limbo_term = limbo->term;
	if (txn_limbo_replica_term(limbo, instance_id) == term)
		return 0;
	/*
	 * The DEMOTE takes effect right on its commit, without gathering any
	 * quorum of its own. Hence the transactions it is going to confirm
	 * must gather their quorums beforehand.
	 */
	int64_t wait_lsn = txn_limbo_wait_acked(limbo, timeout);
	if (wait_lsn < 0)
		return -1;
	int rc = txn_limbo_check_raft_term_intact(limbo, term);
	if (rc != 0)
		return rc;
	/*
	 * Fully ready to execute the demotion now.
	 */
	txn_limbo_begin(limbo);
	limbo_debug("limbo: demote: started");
	rc = txn_limbo_req_demote(limbo, wait_lsn, term, limbo_term);
	if (rc == 0) {
		txn_limbo_commit(limbo);
		limbo_debug("limbo: demote: committed");
		assert(txn_limbo_is_empty(limbo));
	} else {
		txn_limbo_rollback(limbo);
		limbo_debug("limbo: demote: error");
	}
	txn_limbo_assert_consistent(limbo);
	return rc;
}

int
txn_limbo_wait_last_txn(struct txn_limbo *limbo, bool *is_rollback,
			double timeout)
{
	return txn_limbo_queue_wait_last_txn(&limbo->queue, is_rollback,
					     timeout);
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
		return synchro_request_error_split_brain(
			req, "got a request from a foreign synchro queue "
			"owner, the local owner is %u", limbo->queue.owner_id);
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
		return synchro_request_error_unsupported(
			req, "synchronous requests with zero queue owner ID");
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
	return synchro_request_error_unsupported(
		req, "zero LSN for CONFIRM/ROLLBACK");
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
	assert((int64_t)e->raft_term ==
	       vclock_get(&e->term_map, req->origin_id));
	int64_t confirmed_lsn = queue->confirmed_lsn;
	int64_t to_confirm_lsn = vclock_get(&e->confirmed_vclock,
					    queue->owner_id);
	if (confirmed_lsn > to_confirm_lsn) {
		return synchro_request_error_split_brain(
			req, "got a PROMOTE with LSN from an already processed "
			"range, confirming %lld while %lld is already locally "
			"confirmed", (long long)to_confirm_lsn,
			(long long)confirmed_lsn);
	}
	/*
	 * Ahead in some components or incomparable (VCLOCK_ORDER_UNDEFINED
	 * is a positive value too).
	 */
	if (vclock_compare_ignore0(&queue->confirmed_vclock,
				   &e->confirmed_vclock) > 0) {
		return synchro_request_error_split_brain(
			req, "got a PROMOTE with a confirmed vclock from an "
			"already processed range");
	}
	if (e->raft_term <= limbo->term) {
		return synchro_request_error_split_brain(
			req, "trying to commit a PROMOTE reverting the latest "
			"confirmed term %llu", (long long)limbo->term);
	}
	/*
	 * Promotion is always unique per term. The only allowed holder of the
	 * entry's term is the entry itself, when it was pending and its
	 * CONFIRM is being validated now.
	 */
	int holder = txn_limbo_find_exact_term(limbo, e->raft_term);
	if (holder >= 0 && &limbo->nodes[holder].pending != e) {
		return synchro_request_error_split_brain(
			req, "trying to commit a PROMOTE duplicating the term "
			"from another one, of instance %d", holder);
	}
	if (confirmed_lsn == to_confirm_lsn)
		return 0;
	if (txn_limbo_is_empty(limbo)) {
		return synchro_request_error_split_brain(
			req, "got a PROMOTE mentioning future LSN, confirming "
			"%lld ahead of the locally confirmed %lld with an empty"
			" limbo", (long long)to_confirm_lsn,
			(long long)confirmed_lsn);
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
		return synchro_request_error_split_brain(
			req, "got a request LSN out of queue range, confirming "
			"%lld outside of [%lld; %lld]",
			(long long)to_confirm_lsn, (long long)first_lsn,
			(long long)last_lsn);
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
	/*
	 * When the origin has a pending PROMOTE, the only legitimate CONFIRM
	 * from it is the one that applies that PROMOTE.
	 */
	const struct txn_limbo_promote_entry *p =
		&limbo->nodes[req->origin_id].pending;
	if (p->raft_term != 0) {
		if (p->confirm_lsn != req->confirm.lsn ||
		    p->queue_owner_id != req->queue_owner_id) {
			return synchro_request_error_split_brain(
				req, "got a CONFIRM not matching the pending "
				"PROMOTE from the same origin, expecting "
				"confirm lsn %lld and owner %u",
				(long long)p->confirm_lsn,
				(unsigned)p->queue_owner_id);
		}
		return txn_limbo_filter_promote_pre_commit(limbo, req, p);
	}
	/*
	 * A zero LSN can only be carried by a CONFIRM of a pending PROMOTE
	 * which had nothing to confirm at its claim time - the queue was
	 * unclaimed or empty before it. The pending it was applying is already
	 * gone here, superseded by a newer-term promotion. Such a CONFIRM
	 * confirms nothing and is nopified, same as an old-term PROMOTE.
	 */
	if (req->confirm.lsn == 0)
		return 0;
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
	if (req->type == IPROTO_RAFT_DEMOTE && req->promote.wait_ack) {
		return synchro_request_error_unsupported(
			req, "DEMOTE waiting for acks");
	}
	/*
	 * PROMOTE and DEMOTE packets must not have zero
	 * term supplied, otherwise it is a broken packet.
	 */
	if (req->promote.term == 0) {
		return synchro_request_error_unsupported(
			req, "PROMOTE/DEMOTE with a zero term");
	}
	/*
	 * Zero origin appears only in the bootstrap and checkpoint entries,
	 * which are never filtered - they only come from the initial snapshot
	 * data, when the filtering is disabled.
	 */
	if (req->origin_id == REPLICA_ID_NIL) {
		return synchro_request_error_unsupported(
			req, "PROMOTE/DEMOTE with a zero origin_id");
	}
	/*
	 * A PROMOTE of an unowned limbo confirms nothing - a non-zero confirm
	 * lsn can't be attributed to any queue owner.
	 */
	if (req->queue_owner_id == REPLICA_ID_NIL && req->promote.lsn != 0) {
		return synchro_request_error_unsupported(
			req, "PROMOTE/DEMOTE with a confirm LSN but no queue "
			"owner");
	}
	/*
	 * The request must be self-consistent. A legacy request, which
	 * doesn't wait for acks, might be missing the term map or the
	 * confirmed vclock - those get completed from the local state when
	 * the entry is built. But the maps which are present must match the
	 * scalar fields, whoever built them.
	 */
	if ((req->promote.wait_ack ||
	     vclock_is_set(&req->promote.term_map)) &&
	    vclock_get(&req->promote.term_map, req->origin_id) !=
	    (int64_t)req->promote.term) {
		return synchro_request_error_unsupported(
			req, "PROMOTE/DEMOTE term being not equal to its "
			"component in the term map");
	}
	if ((req->promote.wait_ack ||
	     vclock_is_set(&req->promote.confirmed_vclock)) &&
	    vclock_get(&req->promote.confirmed_vclock, req->queue_owner_id) !=
	    req->promote.lsn) {
		return synchro_request_error_unsupported(
			req, "PROMOTE/DEMOTE confirm LSN is not equal to "
			"its component in the confirmed vclock");
	}
	/*
	 * A node's own promote terms only grow, and rows of one
	 * origin are strictly ordered on every delivery path. A
	 * PROMOTE going back in its origin's history is impossible.
	 */
	const struct txn_limbo_node *origin_node =
		&limbo->nodes[req->origin_id];
	if (req->promote.term <= origin_node->latest_term ||
	    req->promote.term <= origin_node->pending.raft_term) {
		return synchro_request_error_split_brain(
			req, "got a PROMOTE reverting its origin's term");
	}
	/*
	 * A term can host at most one PROMOTE. Re-use of an occupied
	 * term means a second leader elected in the same term.
	 *
	 * An old PROMOTE, superseded before getting confirmed, can
	 * still show up late through another replication channel. It
	 * collides with nothing here and is nopified on commit.
	 */
	int holder = txn_limbo_find_exact_term(limbo,
					       req->promote.term);
	if (holder >= 0) {
		return synchro_request_error_split_brain(
			req, "got a PROMOTE with an already used term, taken "
			"by instance %d", holder);
	}
	if (req->promote.wait_ack) {
		/*
		 * A pending PROMOTE takes effect only when its CONFIRM arrives,
		 * so validation of its confirm boundary is deferred until
		 * then - the old owner's transactions legitimately keep
		 * arriving and getting confirmed in the meantime. The term
		 * checks can't wait though - the request occupies its origin's
		 * pending slot right on commit.
		 */
		return 0;
	}
	/*
	 * An immediate PROMOTE/DEMOTE is applied at once. Hence must validate
	 * it right on the spot.
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

/**
 * Check whether the given request, when committed, is going to change the
 * queue ownership, deciding the fate of all the queued transactions.
 *
 * A quorum-waiting PROMOTE doesn't - on commit it only becomes pending, and
 * the txns keep flowing under the old owner. Its ownership transfer happens
 * later, when its own CONFIRM gets committed.
 */
static bool
txn_limbo_req_will_change_ownership(const struct txn_limbo *limbo,
				    const struct synchro_request *req)
{
	switch (req->type) {
	case IPROTO_RAFT_CONFIRM: {
		/* A CONFIRM of a pending PROMOTE applies it on commit. */
		const struct txn_limbo_promote_entry *p =
			&limbo->nodes[req->origin_id].pending;
		return p->raft_term != 0 && p->confirm_lsn == req->confirm.lsn;
	}
	case IPROTO_RAFT_PROMOTE:
		if (req->promote.wait_ack)
			return false;
		return !txn_limbo_request_is_promote_bootstrap(req);
	case IPROTO_RAFT_DEMOTE:
		return true;
	default:
		return false;
	}
}

int
txn_limbo_req_prepare(struct txn_limbo *limbo,
		      const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_assert_consistent(limbo);
	if (txn_limbo_filter_request(limbo, req) < 0) {
		limbo_debug("limbo: req prepare: rejected %s",
			    synchro_request_str(req));
		return -1;
	}
	limbo_debug("limbo: req prepare: %s", synchro_request_str(req));
	/*
	 * An ownership change decides the fate of all the queued txns when the
	 * request gets committed. No new txns can be let into the journal
	 * until it is done - the journal must not contain txns which are
	 * already doomed by an earlier written request.
	 */
	if (txn_limbo_req_will_change_ownership(limbo, req))
		txn_limbo_fence(limbo);
	/* Prepare for request execution and fine-grained filtering. */
	switch (req->type) {
	case IPROTO_RAFT_CONFIRM:
	case IPROTO_RAFT_ROLLBACK:
		break;
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE: {
		txn_limbo_update_system_spaces_is_sync_state(limbo, req);
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
	limbo_debug("limbo: req prepare: rollback %s",
			    synchro_request_str(req));
	/*
	 * The failed WAL write didn't change any state, so the check yields
	 * the same verdict as it did in the prepare.
	 */
	if (txn_limbo_req_will_change_ownership(limbo, req))
		txn_limbo_unfence(limbo);
	switch (req->type) {
	case IPROTO_RAFT_PROMOTE:
	case IPROTO_RAFT_DEMOTE: {
		txn_limbo_update_system_spaces_is_sync_state(limbo, NULL);
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
	if (txn_limbo_req_will_change_ownership(limbo, req)) {
		txn_limbo_apply_promote(limbo, IPROTO_RAFT_PROMOTE,
					req->origin_id);
		return;
	}
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

/**
 * Save a new-style PROMOTE as the pending entry of its origin. The entry
 * takes effect only when the matching CONFIRM is applied. Old entries
 * (already covered by a confirmed PROMOTE or by a newer pending one from
 * the same origin) are silently nopified.
 */
static void
txn_limbo_save_pending_promote(struct txn_limbo *limbo,
			       const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	assert(req->type == IPROTO_RAFT_PROMOTE);
	assert(req->promote.wait_ack);
	uint32_t origin = req->origin_id;
	if (req->promote.term <= limbo->term) {
		limbo_debug("limbo: save pending promote: drop from %u with "
			    "term %lld - outdated from limbo's term %lld",
			    origin, (long long)req->promote.term,
			    (long long)limbo->term);
		return;
	}
	struct txn_limbo_promote_entry *p = &limbo->nodes[origin].pending;
	assert(p->raft_term < req->promote.term);
	txn_limbo_promote_entry_create(limbo, p, req);
	limbo_debug("limbo: save pending promote from %u: %s", origin,
		    txn_limbo_promote_entry_str(p));
}

/** Commit IPROTO_RAFT_PROMOTE/DEMOTE request. */
static void
txn_limbo_req_commit_promote_demote(struct txn_limbo *limbo,
				    const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_assert_consistent(limbo);
	assert(req->type == IPROTO_RAFT_PROMOTE ||
	       req->type == IPROTO_RAFT_DEMOTE);
	uint32_t origin = req->origin_id;
	if (req->promote.wait_ack) {
		assert(origin != 0);
		assert(req->type == IPROTO_RAFT_PROMOTE);
		txn_limbo_save_pending_promote(limbo, req);
		/*
		 * The optimistic flip made by the prepare might need a revert
		 * when the entry was nopified as outdated.
		 */
		txn_limbo_update_system_spaces_is_sync_state(limbo, NULL);
		txn_limbo_assert_consistent(limbo);
		return;
	}
	/* Nop from the bootstrap snapshot. */
	if (txn_limbo_request_is_promote_bootstrap(req)) {
		limbo_debug("limbo: skipping promote-bootstrap entry");
		return;
	}
	struct txn_limbo_promote_entry *entry =
		&limbo->nodes[origin].pending;
	assert(entry->raft_term == 0);
	if (origin == instance_id)
		assert(limbo->own_promote.journal_lsn == 0);
	txn_limbo_promote_entry_create(limbo, entry, req);
	if (entry->raft_term == 0) {
		assert(limbo->is_in_recovery);
		txn_limbo_assert_consistent(limbo);
		return;
	}
	txn_limbo_apply_promote(limbo, req->type, origin);
}

void
txn_limbo_req_commit(struct txn_limbo *limbo, const struct synchro_request *req)
{
	txn_limbo_assert_locked(limbo);
	txn_limbo_assert_consistent(limbo);
	limbo_debug("limbo: req commit: %s", synchro_request_str(req));
	/*
	 * Must be checked before the commit - it wipes the state the check is
	 * looking at (the pending entry of an applied ownership change).
	 */
	bool needs_unfence = txn_limbo_req_will_change_ownership(limbo, req);
	switch (req->type) {
	case IPROTO_RAFT_CONFIRM:
		txn_limbo_req_commit_confirm(limbo, req);
		break;
	case IPROTO_RAFT_ROLLBACK:
		txn_limbo_req_commit_rollback(limbo, req);
		break;
	default:
		txn_limbo_req_commit_promote_demote(limbo, req);
		break;
	}
	if (needs_unfence)
		txn_limbo_unfence(limbo);
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
