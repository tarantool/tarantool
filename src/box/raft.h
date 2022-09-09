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
#include "raft/raft.h"
#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * A public trigger fired on Raft state change, i.e. on a broadcast.
 * It's allowed to yield inside it, and it's run asynchronously.
 */
extern struct rlist box_raft_on_broadcast;

enum election_mode {
	ELECTION_MODE_INVALID = -1,
	ELECTION_MODE_OFF = 0,
	ELECTION_MODE_VOTER = 1,
	ELECTION_MODE_MANUAL = 2,
	ELECTION_MODE_CANDIDATE = 3,
};

struct raft_request;

/**
 * box_election_mode - current mode of operation for raft. Some modes correspond
 * to RAFT operation modes directly, like CANDIDATE, VOTER and OFF.
 * There's a mode which does not map to raft operation mode directly:
 * MANUAL. In this mode RAFT usually operates as a voter, but it may become a
 * candidate for some period of time when user calls `box.ctl.promote()`
 */
extern enum election_mode box_election_mode;

/** Raft state of this instance. */
static inline struct raft *
box_raft(void)
{
	extern struct raft box_raft_global;
	/**
	 * Ensure the raft node can be used. I.e. that it is properly
	 * initialized.
	 */
	return box_raft_global.state != 0 ? &box_raft_global : NULL;
}

/**
 * Let the global raft know that the election quorum could change. It happens
 * when configuration is updated, and when new nodes are added or old are
 * deleted from the cluster.
 */
void
box_raft_update_election_quorum(void);

/** Set the node's election_mode to @a mode. */
void
box_raft_cfg_election_mode(enum election_mode mode);

/**
 * Recover a single Raft request. Raft state machine is not turned on yet, this
 * works only during instance recovery from the journal.
 */
void
box_raft_recover(const struct raft_request *req);

/** Save complete Raft state into a request to be persisted on disk locally. */
void
box_raft_checkpoint_local(struct raft_request *req);

/**
 * Save complete Raft state into a request to be sent to other instances of the
 * cluster.
 */
void
box_raft_checkpoint_remote(struct raft_request *req);

/** Handle a single Raft request from a node with instance id @a source. */
int
box_raft_process(struct raft_request *req, uint32_t source);

/** Block this fiber until the current term's outcome is known. */
int
box_raft_wait_term_outcome(void);

/** Block this fiber until the current volatile term is persisted. */
int
box_raft_wait_term_persisted(void);

/**
 * Enable/disable fencing. If enabled: instance will resign its leader role,
 * when it looses quorum.
 */
void
box_raft_set_election_fencing_enabled(bool enabled);

/**
 * Pause fencing. Instance will not resign its leader role when it looses
 * quorum even if fencing is enabled. Useful to prevent fencing while adding
 * replicas to replicaset or bootstraping.
 */
void
box_raft_election_fencing_pause(void);

void
box_raft_init(void);

void
box_raft_free(void);

#if defined(__cplusplus)
}
#endif
