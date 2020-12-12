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
#include "fakesys/fakeev.h"
#include "fiber.h"
#include "raft/raft.h"
#include "unit.h"

/** WAL simulation. It stores a list of rows which raft wanted to persist. */
struct raft_journal {
	/** Instance ID to propagate the needed vclock component. */
	uint32_t instance_id;
	/** Journal vclock, belongs to the journal, not to the core raft. */
	struct vclock vclock;
	/** An array of rows collected from raft. */
	struct raft_msg *rows;
	/** Number of rows in the journal. */
	int size;
};

/**
 * Network simulation. There is no real sending of anything. Instead, all
 * messages are saved into a list, where they can be checked on their
 * correctness. All messages provided by raft are copied and saved here.
 */
struct raft_net {
	/** Array of messages. */
	struct raft_msg *msgs;
	/** Number of messages. */
	int count;
};

/**
 * Raft node + all its environment. Journal, network, configuration. The node
 * provides helper methods to work with the raft instance.
 */
struct raft_node {
	/** Raft instance. Everything else is the environment. */
	struct raft raft;
	/** Journal. Survives restart. */
	struct raft_journal journal;
	/** Network. Does not survive restart. */
	struct raft_net net;
	/**
	 * Worker fiber for async work. It can be blocked in order to test what
	 * happens when async work is not dispatched too long.
	 */
	struct fiber *worker;
	/**
	 * Trigger installed by the node automatically, to increment update
	 * counter.
	 */
	struct trigger on_update;
	/**
	 * Update counter helps to check if the triggers are called when
	 * expected. Each trigger invocation increments it.
	 */
	int update_count;
	/**
	 * True if async work was scheduled by raft, but it wasn't dispatched
	 * yet.
	 */
	bool has_work;
	/**
	 * True if the worker fiber should stop executing async work and should
	 * wait for an explicit unblock.
	 */
	bool is_work_blocked;

	/**
	 * Configuration options. Saved here for the sake of being able to
	 * survive a restart.
	 */
	bool cfg_is_enabled;
	bool cfg_is_candidate;
	double cfg_election_timeout;
	int cfg_election_quorum;
	double cfg_death_timeout;
	uint32_t cfg_instance_id;
	struct vclock *cfg_vclock;
};

/** Create a raft node from the scratch. */
void
raft_node_create(struct raft_node *node);

/** Remove all network messages. To simplify testing. */
void
raft_node_net_drop(struct raft_node *node);

/** Check if a network message with index @a i matches the given parameters. */
bool
raft_node_net_check_msg(const struct raft_node *node, int i,
			enum raft_state state, uint64_t term, uint32_t vote,
			const char *vclock);

/** Check full state of the raft instance to match the given parameters. */
bool
raft_node_check_full_state(const struct raft_node *node, enum raft_state state,
			   uint32_t leader, uint64_t term, uint32_t vote,
			   uint64_t volatile_term, uint32_t volatile_vote,
			   const char *vclock);

/** Check if a journal message with index @a i matches the given parameters. */
bool
raft_node_journal_check_row(const struct raft_node *node, int i, uint64_t term,
			    uint32_t vote);

/** Simulate @a count of WAL rows from a given replica, to propagate vclock. */
void
raft_node_journal_follow(struct raft_node *node, uint32_t replica_id,
			 int64_t count);

/** Bump term of the instance */
void
raft_node_new_term(struct raft_node *node);

/** Deliver @a msg message from @a source instance to the given node. */
int
raft_node_process_msg(struct raft_node *node, const struct raft_msg *msg,
		      uint32_t source);

/**
 * Deliver a vote response message from @a source instance to the given node.
 * It says @a source voted for @a vote in the specified @a term, and it is in
 * 'follower' state.
 */
int
raft_node_send_vote_response(struct raft_node *node, uint64_t term,
			     uint32_t vote, uint32_t source);

/**
 * Deliver a vote request message from @a source instance to the given node.
 * It says the sender has the specified vclock in @a term, and it state is
 * 'candidate'.
 */
int
raft_node_send_vote_request(struct raft_node *node, uint64_t term,
			    const char *vclock, uint32_t source);

/**
 * Deliver a message from a leader @a source just saying that it is a leader.
 * It says the sender is in 'leader' state, has the specified @a term.
 */
int
raft_node_send_leader(struct raft_node *node, uint64_t term, uint32_t source);

/**
 * Deliver a message from a follower @a source just saying that it is a
 * follower. It says the sender is in 'follower' state, has the specified
 * @a term.
 */
int
raft_node_send_follower(struct raft_node *node, uint64_t term, uint32_t source);

/** Deliver a heartbeat message from @a source instance. */
void
raft_node_send_heartbeat(struct raft_node *node, uint32_t source);

/** Restart the node. The same as stop + start. */
void
raft_node_restart(struct raft_node *node);

/**
 * Stop the node. The raft instance is destroyed, the worker is stopped, network
 * messages are lost.
 */
void
raft_node_stop(struct raft_node *node);

/** Start the node. Raft instance is created and recovered from the journal. */
void
raft_node_start(struct raft_node *node);

/** Block async work execution. */
void
raft_node_block(struct raft_node *node);

/** Unblock async work execution. */
void
raft_node_unblock(struct raft_node *node);

/** Configuration methods. */

void
raft_node_cfg_is_enabled(struct raft_node *node, bool value);

void
raft_node_cfg_is_candidate(struct raft_node *node, bool value);

void
raft_node_cfg_election_timeout(struct raft_node *node, double value);

void
raft_node_cfg_election_quorum(struct raft_node *node, int value);

void
raft_node_cfg_death_timeout(struct raft_node *node, double value);

/** Check that @a msg message matches the given arguments. */
bool
raft_msg_check(const struct raft_msg *msg, enum raft_state state, uint64_t term,
	       uint32_t vote, const char *vclock);

/** Propagate event loop to a next event and handle it. */
void
raft_run_next_event(void);

/** Give worker fibers time to finish their work. */
void
raft_run_async_work(void);

/** Run event loop for @a duration number of seconds. */
void
raft_run_for(double duration);

/** Destroy the raft instance and its environment. */
void
raft_node_destroy(struct raft_node *node);

/** Global monotonic time used by the raft instance. */
static inline double
raft_time(void)
{
	return fakeev_time();
}

/**
 * A helper to simplify transformation of a vclock string to an object. Without
 * caring about errors.
 */
static void
raft_vclock_from_string(struct vclock *vclock, const char *str)
{
	vclock_create(vclock);
	size_t rc = vclock_from_string(vclock, str);
	assert(rc == 0);
	(void)rc;
}

/**
 * A helper to initialize all the necessary subsystems before @a test, and free
 * them afterwards.
 */
void
raft_run_test(const char *log_file, fiber_func test);

#define raft_start_test(n) { \
	header(); \
	say_verbose("-------- RAFT start test %s --------", __func__); \
	plan(n); \
}

#define raft_finish_test() { \
	say_verbose("-------- RAFT end test %s --------", __func__); \
	fakeev_reset(); \
	check_plan(); \
	footer(); \
}
