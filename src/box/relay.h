#ifndef TARANTOOL_REPLICATION_RELAY_H_INCLUDED
#define TARANTOOL_REPLICATION_RELAY_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct iostream;
struct relay;
struct replica;
struct tt_uuid;
struct vclock;
struct checkpoint_cursor;

enum relay_state {
	/**
	 * Applier has not connected to the master or not expected.
	 */
	RELAY_OFF,
	/**
	 * Applier has connected to the master.
	 */
	RELAY_FOLLOW,
	/**
	 * Applier disconnected from the master.
	 */
	RELAY_STOPPED,
};

/** Create a relay which is not running. object. */
struct relay *
relay_new(struct replica *replica);

/** Destroy and delete the relay */
void
relay_delete(struct relay *relay);

/** Get last relay's diagnostic error */
struct diag*
relay_get_diag(struct relay *relay);

/** Return the current state of relay. */
enum relay_state
relay_get_state(const struct relay *relay);

/**
 * Returns relay's vclock
 * @param relay relay
 * @returns relay's vclock
 */
const struct vclock *
relay_vclock(const struct relay *relay);

/**
 * Returns relay's last_row_time
 * @param relay relay
 * @returns relay's last_row_time
 */
double
relay_last_row_time(const struct relay *relay);

/**
 * Returns relay's transaction's lag.
 */
double
relay_txn_lag(const struct relay *relay);

/**
 * Makes the relay issue a new vclock sync request and returns the sync to wait
 * for.
 */
int
relay_trigger_vclock_sync(struct relay *relay, uint64_t *vclock_sync,
			  double deadline);

/**
 * Send a Raft update request to the relay channel. It is not
 * guaranteed that it will be delivered. The connection may break.
 */
void
relay_push_raft(struct relay *relay, const struct raft_request *req);

/**
 * Cancel the relay.
 */
void
relay_cancel(struct relay *relay);

/**
 * Change group_id of the raft request row, if needed. Old versions expect
 * GROUP_LOCAL, new ones - GROUP_DEFAULT.
 */
void
relay_filter_raft(struct xrow_header *packet, uint32_t version_id);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

/**
 * Send initial JOIN rows to the replica
 *
 * @param io        client connection
 * @param sync      sync from incoming JOIN request
 * @param vclock[out] vclock of the read view sent to the replica
 * @param replica_version_id peer's version
 * @param cursor    cursor for checkpoint join, if NULL - read-view join.
 */
void
relay_initial_join(struct iostream *io, uint64_t sync, struct vclock *vclock,
		   uint32_t replica_version_id,
		   struct checkpoint_cursor *cursor);

/**
 * Send final JOIN rows to the replica.
 *
 * @param io        client connection
 * @param sync      sync from incoming JOIN request
 */
void
relay_final_join(struct replica *replica, struct iostream *io, uint64_t sync,
		 const struct vclock *start_vclock,
		 const struct vclock *stop_vclock);

/**
 * Subscribe a replica to updates.
 *
 * @return none.
 */
void
relay_subscribe(struct replica *replica, struct iostream *io, uint64_t sync,
		const struct vclock *start_vclock, uint32_t replica_version_id,
		uint32_t replica_id_filter, uint64_t sent_raft_term);

#endif /* TARANTOOL_REPLICATION_RELAY_H_INCLUDED */
