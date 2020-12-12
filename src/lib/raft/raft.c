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
#include "raft_ev.h"
#include "exception.h"
#include "fiber.h"
#include "tt_static.h"

/**
 * Maximal random deviation of the election timeout. From the configured value.
 */
#define RAFT_RANDOM_ELECTION_FACTOR 0.1

/**
 * When decoding we should never trust that there is
 * a valid data incomes.
 */
const char *
raft_state_str(uint32_t state)
{
	static const char *str[] = {
		[0]			= "invalid (0)",
		[RAFT_STATE_FOLLOWER]	= "follower",
		[RAFT_STATE_CANDIDATE]	= "candidate",
		[RAFT_STATE_LEADER]	= "leader",
	};

	if (state < lengthof(str))
		return str[state];

	return "invalid (x)";
};

/** Shortcut for vtab 'broadcast' method. */
static inline void
raft_broadcast(struct raft *raft, const struct raft_msg *req)
{
	raft->vtab->broadcast(raft, req);
}

/** Shortcut for vtab 'write' method. */
static inline void
raft_write(struct raft *raft, const struct raft_msg *req)
{
	raft->vtab->write(raft, req);
}

/** Shortcut for vtab 'schedule_async' method. */
static inline void
raft_schedule_async(struct raft *raft)
{
	/*
	 * The method is called from inside of the state machine, when yields
	 * are not allowed for its simplicity.
	 */
	int csw = fiber()->csw;
	raft->vtab->schedule_async(raft);
	assert(csw == fiber()->csw);
	(void)csw;
}

/**
 * Check if Raft is completely synced with disk. Meaning all its critical values
 * are in WAL. Only in that state the node can become a leader or a candidate.
 * If the node has a not flushed data, it means either the term was bumped, or
 * a new vote was made.
 *
 * In case of term bump it means either there is another node with a newer term,
 * and this one should be a follower; or this node bumped the term itself along
 * with making a vote to start a new election - then it is also a follower which
 * will turn into a candidate when the flush is done.
 *
 * In case of a new not flushed vote it means either this node voted for some
 * other node, and must be a follower; or it voted for self, and also must be a
 * follower, but will become a candidate when the flush is done.
 *
 * In total - when something is not synced with disk, the instance is a follower
 * in any case.
 */
static bool
raft_is_fully_on_disk(const struct raft *raft)
{
	return raft->volatile_term == raft->term &&
	       raft->volatile_vote == raft->vote;
}

/**
 * Raft protocol says that election timeout should be a bit randomized so as
 * the nodes wouldn't start election at the same time and end up with not having
 * a quorum for anybody. This implementation randomizes the election timeout by
 * adding {election timeout * random factor} value, where max value of the
 * factor is a constant floating point value > 0.
 */
static inline double
raft_new_random_election_shift(const struct raft *raft)
{
	double timeout = raft->election_timeout;
	/* Translate to ms. Integer is needed to be able to use mod below. */
	uint32_t rand_part =
		(uint32_t)(timeout * RAFT_RANDOM_ELECTION_FACTOR * 1000);
	if (rand_part == 0)
		rand_part = 1;
	/*
	 * XXX: this is not giving a good distribution, but it is not so trivial
	 * to implement a correct random value generator. There is a task to
	 * unify all such places. Not critical here.
	 */
	rand_part = rand() % (rand_part + 1);
	return rand_part / 1000.0;
}

/**
 * Raft says that during election a node1 can vote for node2, if node2 has a
 * bigger term, or has the same term but longer log. In case of Tarantool it
 * means the node2 vclock should be >= node1 vclock, in all components. It is
 * not enough to compare only one component. At least because there may be not
 * a previous leader when the election happens first time. Or a node could
 * restart and forget who the previous leader was.
 */
static inline bool
raft_can_vote_for(const struct raft *raft, const struct vclock *v)
{
	int cmp = vclock_compare_ignore0(v, raft->vclock);
	return cmp == 0 || cmp == 1;
}

/** Schedule broadcast of the complete Raft state to all the followers. */
static void
raft_schedule_broadcast(struct raft *raft);

/** Raft state machine methods. 'sm' stands for State Machine. */

/**
 * Start the state machine. When it is stopped, Raft state is updated and
 * goes to WAL when necessary, but it does not affect the instance operation.
 * For example, when Raft is stopped, the instance role does not affect whether
 * it is writable.
 */
static void
raft_sm_start(struct raft *raft);

/**
 * Stop the state machine. Now until Raft is re-enabled,
 * - Raft stops affecting the instance operation;
 * - this node can't become a leader;
 * - this node can't vote.
 */
static void
raft_sm_stop(struct raft *raft);

/**
 * When the instance is a follower but is allowed to be a leader, it will wait
 * for death of the current leader to start new election.
 */
static void
raft_sm_wait_leader_dead(struct raft *raft);

/**
 * Wait for the leader death timeout until a leader lets the node know he is
 * alive. Otherwise the node will start a new term. Can be useful when it is not
 * known whether the leader is alive, but it is undesirable to start a new term
 * immediately. Because in case the leader is alive, a new term would stun him
 * and therefore would stun DB write requests. Usually happens when a follower
 * restarts and may need some time to hear something from the leader.
 */
static void
raft_sm_wait_leader_found(struct raft *raft);

/**
 * If election is started by this node, or it voted for some other node started
 * the election, and it can be a leader itself, it will wait until the current
 * election times out. When it happens, the node will start new election.
 */
static void
raft_sm_wait_election_end(struct raft *raft);

/** Bump volatile term and schedule its flush to disk. */
static void
raft_sm_schedule_new_term(struct raft *raft, uint64_t new_term);

/** Bump volatile vote and schedule its flush to disk. */
static void
raft_sm_schedule_new_vote(struct raft *raft, uint32_t new_vote);

/**
 * Bump term and vote for self immediately. After that is persisted, the
 * election timeout will be activated. Unless during that nothing newer happens.
 */
static void
raft_sm_schedule_new_election(struct raft *raft);

/**
 * The main trigger of Raft state machine - start new election when the current
 * leader dies, or when there is no a leader and the previous election failed.
 */
static void
raft_sm_schedule_new_election_cb(struct ev_loop *loop, struct ev_timer *timer,
				 int events);

/** Start Raft state flush to disk. */
static void
raft_sm_pause_and_dump(struct raft *raft);

static void
raft_sm_become_leader(struct raft *raft);

static void
raft_sm_follow_leader(struct raft *raft, uint32_t leader);

static void
raft_sm_become_candidate(struct raft *raft);

static const char *
raft_msg_to_string(const struct raft_msg *req)
{
	assert(req->term != 0);
	char buf[1024];
	int size = sizeof(buf);
	char *pos = buf;
	int rc = snprintf(pos, size, "{term: %llu",
			  (unsigned long long)req->term);
	assert(rc >= 0 && rc < size);
	pos += rc;
	size -= rc;
	if (req->vote != 0) {
		rc = snprintf(pos, size, ", vote: %u", req->vote);
		assert(rc >= 0 && rc < size);
		pos += rc;
		size -= rc;
	}
	if (req->state != 0) {
		rc = snprintf(pos, size, ", state: %s",
			      raft_state_str(req->state));
		assert(rc >= 0 && rc < size);
		pos += rc;
		size -= rc;
	}
	if (req->vclock != NULL) {
		rc = snprintf(pos, size, ", vclock: %s",
			      vclock_to_string(req->vclock));
		assert(rc >= 0 && rc < size);
		pos += rc;
		size -= rc;
	}
	rc = snprintf(pos, size, "}");
	assert(rc >= 0 && rc < size);
	pos += rc;
	return tt_cstr(buf, pos - buf);
}

void
raft_process_recovery(struct raft *raft, const struct raft_msg *req)
{
	say_verbose("RAFT: recover %s", raft_msg_to_string(req));
	if (req->term != 0) {
		raft->term = req->term;
		raft->volatile_term = req->term;
	}
	if (req->vote != 0) {
		raft->vote = req->vote;
		raft->volatile_vote = req->vote;
	}
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
	assert(!raft_is_enabled(raft));
}

int
raft_process_msg(struct raft *raft, const struct raft_msg *req, uint32_t source)
{
	say_info("RAFT: message %s from %u", raft_msg_to_string(req), source);
	assert(source > 0);
	assert(source != raft->self);
	if (req->term == 0 || req->state == 0) {
		diag_set(RaftError, "Raft term and state can't be zero");
		return -1;
	}
	if (req->state == RAFT_STATE_CANDIDATE &&
	    (req->vote != source || req->vclock == NULL)) {
		diag_set(RaftError, "Candidate should always vote for self and "
			 "provide its vclock");
		return -1;
	}
	/* Outdated request. */
	if (req->term < raft->volatile_term) {
		say_info("RAFT: the message is ignored due to outdated term - "
			 "current term is %u", raft->volatile_term);
		return 0;
	}

	/* Term bump. */
	if (req->term > raft->volatile_term)
		raft_sm_schedule_new_term(raft, req->term);
	/*
	 * Either a vote request during an on-going election. Or an old vote
	 * persisted long time ago and still broadcasted. Or a vote response.
	 */
	if (req->vote != 0) {
		switch (raft->state) {
		case RAFT_STATE_FOLLOWER:
		case RAFT_STATE_LEADER:
			if (!raft->is_enabled) {
				say_info("RAFT: vote request is skipped - RAFT "
					 "is disabled");
				break;
			}
			if (raft->leader != 0) {
				say_info("RAFT: vote request is skipped - the "
					 "leader is already known - %u",
					 raft->leader);
				break;
			}
			if (req->vote == raft->self) {
				/*
				 * This is entirely valid. This instance could
				 * request a vote, then become a follower or
				 * leader, and then get the response.
				 */
				say_info("RAFT: vote request is skipped - "
					 "can't accept vote for self if not a "
					 "candidate");
				break;
			}
			if (req->state != RAFT_STATE_CANDIDATE) {
				say_info("RAFT: vote request is skipped - "
					 "this is a notification about a vote "
					 "for a third node, not a request");
				break;
			}
			if (raft->volatile_vote != 0) {
				say_info("RAFT: vote request is skipped - "
					 "already voted in this term");
				break;
			}
			/* Vclock is not NULL, validated above. */
			if (!raft_can_vote_for(raft, req->vclock)) {
				say_info("RAFT: vote request is skipped - the "
					 "vclock is not acceptable");
				break;
			}
			/*
			 * Either the term is new, or didn't vote in the current
			 * term yet. Anyway can vote now.
			 */
			raft_sm_schedule_new_vote(raft, req->vote);
			break;
		case RAFT_STATE_CANDIDATE:
			/* Check if this is a vote for a competing candidate. */
			if (req->vote != raft->self) {
				say_info("RAFT: vote request is skipped - "
					 "competing candidate");
				break;
			}
			/*
			 * Vote for self was requested earlier in this round,
			 * and now was answered by some other instance.
			 */
			assert(raft->volatile_vote == raft->self);
			bool was_set = bit_set(&raft->vote_mask, source);
			raft->vote_count += !was_set;
			if (raft->vote_count < raft->election_quorum) {
				say_info("RAFT: accepted vote for self, vote "
					 "count is %d/%d", raft->vote_count,
					 raft->election_quorum);
				break;
			}
			raft_sm_become_leader(raft);
			break;
		default:
			unreachable();
		}
	}
	if (req->state != RAFT_STATE_LEADER) {
		if (source == raft->leader) {
			say_info("RAFT: the node %u has resigned from the "
				 "leader role", raft->leader);
			/*
			 * Candidate node clears leader implicitly when starts a
			 * new term, but non-candidate won't do that, so clear
			 * it manually.
			 */
			raft->leader = 0;
			if (raft->is_candidate)
				raft_sm_schedule_new_election(raft);
		}
		return 0;
	}
	/* The node is a leader, but it is already known. */
	if (source == raft->leader)
		return 0;
	/*
	 * XXX: A message from a conflicting leader. Split brain, basically.
	 * Need to decide what to do. Current solution is to do nothing. In
	 * future either this node should try to become a leader, or should stop
	 * all writes and require manual intervention.
	 */
	if (raft->leader != 0) {
		say_warn("RAFT: conflicting leader detected in one term - "
			 "known is %u, received %u", raft->leader, source);
		return 0;
	}

	/* New leader was elected. */
	raft_sm_follow_leader(raft, source);
	return 0;
}

void
raft_process_heartbeat(struct raft *raft, uint32_t source)
{
	/*
	 * Raft handles heartbeats from all instances, including anon instances
	 * which don't participate in Raft.
	 */
	if (source == 0)
		return;
	/*
	 * When not a candidate - don't wait for anything. Therefore do not care
	 * about the leader being dead.
	 */
	if (!raft->is_candidate)
		return;
	/* Don't care about heartbeats when this node is a leader itself. */
	if (raft->state == RAFT_STATE_LEADER)
		return;
	/* Not interested in heartbeats from not a leader. */
	if (raft->leader != source)
		return;
	/*
	 * The instance currently is busy with writing something on disk. Can't
	 * react to heartbeats.
	 */
	if (raft->is_write_in_progress)
		return;
	/*
	 * XXX: it may be expensive to reset the timer like that. It may be less
	 * expensive to let the timer work, and remember last timestamp when
	 * anything was heard from the leader. Then in the timer callback check
	 * the timestamp, and restart the timer, if it is fine.
	 */
	assert(raft_ev_is_active(&raft->timer));
	raft_ev_timer_stop(raft_loop(), &raft->timer);
	raft_sm_wait_leader_dead(raft);
}

/* Dump Raft state to WAL in a blocking way. */
static void
raft_worker_handle_io(struct raft *raft)
{
	assert(raft->is_write_in_progress);
	/* During write Raft can't be anything but a follower. */
	assert(raft->state == RAFT_STATE_FOLLOWER);
	struct raft_msg req;

	if (raft_is_fully_on_disk(raft)) {
end_dump:
		raft->is_write_in_progress = false;
		/*
		 * The state machine is stable. Can see now, to what state to
		 * go.
		 */
		if (!raft->is_candidate) {
			/*
			 * If not a candidate, can't do anything except vote for
			 * somebody (if Raft is enabled). Nothing to do except
			 * staying a follower without timeouts.
			 */
		} else if (raft->leader != 0) {
			/* There is a known leader. Wait until it is dead. */
			raft_sm_wait_leader_dead(raft);
		} else if (raft->vote == raft->self) {
			/* Just wrote own vote. */
			if (raft->election_quorum == 1)
				raft_sm_become_leader(raft);
			else
				raft_sm_become_candidate(raft);
		} else if (raft->vote != 0) {
			/*
			 * Voted for some other node. Wait if it manages to
			 * become a leader.
			 */
			raft_sm_wait_election_end(raft);
		} else {
			/* No leaders, no votes. */
			raft_sm_schedule_new_vote(raft, raft->self);
		}
	} else {
		memset(&req, 0, sizeof(req));
		assert(raft->volatile_term >= raft->term);
		req.term = raft->volatile_term;
		req.vote = raft->volatile_vote;
		/*
		 * Skip vclock. It is used only to be sent to network when vote
		 * for self. It is a job of the vclock owner to persist it
		 * anyhow.
		 *
		 * Skip state. That would be strictly against Raft protocol. The
		 * reason is that it does not make much sense - even if the node
		 * is a leader now, after the node is restarted, there will be
		 * another leader elected by that time likely.
		 */
		raft_write(raft, &req);
		say_info("RAFT: persisted state %s", raft_msg_to_string(&req));

		assert(req.term >= raft->term);
		raft->term = req.term;
		raft->vote = req.vote;
		/*
		 * Persistent state is visible, and it was changed - broadcast.
		 */
		raft_schedule_broadcast(raft);
		if (raft_is_fully_on_disk(raft))
			goto end_dump;
	}
}

/* Broadcast Raft complete state to the followers. */
static void
raft_worker_handle_broadcast(struct raft *raft)
{
	assert(raft->is_broadcast_scheduled);
	struct raft_msg req;
	memset(&req, 0, sizeof(req));
	req.term = raft->term;
	req.vote = raft->vote;
	req.state = raft->state;
	if (req.state == RAFT_STATE_CANDIDATE) {
		assert(raft->vote == raft->self);
		req.vclock = raft->vclock;
	}
	raft_broadcast(raft, &req);
	raft->is_broadcast_scheduled = false;
}

void
raft_process_async(struct raft *raft)
{
	bool is_idle;
	do {
		is_idle = true;
		if (raft->is_write_in_progress) {
			raft_worker_handle_io(raft);
			is_idle = false;
		}
		if (raft->is_broadcast_scheduled) {
			raft_worker_handle_broadcast(raft);
			is_idle = false;
		}
	} while (!is_idle);
	assert(raft_is_fully_on_disk(raft));
}

static void
raft_sm_pause_and_dump(struct raft *raft)
{
	assert(raft->state == RAFT_STATE_FOLLOWER);
	if (raft->is_write_in_progress)
		return;
	raft_ev_timer_stop(raft_loop(), &raft->timer);
	raft_schedule_async(raft);
	raft->is_write_in_progress = true;
}

static void
raft_sm_become_leader(struct raft *raft)
{
	assert(raft->state != RAFT_STATE_LEADER);
	say_info("RAFT: enter leader state with quorum %d",
		 raft->election_quorum);
	assert(raft->leader == 0);
	assert(raft->is_candidate);
	assert(!raft->is_write_in_progress);
	raft->state = RAFT_STATE_LEADER;
	raft->leader = raft->self;
	raft_ev_timer_stop(raft_loop(), &raft->timer);
	/* State is visible and it is changed - broadcast. */
	raft_schedule_broadcast(raft);
}

static void
raft_sm_follow_leader(struct raft *raft, uint32_t leader)
{
	say_info("RAFT: leader is %u, follow", leader);
	assert(raft->state != RAFT_STATE_LEADER);
	assert(raft->leader == 0);
	raft->state = RAFT_STATE_FOLLOWER;
	raft->leader = leader;
	if (!raft->is_write_in_progress && raft->is_candidate) {
		raft_ev_timer_stop(raft_loop(), &raft->timer);
		raft_sm_wait_leader_dead(raft);
	}
	/* State is visible and it is changed - broadcast. */
	raft_schedule_broadcast(raft);
}

static void
raft_sm_become_candidate(struct raft *raft)
{
	say_info("RAFT: enter candidate state with 1 self vote");
	assert(raft->state == RAFT_STATE_FOLLOWER);
	assert(raft->leader == 0);
	assert(raft->vote == raft->self);
	assert(raft->is_candidate);
	assert(!raft->is_write_in_progress);
	assert(raft->election_quorum > 1);
	raft->state = RAFT_STATE_CANDIDATE;
	raft->vote_count = 1;
	raft->vote_mask = 0;
	bit_set(&raft->vote_mask, raft->self);
	raft_sm_wait_election_end(raft);
	/* State is visible and it is changed - broadcast. */
	raft_schedule_broadcast(raft);
}

static void
raft_sm_schedule_new_term(struct raft *raft, uint64_t new_term)
{
	say_info("RAFT: bump term to %llu, follow", new_term);
	assert(new_term > raft->volatile_term);
	assert(raft->volatile_term >= raft->term);
	raft->volatile_term = new_term;
	/* New terms means completely new Raft state. */
	raft->volatile_vote = 0;
	raft->leader = 0;
	raft->state = RAFT_STATE_FOLLOWER;
	raft_sm_pause_and_dump(raft);
	/*
	 * State is visible and it is changed - broadcast. Term is also visible,
	 * but only persistent term. Volatile term is not broadcasted until
	 * saved to disk.
	 */
	raft_schedule_broadcast(raft);
}

static void
raft_sm_schedule_new_vote(struct raft *raft, uint32_t new_vote)
{
	say_info("RAFT: vote for %u, follow", new_vote);
	assert(raft->volatile_vote == 0);
	assert(raft->leader == 0);
	assert(raft->state == RAFT_STATE_FOLLOWER);
	raft->volatile_vote = new_vote;
	raft_sm_pause_and_dump(raft);
	/* Nothing visible is changed - no broadcast. */
}

static void
raft_sm_schedule_new_election(struct raft *raft)
{
	say_info("RAFT: begin new election round");
	assert(raft_is_fully_on_disk(raft));
	assert(raft->is_candidate);
	/* Everyone is a follower until its vote for self is persisted. */
	raft_sm_schedule_new_term(raft, raft->term + 1);
	raft_sm_schedule_new_vote(raft, raft->self);
}

static void
raft_sm_schedule_new_election_cb(struct ev_loop *loop, struct ev_timer *timer,
				 int events)
{
	(void)events;
	struct raft *raft = timer->data;
	assert(timer == &raft->timer);
	raft_ev_timer_stop(loop, timer);
	raft_sm_schedule_new_election(raft);
}

static void
raft_sm_wait_leader_dead(struct raft *raft)
{
	assert(!raft_ev_is_active(&raft->timer));
	assert(!raft->is_write_in_progress);
	assert(raft->is_candidate);
	assert(raft->state == RAFT_STATE_FOLLOWER);
	assert(raft->leader != 0);
	raft_ev_timer_set(&raft->timer, raft->death_timeout, raft->death_timeout);
	raft_ev_timer_start(raft_loop(), &raft->timer);
}

static void
raft_sm_wait_leader_found(struct raft *raft)
{
	assert(!raft_ev_is_active(&raft->timer));
	assert(!raft->is_write_in_progress);
	assert(raft->is_candidate);
	assert(raft->state == RAFT_STATE_FOLLOWER);
	assert(raft->leader == 0);
	raft_ev_timer_set(&raft->timer, raft->death_timeout, raft->death_timeout);
	raft_ev_timer_start(raft_loop(), &raft->timer);
}

static void
raft_sm_wait_election_end(struct raft *raft)
{
	assert(!raft_ev_is_active(&raft->timer));
	assert(!raft->is_write_in_progress);
	assert(raft->is_candidate);
	assert(raft->state == RAFT_STATE_FOLLOWER ||
	       (raft->state == RAFT_STATE_CANDIDATE &&
		raft->volatile_vote == raft->self));
	assert(raft->leader == 0);
	double election_timeout = raft->election_timeout +
				  raft_new_random_election_shift(raft);
	raft_ev_timer_set(&raft->timer, election_timeout, election_timeout);
	raft_ev_timer_start(raft_loop(), &raft->timer);
}

static void
raft_sm_start(struct raft *raft)
{
	say_info("RAFT: start state machine");
	assert(!raft_ev_is_active(&raft->timer));
	assert(!raft->is_enabled);
	assert(raft->state == RAFT_STATE_FOLLOWER);
	raft->is_enabled = true;
	raft->is_candidate = raft->is_cfg_candidate;
	if (raft->is_write_in_progress) {
		/*
		 * Nop. If write is in progress, the state machine is frozen. It
		 * is continued when write ends.
		 */
	} else if (!raft->is_candidate) {
		/*
		 * Nop. When a node is not a candidate, it can't initiate
		 * elections anyway, so it does not need to monitor the leader.
		 */
	} else if (raft->leader != 0) {
		raft_sm_wait_leader_dead(raft);
	} else {
		/*
		 * Don't start new election. The situation is most likely
		 * happened because this node was restarted. Instance restarts
		 * may happen in the cluster, and each restart shouldn't
		 * disturb the current leader. Give it time to notify this node
		 * that there is a leader.
		 */
		raft_sm_wait_leader_found(raft);
	}
	/*
	 * Nothing changed. But when raft was stopped, its state wasn't sent to
	 * replicas. At least this was happening at the moment of this being
	 * written. On the other hand, this instance may have a term bigger than
	 * any other term in the cluster. And if it wouldn't share the term, it
	 * would ignore all the messages, including vote requests.
	 */
	raft_schedule_broadcast(raft);
}

static void
raft_sm_stop(struct raft *raft)
{
	say_info("RAFT: stop state machine");
	assert(raft->is_enabled);
	raft->is_enabled = false;
	raft->is_candidate = false;
	if (raft->state == RAFT_STATE_LEADER)
		raft->leader = 0;
	raft->state = RAFT_STATE_FOLLOWER;
	raft_ev_timer_stop(raft_loop(), &raft->timer);
	/* State is visible and changed - broadcast. */
	raft_schedule_broadcast(raft);
}

void
raft_checkpoint_remote(const struct raft *raft, struct raft_msg *req)
{
	memset(req, 0, sizeof(*req));
	/*
	 * Volatile state is never used for any communications.
	 * Use only persisted state.
	 */
	req->term = raft->term;
	req->vote = raft->vote;
	req->state = raft->state;
	/*
	 * Raft does not own vclock, so it always expects it passed externally.
	 * Vclock is sent out only by candidate instances.
	 */
	if (req->state == RAFT_STATE_CANDIDATE)
		req->vclock = raft->vclock;
}

void
raft_checkpoint_local(const struct raft *raft, struct raft_msg *req)
{
	memset(req, 0, sizeof(*req));
	req->term = raft->term;
	req->vote = raft->vote;
}

void
raft_on_update(struct raft *raft, struct trigger *trigger)
{
	trigger_add(&raft->on_update, trigger);
}

void
raft_cfg_is_enabled(struct raft *raft, bool is_enabled)
{
	if (is_enabled == raft->is_enabled)
		return;

	if (!is_enabled)
		raft_sm_stop(raft);
	else
		raft_sm_start(raft);
}

void
raft_cfg_is_candidate(struct raft *raft, bool is_candidate)
{
	bool old_is_candidate = raft->is_candidate;
	raft->is_cfg_candidate = is_candidate;
	raft->is_candidate = is_candidate && raft->is_enabled;
	if (raft->is_candidate == old_is_candidate)
		return;

	if (raft->is_candidate) {
		assert(raft->state == RAFT_STATE_FOLLOWER);
		if (raft->is_write_in_progress) {
			/*
			 * If there is an on-going WAL write, it means there was
			 * some node who sent newer data to this node. So it is
			 * probably a better candidate. Anyway can't do anything
			 * until the new state is fully persisted.
			 */
		} else if (raft->leader != 0) {
			raft_sm_wait_leader_dead(raft);
		} else {
			raft_sm_wait_leader_found(raft);
		}
	} else {
		if (raft->state != RAFT_STATE_LEADER) {
			/* Do not wait for anything while being a voter. */
			raft_ev_timer_stop(raft_loop(), &raft->timer);
		}
		if (raft->state != RAFT_STATE_FOLLOWER) {
			if (raft->state == RAFT_STATE_LEADER)
				raft->leader = 0;
			raft->state = RAFT_STATE_FOLLOWER;
			/* State is visible and changed - broadcast. */
			raft_schedule_broadcast(raft);
		}
	}
}

void
raft_cfg_election_timeout(struct raft *raft, double timeout)
{
	if (timeout == raft->election_timeout)
		return;

	raft->election_timeout = timeout;
	if (raft->vote != 0 && raft->leader == 0 && raft->is_candidate) {
		assert(raft_ev_is_active(&raft->timer));
		struct ev_loop *loop = raft_loop();
		double timeout = raft_ev_timer_remaining(loop, &raft->timer) -
				 raft->timer.at + raft->election_timeout;
		raft_ev_timer_stop(loop, &raft->timer);
		raft_ev_timer_set(&raft->timer, timeout, timeout);
		raft_ev_timer_start(loop, &raft->timer);
	}
}

void
raft_cfg_election_quorum(struct raft *raft, int election_quorum)
{
	/* At least self is always a part of the quorum. */
	assert(election_quorum > 0);
	raft->election_quorum = election_quorum;
	if (raft->state == RAFT_STATE_CANDIDATE &&
	    raft->vote_count >= raft->election_quorum)
		raft_sm_become_leader(raft);
}

void
raft_cfg_death_timeout(struct raft *raft, double death_timeout)
{
	raft->death_timeout = death_timeout;
	if (raft->state == RAFT_STATE_FOLLOWER && raft->is_candidate &&
	    raft->leader != 0) {
		assert(raft_ev_is_active(&raft->timer));
		struct ev_loop *loop = raft_loop();
		double timeout = raft_ev_timer_remaining(loop, &raft->timer) -
				 raft->timer.at + raft->death_timeout;
		raft_ev_timer_stop(loop, &raft->timer);
		raft_ev_timer_set(&raft->timer, timeout, timeout);
		raft_ev_timer_start(loop, &raft->timer);
	}
}

void
raft_cfg_instance_id(struct raft *raft, uint32_t instance_id)
{
	assert(raft->self == 0);
	assert(instance_id != 0);
	raft->self = instance_id;
}

void
raft_cfg_vclock(struct raft *raft, const struct vclock *vclock)
{
	assert(raft->vclock == NULL);
	assert(vclock != NULL);
	raft->vclock = vclock;
}

void
raft_new_term(struct raft *raft)
{
	if (raft->is_enabled)
		raft_sm_schedule_new_term(raft, raft->volatile_term + 1);
}

static void
raft_schedule_broadcast(struct raft *raft)
{
	/*
	 * Broadcast works not only for network, but also for other subsystems
	 * on the same node. The info is delivered to them via update triggers.
	 * But the broadcast happens from inside of the state machine, so it
	 * can't yield.
	 */
	int csw = fiber()->csw;
	trigger_run(&raft->on_update, raft);
	assert(csw == fiber()->csw);
	(void)csw;

	raft->is_broadcast_scheduled = true;
	raft_schedule_async(raft);
}

void
raft_create(struct raft *raft, const struct raft_vtab *vtab)
{
	*raft = (struct raft) {
		.state = RAFT_STATE_FOLLOWER,
		.volatile_term = 1,
		.term =	1,
		.election_quorum = 1,
		.election_timeout = 5,
		.death_timeout = 5,
		.vtab = vtab,
	};
	raft_ev_timer_init(&raft->timer, raft_sm_schedule_new_election_cb,
			   0, 0);
	raft->timer.data = raft;
	rlist_create(&raft->on_update);
}

void
raft_destroy(struct raft *raft)
{
	raft_ev_timer_stop(raft_loop(), &raft->timer);
	trigger_destroy(&raft->on_update);
}
