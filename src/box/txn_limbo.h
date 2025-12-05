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

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct raft;
struct synchro_request;

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
	/** Synchronous transactions and other ones depending on them. */
	struct txn_limbo_queue queue;
	/**
	 * Latest terms received with PROMOTE entries from remote instances.
	 * Limbo uses them to filter out the transactions coming not from the
	 * limbo owner, but so outdated that they are rolled back everywhere
	 * except outdated nodes.
	 */
	struct vclock promote_term_map;
	/**
	 * The biggest PROMOTE term seen by the instance and persisted in WAL.
	 * It is related to raft term, but not the same. Synchronous replication
	 * represented by the limbo is interested only in the won elections
	 * ended with PROMOTE request.
	 *
	 * It means the limbo's term might be smaller than the raft term, while
	 * there are ongoing elections, or the leader is already known and this
	 * instance hasn't read its PROMOTE request yet.
	 *
	 * It can also be bigger than raft't term in case the limbo has received
	 * and persisted a PROMOTE request before raft's own messages are
	 * delivered.
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
	 * If there is an ongoing PROMOTE being applied. It is not immediate at
	 * least because it needs to be written into the journal.
	 */
	bool is_transition_in_progress;
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
	 * Savepoint of confirmed LSN. To rollback to in case the current
	 * synchro command (promote/demote/...) fails.
	 */
	int64_t svp_confirmed_lsn;
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

bool
txn_limbo_is_ro(struct txn_limbo *limbo);

/**
 * Return the latest term as seen in PROMOTE requests from instance with id
 * @a replica_id.
 */
static inline uint64_t
txn_limbo_replica_term(const struct txn_limbo *limbo, uint32_t replica_id)
{
	return vclock_get(&limbo->promote_term_map, replica_id);
}

/**
 * Return the latest confirmed lsn for the replica with id @replica_id.
 */
static inline int64_t
txn_limbo_replica_confirmed_lsn(const struct txn_limbo *limbo,
				uint32_t replica_id)
{
	return vclock_get(&limbo->queue.confirmed_vclock, replica_id);
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
txn_limbo_assign_lsn(struct txn_limbo *limbo, struct txn_limbo_entry *entry,
		     int64_t lsn);

/**
 * Ack all transactions up to the given LSN on behalf of the
 * replica with the specified ID.
 */
void
txn_limbo_ack(struct txn_limbo *limbo, uint32_t replica_id, int64_t lsn);

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

/** Wait until the limbo is empty. Regardless of how its transactions end. */
int
txn_limbo_wait_empty(struct txn_limbo *limbo, double timeout);

/**
 * Persist limbo state to a given synchro request.
 */
void
txn_limbo_checkpoint(const struct txn_limbo *limbo,
		     struct synchro_request *req);

/**
 * Write a PROMOTE request, which has the same effect as CONFIRM(@a lsn) and
 * ROLLBACK(@a lsn + 1) combined.
 */
int
txn_limbo_write_promote(struct txn_limbo *limbo, int64_t lsn, uint64_t term);

/**
 * Write a DEMOTE request.
 * It has the same effect as PROMOTE and additionally clears limbo ownership.
 */
int
txn_limbo_write_demote(struct txn_limbo *limbo, int64_t lsn, uint64_t term);

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
