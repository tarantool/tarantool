#pragma once
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
#include "latch.h"
#include "errinj.h"
#include "txn_limbo_queue.h"

/**
 * The module implements the synchronous replication part of the VRaft protocol.
 *
 * The limbo executes the outcomes of VRaft elections by transferring the
 * synchronous transaction queue ownership between the leaders as terms are
 * progressing.
 *
 * The limbo also keeps track of the transactions themselves, making sure they
 * respect the replication quorum before they get committed.
 *
 * See more details in the VRaft protocol description and in the sources below.
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct info_handler;
struct raft;
struct synchro_request;

/**
 * A pending PROMOTE entry, waiting until its own CONFIRM is applied.
 *
 * PROMOTE is technically a "transaction" in vanilla Raft terms. This is the
 * first thing made by a new leader once it gets elected, it needs a quorum, and
 * must write CONFIRM later.
 *
 * The original Raft leaves it at that. The VRaft protocol gives this
 * transaction the more explicit form and purpose - the PROMOTE entry, which
 * is an "ownership transfer" transaction. And mutates the limbo's state, not
 * data in spaces.
 *
 * PROMOTE transactions can also form a queue. Then, as vanilla Raft suggests,
 * the last PROMOTE in the queue confirms all the previous ones as well.
 *
 * The problem with such a queue is one - the queue size is potentially
 * unlimited. The original Raft deals with it by avoiding the topic of memory
 * limits, and just stores everything in "log" which can be truncated on
 * rollback.
 *
 * VRaft's PROMOTE solves the infinite queue problem. Specifically, a chain of
 * PROMOTEs of any size can actually be squashed into a single PROMOTE. The core
 * idea of such compaction is that applying any N number of transactions is
 * identical to applying a single transaction updating the dataset to the same
 * state in one go.
 *
 * This is possible, because VRaft is aware of the PROMOTE's after-effects.
 * Each single PROMOTE must change a few attributes of the limbo + potentially
 * confirm and rollback some txns by a single provided border LSN.
 *
 * Lets consider what after-effects PROMOTE has, and for each effect how can 2
 * of them be squashed into 1 (allowing by induction any N to compact into 1).
 *
 * ## The term bumps
 *
 * If PROMOTE P1 bumps term of node N1 to T1, and a newer PROMOTE P2 bumps N2 to
 * T2, then it is exactly like having one PROMOTE P_NEW doing
 *
 *     {N1.term = T1, N2.term = T2}
 *
 * If N1 = N2, then it boils down to {N.term = T2}
 *
 * The size of the term map is limited by the maximal node count in the
 * cluster - by the constant VCLOCK_MAX.
 *
 * This means any N number of PROMOTEs can be squashed into one, which will
 * contain a map of term updates of O(1) size. Any newer PROMOTE can take the
 * previous one, and update one key in this map.
 *
 * This is the `term_map` member below. And the `raft_term` field is the term
 * in which this latest PROMOTE was made. It is equal term_map[origin].
 * Technically it isn't needed, but it exists as a legacy from older Tarantool
 * versions.
 *
 * ## The owner ID setting
 *
 * When N number of PROMOTEs assign the limbo owner ID, one by one, the final
 * result is anyway what the newest PROMOTE says. So it is enough to assign the
 * owner to the origin of the latest PROMOTE, ignoring the older ones.
 *
 * The origin ID is known from xrow_header.replica_id of this PROMOTE's binary
 * form.
 *
 * ## The confirmed LSN
 *
 * PROMOTE(queue_owner_id, confirm_lsn) means that if the currently confirmed
 * owner of the limbo matches this PROMOTE's queue_owner_id, then it must
 * apply all txns having LSN <= the confirmed one, and rollback the newer ones.
 *
 * If the owner doesn't match (can happen in some cases when this PROMOTE isn't
 * the first one in the chain already). Then all txns are rolled back.
 *
 * When a chain of PROMOTEs gets applied, the first PROMOTE will wipe the limbo
 * empty. A decision is made for each and every txn. Just the ones <= the
 * confirmed LSN are confirmed, and the newer ones are rolled back.
 *
 * This means a new squashed PROMOTE can inherit the confirmation LSN and owner
 * ID from the first PROMOTE in the chain.
 *
 * Seeing literally the first PROMOTE isn't needed. Because PROMOTE P2 inherited
 * the LSN and owner ID from P1 (the first PROMOTE). Then P3 can inherit it from
 * P2 (the second inherited from the first). P4 can inherit from P3. And so on
 * by induction.
 *
 * Thus the new PROMOTE can simply take the LSN and owner ID from the current
 * latest pending PROMOTE.
 *
 * The map of confirmed {node[id] = lsn} though must still be updated with all
 * the PROMOTEs in the confirmed chain. They are easy to merge into a single
 * accumulative vclock.
 *
 * These are `queue_owner_id` and `confirm_lsn` fields.
 *
 * ## The confirmed vclock
 *
 * Each PROMOTE not only wipes the limbo by {owner ID + confirmed LSN} pair.
 * The PROMOTE itself is also a transaction, and its origin's component in the
 * confirmed vclock must also be bumped to that PROMOTE's own journal LSN.
 *
 * It means each PROMOTE bumps one component in the confirmed vclock. And the
 * vclock size is VCLOCK_MAX. Then squash of an older PROMOTE P1 and a newer one
 * PROMOTE P2 can be done same as with the terms:
 *
 *     P1: {confirmed[N1] = LSN1}, P2: {confirmed[N2] = LSN2}
 *                         is the same as
 *     P_NEW: {confirmed{N1 = LSN1, N2 = LSN2}}
 *
 * This is the `confirmed_vclock` field.
 */
struct txn_limbo_promote_entry {
	/**
	 * Raft term claimed by this PROMOTE. Zero means the slot holds no
	 * pending PROMOTE - a valid one always has a non-zero term.
	 */
	uint64_t raft_term;
	/**
	 * The previous limbo owner. Their pending transactions will be
	 * confirmed up to confirm_lsn when this PROMOTE is applied.
	 */
	uint32_t queue_owner_id;
	/** Up to which queue_owner_id's transactions this PROMOTE confirms. */
	int64_t confirm_lsn;
	/**
	 * Confirmed vclock the limbo will inherit when this PROMOTE is
	 * applied.
	 */
	struct vclock confirmed_vclock;
	/** Confirmed term bumps from all the nodes in the replicaset. */
	struct vclock term_map;
};

/** Per-instance limbo state, indexed by replica id. */
struct txn_limbo_node {
	/**
	 * Biggest PROMOTE/DEMOTE term ever applied from this instance.
	 */
	uint64_t latest_term;
	/** Latest known pending PROMOTE from this instance, if any. */
	struct txn_limbo_promote_entry pending;
};

/**
 * State of this instance's own pending PROMOTE, kept while it gathers quorum
 * and waits for its CONFIRM.
 */
struct txn_limbo_promote_state {
	/**
	 * Journal LSN at which the own PROMOTE was written. Zero when nothing
	 * is written yet, and if there is no ongoing promotion at all.
	 */
	int64_t journal_lsn;
	/** Acks of the pending PROMOTE. */
	struct vclock acks;
	/** Trigger to receive replication acks. */
	struct trigger on_ack;
};

/** Limbo state. */
enum txn_limbo_state {
	/**
	 * The limbo has no owner and is empty. It makes no effect on this
	 * instance.
	 */
	TXN_LIMBO_STATE_INACTIVE,
	/**
	 * The limbo is actively and fully owned by the current instance right
	 * now and is writable.
	 */
	TXN_LIMBO_STATE_LEADER,
	/**
	 * The limbo is owned by somebody (even perhaps by this instance), but
	 * this instance can't put new transactions into it.
	 *
	 * The case of it being owned by another node is clear - that other node
	 * will put transactions into the queue and this instance will apply
	 * them.
	 *
	 * But it can also be that the queue is owned by the current instance in
	 * a term mismatching the Raft elections. Technically, the ownership is
	 * with the current instance, but actually it can't be fully exercised
	 * yet / already.
	 */
	TXN_LIMBO_STATE_REPLICA,
};

/**
 * Limbo is a place where transactions are stored, which are
 * finished, but not committed nor rolled back. These are
 * synchronous transactions in progress of collecting ACKs from
 * replicas.
 * Limbo's main purposes
 *   - maintain the transactions ordered by LSN of their emitter;
 *   - be a link between transaction and replication modules, so
 *     as they wouldn't depend on each other directly.
 */
struct txn_limbo {
	/** Limbo state. */
	enum txn_limbo_state state;
	/**
	 * Triggers run on every event which might update the limbo state. Even
	 * when the state didn't actually change.
	 */
	struct rlist on_state_update;
	/** Synchronous transactions and other ones depending on them. */
	struct txn_limbo_queue queue;
	/**
	 * Per-instance state. Tracks latest known control commands coming from
	 * each node. It is in turn used for transaction filtering, split-brain
	 * detection, pending PROMOTEs tracking.
	 */
	struct txn_limbo_node nodes[VCLOCK_MAX];
	/**
	 * The biggest confirmed PROMOTE term seen by the instance and persisted
	 * in WAL. It is related to raft term, but not the same. Synchronous
	 * replication represented by the limbo is interested only in the won
	 * elections ended with a confirmed PROMOTE request.
	 *
	 * It means the limbo's term might be smaller than the raft term, while
	 * there are ongoing elections, or the leader is already known and this
	 * instance hasn't read its confirmed PROMOTE request yet.
	 *
	 * It can also be bigger than raft's term in case the limbo has
	 * received, persisted, and confirmed a PROMOTE request before raft's
	 * own messages are delivered.
	 *
	 * During other times the limbo and raft are in sync and the terms are
	 * the same.
	 */
	uint64_t term;
	/** To linearize any sort of state changes. */
	struct latch state_latch;
	/**
	 * If the limbo is being recovered right now and isn't serving new
	 * requests. Only re-applying old ones. This is used in order to
	 * distinguish between old and new promotion.
	 */
	bool is_in_recovery;
	/**
	 * If the limbo has seen a fresh promote after recovery was finished.
	 * A node can't be a leader until it sees / makes a newly made promote
	 * since its restart.
	 */
	bool saw_promote;
	/**
	 * The elections state machine that controls the limbo when elections
	 * are enabled.
	 */
	struct raft *raft;
	/**
	 * Asynchronously tries to close the gap between the `confirmed_lsn` and
	 * the `volatile_confirmed_lsn` by writing a CONFIRM request to the WAL
	 * and retrying it on failure. Must always be woken up when the
	 * `volatile_confirmed_lsn` is updated separately from the
	 * `confirmed_lsn`.
	 */
	struct fiber *worker;
	/** Own pending PROMOTE state tracker. */
	struct txn_limbo_promote_state own_promote;
	/** A trigger invoked on replica acks. */
	struct trigger on_ack;
};

/**
 * Global limbo entry. So far an instance can have only one limbo,
 * where master's transactions are stored. Eventually there may
 * appear more than one limbo for master-master support.
 */
extern struct txn_limbo txn_limbo;

static inline bool
txn_limbo_is_empty(struct txn_limbo *limbo)
{
	return txn_limbo_queue_is_empty(&limbo->queue);
}

static inline void
txn_limbo_lock(struct txn_limbo *limbo)
{
	latch_lock(&limbo->state_latch);
}

static inline void
txn_limbo_unlock(struct txn_limbo *limbo)
{
	latch_unlock(&limbo->state_latch);
}

/**
 * Make the limbo actualize its state in case any conditions affecting it have
 * been changed.
 */
void
txn_limbo_update_state(struct txn_limbo *limbo);

/** See if submission to the limbo would yield if done right now. */
bool
txn_limbo_would_block(struct txn_limbo *limbo);

/**
 * Return the latest term as seen in PROMOTE requests from instance with id
 * @a replica_id.
 */
static inline uint64_t
txn_limbo_replica_term(const struct txn_limbo *limbo, uint32_t replica_id)
{
	assert(replica_id < VCLOCK_MAX);
	return limbo->nodes[replica_id].latest_term;
}

/**
 * Return the last synchronous transaction in the limbo or NULL when it is
 * empty.
 */
struct txn_limbo_entry *
txn_limbo_last_synchro_entry(struct txn_limbo *limbo);

/**
 * Allocate, create, and append a new transaction to the limbo.
 * The limbo entry is allocated on the transaction's region.
 */
int
txn_limbo_submit(struct txn_limbo *limbo, uint32_t id, struct txn *txn,
		 size_t approx_len);

/**
 * Wait until all the limbo entries existing at the moment of calling are fully
 * submitted into the limbo.
 *
 * It is guaranteed that if this function returns success, then all those limbo
 * entries have been submitted to WAL. And the caller, for example, might do a
 * journal sync right away to find out the vclock at the moment of the last
 * limbo entry journal write.
 *
 * Any limbo entries added during the waiting are not going to be waited for.
 * And are guaranteed not to be sent to the journal yet after this function
 * returns success, until the next yield of the caller fiber.
 */
int
txn_limbo_flush(struct txn_limbo *limbo);

/** Remove the entry from the limbo, mark as rolled back. */
void
txn_limbo_abort(struct txn_limbo *limbo, struct txn_limbo_entry *entry);

/** Assign the LSN to the queue entry. */
void
txn_limbo_assign_lsn(struct txn_limbo *limbo, uint32_t origin_id,
		     struct txn_limbo_entry *entry, int64_t lsn);

/** Try to wait for the given entry's completion. */
enum txn_limbo_wait_entry_result
txn_limbo_wait_complete(struct txn_limbo *limbo, struct txn_limbo_entry *entry);

/**
 * Initiate execution of a synchronous replication request.
 */
static inline void
txn_limbo_begin(struct txn_limbo *limbo)
{
	ERROR_INJECT_COUNTDOWN(ERRINJ_TXN_LIMBO_BEGIN_DELAY_COUNTDOWN, {
		struct errinj *e =
			errinj(ERRINJ_TXN_LIMBO_BEGIN_DELAY, ERRINJ_BOOL);
		e->bparam = true;
	});
	ERROR_INJECT_YIELD(ERRINJ_TXN_LIMBO_BEGIN_DELAY);
	txn_limbo_lock(limbo);
}

/** Commit a synchronous replication request. */
static inline void
txn_limbo_commit(struct txn_limbo *limbo)
{
	txn_limbo_unlock(limbo);
}

/** Rollback a synchronous replication request. */
static inline void
txn_limbo_rollback(struct txn_limbo *limbo)
{
	txn_limbo_unlock(limbo);
}

/**
 * Prepare a limbo request for WAL write and commit. And check if the request is
 * valid. Similar to txn_stmt prepare.
 */
int
txn_limbo_req_prepare(struct txn_limbo *limbo,
		      const struct synchro_request *req);

/**
 * Rollback a limbo request after a fail, such as a bad WAL write. Similar to
 * txn_stmt rollback.
 */
void
txn_limbo_req_rollback(struct txn_limbo *limbo,
		       const struct synchro_request *req);

/**
 * Commit a synchronous replication request after a successful WAL write.
 * Similar to txn_stmt commit.
 */
void
txn_limbo_req_commit(struct txn_limbo *limbo,
		     const struct synchro_request *req);

/** Process a synchronous replication request. */
int
txn_limbo_process(struct txn_limbo *limbo, const struct synchro_request *req);

/**
 * Wait until the last transaction in the limbo is finished and gets its result.
 */
int
txn_limbo_wait_last_txn(struct txn_limbo *limbo, bool *is_rollback,
			double timeout);

/** Collect the limbo state for persisting. */
void
txn_limbo_checkpoint(const struct txn_limbo *limbo,
		     struct synchro_snapshot *out);

/**
 * Execute promotion to catch up with the Raft state, in case this node is a
 * Raft leader in a new term, and the limbo is behind.
 */
int
txn_limbo_promote(struct txn_limbo *limbo, double timeout);

/**
 * Execute demotion - exit from the synchronous replication by making the limbo
 * unowned.
 */
int
txn_limbo_demote(struct txn_limbo *limbo, double timeout);

/**
 * Execute promotion the legacy way, used when the elections are disabled: the
 * PROMOTE gathers no quorum of its own and takes effect right on its commit.
 * The transactions it confirms get their quorums collected beforehand.
 *
 * XXX: election-less PROMOTE is going to be deprecated. This entire function
 * can be deleted when it happens.
 */
int
txn_limbo_promote_legacy(struct txn_limbo *limbo, double timeout);

/**
 * Wait until the limbo is claimed by this instance in the given Raft term: the
 * own PROMOTE of the term is written and confirmed. Fails as soon as the claim
 * can't happen anymore - the leadership of the term is lost, a newer promotion
 * got applied, or the own PROMOTE got poisoned.
 */
int
txn_limbo_wait_for_claim(struct txn_limbo *limbo, uint64_t term,
			 double deadline);

/**
 * Update qsync parameters dynamically.
 */
void
txn_limbo_on_parameters_change(struct txn_limbo *limbo);

/**
 * Rollback all the volatile txns. That is, the ones waiting for space in the
 * limbo and not yet sent to the journal. It is supposed to happen when some
 * older txn wants to get rolled back. For example, when its WAL write fails.
 * The it must cascading-rollback all the newer txns, including the ones not yet
 * visible to the journal.
 */
void
txn_limbo_rollback_all_volatile(struct txn_limbo *limbo);

/** Tell the limbo that the recovery is finished. */
void
txn_limbo_finish_recovery(struct txn_limbo *limbo);

/** Return whether limbo has an owner. */
static inline bool
txn_limbo_has_owner(struct txn_limbo *limbo)
{
	return limbo->queue.owner_id != REPLICA_ID_NIL;
}

/** Return whether limbo is owned by current instance. */
static inline bool
txn_limbo_is_owned_by_current_instance(const struct txn_limbo *limbo)
{
	return txn_limbo_queue_is_owned_by_current_instance(&limbo->queue);
}

/**
 * Initialize qsync engine.
 */
void
txn_limbo_init(struct raft *raft);

/**
 * Denitialize qsync engine.
 */
void
txn_limbo_free();

void
txn_limbo_shutdown(void);

/** Set maximal limbo size in bytes. */
void
txn_limbo_set_max_size(struct txn_limbo *limbo, int64_t size);

/** Fill box.info.synchro. */
void
txn_limbo_info(struct txn_limbo *limbo, struct info_handler *h);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
