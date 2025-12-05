/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "core/fiber_cond.h"
#include "replication.h"
#include "small/rlist.h"
#include "vclock/vclock.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct txn;

enum txn_limbo_entry_state {
	/**
	 * Is saved in the queue, but isn't accounted yet and isn't persisted
	 * anywhere.
	 */
	TXN_LIMBO_ENTRY_VOLATILE,
	/** Is saved and accounted in the queue. */
	TXN_LIMBO_ENTRY_SUBMITTED,
	/** Committed, not in the queue anymore. */
	TXN_LIMBO_ENTRY_COMMIT,
	/** Rolled back, not in the queue anymore. */
	TXN_LIMBO_ENTRY_ROLLBACK,
};

/**
 * Wait-complete API in the limbo and its queue is a broken legacy which has
 * surprisingly untrivial set of possible outcomes when it returns.
 */
enum txn_limbo_wait_entry_result {
	/** Transaction is committed successfully. */
	TXN_LIMBO_WAIT_ENTRY_SUCCESS,
	/**
	 * Transaction couldn't be committed, but can't be rolled back either.
	 * It needs to be detached and will end on its own later. Can happen,
	 * for example, when the fiber was cancelled while waiting.
	 */
	TXN_LIMBO_WAIT_ENTRY_FAIL_DETACH,
	/** Transaction is rolled back due to an error. */
	TXN_LIMBO_WAIT_ENTRY_FAIL_COMPLETE,
	/**
	 * Transaction is not rolled back, but it needs to be. And all the newer
	 * ones too. This is a bad state which is not compatible with Raft and
	 * it needs to be deleted as soon as the backward compatibility policy
	 * allows that.
	 */
	TXN_LIMBO_WAIT_ENTRY_NEED_ROLLBACK,
};

/** Transaction and its quorum metadata, to be stored in queue. */
struct txn_limbo_entry {
	/** Link for the entry queue. */
	struct rlist in_queue;
	/** Transaction, waiting for a quorum. */
	struct txn *txn;
	/** Approximate size of this request when encoded. */
	size_t approx_len;
	/**
	 * LSN of the transaction by the originator's vclock component. May be
	 * -1 in case the transaction is not written to WAL yet.
	 */
	int64_t lsn;
	/** State of this entry. */
	enum txn_limbo_entry_state state;
	/** When this entry was added to the queue. */
	double insertion_time;
};

/**
 * Synchronous transactions and other ones depending on them. The limbo-queue
 * encapsulates all the logic of the simple but a bit bulky management of the
 * transactions, like their appending, popping, applying confirms and rollbacks,
 * tracking LSNs and acks, and so on. Technically none of that concerns the
 * main state of the limbo itself (if it is read-only or read-write, leader or
 * a replica, owner or not an owner or whatever).
 */
struct txn_limbo_queue {
	/**
	 * Instance ID of the owner of all the transactions in the queue.
	 * Strictly speaking, nothing prevents to store not own transactions
	 * here, originated from some other instance. But still the queue may
	 * contain only transactions of the same instance. Otherwise LSN order
	 * won't make sense - different nodes have own independent LSNs in their
	 * vclock components.
	 */
	uint32_t owner_id;
	/**
	 * Queue of entries. Ordered by LSN. Some of the entries in the end may
	 * not have an LSN yet (their local WAL write is still in progress), but
	 * their order won't change anyway. Because WAL write completions will
	 * give them LSNs in the same order.
	 */
	struct rlist entries;
	/** Number of entries in queue. */
	int64_t len;
	/**
	 * Maximal size of entries allowed to be in the submitted state (in
	 * bytes).
	 */
	int64_t max_size;
	/** Current approximate size of queue in bytes. */
	int64_t size;
	/**
	 * Maximal LSN that gathered quorum and has already been persisted in
	 * the WAL. Any attempt to confirm something smaller than this value can
	 * be safely ignored. Moreover, any attempt to rollback something
	 * starting from <= this LSN is illegal.
	 */
	int64_t confirmed_lsn;
	/**
	 * Maximal LSN that gathered quorum and has not yet been persisted in
	 * the WAL. No filtering can be performed based on this value. The
	 * `worker` must always be woken up if this value is bumped separately
	 * from the `confirmed_lsn` in order to asynchronously write a CONFIRM
	 * request.
	 */
	int64_t volatile_confirmed_lsn;
	/**
	 * All components of the vclock are versions of the queue owner's LSN,
	 * how it is visible on other nodes. For example, assume instance ID of
	 * the queue owner is 1. Then vclock[1] here is local LSN of the
	 * instance 1. vclock[2] is how replica with ID 2 sees LSN of
	 * instance 1. vclock[3] is how replica with ID 3 sees LSN of
	 * instance 1, and so on.
	 *
	 * In that way by looking at this vclock it is always can be said up to
	 * which LSN there is a sync quorum for transactions, created on the
	 * queue's owner node.
	 */
	struct vclock vclock;
	/**
	 * A vclock containing biggest known confirmed lsns for each previous
	 * limbo owner. It can never go back.
	 */
	struct vclock confirmed_vclock;
	/**
	 * The first unconfirmed synchronous transaction. Is NULL if there is no
	 * such transaction, or if the queue is not owned by the current
	 * instance. Or if CONFIRM can't really be written anymore due to new
	 * owner elections ongoing.
	 */
	struct txn_limbo_entry *entry_to_confirm;
	/**
	 * Number of ACKs of the first unconfirmed synchronous transaction
	 * (entry_to_confirm->txn). Contains the actual value only for a
	 * non-NULL entry_to_confirm with a local lsn assigned. Otherwise it may
	 * contain any trash.
	 */
	int ack_count;
	/**
	 * The time that the latest successfully confirmed entry waited for
	 * quorum.
	 */
	double confirm_lag;
	/**
	 * Condition on which the transactions can be waiting when blocked on
	 * anything like submission into the queue when the max size is already
	 * reached.
	 */
	struct fiber_cond cond;
};

static inline bool
txn_limbo_entry_is_complete(const struct txn_limbo_entry *e)
{
	return e->state > TXN_LIMBO_ENTRY_SUBMITTED;
}

static inline bool
txn_limbo_queue_is_empty(struct txn_limbo_queue *queue)
{
	return rlist_empty(&queue->entries);
}

static inline bool
txn_limbo_queue_is_owned_by_current_instance(
	const struct txn_limbo_queue *queue)
{
	return queue->owner_id == instance_id;
}

/** The age of the oldest non-confirmed queue entry. */
double
txn_limbo_queue_age(struct txn_limbo_queue *queue);

/** The last synchronous transaction in the queue or NULL when it is empty. */
struct txn_limbo_entry *
txn_limbo_queue_last_synchro_entry(struct txn_limbo_queue *queue);

/** See if submission to the queue would yield if done right now. */
bool
txn_limbo_queue_would_block(struct txn_limbo_queue *queue);

/**
 * Append the new transaction to the queue. If the queue is already full, will
 * yield until an error or successful submission.
 */
int
txn_limbo_queue_submit(struct txn_limbo_queue *queue, uint32_t origin_id,
		       struct txn *txn, size_t approx_len);

/**
 * Wait until all the queue entries existing at the moment of calling are fully
 * submitted into the queue.
 *
 * See more in the limbo doc.
 */
int
txn_limbo_queue_flush(struct txn_limbo_queue *queue);

/** Remove the entry from the limbo, mark as rolled back. */
void
txn_limbo_queue_abort(struct txn_limbo_queue *queue,
		      struct txn_limbo_entry *entry);

/** Assign the LSN to the queue entry. */
void
txn_limbo_queue_assign_lsn(struct txn_limbo_queue *queue,
			   struct txn_limbo_entry *entry, int64_t lsn);

/** Try to wait for the given entry's completion. */
enum txn_limbo_wait_entry_result
txn_limbo_queue_wait_complete(struct txn_limbo_queue *queue,
			      struct txn_limbo_entry *entry);

/** Get the LSNs of the first and last synchronous transactions in the queue. */
void
txn_limbo_queue_get_lsn_range(struct txn_limbo_queue *queue, int64_t *first_lsn,
			      int64_t *last_lsn);

/** Confirm all the entries <= @a lsn. */
void
txn_limbo_queue_apply_confirm(struct txn_limbo_queue *queue, int64_t lsn);

/** Rollback all the entries >= @a lsn. */
void
txn_limbo_queue_apply_rollback(struct txn_limbo_queue *queue, int64_t lsn,
			       int64_t signature);

/**
 * Transfer ownership of the queue to a new owner with the given ID. The
 * transactions already stored in the queue are all confirmed for LSNs <= the
 * given border LSN, and the newer ones are rolled back.
 */
void
txn_limbo_queue_transfer_ownership(struct txn_limbo_queue *queue,
				   uint32_t new_owner_id, int64_t border_lsn);

/**
 * Ack all transactions up to the given LSN on behalf of the replica with the
 * specified ID.
 * @retval true Quorum is reached for new transactions.
 * @retval false The opposite.
 */
bool
txn_limbo_queue_ack(struct txn_limbo_queue *queue, uint32_t replica_id,
		    int64_t lsn);

/** Try to bump the volatile confirmed LSN. */
bool
txn_limbo_queue_bump_volatile_confirm(struct txn_limbo_queue *queue);

/**
 * Wait until the last transaction in the queue is finished and get its result.
 */
int
txn_limbo_queue_wait_last_txn(struct txn_limbo_queue *queue, bool *is_rollback,
			      double timeout);

/** Wait until the queue is empty. Regardless of how its transactions end. */
int
txn_limbo_queue_wait_empty(struct txn_limbo_queue *queue, double timeout);

/** Wait until all the entries receive an lsn. */
int
txn_limbo_queue_wait_persisted(struct txn_limbo_queue *queue);

/** Rollback all the volatile txns. See more in the limbo doc. */
void
txn_limbo_queue_rollback_all_volatile(struct txn_limbo_queue *queue);

/**
 * Perform sanity checks with no changes. For debug build it is going to do
 * more expensive checking and assertions. For release build might do cheap
 * ones and panic on mistakes.
 */
void
txn_limbo_queue_sanity_check(struct txn_limbo_queue *queue);

/** Initialize the queue. */
void
txn_limbo_queue_create(struct txn_limbo_queue *queue);

/** Destroy the queue. */
void
txn_limbo_queue_destroy(struct txn_limbo_queue *queue);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
