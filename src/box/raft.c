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
#include "raft.h"

#include "error.h"
#include "journal.h"
#include "xrow.h"
#include "small/region.h"
#include "replication.h"
#include "relay.h"

const char *raft_state_strs[] = {
	NULL,
	"follower",
	"candidate",
	"leader",
};

/** Raft state of this instance. */
struct raft raft = {
	.leader = 0,
	.state = RAFT_STATE_FOLLOWER,
	.is_enabled = false,
	.is_candidate = false,
	.term = 1,
	.vote = 0,
};

void
raft_process_recovery(const struct raft_request *req)
{
	if (req->term != 0)
		raft.term = req->term;
	if (req->vote != 0)
		raft.vote = req->vote;
	/*
	 * Role is never persisted. If recovery is happening, the
	 * node was restarted, and the former role can be false
	 * anyway.
	 */
	assert(req->state == 0);
	/*
	 * Vclock is always persisted by some other subsystem - WAL, snapshot.
	 * It is used only to decide to whom to give the vote during election,
	 * as a part of the volatile state.
	 */
	assert(req->vclock == NULL);
	/* Raft is not enabled until recovery is finished. */
	assert(!raft_is_enabled());
}

int
raft_process_msg(const struct raft_request *req, uint32_t source)
{
	(void)source;
	if (req->term > raft.term) {
		// Update term.
		// The logic will be similar, but the code
		// below is for testing purposes.
		raft.term = req->term;
	}
	if (req->vote > 0) {
		// Check whether the vote's for us.
	}
	switch (req->state) {
	case RAFT_STATE_FOLLOWER:
	    break;
	case RAFT_STATE_CANDIDATE:
	    // Perform voting logic.
	    break;
	case RAFT_STATE_LEADER:
	    // Switch to a new leader.
	    break;
	default:
	    break;
	}
	return 0;
}

void
raft_serialize_for_network(struct raft_request *req, struct vclock *vclock)
{
	memset(req, 0, sizeof(*req));
	req->term = raft.term;
	req->vote = raft.vote;
	req->state = raft.state;
	/*
	 * Raft does not own vclock, so it always expects it passed externally.
	 * Vclock is sent out only by candidate instances.
	 */
	if (req->state == RAFT_STATE_CANDIDATE) {
		req->vclock = vclock;
		vclock_copy(vclock, &replicaset.vclock);
	}
}

void
raft_serialize_for_disk(struct raft_request *req)
{
	memset(req, 0, sizeof(*req));
	req->term = raft.term;
	req->vote = raft.vote;
}

void
raft_cfg_is_enabled(bool is_enabled)
{
	raft.is_enabled = is_enabled;
}

void
raft_cfg_is_candidate(bool is_candidate)
{
	raft.is_candidate = is_candidate;
}

void
raft_cfg_election_timeout(double timeout)
{
	raft.election_timeout = timeout;
}

void
raft_cfg_election_quorum(void)
{
}

void
raft_cfg_death_timeout(void)
{
}

void
raft_broadcast(const struct raft_request *req)
{
	replicaset_foreach(replica)
		relay_push_raft(replica->relay, req);
}
