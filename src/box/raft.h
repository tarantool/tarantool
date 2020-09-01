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
#include <stdint.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct raft_request;

struct raft {
	bool is_enabled;
	bool is_candidate;
	uint64_t term;
	uint32_t vote;
	double election_timeout;
};

extern struct raft raft;

/** Process a raft entry stored in WAL/snapshot. */
void
raft_process_recovery(const struct raft_request *req);

/** Configure whether Raft is enabled. */
void
raft_cfg_is_enabled(bool is_enabled);

/**
 * Configure whether the instance can be elected as Raft leader. Even if false,
 * the node still can vote, when Raft is enabled.
 */
void
raft_cfg_is_candidate(bool is_candidate);

/** Configure Raft leader election timeout. */
void
raft_cfg_election_timeout(double timeout);

/**
 * Configure Raft leader election quorum. There is no a separate option.
 * Instead, synchronous replication quorum is used. Since Raft is tightly bound
 * with synchronous replication.
 */
void
raft_cfg_election_quorum(void);

/**
 * Configure Raft leader death timeout. I.e. number of seconds without
 * heartbeats from the leader to consider it dead. There is no a separate
 * option. Raft uses replication timeout for that.
 */
void
raft_cfg_death_timeout(void);

/**
 * Save complete Raft state into a request to be sent to other instances of the
 * cluster. It is allowed to save anything here, not only persistent state.
 */
void
raft_serialize_for_network(struct raft_request *req);

/**
 * Save complete Raft state into a request to be persisted on disk. Only term
 * and vote are being persisted.
 */
void
raft_serialize_for_disk(struct raft_request *req);

#if defined(__cplusplus)
}
#endif
