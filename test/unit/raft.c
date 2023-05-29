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
#include "raft_test_utils.h"

/**
 * Test result is a real returned value of main_f. Fiber_join can not be used,
 * because it expects if a returned value < 0 then diag is not empty. But in
 * unit tests it can be violated - check_plan() does not set diag.
 */
static int test_result;

static void
raft_test_leader_election(void)
{
	raft_start_test(24);
	struct raft_node node;
	raft_node_create(&node);

	is(node.net.count, 1, "1 pending message at start");
	ok(node.update_count > 0, "trigger worked");
	node.update_count = 0;
	ok(raft_node_net_check_msg(&node,
		0 /* Index. */,
		RAFT_STATE_FOLLOWER /* State. */,
		1 /* Term. */,
		0 /* Vote. */,
		NULL /* Vclock. */
	), "broadcast at start");
	raft_node_net_drop(&node);

	double death_timeout = node.cfg_death_timeout;
	raft_run_next_event();
	ok(raft_time() >= death_timeout, "next event is leader death");

	/* Elections are started with a new term, which is persisted. */
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "elections with a new term");
	is(raft_vote_count(&node.raft), 1, "single vote for self");
	ok(node.update_count > 0, "trigger worked");
	node.update_count = 0;

	/* Check if all async work is done properly. */

	is(node.journal.size, 1, "1 record in the journal");
	ok(raft_node_journal_check_row(
		&node,
		0 /* Index. */,
		2 /* Term. */,
		1 /* Vote. */
	), "term and vote are on disk");

	is(node.net.count, 1, "1 pending message");
	ok(raft_node_net_check_msg(&node,
		0 /* Index. */,
		RAFT_STATE_CANDIDATE /* State. */,
		2 /* Term. */,
		1 /* Vote. */,
		"{0: 1}" /* Vclock. */
	), "term bump and vote are sent");
	raft_node_net_drop(&node);

	/* Simulate first response. Nothing should happen, quorum is 3. */

	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "vote response from 2");
	is(raft_vote_count(&node.raft), 2, "2 votes - 1 self and 1 foreign");
	ok(!node.has_work, "no work to do - not enough votes yet");

	raft_run_for(node.cfg_election_timeout / 2);
	is(node.raft.state, RAFT_STATE_CANDIDATE, "still candidate, waiting "
	   "for elections");
	is(node.update_count, 0, "trigger is the same");

	/* Simulate second response. Quorum is reached. */

	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		1 /* Vote. */,
		3 /* Source. */
	), 0, "vote response from 3");
	is(raft_vote_count(&node.raft), 3, "2 votes - 1 self and 2 foreign");
	is(node.raft.state, RAFT_STATE_LEADER, "became leader");
	ok(node.update_count > 0, "trigger worked");
	node.update_count = 0;

	/* New leader should do a broadcast when elected. */

	ok(!node.has_work, "no work - broadcast should be done");
	is(node.journal.size, 1, "no new rows in the journal - state change "
	   "is not persisted");
	is(node.net.count, 1, "1 pending message");
	ok(raft_node_net_check_msg(&node,
		0 /* Index. */,
		RAFT_STATE_LEADER /* State. */,
		2 /* Term. */,
		1 /* Vote. */,
		NULL /* Vclock. */
	), "sent new-leader notification");
	raft_node_net_drop(&node);

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_recovery(void)
{
	raft_start_test(13);
	struct raft_msg msg;
	struct raft_node node;
	raft_node_create(&node);

	raft_run_next_event();
	is(node.raft.state, RAFT_STATE_CANDIDATE, "became candidate");

	/* Candidate's checkpoint. */

	raft_checkpoint_remote(&node.raft, &msg);
	ok(raft_msg_check(&msg,
		RAFT_STATE_CANDIDATE /* State. */,
		2 /* Term. */,
		1 /* Vote. */,
		"{0: 1}" /* Vclock. */
	), "remote checkpoint of a candidate");

	raft_checkpoint_local(&node.raft, &msg);
	/* State and vclock are not persisted in a local checkpoint. */
	ok(raft_msg_check(&msg,
		0 /* State. */,
		2 /* Term. */,
		1 /* Vote. */,
		NULL /* Vclock. */
	), "local checkpoint of a candidate");

	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "vote response from 2");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		1 /* Vote. */,
		3 /* Source. */
	), 0, "vote response from 3");
	is(node.raft.state, RAFT_STATE_LEADER, "became leader");

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_LEADER /* State. */,
		1 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "election is finished");

	/* Leader's checkpoint. */

	raft_checkpoint_remote(&node.raft, &msg);
	/* Leader does not send vclock. */
	ok(raft_msg_check(&msg,
		RAFT_STATE_LEADER /* State. */,
		2 /* Term. */,
		1 /* Vote. */,
		NULL /* Vclock. */
	), "remote checkpoint of a leader");

	raft_checkpoint_local(&node.raft, &msg);
	/* State and vclock are not persisted in a local checkpoint. */
	ok(raft_msg_check(&msg,
		0 /* State. */,
		2 /* Term. */,
		1 /* Vote. */,
		NULL /* Vclock. */
	), "local checkpoint of a leader");

	/* Restart leads to state loss. Look at follower's checkpoint. */

	raft_node_restart(&node);
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "restart always as a follower");

	is(raft_vote_count(&node.raft), 1, "vote count is restored correctly");

	raft_checkpoint_remote(&node.raft, &msg);
	ok(raft_msg_check(&msg,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Term. */,
		1 /* Vote. */,
		NULL /* Vclock. */
	), "remote checkpoint of a leader");

	raft_checkpoint_local(&node.raft, &msg);
	ok(raft_msg_check(&msg,
		0 /* State. */,
		2 /* Term. */,
		1 /* Vote. */,
		NULL /* Vclock. */
	), "local checkpoint of a leader");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_bad_msg(void)
{
	raft_start_test(11);
	struct raft_msg msg;
	struct raft_node node;
	struct vclock vclock;
	raft_node_create(&node);

	msg = (struct raft_msg){
		.state = 0,
		.term = 10,
	};
	is(raft_node_process_msg(&node, &msg, 2), -1, "state can't be 0");
	is(node.raft.term, 1, "term from the bad message wasn't used");

	raft_vclock_from_string(&vclock, "{2: 1}");
	msg = (struct raft_msg){
		.state = RAFT_STATE_CANDIDATE,
		.term = 10,
		.vote = 3,
		.vclock = &vclock,
	};
	is(raft_node_process_msg(&node, &msg, 2), -1, "node can't be a "
	   "candidate but vote for another node");
	is(node.raft.term, 1, "term from the bad message wasn't used");

	msg = (struct raft_msg){
		.state = RAFT_STATE_CANDIDATE,
		.term = 10,
		.vote = 2,
	};
	is(raft_node_process_msg(&node, &msg, 2), -1, "node can't be a "
	   "candidate without vclock");
	is(node.raft.term, 1, "term from the bad message wasn't used");

	msg = (struct raft_msg){
		.state = RAFT_STATE_FOLLOWER,
		.term = 0,
	};
	is(raft_node_process_msg(&node, &msg, 2), -1, "term can't be 0");

	msg = (struct raft_msg){
		.state = 10000,
		.term = 10,
		.vote = 2,
	};
	is(raft_node_process_msg(&node, &msg, 2), -1, "bad state");
	is(node.raft.term, 1, "term from the bad message wasn't used");

	msg = (struct raft_msg){
		.state = -1,
		.term = 10,
		.vote = 2,
	};
	is(raft_node_process_msg(&node, &msg, 2), -1, "bad negative state");
	is(node.raft.term, 1, "term from the bad message wasn't used");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_vote(void)
{
	raft_start_test(6);
	struct raft_node node;
	raft_node_create(&node);

	/* Vote for other node. */

	is(raft_node_send_vote_request(&node,
		2 /* Term. */,
		"{}" /* Vclock. */,
		2 /* Source. */
	), 0, "vote request from 2");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Volatile term. */,
		2 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "voted for 2");

	is(raft_node_send_vote_request(&node,
		2 /* Term. */,
		"{}" /* Vclock. */,
		3 /* Source. */
	), 0, "vote request from 3");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Volatile term. */,
		2 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "still kept vote for 2");

	/* If the candidate didn't become a leader, start own election. */

	double ts = raft_time();
	raft_run_next_event();
	ok(raft_time() - ts >= node.cfg_election_timeout, "election timeout "
	   "passed");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 3}" /* Vclock. */
	), "became candidate");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_vote_skip(void)
{
	raft_start_test(39);
	struct raft_node node;
	raft_node_create(&node);

	/* Everything is skipped if the term is outdated. */

	raft_run_next_event();
	is(node.raft.state, RAFT_STATE_CANDIDATE, "became candidate");
	is(node.raft.term, 2, "term is bumped");

	is(raft_node_send_vote_response(&node,
		1 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "message is accepted");
	is(raft_vote_count(&node.raft), 1, "but ignored - too old term");

	/* Competing vote requests are skipped. */

	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		3 /* Vote. */,
		2 /* Source. */
	), 0, "message is accepted");
	is(raft_vote_count(&node.raft), 1,
	   "but ignored - vote not for this node");
	is(node.raft.state, RAFT_STATE_CANDIDATE, "this node does not give up");

	/* Vote requests are ignored when node is disabled. */

	raft_node_cfg_is_enabled(&node, false);

	is(raft_node_send_follower(&node,
		3 /* Term. */,
		2 /* Source. */
	), 0, "message is accepted");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		0 /* Vote. */,
		3 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "term bump to be able to vote again");
	is(raft_node_send_vote_request(&node,
		3 /* Term. */,
		"{}" /* Vclock. */,
		2 /* Source. */
	), 0, "message is accepted");
	is(node.raft.vote, 0, "but ignored - node is disabled");

	/* Disabled node still takes term from the vote request. */

	is(raft_node_send_vote_request(&node,
		4 /* Term. */,
		"{}" /* Vclock. */,
		2 /* Source. */
	), 0, "message is accepted");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		4 /* Term. */,
		0 /* Vote. */,
		4 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 3}" /* Vclock. */
	), "term is bumped, but vote request is ignored");

	raft_node_cfg_is_enabled(&node, true);

	/* Not a candidate won't accept vote request for self. */

	is(raft_node_send_vote_response(&node,
		4 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "message is accepted");
	is(node.raft.vote, 0, "but ignored - vote works only on a candidate");

	/* Ignore vote response for some third node. */

	is(raft_node_send_vote_response(&node,
		4 /* Term. */,
		3 /* Vote. */,
		2 /* Source. */
	), 0, "message is accepted");
	is(node.raft.vote, 0, "but ignored - sender != vote, so it is not a "
	   "request");

	/* Ignore if leader is already known. */

	is(raft_node_send_leader(&node,
		4 /* Term. */,
		2 /* Source. */
	), 0, "message is accepted");
	is(node.raft.leader, 2, "leader is accepted");

	is(raft_node_send_vote_request(&node,
		4 /* Term. */,
		"{}" /* Vclock. */,
		3 /* Source. */
	), 0, "message is accepted");
	is(node.raft.vote, 0, "but ignored - leader is already known");
	is(node.raft.leader, 2, "leader is not changed");

	/* Ignore too small vclock. */

	/*
	 * Need to turn off the candidate role to bump the term and not become
	 * a candidate.
	 */
	raft_node_cfg_is_candidate(&node, false);

	raft_node_journal_follow(&node, 1, 5);
	raft_node_journal_follow(&node, 2, 5);
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		4 /* Term. */,
		0 /* Vote. */,
		4 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 3, 1: 5, 2: 5}" /* Vclock. */
	), "vclock is bumped");

	is(raft_node_send_vote_request(&node,
		5 /* Term. */,
		"{1: 4}" /* Vclock. */,
		3 /* Source. */
	), 0, "message is accepted");
	is(node.raft.vote, 0, "but ignored - vclock is too small");
	is(node.raft.term, 5, "new term");
	is(node.raft.leader, 0, "leader is dropped in the new term");

	/* Ignore incomparable vclock. */

	is(raft_node_send_vote_request(&node,
		5 /* Term. */,
		"{1: 4, 2: 6}" /* Vclock. */,
		3 /* Source. */
	), 0, "message is accepted");
	is(node.raft.vote, 0, "but ignored - vclock is incomparable");

	/* Ignore if voted in the current term. */

	is(raft_node_send_vote_request(&node,
		6 /* Term. */,
		"{1: 5, 2: 5}" /* Vclock. */,
		2 /* Source. */
	), 0, "message is accepted");
	is(node.raft.vote, 2, "voted");

	is(raft_node_send_vote_request(&node,
		6 /* Term. */,
		"{1: 5, 2: 5}" /* Vclock. */,
		3 /* Source. */
	), 0, "message is accepted");
	is(node.raft.vote, 2, "but ignored - already voted in the term");

	/* After restart it still will ignore requests in the current term. */

	raft_node_restart(&node);
	is(raft_node_send_vote_request(&node,
		6 /* Term. */,
		"{1: 5, 2: 5}" /* Vclock. */,
		3 /* Source. */
	), 0, "message is accepted");
	is(node.raft.vote, 2, "but ignored - already voted in the term");

	raft_node_cfg_is_candidate(&node, true);

	/*
	 * Vote response with a bigger term must be skipped, but it will bump
	 * the term.
	 */

	/* Re-create the node so as not to write the vclock each time. */
	raft_node_destroy(&node);
	raft_node_create(&node);
	/*
	 * Set quorum to 2 to ensure the node does not count the bigger-term
	 * vote and doesn't become a leader.
	 */
	raft_node_cfg_election_quorum(&node, 2);

	raft_run_next_event();
	is(node.raft.state, RAFT_STATE_CANDIDATE, "became candidate");
	is(node.raft.term, 2, "term is bumped");

	is(raft_node_send_vote_response(&node,
		3 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "message is accepted");

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 3}" /* Vclock. */
	), "term is bumped and became candidate");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_vote_during_wal_write(void)
{
	raft_start_test(11);
	struct raft_node node;

	/*
	 * Vote request from another node causes WAL flush before the current
	 * node can make a vote decision.
	 */

	raft_node_create(&node);
	/*
	 * Server1 wins elections in the current term.
	 */
	raft_run_next_event();
	is(node.raft.state, RAFT_STATE_CANDIDATE, "became candidate");
	is(raft_node_send_vote_response(
		&node,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "vote response from 2");
	is(raft_node_send_vote_response(
		&node,
		2 /* Term. */,
		1 /* Vote. */,
		3 /* Source. */
	), 0, "vote response from 3");
	raft_node_journal_follow(&node, 1, 3);
	raft_node_journal_follow(&node, 2, 5);
	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_LEADER /* State. */,
		1 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1, 1: 3, 2: 5}" /* Vclock. */
	), "became leader");
	/*
	 * Server1 WAL is blocked and it gets a vote request with a matching
	 * vclock.
	 */
	raft_node_block(&node);
	is(raft_node_send_vote_request(
		&node,
		3 /* Term. */,
		"{1: 3, 2: 5}" /* Vclock. */,
		2 /* Source. */
	), 0, "vote request in a new term but WAL is blocked");
	/*
	 * A WAL write ends, which was started before the vote request arrived.
	 */
	raft_node_journal_follow(&node, 1, 1);
	raft_node_unblock(&node);
	/*
	 * Server1 rejects the vote request then, because its own vclock became
	 * bigger after the WAL sync. Instead, it voted for self.
	 */
	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 3, 1: 4, 2: 5}" /* Vclock. */
	), "canceled the vote for other node and voted for self");

	raft_node_destroy(&node);
	raft_node_create(&node);

	/*
	 * Vote request for self works always even if there were some pending
	 * rows in the WAL queue when the vote was issued.
	 */

	raft_run_next_event();
	is(node.raft.state, RAFT_STATE_CANDIDATE, "became candidate");
	is(node.raft.term, 2, "term is 2");
	raft_node_block(&node);
	/* Start new term on election timeout, but can't persist anything. */
	raft_run_next_event();
	is(node.raft.term, 2, "term is 2");
	is(node.raft.volatile_term, 3, "volatile term is 3");
	/* WAL queue is flushed and there was some data before the vote. */
	raft_node_journal_follow(&node, 1, 10);
	raft_node_unblock(&node);
	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2, 1: 10}" /* Vclock. */
	), "vote for self worked even though the WAL had non-empty queue");

	raft_node_destroy(&node);

	raft_finish_test();
}

static void
raft_test_leader_resign(void)
{
	raft_start_test(24);
	struct raft_node node;

	/*
	 * When a node resignes from leader role voluntarily, the other nodes
	 * will start next election.
	 */

	raft_node_create(&node);

	is(raft_node_send_leader(&node, 1, 2), 0, "message is accepted");
	is(node.raft.leader, 2, "leader is elected");

	is(raft_node_send_follower(&node, 1, 2), 0, "message is accepted");
	is(node.raft.leader, 0, "leader has resigned");

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "became candidate");

	raft_node_destroy(&node);

	/* Resign does not do anything if the node is not a candidate. */

	raft_node_create(&node);

	is(raft_node_send_leader(&node, 1, 2), 0, "message is accepted");
	is(node.raft.leader, 2, "leader is elected");

	raft_node_cfg_is_candidate(&node, false);
	/* Multiple candidate reset won't break anything. */
	raft_node_cfg_is_candidate(&node, false);

	int update_count = node.update_count;
	is(raft_node_send_follower(&node, 1, 2), 0, "message is accepted");
	is(node.raft.leader, 0, "leader has resigned");
	is(node.update_count, update_count + 1, "resign makes a broadcast");

	raft_run_for(node.cfg_death_timeout * 2);

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		1 /* Term. */,
		0 /* Vote. */,
		1 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{}" /* Vclock. */
	), "still follower");

	raft_node_destroy(&node);

	/* Resign by refusing to be a candidate. */

	raft_node_create(&node);

	raft_run_next_event();
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "vote from 2");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		1 /* Vote. */,
		3 /* Source. */
	), 0, "vote from 3");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_LEADER /* State. */,
		1 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "became leader");

	raft_node_net_drop(&node);
	raft_node_cfg_is_candidate(&node, false);
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "the leader has resigned");
	ok(raft_node_net_check_msg(&node,
		0 /* Index. */,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Term. */,
		1 /* Vote. */,
		NULL /* Vclock. */
	), "resign notification is sent");

	/*
	 * gh-6129: resign of a remote leader during a local WAL write should
	 * schedule a new election after the WAL write.
	 *
	 * Firstly start a new term.
	 */
	raft_node_block(&node);
	raft_node_cfg_is_candidate(&node, true);
	raft_run_next_event();
	/* Volatile term is new, but the persistent one is not updated yet. */
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "new election is waiting for WAL write");

	/* Now another node wins the election earlier. */
	is(raft_node_send_leader(&node,
		3 /* Term. */,
		2 /* Source. */
	), 0, "message is accepted");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "the leader is accepted");

	/*
	 * The leader resigns and triggers a new election round on the first
	 * node. A new election is triggered, but still waiting for the previous
	 * WAL write to end.
	 */
	is(raft_node_send_follower(&node,
		3 /* Term. */,
		2 /* Source. */
	), 0, "message is accepted");
	/* Note how the volatile term is updated again. */
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		4 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "the leader has resigned, new election is scheduled");
	raft_node_unblock(&node);

	/* Ensure the node still collects votes after the WAL write. */
	is(raft_node_send_vote_response(&node,
		4 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "vote from 2");
	is(raft_node_send_vote_response(&node,
		4 /* Term. */,
		1 /* Vote. */,
		3 /* Source. */
	), 0, "vote from 3");
	raft_run_next_event();
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_LEADER /* State. */,
		1 /* Leader. */,
		4 /* Term. */,
		1 /* Vote. */,
		4 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "the leader is elected");

	raft_node_destroy(&node);

	raft_finish_test();
}

static void
raft_test_split_brain(void)
{
	raft_start_test(4);
	struct raft_node node;
	raft_node_create(&node);

	/*
	 * Split brain is ignored, as there is nothing to do with it
	 * automatically.
	 */

	is(raft_node_send_leader(&node,
		2 /* Term. */,
		2 /* Source. */
	), 0, "first leader notification");
	is(node.raft.leader, 2, "leader is found");

	is(raft_node_send_leader(&node,
		2 /* Term. */,
		3 /* Source. */
	), 0, "second leader notification");
	is(node.raft.leader, 2, "split brain, the old leader is kept");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_heartbeat(void)
{
	raft_start_test(12);
	struct raft_node node;
	raft_node_create(&node);

	/* Let the node know there is a leader somewhere. */

	is(raft_node_send_leader(&node,
		2 /* Term. */,
		2 /* Source. */
	), 0, "leader notification");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "follow the leader after notification");

	/* Leader can send the same message many times. */

	is(raft_node_send_leader(&node,
		2 /* Term. */,
		2 /* Source. */
	), 0, "leader notification");

	/* The node won't do anything if it is not a candidate. */

	raft_node_cfg_is_candidate(&node, false);
	raft_run_for(node.cfg_death_timeout * 2);
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "follow the leader because no candidate");
	raft_node_cfg_is_candidate(&node, true);

	/* Heartbeats from the leader are accepted. */

	for (int i = 0; i < 5; ++i) {
		raft_run_for(node.cfg_death_timeout / 2);
		raft_node_send_heartbeat(&node, 2);
	}
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "follow the leader because had heartbeats");

	/* Heartbeats not from the leader won't do anything. */

	double start = raft_time();
	raft_run_for(node.cfg_death_timeout / 3);
	raft_node_send_heartbeat(&node, 3);
	raft_run_for(node.cfg_death_timeout / 3);
	raft_node_send_heartbeat(&node, 0);
	raft_run_next_event();
	double deadline = start + node.cfg_death_timeout;
	/*
	 * Compare == with 0.1 precision. Because '/ 3' operations above will
	 * make the doubles contain some small garbage.
	 */
	ok(raft_time() + 0.1 >= deadline && raft_time() - 0.1 <= deadline,
	   "death timeout passed");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "enter candidate state when no heartbeats from the leader");

	/* Non-candidate ignores heartbeats. */

	raft_node_cfg_is_candidate(&node, false);
	raft_node_send_heartbeat(&node, 2);
	raft_node_cfg_is_candidate(&node, true);

	/* Leader ignores all heartbeats - nothing to wait for. */

	raft_node_new_term(&node);
	is(raft_node_send_vote_response(&node,
		4 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "vote from 2");
	is(raft_node_send_vote_response(&node,
		4 /* Term. */,
		1 /* Vote. */,
		3 /* Source. */
	), 0, "vote from 3");
	is(node.raft.state, RAFT_STATE_LEADER, "became leader");
	/* From self. */
	raft_node_send_heartbeat(&node, 1);
	/* From somebody else. */
	raft_node_send_heartbeat(&node, 2);

	/* Heartbeats are ignored during WAL write. */

	raft_node_block(&node);
	is(raft_node_send_leader(&node,
		5 /* Term. */,
		2 /* Source. */
	), 0, "message from leader");
	raft_node_send_heartbeat(&node, 2);
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		4 /* Term. */,
		1 /* Vote. */,
		5 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 4}" /* Vclock. */
	), "nothing changed - waiting for WAL write");
	raft_node_unblock(&node);

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_election_timeout(void)
{
	raft_start_test(13);
	struct raft_node node;
	raft_node_create(&node);

	/* Configuration works when done before election. */

	double election_timeout = node.cfg_election_timeout;
	double death_timeout = node.cfg_death_timeout;
	double ts = raft_time();
	raft_run_next_event();
	ok(raft_time() == ts + death_timeout, "election is started");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "enter candidate state");

	ts = raft_time();
	raft_run_next_event();
	ok(raft_time() >= ts + election_timeout, "new election is started");
	ok(raft_time() <= ts + election_timeout * 1.1, "but not too late");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "re-enter candidate state");

	/* Reconfiguration works when done during election. */

	ts = raft_time();
	raft_run_for(election_timeout / 2);
	raft_node_cfg_election_timeout(&node, election_timeout * 2);
	raft_run_for(election_timeout);
	election_timeout = node.cfg_election_timeout;

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "still in the same term - new election timeout didn't expire");

	raft_run_next_event();
	/*
	 * 0.1 precision is used because random double numbers sometimes loose
	 * tiny values.
	 */
	ok(raft_time() + 0.1 >= ts + election_timeout, "new election timeout is "
	   "respected");
	ok(raft_time() - 0.1 <= ts + election_timeout * 1.1, "but not too "
	   "late");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		4 /* Term. */,
		1 /* Vote. */,
		4 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 3}" /* Vclock. */
	), "re-enter candidate state");

	/* Decrease election timeout to earlier than now. */

	raft_run_for(election_timeout / 2);
	raft_node_cfg_election_timeout(&node, election_timeout / 4);
	ts = raft_time();
	raft_run_next_event();

	ok(raft_time() == ts, "the new timeout acts immediately");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		5 /* Term. */,
		1 /* Vote. */,
		5 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 4}" /* Vclock. */
	), "re-enter candidate state");

	/*
	 * Timeout smaller than a millisecond. Election random shift has
	 * millisecond precision. When timeout is smaller, maximal shift is
	 * rounded up to 1 ms.
	 */
	election_timeout = 0.000001;
	raft_node_cfg_election_timeout(&node, election_timeout);
	uint64_t term = node.raft.term;
	do {
		ts = raft_time();
		raft_run_next_event();
		++term;
		/* If random part is 0, the loop would become infinite. */
	} while (raft_time() - ts == election_timeout);
	is(node.raft.term, term, "term is bumped, timeout was truly random");
	is(node.raft.state, RAFT_STATE_CANDIDATE, "still candidate");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_election_quorum(void)
{
	raft_start_test(7);
	struct raft_node node;
	raft_node_create(&node);

	/*
	 * Quorum decrease during election leads to immediate win if vote count
	 * is already sufficient.
	 */

	raft_node_cfg_election_quorum(&node, 5);
	raft_run_next_event();
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "enter candidate state");

	raft_node_cfg_election_quorum(&node, 3);
	is(node.raft.state, RAFT_STATE_CANDIDATE, "still candidate");

	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "send vote response from second node");
	is(raft_vote_count(&node.raft), 2, "vote is accepted");
	is(node.raft.state, RAFT_STATE_CANDIDATE, "but still candidate");

	raft_node_cfg_election_quorum(&node, 2);
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_LEADER /* State. */,
		1 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "enter leader state after another quorum lowering");

	/* Quorum 1 allows to become leader right after WAL write. */

	raft_node_cfg_election_quorum(&node, 1);
	raft_node_new_term(&node);
	ok(raft_node_check_full_state(&node,
	    RAFT_STATE_LEADER /* State. */,
	    1 /* Leader. */,
	    3 /* Term. */,
	    1 /* Vote. */,
	    3 /* Volatile term. */,
	    1 /* Volatile vote. */,
	    "{0: 3}" /* Vclock. */
	), "became leader again immediately with 1 self vote");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_death_timeout(void)
{
	raft_start_test(9);
	struct raft_node node;
	raft_node_create(&node);

	/* Change death timeout during leader death wait. */

	is(raft_node_send_leader(&node,
		2 /* Term. */,
		2 /* Source. */
	), 0, "leader notification");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "follow the leader");

	double timeout = node.cfg_death_timeout;
	raft_run_for(timeout / 2);
	raft_node_cfg_death_timeout(&node, timeout * 2);
	raft_run_for(timeout);
	timeout = node.cfg_death_timeout;

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "the leader still is considered alive");

	raft_run_for(timeout / 2);
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "enter candidate state when the new death timeout expires");

	/* Decrease timeout to earlier than now. */

	is(raft_node_send_leader(&node,
		3 /* Term. */,
		2 /* Source. */
	), 0, "message from leader");
	is(node.raft.leader, 2, "leader is accepted");
	is(node.raft.state, RAFT_STATE_FOLLOWER, "became follower");

	raft_run_for(timeout / 2);
	raft_node_cfg_death_timeout(&node, timeout / 4);
	double ts = raft_time();
	raft_run_next_event();
	ok(raft_time() == ts, "death is detected immediately");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		4 /* Term. */,
		1 /* Vote. */,
		4 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 3}" /* Vclock. */
	), "enter candidate state");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_enable_disable(void)
{
	raft_start_test(11);
	struct raft_node node;
	raft_node_create(&node);

	/* Disabled node can track a leader. */

	raft_node_cfg_is_enabled(&node, false);
	is(raft_node_send_leader(&node,
		2 /* Term. */,
		2 /* Source. */
	), 0, "accepted a leader notification");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "leader is seen");

	/* When re-enabled, the leader death timer is started. */

	raft_node_cfg_is_enabled(&node, true);
	double ts = raft_time();
	raft_run_next_event();
	ok(raft_time() - ts == node.cfg_death_timeout, "death timeout passed");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "became candidate");

	/* Multiple enabling does not break anything. */

	raft_node_cfg_is_enabled(&node, true);
	raft_node_cfg_is_enabled(&node, true);
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "nothing changed");

	/* Leader disable makes it forget he was a leader. */

	is(raft_node_send_vote_response(&node,
		3 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "vote from 2");
	is(raft_node_send_vote_response(&node,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Source. */
	), 0, "vote from 3");
	is(node.raft.state, RAFT_STATE_LEADER, "became leader");

	raft_node_cfg_is_enabled(&node, false);

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "resigned from leader state");

	/* Multiple disabling does not break anything. */

	raft_node_cfg_is_enabled(&node, false);
	raft_node_cfg_is_enabled(&node, false);

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "nothing changed");

	/* Disabled node still bumps the term when needed. */
	raft_node_new_term(&node);

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		4 /* Term. */,
		0 /* Vote. */,
		4 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 3}" /* Vclock. */
	), "term bump when disabled");
	raft_node_destroy(&node);

	raft_finish_test();
}

static void
raft_test_too_long_wal_write(void)
{
	raft_start_test(22);
	struct raft_node node;
	raft_node_create(&node);

	/* During WAL write the node does not wait for leader death. */

	raft_node_block(&node);
	is(raft_node_send_vote_request(&node,
		2 /* Term. */,
		"{2: 1}" /* Vclock. */,
		2 /* Source. */
	), 0, "vote for 2");

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		1 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		2 /* Volatile vote. */,
		"{}" /* Vclock. */
	), "vote is volatile");

	is(raft_node_send_leader(&node,
		2 /* Term. */,
		2 /* Source. */
	), 0, "message from leader");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		1 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		2 /* Volatile vote. */,
		"{}" /* Vclock. */
	), "leader is known");

	raft_run_for(node.cfg_death_timeout * 2);

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		1 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		2 /* Volatile vote. */,
		"{}" /* Vclock. */
	), "nothing changed");

	raft_node_unblock(&node);

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Volatile term. */,
		2 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "wal write is finished");

	double ts = raft_time();
	raft_run_next_event();
	ok(raft_time() - ts == node.cfg_death_timeout, "timer works again");
	is(node.raft.state, RAFT_STATE_CANDIDATE, "became candidate");

	/*
	 * During WAL write it is possible to reconfigure election timeout.
	 * The dangerous case is when the timer is active already. It happens
	 * when the node voted and is a candidate, but leader is unknown.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);

	raft_node_cfg_election_timeout(&node, 100);
	raft_run_next_event();
	is(node.raft.term, 2, "term is bumped");

	/* Bump term again but it is not written to WAL yet. */
	raft_node_block(&node);
	is(raft_node_send_vote_response(&node,
		3 /* Term. */,
		3 /* Vote. */,
		2 /* Source. */
	), 0, "2 votes for 3 in a new term");
	raft_run_next_event();
	is(node.raft.term, 2, "term is old");
	is(node.raft.vote, 1, "vote is used for self");
	is(node.raft.volatile_term, 3, "volatile term is new");
	is(node.raft.volatile_vote, 0, "volatile vote is unused");

	raft_node_cfg_election_timeout(&node, 50);
	raft_node_unblock(&node);
	ts = raft_time();
	raft_run_next_event();
	ts = raft_time() - ts;
	/* 50 + <= 10% random delay. */
	ok(ts >= 50 && ts <= 55, "new election timeout works");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		4 /* Term. */,
		1 /* Vote. */,
		4 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 4}" /* Vclock. */
	), "new term is started with vote for self");

	/*
	 * Similar case when a vote is being written but not finished yet.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);

	raft_node_cfg_election_timeout(&node, 100);
	raft_node_block(&node);
	raft_run_next_event();
	is(node.raft.term, 1, "term is old");
	is(node.raft.vote, 0, "vote is unused");
	is(node.raft.volatile_term, 2, "volatile term is new");
	is(node.raft.volatile_vote, 1, "volatile vote is self");

	raft_node_cfg_election_timeout(&node, 50);
	raft_node_unblock(&node);
	ts = raft_time();
	raft_run_next_event();
	ts = raft_time() - ts;
	/* 50 + <= 10% random delay. */
	ok(ts >= 50 && ts <= 55, "new election timeout works");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "new term is started with vote for self");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_promote_restore(void)
{
	raft_start_test(21);
	struct raft_node node;
	raft_node_create(&node);

	raft_node_cfg_is_candidate(&node, false);
	raft_node_cfg_election_quorum(&node, 1);

	raft_node_promote(&node);
	raft_run_next_event();
	is(node.raft.state, RAFT_STATE_LEADER, "became leader after promotion");

	raft_node_restore(&node);
	is(node.raft.state, RAFT_STATE_FOLLOWER, "restore drops a "
	   "non-candidate leader to a follower");

	/*
	 * Ensure the non-candidate leader is demoted when sees a new term, and
	 * does not try election again.
	 */
	raft_node_promote(&node);
	raft_run_next_event();
	is(node.raft.state, RAFT_STATE_LEADER, "became leader after promotion");
	ok(node.raft.is_candidate, "is a candidate");

	is(raft_node_send_vote_request(&node,
		4 /* Term. */,
		"{}" /* Vclock. */,
		2 /* Source. */
	), 0, "vote request from 2");
	is(node.raft.state, RAFT_STATE_FOLLOWER, "demote once new election "
	   "starts");
	ok(!node.raft.is_candidate, "is not a candidate after term bump");

	raft_run_for(node.cfg_election_timeout * 2);
	is(node.raft.state, RAFT_STATE_FOLLOWER, "still follower");
	is(node.raft.term, 4, "still the same term");

	/* Promote does not do anything on a disabled node. */
	raft_node_cfg_is_candidate(&node, true);
	raft_node_cfg_is_enabled(&node, false);
	raft_node_promote(&node);
	is(node.raft.term, 4, "still old term");
	ok(!node.raft.is_candidate, "not a candidate");

	/* Restore takes into account if Raft is enabled. */
	raft_node_restore(&node);
	ok(!node.raft.is_candidate, "not a candidate");

	/*
	 * The node doesn't schedule new elections in the next round after
	 * promotion.
	 */
	raft_node_cfg_is_candidate(&node, false);
	raft_node_cfg_is_enabled(&node, true);
	raft_node_cfg_election_quorum(&node, 2);
	raft_node_promote(&node);

	is(node.raft.state, RAFT_STATE_CANDIDATE, "became candidate");
	is(node.raft.term, 5, "new term");

	/* Wait for the election timeout. */
	double ts = raft_time();
	raft_run_next_event();
	ok(raft_time() - ts >= node.raft.election_timeout,
	   "election timeout passed");
	is(node.raft.state, RAFT_STATE_FOLLOWER, "resigned from candidate");
	is(node.raft.term, 5, "do not bump term");

	is(raft_node_send_leader(
		&node,
		5 /* Term. */,
		2 /* Source. */
	), 0, "another leader is accepted");

	is(raft_node_send_follower(
		&node,
		5 /* Term. */,
		2 /* Source. */
	), 0, "leader resign is accepted");

	is(node.raft.state, RAFT_STATE_FOLLOWER, "stay follower");
	is(node.raft.term, 5, "do not bump term");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_bump_term_before_cfg()
{
	raft_start_test(6);
	struct raft_node node;
	raft_node_create(&node);
	/*
	 * Term bump is started between recovery and instance ID configuration
	 * but WAL write is not finished yet.
	 */
	raft_run_next_event();
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "new term is started with vote for self");

	raft_node_stop(&node);
	raft_node_recover(&node);
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		NULL /* Vclock. */
	), "recovered");
	is(node.raft.self, 0, "instance id is unknown");

	raft_node_block(&node);
	is(raft_node_send_follower(&node,
		3 /* Term. */,
		2 /* Source. */
	), 0, "bump term externally");
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		0 /* Volatile vote. */,
		NULL /* Vclock. */
	), "term write is in progress");

	raft_node_cfg(&node);
	raft_node_unblock(&node);
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 3}" /* Vclock. */
	), "started new term");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_split_vote(void)
{
	raft_start_test(67);
	struct raft_node node;
	raft_node_create(&node);

	/*
	 * Normal split vote.
	 */
	raft_node_cfg_cluster_size(&node, 4);
	raft_node_cfg_election_quorum(&node, 3);
	raft_run_next_event();

	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "elections with a new term");

	/* Make so node 1 has votes 1 and 2. Node 3 has votes 3 and 4. */
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Source. */
	), 0, "vote response for 1 from 2");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		3 /* Vote. */,
		3 /* Source. */
	), 0, "vote response for 3 from 3");

	ok(node.raft.timer.repeat >= node.raft.election_timeout, "term "
	   "timeout >= election timeout normally");

	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		3 /* Vote. */,
		4 /* Source. */
	), 0, "vote response for 3 from 4");

	ok(node.raft.timer.repeat < node.raft.election_timeout, "split vote "
	   "reduced the term timeout");

	raft_run_next_event();
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "a new term");

	ok(node.raft.timer.repeat >= node.raft.election_timeout, "timeout is "
	   "normal again");

	/*
	 * Cluster size change can make split vote.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);
	raft_node_cfg_cluster_size(&node, 3);
	raft_node_cfg_election_quorum(&node, 2);
	raft_run_next_event();
	is(node.raft.state, RAFT_STATE_CANDIDATE, "is candidate");
	is(node.raft.vote, 1, "voted for self");

	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Source. */
	), 0, "vote response for 2 from 2");
	ok(node.raft.timer.repeat >= node.raft.election_timeout, "the vote is "
	   "not split yet");

	raft_node_cfg_cluster_size(&node, 2);
	ok(node.raft.timer.repeat < node.raft.election_timeout, "cluster size "
	   "change makes split vote");

	raft_run_next_event();
	ok(raft_node_check_full_state(&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "a new term");

	/*
	 * Split vote can be even when the leader is already known - then
	 * nothing to do. Votes are just left from the beginning of the term and
	 * then probably cluster size reduced a bit.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);
	raft_node_cfg_cluster_size(&node, 3);
	raft_node_cfg_election_quorum(&node, 2);
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Source. */
	), 0, "vote response for 2 from 2");
	/* There is also a vote from 3 for 2. But it wasn't delivered to 1. */
	is(raft_node_send_leader(&node,
		2 /* Term. */,
		2 /* Source. */
	), 0, "message is accepted");
	is(node.raft.leader, 2, "other node's leadership is accepted");
	is(raft_vote_count(&node.raft), 1, "the only own vote was from self");

	raft_node_cfg_cluster_size(&node, 2);
	ok(node.raft.timer.repeat >= node.raft.death_timeout, "cluster change "
	   "does not affect the leader's death waiting");

	/*
	 * Non-candidate should ignore split vote.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);
	raft_node_cfg_cluster_size(&node, 3);
	raft_node_cfg_election_quorum(&node, 3);
	raft_node_cfg_is_candidate(&node, false);

	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Source. */
	), 0, "vote response for 2 from 2");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		3 /* Vote. */,
		3 /* Source. */
	), 0, "vote response for 3 from 3");

	ok(!raft_ev_is_active(&node.raft.timer), "voter couldn't schedule "
	    "new term");

	/*
	 * Split vote can get worse, but it shouldn't lead to new term delay
	 * restart.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);
	raft_node_cfg_cluster_size(&node, 3);
	raft_node_cfg_election_quorum(&node, 3);

	raft_run_next_event();
	is(node.raft.term, 2, "bump term");
	is(node.raft.vote, 1, "vote for self");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Source. */
	), 0, "vote response for 2 from 2");

	double delay = node.raft.timer.repeat;
	ok(delay < node.raft.election_timeout, "split vote is already "
	   "inevitable");

	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		3 /* Vote. */,
		3 /* Source. */
	), 0, "vote response for 3 from 3");

	is(delay, node.raft.timer.repeat, "split vote got worse, but delay "
	   "didn't change");

	/*
	 * Handle split vote when WAL write is in progress.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);
	raft_node_cfg_cluster_size(&node, 2);
	raft_node_cfg_election_quorum(&node, 2);

	raft_node_block(&node);
	raft_run_next_event();
	is(node.raft.term, 1, "old term");
	is(node.raft.vote, 0, "unused vote");
	is(node.raft.volatile_term, 2, "new volatile term");
	is(node.raft.volatile_vote, 1, "new volatile vote");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Source. */
	), 0, "vote response for 2 from 2");

	raft_node_unblock(&node);
	is(node.raft.term, 2, "new term");
	is(node.raft.vote, 1, "voted for self");
	is(node.raft.volatile_term, 2, "volatile term");
	is(node.raft.volatile_vote, 1, "volatile vote");
	ok(node.raft.timer.repeat < node.raft.election_timeout, "found split "
	   "vote after WAL write");

	raft_run_next_event();
	is(node.raft.term, 3, "bump term");
	is(node.raft.vote, 1, "vote for self");

	/*
	 * Split vote check is disabled when cluster size < quorum. Makes no
	 * sense to speed the elections up.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);
	raft_node_cfg_cluster_size(&node, 1);
	raft_node_cfg_election_quorum(&node, 2);

	raft_run_next_event();
	is(node.raft.term, 2, "bump term");
	is(node.raft.vote, 1, "vote for self");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Source. */
	), 0, "vote response for 2 from 2");

	ok(node.raft.timer.repeat >= node.raft.election_timeout, "split vote "
	   "is not checked for cluster < quorum");

	/*
	 * Split vote check is disabled when vote count > cluster size. The
	 * reason is the same as with quorum > cluster size - something is odd,
	 * more term bumps won't help.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);
	raft_node_cfg_cluster_size(&node, 3);
	raft_node_cfg_election_quorum(&node, 2);

	raft_run_next_event();
	is(node.raft.term, 2, "bump term");
	is(node.raft.vote, 1, "vote for self");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Source. */
	), 0, "vote response for 2 from 2");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		2 /* Vote. */,
		3 /* Source. */
	), 0, "vote response for 2 from 3");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		3 /* Vote. */,
		4 /* Source. */
	), 0, "vote response for 3 from 4");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		4 /* Vote. */,
		5 /* Source. */
	), 0, "vote response for 4 from 5");

	ok(node.raft.timer.repeat >= node.raft.election_timeout, "split vote "
	   "is not checked when vote count > cluster size");

	/*
	 * Split vote can happen if quorum was suddenly increased.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);
	raft_node_cfg_cluster_size(&node, 3);
	raft_node_cfg_election_quorum(&node, 2);

	raft_run_next_event();
	is(node.raft.term, 2, "bump term");
	is(node.raft.vote, 1, "vote for self");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Source. */
	), 0, "vote response for 2 from 2");

	ok(node.raft.timer.repeat >= node.raft.election_timeout, "not split "
	   "vote yet");

	raft_node_cfg_election_quorum(&node, 3);
	ok(node.raft.timer.repeat < node.raft.election_timeout, "split vote "
	   "after quorum increase");

	raft_run_next_event();
	is(node.raft.term, 3, "bump term");
	is(node.raft.vote, 1, "vote for self");

	/*
	 * Split vote can make delay to next election 0. Timer with 0 timeout
	 * has a special state in libev. Another vote can come on the next
	 * even loop iteration just before the timer is triggered. It should be
	 * ready to the special state of the timer.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);
	raft_node_cfg_cluster_size(&node, 3);
	raft_node_cfg_election_quorum(&node, 3);
	raft_node_cfg_max_shift(&node, 0);

	raft_run_next_event();
	is(node.raft.term, 2, "bump term");
	is(node.raft.vote, 1, "vote for self");
	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Source. */
	), 0, "vote response for 2 from 2");

	is(node.raft.timer.repeat, 0, "planned new election after yield");

	is(raft_node_send_vote_response(&node,
		2 /* Term. */,
		3 /* Vote. */,
		3 /* Source. */
	), 0, "vote response for 3 from 3");

	is(node.raft.timer.repeat, 0, "still waiting for yield");

	/*
	 * gh-8698: a candidate might erroneously discover a split vote when
	 * simply voting for another node.
	 */
	raft_node_destroy(&node);
	raft_node_create(&node);
	raft_node_cfg_cluster_size(&node, 2);
	raft_node_cfg_election_quorum(&node, 2);

	is(raft_node_send_vote_request(
		&node,
		2 /* Term. */,
		"{}" /* Vclock. */,
		2 /* Source. */
	), 0, "vote for 2");

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		2 /* Vote. */,
		2 /* Volatile term. */,
		2 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "term and vote are persisted");
	ok(node.raft.timer.repeat >= node.raft.election_timeout,
	   "no split vote");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_pre_vote(void)
{
	raft_start_test(43);
	struct raft_node node;
	raft_node_create(&node);

	/* Check leader_idle calculations. */
	raft_node_block(&node);
	is(raft_node_send_leader(
		&node,
		2 /* Term. */,
		2 /* Source. */
	), 0, "leader notification");
	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		1 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 0}" /* Vclock. */
	), "WAL write is in progress");
	is(raft_leader_idle(&node.raft), 0, "leader just appeared");

	raft_run_for(node.cfg_death_timeout / 2);
	is(raft_leader_idle(&node.raft), node.cfg_death_timeout / 2,
	   "leader_idle increased");
	raft_node_send_heartbeat(&node, 2);
	is(raft_leader_idle(&node.raft), 0, "heartbeat resets idle counter "
					    "during WAL write");

	raft_node_unblock(&node);
	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "WAL write finished");
	raft_run_for(node.cfg_death_timeout / 2);
	is(raft_leader_idle(&node.raft), node.cfg_death_timeout / 2,
	   "leader_idle increased");
	raft_node_send_heartbeat(&node, 2);
	is(raft_leader_idle(&node.raft), 0, "heartbeat resets idle counter "
					    "when no WAL write");

	raft_node_cfg_is_candidate(&node, false);

	ok(raft_ev_is_active(&node.raft.timer), "voter tracks leader death");

	raft_run_for(2 * node.cfg_death_timeout);

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "leader still remembered");

	is(raft_leader_idle(&node.raft), 2 * node.cfg_death_timeout,
	   "idle increased");
	ok(!raft_ev_is_active(&node.raft.timer), "timed out");

	raft_node_send_heartbeat(&node, 2);
	is(raft_leader_idle(&node.raft), 0, "heartbeat resets idle");
	ok(raft_ev_is_active(&node.raft.timer), "heartbeat restarts timer");

	raft_node_cfg_is_candidate(&node, true);

	is(raft_node_send_is_leader_seen(
		&node,
		2 /* Term. */,
		true /* Is leader seen. */,
		3 /* Source. */
	), 0, "leader seen notification accepted");

	raft_run_for(2 * node.cfg_death_timeout);
	is(raft_leader_idle(&node.raft), 2 * node.cfg_death_timeout,
	   "leader not seen");
	ok(!raft_ev_is_active(&node.raft.timer), "timed out");
	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "no elections when leader seen indirectly");

	is(raft_node_send_is_leader_seen(
		&node,
		2 /* Term. */,
		false /* Is leader seen. */,
		3 /* Source. */
	), 0, "leader not seen notification accepted");

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_CANDIDATE /* State. */,
		0 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "elections once no one sees the leader");

	raft_node_cfg_election_quorum(&node, 1);

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_LEADER /* State. */,
		1 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "become leader on quorum change");

	raft_cfg_is_candidate_later(&node.raft, false);

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_LEADER /* State. */,
		1 /* Leader. */,
		3 /* Term. */,
		1 /* Vote. */,
		3 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 2}" /* Vclock. */
	), "cfg_is_candidate_later doesn't disrupt leader");

	is(raft_node_send_follower(
		&node,
		4 /* Term. */,
		2 /* Source. */
	), 0, "accept term bump");

	raft_run_for(node.cfg_death_timeout * 2);

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		4 /* Term. */,
		0 /* Vote. */,
		4 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 3}" /* Vclock. */
	), "term bump after cfg_is_candidate_later makes node a voter.");

	raft_cfg_is_candidate_later(&node.raft, true);

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		4 /* Term. */,
		0 /* Vote. */,
		4 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 3}" /* Vclock. */
	), "cfg_is_candidate_later doesn't transfer voter to a candidate");

	is(raft_node_send_follower(
		&node,
		5 /* Term. */,
		2 /* Source. */
	), 0, "accept term bump");

	raft_run_for(node.cfg_death_timeout);

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_LEADER /* State. */,
		1 /* Leader. */,
		5 /* Term. */,
		1 /* Vote. */,
		5 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 5}" /* Vclock. */
	), "Term bump with cfg_is_candidate_later transfers voter to candiate");

	is(raft_leader_idle(&node.raft), 0,
	   "leader_idle is zero on the current leader");

	raft_node_cfg_is_candidate(&node, false);

	raft_run_for(node.cfg_death_timeout / 2);
	is(raft_leader_idle(&node.raft), node.cfg_death_timeout / 2,
	   "leader_idle counts from 0 on a previous leader")

	raft_node_cfg_is_enabled(&node, false);

	is(raft_node_send_is_leader_seen(
		&node,
		6 /* Term. */,
		true /* Is leader seen. */,
		2 /* Source. */
	), 0, "leader is seen message accepted when raft disabled");

	ok(&node.raft.leader_witness_map != 0,
	   "who sees leader is tracked on disabled node");

	ok(!raft_ev_is_active(&node.raft.timer),
	   "disabled node doesn't wait for anything");

	raft_node_cfg_is_candidate(&node, true);
	raft_node_cfg_is_enabled(&node, true);

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		6 /* Term. */,
		0 /* Vote. */,
		6 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 6}" /* Vclock. */
	), "no elections on start when someone sees the leader");

	ok(!raft_ev_is_active(&node.raft.timer),
	   "nothing to wait for as long as someone sees the leader");

	raft_node_cfg_is_candidate(&node, false);
	raft_node_cfg_is_candidate(&node, true);

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		6 /* Term. */,
		0 /* Vote. */,
		6 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 6}" /* Vclock. */
	), "no elections on becoming candidate when someone sees the leader");

	ok(!raft_ev_is_active(&node.raft.timer),
	   "nothing to wait for as long as someone sees the leader");

	is(raft_node_send_leader(
		&node,
		6 /* Term. */,
		3 /* Source. */
	), 0, "leader is accepted");

	raft_run_for(node.cfg_death_timeout * 2);
	ok(!raft_ev_is_active(&node.raft.timer), "timed out");
	raft_node_cfg_death_timeout(&node, node.cfg_death_timeout / 2);
	ok(!raft_ev_is_active(&node.raft.timer),
	   "No timer re-start on death timeout reconfig "
	   "when already timed-out");

	is(raft_node_send_vote_request(
		&node,
		7 /* Term. */,
		"{2: 1}" /* Vclock. */,
		2 /* Source. */
	), 0, "vote for 2");
	is(raft_node_send_leader(
		&node,
		7 /* Term. */,
		2 /* Source. */
	), 0, "leader accepted");
	is(raft_node_send_is_leader_seen(
		&node,
		7 /* Term. */,
		true /* Is leader seen. */,
		3 /* Source. */
	), 0, "leader seen notification accepted");

	raft_run_for(node.cfg_death_timeout * 2);
	raft_node_cfg_election_timeout(&node, node.cfg_election_timeout / 2);
	ok(!raft_ev_is_active(&node.raft.timer),
	   "No timer re_start on election timeout reconfig "
	   "when it's not time for elections yet");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_resign(void)
{
	raft_start_test(2);
	struct raft_node node;
	raft_node_create(&node);
	raft_node_cfg_is_candidate(&node, true);

	raft_node_cfg_election_quorum(&node, 1);
	raft_node_promote(&node);
	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_LEADER /* State. */,
		1 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "became leader");

	raft_node_resign(&node);

	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		0 /* Leader. */,
		2 /* Term. */,
		1 /* Vote. */,
		2 /* Volatile term. */,
		1 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "resigned from leader state");

	raft_node_destroy(&node);
	raft_finish_test();
}

static void
raft_test_candidate_disable_during_wal_write(void)
{
	raft_start_test(2);
	/*
	 * There was a false-positive assertion failure in a special case: the
	 * node has just received a is_leader notification and is currently
	 * writing it on disk. At the same time it is configured as voter
	 * (gh-8169).
	 */
	struct raft_node node;
	raft_node_create(&node);
	raft_node_cfg_is_candidate(&node, true);
	raft_node_block(&node);
	raft_node_send_leader(&node, 2, 2);
	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		1 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{}" /* Vclock. */
	), "Leader is seen, but wal write is in progress");
	raft_node_cfg_is_candidate(&node, false);
	raft_node_unblock(&node);
	ok(raft_node_check_full_state(
		&node,
		RAFT_STATE_FOLLOWER /* State. */,
		2 /* Leader. */,
		2 /* Term. */,
		0 /* Vote. */,
		2 /* Volatile term. */,
		0 /* Volatile vote. */,
		"{0: 1}" /* Vclock. */
	), "State is persisted");

	raft_node_destroy(&node);
	raft_finish_test();
}

static int
main_f(va_list ap)
{
	raft_start_test(20);

	(void) ap;
	fakeev_init();

	raft_test_leader_election();
	raft_test_recovery();
	raft_test_bad_msg();
	raft_test_vote();
	raft_test_vote_skip();
	raft_test_vote_during_wal_write();
	raft_test_leader_resign();
	raft_test_split_brain();
	raft_test_heartbeat();
	raft_test_election_timeout();
	raft_test_election_quorum();
	raft_test_death_timeout();
	raft_test_enable_disable();
	raft_test_too_long_wal_write();
	raft_test_promote_restore();
	raft_test_bump_term_before_cfg();
	raft_test_split_vote();
	raft_test_pre_vote();
	raft_test_resign();
	raft_test_candidate_disable_during_wal_write();

	fakeev_free();

	test_result = check_plan();
	footer();
	return 0;
}

int
main()
{
	raft_run_test("raft.txt", main_f);
	return test_result;
}
