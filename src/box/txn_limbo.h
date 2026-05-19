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

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct raft;
struct synchro_request;

/**
 * A pending PROMOTE entry, waiting until its own CONFIRM is applied.
 *
 * PROMOTE is technically a "transaction" in vanilla Raft terms. Indeed - this
 * is the first thing made by a new leader once it gets elected, it needs a
 * quorum, and must write CONFIRM later.
 *
 * The unusual thing about this transaction is that 1) no new normal
 * transactions are allowed until this one is confirmed, 2) the "data" of this
 * transaction is:
 * - The term bump for the new owner.
 * - The new owner ID.
 * - The confirmed LSN bump for the old owner, to finalize the transactions of
 *   the previous term.
 *
 * Even more unusual is that these "transactions" can still form a queue, like
 * normal transactions, and the queue might even contain more than 1 entry from
 * the same node (with different terms).
 *
 * The limbo however doesn't store such queue. The problem is that PROMOTEs can
 * be generated in a real cluster potentially for unlimited amount of time
 * without any of them getting confirmed. This would create an unbounded queue
 * of PROMOTE transactions right in-memory, leading to OOM (unlikely but
 * possible).
 *
 * Nonetheless the limbo manages to "compact" this queue in O(1) space, while
 * keeping the effects of confirmation of any number of PROMOTEs in this
 * non-existing queue.
 *
 * The main idea is that applying any N number of transactions is identical to
 * applying a single transaction bringing the dataset to the same state in one
 * go.
 *
 * The dataset here means the limbo state. Each single PROMOTE must change a few
 * attributes of the limbo + potentially confirm and rollback some txns by a
 * single provided border LSN.
 *
 * ## The term bumps
 *
 * Number N of PROMOTEs bumping terms by replica_ids is identical to just having
 * one promote bumping all the needed terms in one go, from a single map of
 * {node[id] = term}, whose size is limited by VCLOCK_MAX. Lets then make each
 * squashed PROMOTE carry its own term bump merged with the term bumps of the
 * previous squashed PROMOTE. The effect of the new one will include the effect
 * of all the previous ones inductively.
 *
 * This is the `term_map` member below. And the `raft_term` field is the latest
 * of the terms.
 *
 * ## The owner ID setting
 *
 * When N number of PROMOTEs assign the limbo owner ID, one by one, the final
 * result is anyway what the newest PROMOTE says. So it is enough to assign the
 * current instance ID in the new squashed PROMOTE, ignoring the owners of the
 * previous PROMOTEs.
 *
 * This is the `origin_id`, used as an index in the map of squashed PROMOTEs.
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
 * Easy to see, that if there is a chain of PROMOTEs, then the first one of them
 * will leave the limbo empty, either way. This means the new squashed PROMOTE
 * must inherit the confirmation LSN from the first PROMOTE in the chain. This
 * is done inductively as well.
 *
 * The map of confirmed {node[id] = lsn} though must still be updated with all
 * the PROMOTEs in the confirmed chain. They are easy to merge into a single
 * accumulative vclock.
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
	 * applied. Accumulates all the previous confirmed LSNs from the chain
	 * of confirmed PROMOTEs since this replicaset's bootstrap.
	 */
	struct vclock confirmed_vclock;
	/**
	 * Confirmed term bumps from all the nodes in the replicaset. This map
	 * exists, because term bumps are sent as separate entries, not with
	 * each transaction. So the terms of all nodes must be explicitly stored
	 * and updated in memory. Accumulates all the previous term bumps from
	 * the chain of confirmed PROMOTEs since this replicaset's bootstrap.
	 */
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
 * and waits for its CONFIRM. Zeroed when there is no own pending PROMOTE.
 *
 * This technically could be stored in the node's promote entry member, but this
 * would be a lot of unnecessary data, because this node is really only
 * interested in confirming its own pending PROMOTE.
 */
struct txn_limbo_promote_state {
	/** Journal LSN at which the own PROMOTE was written. */
	int64_t journal_lsn;
	/** Per-replica acked LSNs to track quorum of the pending PROMOTE. */
	struct vclock acks;
	/** Trigger to track replication acks. */
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
	 * Triggers run on every limbo state update, even when the state
	 * itself ends up unchanged, and on any other state-related limbo
	 * changes. Anything waiting for a certain limbo state condition
	 * should subscribe here and re-check its condition on each run.
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
	 * Whether the limbo is in rollback mode. The meaning is exactly the
	 * same as for the similar WAL flag. In theory this should be deleted
	 * if the limbo will be ever moved to WAL thread. It would reuse the WAL
	 * flag.
	 * It is a sign to immediately rollback all new limbo entries, if there
	 * is an existing rollback in progress. This technique is called
	 * 'cascading rollback'. Cascading rollback does not allow to write to
	 * WAL anything new so as not to violate the 'reversed rollback order'
	 * rule.
	 * Without cascading rollback it could happen, that the limbo would
	 * start writing ROLLBACK to WAL, then a new transaction would be added
	 * to limbo and sent to WAL too. In the result the new transaction would
	 * be stored in WAL after ROLLBACK, and yet it should be rolled back too
	 * by the 'reversed rollback order' rule - contradiction.
	 */
	bool is_in_rollback;
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
	/** Bookkeeping for our own pending PROMOTE waiting for quorum. */
	struct txn_limbo_promote_state own_promote;
	/**
	 * Whether this instance validates incoming synchro requests. When the
	 * setting is on, the instance only allows CONFIRM/ROLLBACK from the
	 * limbo owner, tracks PROMOTE/DEMOTE term and owner_id consistency.
	 * The filtering is turned off during bootstrap, because it makes no
	 * sense when applying a full copy of a remote instance's data. There
	 * can't be any inconsistencies.
	 */
	bool do_validate;
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

/**
 * Persist limbo state to a given synchro request.
 */
void
txn_limbo_checkpoint(const struct txn_limbo *limbo,
		     struct synchro_request *req);

/**
 * Execute promotion/demotion to catch up with the Raft state, in case this node
 * is a Raft leader in a new term, and the limbo is behind.
 */
int
txn_limbo_promote(struct txn_limbo *limbo, uint16_t type, double timeout);

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

/** Start filtering incoming syncrho requests. */
void
txn_limbo_filter_enable(struct txn_limbo *limbo);

/** Stop filtering incoming synchro requests. */
void
txn_limbo_filter_disable(struct txn_limbo *limbo);

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

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
