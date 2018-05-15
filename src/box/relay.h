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

struct relay;
struct replica;
struct tt_uuid;
struct vclock;

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

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

/**
 * Send initial JOIN rows to the replica
 *
 * @param fd        client connection
 * @param sync      sync from incoming JOIN request
 * @param vclock    vclock of the last checkpoint
 */
void
relay_initial_join(int fd, uint64_t sync, struct vclock *vclock);

/**
 * Send final JOIN rows to the replica.
 *
 * @param fd        client connection
 * @param sync      sync from incoming JOIN request
 */
void
relay_final_join(struct replica *replica, int fd, uint64_t sync,
		 struct vclock *start_vclock, struct vclock *stop_vclock);

/**
 * Subscribe a replica to updates.
 *
 * @return none.
 */
void
relay_subscribe(struct replica *replica, int fd, uint64_t sync,
		struct vclock *replica_vclock, uint32_t replica_version_id);

#endif /* TARANTOOL_REPLICATION_RELAY_H_INCLUDED */
