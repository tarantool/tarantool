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
#include "memory.h"
#include "raft/raft_ev.h"
#include "raft_test_utils.h"
#include "random.h"

#include <fcntl.h>

void
raft_ev_timer_start(struct ev_loop *loop, struct ev_timer *watcher)
{
	fakeev_timer_start(loop, watcher);
}

double
raft_ev_timer_remaining(struct ev_loop *loop, struct ev_timer *watcher)
{
	return fakeev_timer_remaining(loop, watcher);
}

void
raft_ev_timer_stop(struct ev_loop *loop, struct ev_timer *watcher)
{
	fakeev_timer_stop(loop, watcher);
}

struct ev_loop *
raft_loop(void)
{
	return fakeev_loop();
}

static void
raft_node_broadcast_f(struct raft *raft, const struct raft_msg *msg);

static void
raft_node_write_f(struct raft *raft, const struct raft_msg *msg);

static void
raft_node_schedule_async_f(struct raft *raft);

static struct raft_vtab raft_vtab = {
	.broadcast = raft_node_broadcast_f,
	.write = raft_node_write_f,
	.schedule_async = raft_node_schedule_async_f,
};

static int
raft_node_on_update(struct trigger *t, void *event)
{
	struct raft_node *n = t->data;
	assert(&n->on_update == t);
	assert(&n->raft == event);
	(void)event;
	++n->update_count;
	return 0;
}

static void
raft_node_on_destroy(struct trigger *t)
{
	struct raft_node *n = t->data;
	assert(&n->on_update == t);
	n->update_count = 0;
}

static inline bool
raft_node_is_started(const struct raft_node *node)
{
	return node->worker != NULL;
}

static int
raft_node_worker_f(va_list va);

static void
raft_journal_create(struct raft_journal *journal, uint32_t instance_id)
{
	memset(journal, 0, sizeof(*journal));
	vclock_create(&journal->vclock);
	journal->instance_id = instance_id;
}

static void
raft_journal_write(struct raft_journal *journal, const struct raft_msg *msg)
{
	assert(msg->vclock == NULL);
	int index = journal->size;
	int new_size = index + 1;
	journal->rows = realloc(journal->rows,
				sizeof(journal->rows[0]) * new_size);
	assert(journal->rows != NULL);
	journal->rows[index] = *msg;
	journal->size = new_size;
	vclock_inc(&journal->vclock, 0);
}

static void
raft_journal_follow(struct raft_journal *journal, uint32_t replica_id,
		    int64_t count)
{
	int64_t lsn = vclock_get(&journal->vclock, replica_id);
	lsn += count;
	vclock_follow(&journal->vclock, replica_id, lsn);
}

static void
raft_journal_destroy(struct raft_journal *journal)
{
	free(journal->rows);
}

static void
raft_net_create(struct raft_net *net)
{
	memset(net, 0, sizeof(*net));
}

static void
raft_net_send(struct raft_net *net, const struct raft_msg *msg)
{
	int index = net->count;
	int new_count = index + 1;
	net->msgs = realloc(net->msgs, sizeof(net->msgs[0]) * new_count);
	assert(net->msgs != NULL);
	net->msgs[index] = *msg;
	struct raft_msg *new_msg = &net->msgs[index];
	if (new_msg->vclock != NULL) {
		/*
		 * Network messages can contain vclock, which references the
		 * original raft vclock. Must copy it, otherwise all net
		 * messages will point at the same vclock.
		 */
		struct vclock *v = malloc(sizeof(*v));
		assert(v != NULL);
		vclock_copy(v, new_msg->vclock);
		new_msg->vclock = v;
	}
	net->count = new_count;
}

static void
raft_net_drop(struct raft_net *net)
{
	for (int i = 0; i < net->count; ++i)
		free((struct vclock *)net->msgs[i].vclock);
	free(net->msgs);
	net->msgs = NULL;
	net->count = 0;
}

static void
raft_net_destroy(struct raft_net *net)
{
	raft_net_drop(net);
}

void
raft_node_create(struct raft_node *node)
{
	memset(node, 0, sizeof(*node));
	node->cfg_is_enabled = true;
	node->cfg_is_candidate = true;
	node->cfg_election_timeout = 5;
	node->cfg_election_quorum = 3;
	node->cfg_death_timeout = 5;
	node->cfg_instance_id = 1;
	node->cfg_vclock = &node->journal.vclock;
	raft_journal_create(&node->journal, node->cfg_instance_id);
	raft_node_start(node);
}

void
raft_node_net_drop(struct raft_node *node)
{
	assert(raft_node_is_started(node));
	raft_net_drop(&node->net);
}

bool
raft_node_net_check_msg(const struct raft_node *node, int i,
			enum raft_state state, uint64_t term, uint32_t vote,
			const char *vclock)
{
	assert(raft_node_is_started(node));
	assert(node->net.count > i);
	return raft_msg_check(&node->net.msgs[i], state, term, vote, vclock);
}

bool
raft_node_check_full_state(const struct raft_node *node, enum raft_state state,
			   uint32_t leader, uint64_t term, uint32_t vote,
			   uint64_t volatile_term, uint32_t volatile_vote,
			   const char *vclock)
{
	assert(raft_node_is_started(node));
	const struct raft *raft = &node->raft;
	struct vclock v;
	raft_vclock_from_string(&v, vclock);
	return raft->state == state && raft->leader == leader &&
	       raft->term == term && raft->vote == vote &&
	       raft->volatile_term == volatile_term &&
	       raft->volatile_vote == volatile_vote &&
	       vclock_compare(&v, raft->vclock) == 0;
}

bool
raft_node_journal_check_row(const struct raft_node *node, int i, uint64_t term,
			    uint32_t vote)
{
	assert(raft_node_is_started(node));
	assert(node->journal.size > i);
	return raft_msg_check(&node->journal.rows[i], 0, term, vote, NULL);
}

void
raft_node_journal_follow(struct raft_node *node, uint32_t replica_id,
			 int64_t count)
{
	raft_journal_follow(&node->journal, replica_id, count);
}

void
raft_node_new_term(struct raft_node *node)
{
	raft_new_term(&node->raft);
	raft_run_async_work();
}

int
raft_node_process_msg(struct raft_node *node, const struct raft_msg *msg,
		      uint32_t source)
{
	int rc = raft_process_msg(&node->raft, msg, source);
	raft_run_async_work();
	return rc;
}

int
raft_node_send_vote_response(struct raft_node *node, uint64_t term,
			     uint32_t vote, uint32_t source)
{
	struct raft_msg msg = {
		.state = RAFT_STATE_FOLLOWER,
		.term = term,
		.vote = vote,
	};
	return raft_node_process_msg(node, &msg, source);
}

int
raft_node_send_vote_request(struct raft_node *node, uint64_t term,
			    const char *vclock, uint32_t source)
{
	struct vclock v;
	raft_vclock_from_string(&v, vclock);
	struct raft_msg msg = {
		.state = RAFT_STATE_CANDIDATE,
		.term = term,
		.vote = source,
		.vclock = &v,
	};
	return raft_node_process_msg(node, &msg, source);
}

int
raft_node_send_leader(struct raft_node *node, uint64_t term, uint32_t source)
{
	struct raft_msg msg = {
		.state = RAFT_STATE_LEADER,
		.term = term,
	};
	return raft_node_process_msg(node, &msg, source);
}

int
raft_node_send_follower(struct raft_node *node, uint64_t term, uint32_t source)
{
	struct raft_msg msg = {
		.state = RAFT_STATE_FOLLOWER,
		.term = term,
	};
	return raft_node_process_msg(node, &msg, source);
}

void
raft_node_send_heartbeat(struct raft_node *node, uint32_t source)
{
	assert(raft_node_is_started(node));
	raft_process_heartbeat(&node->raft, source);
}

void
raft_node_restart(struct raft_node *node)
{
	assert(raft_node_is_started(node));
	raft_node_stop(node);
	raft_node_start(node);
}

void
raft_node_stop(struct raft_node *node)
{
	assert(raft_node_is_started(node));
	fiber_cancel(node->worker);
	fiber_join(node->worker);
	raft_destroy(&node->raft);
	assert(node->update_count == 0);
	raft_net_destroy(&node->net);
	node->worker = NULL;
	node->has_work = false;
}

void
raft_node_start(struct raft_node *node)
{
	assert(!raft_node_is_started(node));

	raft_net_create(&node->net);

	node->worker = fiber_new("raft_node_worker", raft_node_worker_f);
	node->worker->f_arg = node;
	fiber_set_joinable(node->worker, true);
	fiber_wakeup(node->worker);
	trigger_create(&node->on_update, raft_node_on_update, node,
		       raft_node_on_destroy);
	raft_create(&node->raft, &raft_vtab);
	raft_on_update(&node->raft, &node->on_update);

	for (int i = 0; i < node->journal.size; ++i)
		raft_process_recovery(&node->raft, &node->journal.rows[i]);

	raft_cfg_is_enabled(&node->raft, node->cfg_is_enabled);
	raft_cfg_is_candidate(&node->raft, node->cfg_is_candidate);
	raft_cfg_election_timeout(&node->raft, node->cfg_election_timeout);
	raft_cfg_election_quorum(&node->raft, node->cfg_election_quorum);
	raft_cfg_death_timeout(&node->raft, node->cfg_death_timeout);
	raft_cfg_instance_id(&node->raft, node->cfg_instance_id);
	raft_cfg_vclock(&node->raft, node->cfg_vclock);
	raft_run_async_work();
}

void
raft_node_block(struct raft_node *node)
{
	assert(!node->is_work_blocked);
	node->is_work_blocked = true;
}

void
raft_node_unblock(struct raft_node *node)
{
	assert(node->is_work_blocked);
	node->is_work_blocked = false;
	if (raft_node_is_started(node)) {
		fiber_wakeup(node->worker);
		raft_run_async_work();
	}
}

void
raft_node_start_candidate(struct raft_node *node)
{
	assert(raft_node_is_started(node));
	raft_start_candidate(&node->raft);
}

void
raft_node_stop_candidate(struct raft_node *node)
{
	assert(raft_node_is_started(node));
	raft_stop_candidate(&node->raft, false);
}

void
raft_node_demote_candidate(struct raft_node *node)
{
	assert(raft_node_is_started(node));
	raft_stop_candidate(&node->raft, true);
}

void
raft_node_cfg_is_enabled(struct raft_node *node, bool value)
{
	node->cfg_is_enabled = value;
	if (raft_node_is_started(node)) {
		raft_cfg_is_enabled(&node->raft, value);
		raft_run_async_work();
	}
}

void
raft_node_cfg_is_candidate(struct raft_node *node, bool value)
{
	node->cfg_is_candidate = value;
	if (raft_node_is_started(node)) {
		raft_cfg_is_candidate(&node->raft, value);
		raft_run_async_work();
	}
}

void
raft_node_cfg_election_timeout(struct raft_node *node, double value)
{
	node->cfg_election_timeout = value;
	if (raft_node_is_started(node)) {
		raft_cfg_election_timeout(&node->raft, value);
		raft_run_async_work();
	}
}

void
raft_node_cfg_election_quorum(struct raft_node *node, int value)
{
	node->cfg_election_quorum = value;
	if (raft_node_is_started(node)) {
		raft_cfg_election_quorum(&node->raft, value);
		raft_run_async_work();
	}
}

void
raft_node_cfg_death_timeout(struct raft_node *node, double value)
{
	node->cfg_death_timeout = value;
	if (raft_node_is_started(node)) {
		raft_cfg_death_timeout(&node->raft, value);
		raft_run_async_work();
	}
}

bool
raft_msg_check(const struct raft_msg *msg, enum raft_state state, uint64_t term,
	       uint32_t vote, const char *vclock)
{
	if (vclock != NULL) {
		if (msg->vclock == NULL)
			return false;
		struct vclock v;
		raft_vclock_from_string(&v, vclock);
		if (vclock_compare(&v, msg->vclock) != 0)
			return false;
	} else if (msg->vclock != NULL) {
		return false;
	}
	return msg->state == state && msg->term == term && msg->vote == vote;
}

void
raft_run_next_event(void)
{
	fakeev_loop_update(fakeev_loop());
	raft_run_async_work();
}

void
raft_run_async_work(void)
{
	fiber_sleep(0);
}

void
raft_run_for(double duration)
{
	assert(duration > 0);
	fakeev_set_brk(duration);
	double deadline = fakeev_time() + duration;
	while (fakeev_time() < deadline)
		raft_run_next_event();
}

void
raft_node_destroy(struct raft_node *node)
{
	if (raft_node_is_started(node))
		raft_node_stop(node);
	raft_journal_destroy(&node->journal);
}

static void
raft_node_broadcast_f(struct raft *raft, const struct raft_msg *msg)
{
	struct raft_node *node = container_of(raft, struct raft_node, raft);
	raft_net_send(&node->net, msg);
}

static void
raft_node_write_f(struct raft *raft, const struct raft_msg *msg)
{
	struct raft_node *node = container_of(raft, struct raft_node, raft);
	raft_journal_write(&node->journal, msg);
}

static void
raft_node_schedule_async_f(struct raft *raft)
{
	struct raft_node *node = container_of(raft, struct raft_node, raft);
	node->has_work = true;
	fiber_wakeup(node->worker);
}

static int
raft_node_worker_f(va_list va)
{
	(void)va;
	struct raft_node *node = fiber()->f_arg;
	while (!fiber_is_cancelled()) {
		node->has_work = false;

		while (node->is_work_blocked) {
			if (fiber_is_cancelled())
				return 0;
			fiber_yield();
		}
		raft_process_async(&node->raft);

		if (!node->has_work) {
			if (fiber_is_cancelled())
				return 0;
			fiber_yield();
		}
	}
	return 0;
}

void
raft_run_test(const char *log_file, fiber_func test)
{
	random_init();
	time_t seed = time(NULL);
	srand(seed);
	memory_init();
	fiber_init(fiber_c_invoke);
	int fd = open(log_file, O_TRUNC);
	if (fd != -1)
		close(fd);
	say_logger_init(log_file, 5, 1, "plain", 0);
	/* Print the seed to be able to reproduce a bug with the same seed. */
	say_info("Random seed = %llu", (unsigned long long) seed);

	struct fiber *main_fiber = fiber_new("main", test);
	fiber_set_joinable(main_fiber, true);
	assert(main_fiber != NULL);
	fiber_wakeup(main_fiber);
	ev_run(loop(), 0);
	fiber_join(main_fiber);

	say_logger_free();
	fiber_free();
	memory_free();
	random_free();
}
