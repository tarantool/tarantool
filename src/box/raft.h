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
#include "tarantool_ev.h"
#include "trigger.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * This is an implementation of Raft leader election protocol, separated from
 * synchronous replication part.
 *
 * The protocol describes an algorithm which helps to elect a single leader in
 * the cluster, which is supposed to handle write requests. And re-elect a new
 * leader, when the current leader dies.
 *
 * The implementation follows the protocol to the letter except a few important
 * details.
 *
 * Firstly, the original Raft assumes, that all nodes share the same log record
 * numbers. In Tarantool they are called LSNs. But in case of Tarantool each
 * node has its own LSN in its own component of vclock. That makes the election
 * messages a bit heavier, because the nodes need to send and compare complete
 * vclocks of each other instead of a single number like in the original Raft.
 * But logic becomes simpler. Because in the original Raft there is a problem of
 * uncertainty about what to do with records of an old leader right after a new
 * leader is elected. They could be rolled back or confirmed depending on
 * circumstances. The issue disappears when vclock is used.
 *
 * Secondly, leader election works differently during cluster bootstrap, until
 * number of bootstrapped replicas becomes >= election quorum. That arises from
 * specifics of replicas bootstrap and order of systems initialization. In
 * short: during bootstrap a leader election may use a smaller election quorum
 * than the configured one. See more details in the code.
 */

struct fiber;
struct raft_request;
struct vclock;

enum raft_state {
	/**
	 * Can't write. Can only accept data from a leader. Node in this state
	 * either monitors an existing leader, or there is an on-going election
	 * and the node voted for another node, or it can't be a candidate and
	 * does not do anything.
	 */
	RAFT_STATE_FOLLOWER = 1,
	/**
	 * The node can't write. There is an active election, in which the node
	 * voted for self. Now it waits for election outcome.
	 */
	RAFT_STATE_CANDIDATE = 2,
	/** Election was successful. The node accepts write requests. */
	RAFT_STATE_LEADER = 3,
};

extern const char *raft_state_strs[];

struct raft {
	/** Instance ID of leader of the current term. */
	uint32_t leader;
	/** State of the instance. */
	enum raft_state state;
	/**
	 * Volatile part of the Raft state, whose WAL write may be still
	 * in-progress, and yet the state may be already used. Volatile state is
	 * never sent to anywhere, but the state machine makes decisions based
	 * on it. That is vital.
	 * As an example, volatile vote needs to be used to reject votes inside
	 * a term, where the instance already voted (even if the vote WAL write
	 * is not finished yet). Otherwise the instance would try to write
	 * several votes inside one term.
	 */
	uint64_t volatile_term;
	uint32_t volatile_vote;
	/**
	 * Flag whether Raft is enabled. When disabled, it still persists terms
	 * so as to quickly enroll into the cluster when (if) it is enabled. In
	 * everything else disabled Raft does not affect instance work.
	 */
	bool is_enabled;
	/**
	 * Flag whether the node can become a leader. It is an accumulated value
	 * of configuration options Raft enabled and Raft candidate. If at least
	 * one is false - the instance is not a candidate.
	 */
	bool is_candidate;
	/** Flag whether the instance is allowed to be a leader. */
	bool is_cfg_candidate;
	/**
	 * Flag whether Raft currently tries to write something into WAL. It
	 * happens asynchronously, not right after Raft state is updated.
	 */
	bool is_write_in_progress;
	/**
	 * Flag whether Raft wants to broadcast its state. It is done
	 * asynchronously in the worker fiber. That allows to collect multiple
	 * updates into one batch if they happen in one event loop iteration.
	 * Usually even in one function.
	 */
	bool is_broadcast_scheduled;
	/**
	 * Persisted Raft state. These values are used when need to tell current
	 * Raft state to other nodes.
	 */
	uint64_t term;
	uint32_t vote;
	/**
	 * Bit 1 on position N means that a vote from instance with ID = N was
	 * obtained.
	 */
	uint32_t vote_mask;
	/** Number of votes for this instance. Valid only in candidate state. */
	int vote_count;
	/** State machine timed event trigger. */
	struct ev_timer timer;
	/** Worker fiber to execute blocking tasks like IO. */
	struct fiber *worker;
	/** Configured election timeout in seconds. */
	double election_timeout;
	/**
	 * Trigger invoked each time any of the Raft node visible attributes are
	 * changed.
	 */
	struct rlist on_update;
};

extern struct raft raft;

/**
 * A flag whether the instance is read-only according to Raft. Even if Raft
 * allows writes though, it does not mean the instance is writable. It can be
 * affected by box.cfg.read_only, connection quorum.
 */
static inline bool
raft_is_ro(void)
{
	return raft.is_enabled && raft.state != RAFT_STATE_LEADER;
}

/** See if the instance can accept rows from an instance with the given ID. */
static inline bool
raft_is_source_allowed(uint32_t source_id)
{
	return !raft.is_enabled || raft.leader == source_id;
}

/** Check if Raft is enabled. */
static inline bool
raft_is_enabled(void)
{
	return raft.is_enabled;
}

/** Process a raft entry stored in WAL/snapshot. */
void
raft_process_recovery(const struct raft_request *req);

/**
 * Process a raft status message coming from the network.
 * @param req Raft request.
 * @param source Instance ID of the message sender.
 */
int
raft_process_msg(const struct raft_request *req, uint32_t source);

/**
 * Process a heartbeat message from an instance with the given ID. It is used to
 * watch leader's health and start election when necessary.
 */
void
raft_process_heartbeat(uint32_t source);

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
 * Bump the term. When it is persisted, the node checks if there is a leader,
 * and if there is not, a new election is started. That said, this function can
 * be used as tool to forcefully start new election, or restart an existing.
 */
void
raft_new_term(void);

/**
 * Save complete Raft state into a request to be sent to other instances of the
 * cluster. It is allowed to save anything here, not only persistent state.
 */
void
raft_serialize_for_network(struct raft_request *req, struct vclock *vclock);

/**
 * Save complete Raft state into a request to be persisted on disk. Only term
 * and vote are being persisted.
 */
void
raft_serialize_for_disk(struct raft_request *req);

/**
 * Add a trigger invoked each time any of the Raft node visible attributes are
 * changed.
 */
void
raft_on_update(struct trigger *trigger);

/** Initialize Raft global data structures. */
void
raft_init(void);

#if defined(__cplusplus)
}
#endif
