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
#include "box.h"
#include "tt_static.h"

/**
 * Maximal random deviation of the election timeout. From the configured value.
 */
#define RAFT_RANDOM_ELECTION_FACTOR 0.1

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
	.volatile_term = 1,
	.volatile_vote = 0,
	.is_enabled = false,
	.is_candidate = false,
	.is_cfg_candidate = false,
	.is_write_in_progress = false,
	.is_broadcast_scheduled = false,
	.term = 1,
	.vote = 0,
	.vote_mask = 0,
	.vote_count = 0,
	.worker = NULL,
	.election_timeout = 5,
};

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
raft_is_fully_on_disk(void)
{
	return raft.volatile_term == raft.term &&
	       raft.volatile_vote == raft.vote;
}

/**
 * Raft protocol says that election timeout should be a bit randomized so as
 * the nodes wouldn't start election at the same time and end up with not having
 * a quorum for anybody. This implementation randomizes the election timeout by
 * adding {election timeout * random factor} value, where max value of the
 * factor is a constant floating point value > 0.
 */
static inline double
raft_new_random_election_shift(void)
{
	double timeout = raft.election_timeout;
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
raft_can_vote_for(const struct vclock *v)
{
	int cmp = vclock_compare_ignore0(v, &replicaset.vclock);
	return cmp == 0 || cmp == 1;
}

/**
 * Election quorum is not strictly equal to synchronous replication quorum.
 * Sometimes it can be lowered. That is about bootstrap.
 *
 * The problem with bootstrap is that when the replicaset boots, all the
 * instances can't write to WAL and can't recover from their initial snapshot.
 * They need one node which will boot first, and then they will replicate from
 * it.
 *
 * This one node should boot from its zero snapshot, create replicaset UUID,
 * register self with ID 1 in _cluster space, and then register all the other
 * instances here. To do that the node must be writable. It should have
 * read_only = false, connection quorum satisfied, and be a Raft leader if Raft
 * is enabled.
 *
 * To be elected a Raft leader it needs to perform election. But it can't be
 * done before at least synchronous quorum of the replicas is bootstrapped. And
 * they can't be bootstrapped because wait for a leader to initialize _cluster.
 * Cyclic dependency.
 *
 * This is resolved by truncation of the election quorum to the number of
 * registered replicas, if their count is less than synchronous quorum. That
 * helps to elect a first leader.
 *
 * It may seem that the first node could just declare itself a leader and then
 * strictly follow the protocol from now on, but that won't work, because if the
 * first node will restart after it is booted, but before quorum of replicas is
 * booted, the cluster will stuck again.
 *
 * The current solution is totally safe because
 *
 * - after all the cluster will have node count >= quorum, if user used a
 *   correct config (God help him if he didn't);
 *
 * - synchronous replication quorum is untouched - it is not truncated. Only
 *   leader election quorum is affected. So synchronous data won't be lost.
 */
static inline int
raft_election_quorum(void)
{
	return MIN(replication_synchro_quorum, replicaset.registered_count);
}

/**
 * Wakeup the Raft worker fiber in order to do some async work. If the fiber
 * does not exist yet, it is created.
 */
static void
raft_worker_wakeup(void);

/** Schedule broadcast of the complete Raft state to all the followers. */
static void
raft_schedule_broadcast(void);

/** Raft state machine methods. 'sm' stands for State Machine. */

/**
 * Start the state machine. When it is stopped, Raft state is updated and
 * goes to WAL when necessary, but it does not affect the instance operation.
 * For example, when Raft is stopped, the instance role does not affect whether
 * it is writable.
 */
static void
raft_sm_start(void);

/**
 * Stop the state machine. Now until Raft is re-enabled,
 * - Raft stops affecting the instance operation;
 * - this node can't become a leader;
 * - this node can't vote.
 */
static void
raft_sm_stop(void);

/**
 * When the instance is a follower but is allowed to be a leader, it will wait
 * for death of the current leader to start new election.
 */
static void
raft_sm_wait_leader_dead(void);

/**
 * Wait for the leader death timeout until a leader lets the node know he is
 * alive. Otherwise the node will start a new term. Can be useful when it is not
 * known whether the leader is alive, but it is undesirable to start a new term
 * immediately. Because in case the leader is alive, a new term would stun him
 * and therefore would stun DB write requests. Usually happens when a follower
 * restarts and may need some time to hear something from the leader.
 */
static void
raft_sm_wait_leader_found(void);

/**
 * If election is started by this node, or it voted for some other node started
 * the election, and it can be a leader itself, it will wait until the current
 * election times out. When it happens, the node will start new election.
 */
static void
raft_sm_wait_election_end(void);

/** Bump volatile term and schedule its flush to disk. */
static void
raft_sm_schedule_new_term(uint64_t new_term);

/** Bump volatile vote and schedule its flush to disk. */
static void
raft_sm_schedule_new_vote(uint32_t new_vote);

/**
 * Bump term and vote for self immediately. After that is persisted, the
 * election timeout will be activated. Unless during that nothing newer happens.
 */
static void
raft_sm_schedule_new_election(void);

/**
 * The main trigger of Raft state machine - start new election when the current
 * leader dies, or when there is no a leader and the previous election failed.
 */
static void
raft_sm_schedule_new_election_cb(struct ev_loop *loop, struct ev_timer *timer,
				 int events);

/** Start Raft state flush to disk. */
static void
raft_sm_pause_and_dump(void);

static void
raft_sm_become_leader(void);

static void
raft_sm_follow_leader(uint32_t leader);

static void
raft_sm_become_candidate(void);

static const char *
raft_request_to_string(const struct raft_request *req)
{
	assert(req->term != 0);
	int size = 1024;
	char buf[1024];
	char *pos = buf;
	int rc = snprintf(pos, size, "{term: %llu",
			  (unsigned long long)req->term);
	assert(rc >= 0);
	pos += rc;
	size -= rc;
	if (req->vote != 0) {
		rc = snprintf(pos, size, ", vote: %u", req->vote);
		assert(rc >= 0);
		pos += rc;
		size -= rc;
	}
	if (req->state != 0) {
		rc = snprintf(pos, size, ", state: %s",
			      raft_state_strs[req->state]);
		assert(rc >= 0);
		pos += rc;
		size -= rc;
	}
	if (req->vclock != NULL) {
		rc = snprintf(pos, size, ", vclock: %s",
			      vclock_to_string(req->vclock));
		assert(rc >= 0);
		pos += rc;
		size -= rc;
	}
	rc = snprintf(pos, size, "}");
	assert(rc >= 0);
	pos += rc;
	return tt_cstr(buf, pos - buf);
}

void
raft_process_recovery(const struct raft_request *req)
{
	say_verbose("RAFT: recover %s", raft_request_to_string(req));
	if (req->term != 0) {
		raft.term = req->term;
		raft.volatile_term = req->term;
	}
	if (req->vote != 0) {
		raft.vote = req->vote;
		raft.volatile_vote = req->vote;
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
	assert(!raft_is_enabled());
}

int
raft_process_msg(const struct raft_request *req, uint32_t source)
{
	say_info("RAFT: message %s from %u", raft_request_to_string(req),
		 source);
	assert(source > 0);
	assert(source != instance_id);
	if (req->term == 0 || req->state == 0) {
		diag_set(ClientError, ER_PROTOCOL, "Raft term and state can't "
			 "be zero");
		return -1;
	}
	if (req->state == RAFT_STATE_CANDIDATE &&
	    (req->vote != source || req->vclock == NULL)) {
		diag_set(ClientError, ER_PROTOCOL, "Candidate should always "
			 "vote for self and provide its vclock");
		return -1;
	}
	/* Outdated request. */
	if (req->term < raft.volatile_term) {
		say_info("RAFT: the message is ignored due to outdated term - "
			 "current term is %u", raft.volatile_term);
		return 0;
	}

	/* Term bump. */
	if (req->term > raft.volatile_term)
		raft_sm_schedule_new_term(req->term);
	/*
	 * Either a vote request during an on-going election. Or an old vote
	 * persisted long time ago and still broadcasted. Or a vote response.
	 */
	if (req->vote != 0) {
		switch (raft.state) {
		case RAFT_STATE_FOLLOWER:
		case RAFT_STATE_LEADER:
			if (!raft.is_enabled) {
				say_info("RAFT: vote request is skipped - RAFT "
					 "is disabled");
				break;
			}
			if (raft.leader != 0) {
				say_info("RAFT: vote request is skipped - the "
					 "leader is already known - %u",
					 raft.leader);
				break;
			}
			if (req->vote == instance_id) {
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
			if (raft.volatile_vote != 0) {
				say_info("RAFT: vote request is skipped - "
					 "already voted in this term");
				break;
			}
			/* Vclock is not NULL, validated above. */
			if (!raft_can_vote_for(req->vclock)) {
				say_info("RAFT: vote request is skipped - the "
					 "vclock is not acceptable");
				break;
			}
			/*
			 * Either the term is new, or didn't vote in the current
			 * term yet. Anyway can vote now.
			 */
			raft_sm_schedule_new_vote(req->vote);
			break;
		case RAFT_STATE_CANDIDATE:
			/* Check if this is a vote for a competing candidate. */
			if (req->vote != instance_id) {
				say_info("RAFT: vote request is skipped - "
					 "competing candidate");
				break;
			}
			/*
			 * Vote for self was requested earlier in this round,
			 * and now was answered by some other instance.
			 */
			assert(raft.volatile_vote == instance_id);
			int quorum = raft_election_quorum();
			bool was_set = bit_set(&raft.vote_mask, source);
			raft.vote_count += !was_set;
			if (raft.vote_count < quorum) {
				say_info("RAFT: accepted vote for self, vote "
					 "count is %d/%d", raft.vote_count,
					 quorum);
				break;
			}
			raft_sm_become_leader();
			break;
		default:
			unreachable();
		}
	}
	if (req->state != RAFT_STATE_LEADER) {
		if (source == raft.leader) {
			say_info("RAFT: the node %u has resigned from the "
				 "leader role", raft.leader);
			/*
			 * Candidate node clears leader implicitly when starts a
			 * new term, but non-candidate won't do that, so clear
			 * it manually.
			 */
			raft.leader = 0;
			if (raft.is_candidate)
				raft_sm_schedule_new_election();
		}
		return 0;
	}
	/* The node is a leader, but it is already known. */
	if (source == raft.leader)
		return 0;
	/*
	 * XXX: A message from a conflicting leader. Split brain, basically.
	 * Need to decide what to do. Current solution is to do nothing. In
	 * future either this node should try to become a leader, or should stop
	 * all writes and require manual intervention.
	 */
	if (raft.leader != 0) {
		say_warn("RAFT: conflicting leader detected in one term - "
			 "known is %u, received %u", raft.leader, source);
		return 0;
	}

	/* New leader was elected. */
	raft_sm_follow_leader(source);
	return 0;
}

void
raft_process_heartbeat(uint32_t source)
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
	if (!raft.is_candidate)
		return;
	/* Don't care about heartbeats when this node is a leader itself. */
	if (raft.state == RAFT_STATE_LEADER)
		return;
	/* Not interested in heartbeats from not a leader. */
	if (raft.leader != source)
		return;
	/*
	 * The instance currently is busy with writing something on disk. Can't
	 * react to heartbeats.
	 */
	if (raft.is_write_in_progress)
		return;
	/*
	 * XXX: it may be expensive to reset the timer like that. It may be less
	 * expensive to let the timer work, and remember last timestamp when
	 * anything was heard from the leader. Then in the timer callback check
	 * the timestamp, and restart the timer, if it is fine.
	 */
	assert(ev_is_active(&raft.timer));
	ev_timer_stop(loop(), &raft.timer);
	raft_sm_wait_leader_dead();
}

/** Wakeup Raft state writer fiber waiting for WAL write end. */
static void
raft_write_cb(struct journal_entry *entry)
{
	fiber_wakeup(entry->complete_data);
}

/** Synchronously write a Raft request into WAL. */
static void
raft_write_request(const struct raft_request *req)
{
	assert(raft.is_write_in_progress);
	/*
	 * Vclock is never persisted by Raft. It is used only to
	 * be sent to network when vote for self.
	 */
	assert(req->vclock == NULL);
	/*
	 * State is not persisted. That would be strictly against Raft protocol.
	 * The reason is that it does not make much sense - even if the node is
	 * a leader now, after the node is restarted, there will be another
	 * leader elected by that time likely.
	 */
	assert(req->state == 0);
	struct region *region = &fiber()->gc;
	uint32_t svp = region_used(region);
	struct xrow_header row;
	char buf[sizeof(struct journal_entry) +
		 sizeof(struct xrow_header *)];
	struct journal_entry *entry = (struct journal_entry *)buf;
	entry->rows[0] = &row;

	if (xrow_encode_raft(&row, region, req) != 0)
		goto fail;
	journal_entry_create(entry, 1, xrow_approx_len(&row), raft_write_cb,
			     fiber());

	if (journal_write(entry) != 0 || entry->res < 0) {
		diag_set(ClientError, ER_WAL_IO);
		diag_log();
		goto fail;
	}

	region_truncate(region, svp);
	return;
fail:
	/*
	 * XXX: the stub is supposed to be removed once it is defined what to do
	 * when a raft request WAL write fails.
	 */
	panic("Could not write a raft request to WAL\n");
}

/* Dump Raft state to WAL in a blocking way. */
static void
raft_worker_handle_io(void)
{
	assert(raft.is_write_in_progress);
	/* During write Raft can't be anything but a follower. */
	assert(raft.state == RAFT_STATE_FOLLOWER);
	struct raft_request req;

	if (raft_is_fully_on_disk()) {
end_dump:
		raft.is_write_in_progress = false;
		/*
		 * The state machine is stable. Can see now, to what state to
		 * go.
		 */
		if (!raft.is_candidate) {
			/*
			 * If not a candidate, can't do anything except vote for
			 * somebody (if Raft is enabled). Nothing to do except
			 * staying a follower without timeouts.
			 */
		} else if (raft.leader != 0) {
			/* There is a known leader. Wait until it is dead. */
			raft_sm_wait_leader_dead();
		} else if (raft.vote == instance_id) {
			/* Just wrote own vote. */
			if (raft_election_quorum() == 1)
				raft_sm_become_leader();
			else
				raft_sm_become_candidate();
		} else if (raft.vote != 0) {
			/*
			 * Voted for some other node. Wait if it manages to
			 * become a leader.
			 */
			raft_sm_wait_election_end();
		} else {
			/* No leaders, no votes. */
			raft_sm_schedule_new_vote(instance_id);
		}
	} else {
		memset(&req, 0, sizeof(req));
		assert(raft.volatile_term >= raft.term);
		req.term = raft.volatile_term;
		req.vote = raft.volatile_vote;

		raft_write_request(&req);
		say_info("RAFT: persisted state %s",
			 raft_request_to_string(&req));

		assert(req.term >= raft.term);
		raft.term = req.term;
		raft.vote = req.vote;
		/*
		 * Persistent state is visible, and it was changed - broadcast.
		 */
		raft_schedule_broadcast();
		if (raft_is_fully_on_disk())
			goto end_dump;
	}
}

/* Broadcast Raft complete state to the followers. */
static void
raft_worker_handle_broadcast(void)
{
	assert(raft.is_broadcast_scheduled);
	struct raft_request req;
	memset(&req, 0, sizeof(req));
	req.term = raft.term;
	req.vote = raft.vote;
	req.state = raft.state;
	if (req.state == RAFT_STATE_CANDIDATE) {
		assert(raft.vote == instance_id);
		req.vclock = &replicaset.vclock;
	}
	replicaset_foreach(replica)
		relay_push_raft(replica->relay, &req);
	trigger_run(&raft.on_update, NULL);
	raft.is_broadcast_scheduled = false;
}

static int
raft_worker_f(va_list args)
{
	(void)args;
	bool is_idle;
	while (!fiber_is_cancelled()) {
		is_idle = true;
		if (raft.is_write_in_progress) {
			raft_worker_handle_io();
			is_idle = false;
		}
		if (raft.is_broadcast_scheduled) {
			raft_worker_handle_broadcast();
			is_idle = false;
		}
		fiber_sleep(0);
		if (!is_idle)
			continue;
		assert(raft_is_fully_on_disk());
		fiber_yield();
	}
	return 0;
}

static void
raft_sm_pause_and_dump(void)
{
	assert(raft.state == RAFT_STATE_FOLLOWER);
	if (raft.is_write_in_progress)
		return;
	ev_timer_stop(loop(), &raft.timer);
	raft_worker_wakeup();
	raft.is_write_in_progress = true;
}

static void
raft_sm_become_leader(void)
{
	assert(raft.state != RAFT_STATE_LEADER);
	say_info("RAFT: enter leader state with quorum %d",
		 raft_election_quorum());
	assert(raft.leader == 0);
	assert(raft.is_candidate);
	assert(!raft.is_write_in_progress);
	raft.state = RAFT_STATE_LEADER;
	raft.leader = instance_id;
	ev_timer_stop(loop(), &raft.timer);
	/* Make read-write (if other subsystems allow that. */
	box_update_ro_summary();
	/* State is visible and it is changed - broadcast. */
	raft_schedule_broadcast();
}

static void
raft_sm_follow_leader(uint32_t leader)
{
	say_info("RAFT: leader is %u, follow", leader);
	assert(raft.state != RAFT_STATE_LEADER);
	assert(raft.leader == 0);
	raft.state = RAFT_STATE_FOLLOWER;
	raft.leader = leader;
	if (!raft.is_write_in_progress && raft.is_candidate) {
		ev_timer_stop(loop(), &raft.timer);
		raft_sm_wait_leader_dead();
	}
	/* State is visible and it is changed - broadcast. */
	raft_schedule_broadcast();
}

static void
raft_sm_become_candidate(void)
{
	say_info("RAFT: enter candidate state with 1 self vote");
	assert(raft.state == RAFT_STATE_FOLLOWER);
	assert(raft.leader == 0);
	assert(raft.vote == instance_id);
	assert(raft.is_candidate);
	assert(!raft.is_write_in_progress);
	assert(raft_election_quorum() > 1);
	raft.state = RAFT_STATE_CANDIDATE;
	raft.vote_count = 1;
	raft.vote_mask = 0;
	bit_set(&raft.vote_mask, instance_id);
	raft_sm_wait_election_end();
	/* State is visible and it is changed - broadcast. */
	raft_schedule_broadcast();
}

static void
raft_sm_schedule_new_term(uint64_t new_term)
{
	say_info("RAFT: bump term to %llu, follow", new_term);
	assert(new_term > raft.volatile_term);
	assert(raft.volatile_term >= raft.term);
	raft.volatile_term = new_term;
	/* New terms means completely new Raft state. */
	raft.volatile_vote = 0;
	raft.leader = 0;
	raft.state = RAFT_STATE_FOLLOWER;
	box_update_ro_summary();
	raft_sm_pause_and_dump();
	/*
	 * State is visible and it is changed - broadcast. Term is also visible,
	 * but only persistent term. Volatile term is not broadcasted until
	 * saved to disk.
	 */
	raft_schedule_broadcast();
}

static void
raft_sm_schedule_new_vote(uint32_t new_vote)
{
	say_info("RAFT: vote for %u, follow", new_vote, raft.volatile_term);
	assert(raft.volatile_vote == 0);
	assert(raft.leader == 0);
	assert(raft.state == RAFT_STATE_FOLLOWER);
	raft.volatile_vote = new_vote;
	raft_sm_pause_and_dump();
	/* Nothing visible is changed - no broadcast. */
}

static void
raft_sm_schedule_new_election(void)
{
	say_info("RAFT: begin new election round");
	assert(raft_is_fully_on_disk());
	assert(raft.is_candidate);
	/* Everyone is a follower until its vote for self is persisted. */
	raft_sm_schedule_new_term(raft.term + 1);
	raft_sm_schedule_new_vote(instance_id);
	box_update_ro_summary();
}

static void
raft_sm_schedule_new_election_cb(struct ev_loop *loop, struct ev_timer *timer,
				 int events)
{
	assert(timer == &raft.timer);
	(void)events;
	ev_timer_stop(loop, timer);
	raft_sm_schedule_new_election();
}

static void
raft_sm_wait_leader_dead(void)
{
	assert(!ev_is_active(&raft.timer));
	assert(!raft.is_write_in_progress);
	assert(raft.is_candidate);
	assert(raft.state == RAFT_STATE_FOLLOWER);
	assert(raft.leader != 0);
	double death_timeout = replication_disconnect_timeout();
	ev_timer_set(&raft.timer, death_timeout, death_timeout);
	ev_timer_start(loop(), &raft.timer);
}

static void
raft_sm_wait_leader_found(void)
{
	assert(!ev_is_active(&raft.timer));
	assert(!raft.is_write_in_progress);
	assert(raft.is_candidate);
	assert(raft.state == RAFT_STATE_FOLLOWER);
	assert(raft.leader == 0);
	double death_timeout = replication_disconnect_timeout();
	ev_timer_set(&raft.timer, death_timeout, death_timeout);
	ev_timer_start(loop(), &raft.timer);
}

static void
raft_sm_wait_election_end(void)
{
	assert(!ev_is_active(&raft.timer));
	assert(!raft.is_write_in_progress);
	assert(raft.is_candidate);
	assert(raft.state == RAFT_STATE_FOLLOWER ||
	       (raft.state == RAFT_STATE_CANDIDATE &&
		raft.volatile_vote == instance_id));
	assert(raft.leader == 0);
	double election_timeout = raft.election_timeout +
				  raft_new_random_election_shift();
	ev_timer_set(&raft.timer, election_timeout, election_timeout);
	ev_timer_start(loop(), &raft.timer);
}

static void
raft_sm_start(void)
{
	say_info("RAFT: start state machine");
	assert(!ev_is_active(&raft.timer));
	assert(!raft.is_write_in_progress);
	assert(!raft.is_enabled);
	assert(raft.state == RAFT_STATE_FOLLOWER);
	raft.is_enabled = true;
	raft.is_candidate = raft.is_cfg_candidate;
	if (!raft.is_candidate) {
		/* Nop. */;
	} else if (raft.leader != 0) {
		raft_sm_wait_leader_dead();
	} else {
		/*
		 * Don't start new election. The situation is most likely
		 * happened because this node was restarted. Instance restarts
		 * may happen in the cluster, and each restart shouldn't
		 * disturb the current leader. Give it time to notify this node
		 * that there is a leader.
		 */
		raft_sm_wait_leader_found();
	}
	box_update_ro_summary();
}

static void
raft_sm_stop(void)
{
	say_info("RAFT: stop state machine");
	assert(raft.is_enabled);
	raft.is_enabled = false;
	raft.is_candidate = false;
	if (raft.state == RAFT_STATE_LEADER)
		raft.leader = 0;
	raft.state = RAFT_STATE_FOLLOWER;
	ev_timer_stop(loop(), &raft.timer);
	box_update_ro_summary();
	/* State is visible and changed - broadcast. */
	raft_schedule_broadcast();
}

void
raft_serialize_for_network(struct raft_request *req, struct vclock *vclock)
{
	memset(req, 0, sizeof(*req));
	/*
	 * Volatile state is never used for any communications.
	 * Use only persisted state.
	 */
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
raft_on_update(struct trigger *trigger)
{
	trigger_add(&raft.on_update, trigger);
}

void
raft_cfg_is_enabled(bool is_enabled)
{
	if (is_enabled == raft.is_enabled)
		return;

	if (!is_enabled)
		raft_sm_stop();
	else
		raft_sm_start();
}

void
raft_cfg_is_candidate(bool is_candidate)
{
	bool old_is_candidate = raft.is_candidate;
	raft.is_cfg_candidate = is_candidate;
	raft.is_candidate = is_candidate && raft.is_enabled;
	if (raft.is_candidate == old_is_candidate)
		return;

	if (raft.is_candidate) {
		assert(raft.state == RAFT_STATE_FOLLOWER);
		if (raft.leader != 0) {
			raft_sm_wait_leader_dead();
		} else if (raft_is_fully_on_disk()) {
			raft_sm_wait_leader_found();
		} else {
			/*
			 * If there is an on-going WAL write, it means there was
			 * some node who sent newer data to this node. So it is
			 * probably a better candidate. Anyway can't do anything
			 * until the new state is fully persisted.
			 */
		}
	} else {
		if (raft.state != RAFT_STATE_LEADER) {
			/* Do not wait for anything while being a voter. */
			ev_timer_stop(loop(), &raft.timer);
		}
		if (raft.state != RAFT_STATE_FOLLOWER) {
			if (raft.state == RAFT_STATE_LEADER)
				raft.leader = 0;
			raft.state = RAFT_STATE_FOLLOWER;
			/* State is visible and changed - broadcast. */
			raft_schedule_broadcast();
		}
	}
	box_update_ro_summary();
}

void
raft_cfg_election_timeout(double timeout)
{
	if (timeout == raft.election_timeout)
		return;

	raft.election_timeout = timeout;
	if (raft.vote != 0 && raft.leader == 0 && raft.is_candidate) {
		assert(ev_is_active(&raft.timer));
		double timeout = ev_timer_remaining(loop(), &raft.timer) -
				 raft.timer.at + raft.election_timeout;
		ev_timer_stop(loop(), &raft.timer);
		ev_timer_set(&raft.timer, timeout, timeout);
		ev_timer_start(loop(), &raft.timer);
	}
}

void
raft_cfg_election_quorum(void)
{
	if (raft.state != RAFT_STATE_CANDIDATE ||
	    raft.state == RAFT_STATE_LEADER)
		return;
	if (raft.vote_count < raft_election_quorum())
		return;
	raft_sm_become_leader();
}

void
raft_cfg_death_timeout(void)
{
	if (raft.state == RAFT_STATE_FOLLOWER && raft.is_candidate &&
	    raft.leader != 0) {
		assert(ev_is_active(&raft.timer));
		double death_timeout = replication_disconnect_timeout();
		double timeout = ev_timer_remaining(loop(), &raft.timer) -
				 raft.timer.at + death_timeout;
		ev_timer_stop(loop(), &raft.timer);
		ev_timer_set(&raft.timer, timeout, timeout);
		ev_timer_start(loop(), &raft.timer);
	}
}

void
raft_new_term(void)
{
	if (raft.is_enabled)
		raft_sm_schedule_new_term(raft.volatile_term + 1);
}

static void
raft_worker_wakeup(void)
{
	if (raft.worker == NULL) {
		raft.worker = fiber_new("raft_worker", raft_worker_f);
		if (raft.worker == NULL) {
			/*
			 * XXX: should be handled properly, no need to panic.
			 * The issue though is that most of the Raft state
			 * machine functions are not supposed to fail, and also
			 * they usually wakeup the fiber when their work is
			 * finished. So it is too late to fail. On the other
			 * hand it looks not so good to create the fiber when
			 * Raft is initialized. Because then it will occupy
			 * memory even if Raft is not used.
			 */
			diag_log();
			panic("Could't create Raft worker fiber");
			return;
		}
		fiber_set_joinable(raft.worker, true);
	}
	/*
	 * Don't wake the fiber if it writes something. Otherwise it would be a
	 * spurious wakeup breaking the WAL write not adapted to this. Also
	 * don't wakeup the current fiber - it leads to undefined behaviour.
	 */
	if (!raft.is_write_in_progress && fiber() != raft.worker)
		fiber_wakeup(raft.worker);
}

static void
raft_schedule_broadcast(void)
{
	raft.is_broadcast_scheduled = true;
	raft_worker_wakeup();
}

void
raft_init(void)
{
	ev_timer_init(&raft.timer, raft_sm_schedule_new_election_cb, 0, 0);
	rlist_create(&raft.on_update);
}
