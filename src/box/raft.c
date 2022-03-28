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
#include "box.h"
#include "error.h"
#include "journal.h"
#include "raft.h"
#include "relay.h"
#include "replication.h"
#include "xrow.h"

struct raft box_raft_global = {
	/*
	 * Set an invalid state to validate in runtime the global raft node is
	 * not used before initialization.
	 */
	.state = 0,
};

enum election_mode box_election_mode = ELECTION_MODE_INVALID;

/**
 * A trigger executed each time the Raft state machine updates any
 * of its visible attributes.
 */
static struct trigger box_raft_on_update;

struct rlist box_raft_on_broadcast =
	RLIST_HEAD_INITIALIZER(box_raft_on_broadcast);

/**
 * Worker fiber does all the asynchronous work, which may need yields and can be
 * long. These are WAL writes, network broadcasts. That allows not to block the
 * Raft state machine.
 */
static struct fiber *box_raft_worker = NULL;
/** Flag installed each time when new work appears for the worker fiber. */
static bool box_raft_has_work = false;

static void
box_raft_msg_to_request(const struct raft_msg *msg, struct raft_request *req)
{
	*req = (struct raft_request) {
		.term = msg->term,
		.vote = msg->vote,
		.state = msg->state,
		.vclock = msg->vclock,
	};
}

static void
box_raft_request_to_msg(const struct raft_request *req, struct raft_msg *msg)
{
	*msg = (struct raft_msg) {
		.term = req->term,
		.vote = req->vote,
		.state = req->state,
		.vclock = req->vclock,
	};
}

static void
box_raft_update_synchro_queue(struct raft *raft)
{
	assert(raft == box_raft());
	if (raft->state != RAFT_STATE_LEADER)
		return;
	int rc = 0;
	uint32_t errcode = 0;
	do {
		rc = box_promote_qsync();
		if (rc != 0) {
			struct error *err = diag_last_error(diag_get());
			errcode = box_error_code(err);
			diag_log();
		}
	} while (rc != 0 && errcode == ER_QUORUM_WAIT && !fiber_is_cancelled());
}

static int
box_raft_worker_f(va_list args)
{
	(void)args;
	struct raft *raft = fiber()->f_arg;
	assert(raft == box_raft());
	while (!fiber_is_cancelled()) {
		box_raft_has_work = false;

		raft_process_async(raft);
		box_raft_update_synchro_queue(raft);

		if (!box_raft_has_work)
			fiber_yield();
	}
	return 0;
}

static void
box_raft_schedule_async(struct raft *raft)
{
	assert(raft == box_raft());
	if (box_raft_worker == NULL) {
		box_raft_worker = fiber_new("raft_worker", box_raft_worker_f);
		if (box_raft_worker == NULL) {
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
		box_raft_worker->f_arg = raft;
		fiber_set_joinable(box_raft_worker, true);
	}
	/*
	 * Don't wake the fiber if it writes something (not cancellable).
	 * Otherwise it would be a spurious wakeup breaking the WAL write not
	 * adapted to this. Also don't wakeup the current fiber - it leads to
	 * undefined behaviour.
	 */
	if ((box_raft_worker->flags & FIBER_IS_CANCELLABLE) != 0)
		fiber_wakeup(box_raft_worker);
	box_raft_has_work = true;
}

static int
box_raft_on_update_f(struct trigger *trigger, void *event)
{
	(void)trigger;
	struct raft *raft = (struct raft *)event;
	assert(raft == box_raft());
	/*
	 * When the instance becomes a follower, it's good to make it read-only
	 * ASAP. This way we make sure followers don't write anything.
	 * However, if the instance is transitioning to leader it'll become
	 * writable only after it clears its synchro queue.
	 */
	box_update_ro_summary();
	box_broadcast_election();
	if (raft->state != RAFT_STATE_LEADER)
		return 0;
	/*
	 * If the node became a leader, time to clear the synchro queue. But it
	 * must be done in the worker fiber so as not to block the state
	 * machine, which called this trigger.
	 */
	box_raft_schedule_async(raft);
	return 0;
}

void
box_raft_update_election_quorum(void)
{
	/*
	 * When the instance is started first time, it does not have an ID, so
	 * the registered count is 0. But the quorum can never be 0. At least
	 * the current instance should participate in the quorum.
	 */
	int max = MAX(replicaset.registered_count, 1);
	/*
	 * Election quorum is not strictly equal to synchronous replication
	 * quorum. Sometimes it can be lowered. That is about bootstrap.
	 *
	 * The problem with bootstrap is that when the replicaset boots, all the
	 * instances can't write to WAL and can't recover from their initial
	 * snapshot. They need one node which will boot first, and then they
	 * will replicate from it.
	 *
	 * This one node should boot from its zero snapshot, create replicaset
	 * UUID, register self with ID 1 in _cluster space, and then register
	 * all the other instances here. To do that the node must be writable.
	 * It should have read_only = false, connection quorum satisfied, and be
	 * a Raft leader if Raft is enabled.
	 *
	 * To be elected a Raft leader it needs to perform election. But it
	 * can't be done before at least synchronous quorum of the replicas is
	 * bootstrapped. And they can't be bootstrapped because wait for a
	 * leader to initialize _cluster. Cyclic dependency.
	 *
	 * This is resolved by truncation of the election quorum to the number
	 * of registered replicas, if their count is less than synchronous
	 * quorum. That helps to elect a first leader.
	 *
	 * It may seem that the first node could just declare itself a leader
	 * and then strictly follow the protocol from now on, but that won't
	 * work, because if the first node will restart after it is booted, but
	 * before quorum of replicas is booted, the cluster will stuck again.
	 *
	 * The current solution is totally safe because
	 *
	 * - after all the cluster will have node count >= quorum, if user used
	 *   a correct config (God help him if he didn't);
	 *
	 * - synchronous replication quorum is untouched - it is not truncated.
	 *   Only leader election quorum is affected. So synchronous data won't
	 *   be lost.
	 */
	int quorum = MIN(replication_synchro_quorum, max);
	struct raft *raft = box_raft();
	raft_cfg_election_quorum(raft, quorum);
	raft_cfg_cluster_size(raft, max);
}

void
box_raft_recover(const struct raft_request *req)
{
	struct raft_msg msg;
	box_raft_request_to_msg(req, &msg);
	raft_process_recovery(box_raft(), &msg);
}

void
box_raft_checkpoint_local(struct raft_request *req)
{
	struct raft_msg msg;
	raft_checkpoint_local(box_raft(), &msg);
	box_raft_msg_to_request(&msg, req);
}

void
box_raft_checkpoint_remote(struct raft_request *req)
{
	struct raft_msg msg;
	raft_checkpoint_remote(box_raft(), &msg);
	box_raft_msg_to_request(&msg, req);
}

int
box_raft_process(struct raft_request *req, uint32_t source)
{
	struct raft_msg msg;
	box_raft_request_to_msg(req, &msg);
	return raft_process_msg(box_raft(), &msg, source);
}

static void
box_raft_broadcast(struct raft *raft, const struct raft_msg *msg)
{
	(void)raft;
	assert(raft == box_raft());
	struct raft_request req;
	box_raft_msg_to_request(msg, &req);
	replicaset_foreach(replica)
		relay_push_raft(replica->relay, &req);
	trigger_run(&box_raft_on_broadcast, NULL);
}

static void
box_raft_write(struct raft *raft, const struct raft_msg *msg)
{
	(void)raft;
	assert(raft == box_raft());
	/* See Raft implementation why these fields are never written. */
	assert(msg->vclock == NULL);
	assert(msg->state == 0);

	struct raft_request req;
	box_raft_msg_to_request(msg, &req);
	struct region *region = &fiber()->gc;
	uint32_t svp = region_used(region);
	struct xrow_header row;
	char buf[sizeof(struct journal_entry) +
		 sizeof(struct xrow_header *)];
	struct journal_entry *entry = (struct journal_entry *)buf;
	entry->rows[0] = &row;

	if (xrow_encode_raft(&row, region, &req) != 0)
		goto fail;
	journal_entry_create(entry, 1, xrow_approx_len(&row),
			     journal_entry_fiber_wakeup_cb, fiber());

	/*
	 * A non-cancelable fiber is considered non-wake-able, generally. Raft
	 * follows this pattern of 'protection'.
	 */
	bool cancellable = fiber_set_cancellable(false);
	bool is_err = journal_write(entry) != 0;
	fiber_set_cancellable(cancellable);
	if (is_err)
		goto fail;
	if (entry->res < 0) {
		diag_set_journal_res(entry->res);
		goto fail;
	}

	region_truncate(region, svp);
	return;
fail:
	diag_log();
	/*
	 * XXX: the stub is supposed to be removed once it is defined what to do
	 * when a raft request WAL write fails.
	 */
	panic("Could not write a raft request to WAL\n");
}

/**
 * Context of waiting for a Raft term outcome. Which is either a leader is
 * elected, or a new term starts, or Raft is disabled.
 */
struct box_raft_watch_ctx {
	bool is_done;
	uint64_t term;
	struct fiber *owner;
};

static int
box_raft_wait_term_outcome_f(struct trigger *trig, void *event)
{
	struct raft *raft = event;
	assert(raft == box_raft());
	struct box_raft_watch_ctx *ctx = trig->data;
	/*
	 * Term ended with nothing, probably split vote which led to a next
	 * term.
	 */
	if (raft->volatile_term > ctx->term)
		goto done;
	/* Instance does not participate in terms anymore. */
	if (!raft->is_enabled)
		goto done;
	/* The term ended with a leader being found. */
	if (raft->leader != REPLICA_ID_NIL)
		goto done;
	/* The term still continues with no resolution. */
	return 0;
done:
	ctx->is_done = true;
	fiber_wakeup(ctx->owner);
	return 0;
}

int
box_raft_wait_term_outcome(void)
{
	struct raft *raft = box_raft();
	struct trigger trig;
	struct box_raft_watch_ctx ctx = {
		.is_done = false,
		.term = raft->volatile_term,
		.owner = fiber(),
	};
	trigger_create(&trig, box_raft_wait_term_outcome_f, &ctx, NULL);
	raft_on_update(raft, &trig);
	/*
	 * XXX: it is not a good idea not to have a timeout here. If all nodes
	 * are voters, the term might never end with any result nor bump to a
	 * new value.
	 */
	while (!fiber_is_cancelled() && !ctx.is_done)
		fiber_yield();
	trigger_clear(&trig);
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	if (!raft->is_enabled) {
		diag_set(ClientError, ER_ELECTION_DISABLED);
		return -1;
	}
	return 0;
}

struct raft_wait_persisted_data {
	struct fiber *waiter;
	uint64_t term;
};

static int
box_raft_wait_term_persisted_f(struct trigger *trig, void *event)
{
	struct raft *raft = event;
	struct raft_wait_persisted_data *data = trig->data;
	if (raft->term >= data->term)
		fiber_wakeup(data->waiter);
	return 0;
}

int
box_raft_wait_term_persisted(void)
{
	struct raft *raft = box_raft();
	if (raft->term == raft->volatile_term)
		return 0;
	struct raft_wait_persisted_data data = {
		.waiter = fiber(),
		.term = raft->volatile_term,
	};
	struct trigger trig;
	trigger_create(&trig, box_raft_wait_term_persisted_f, &data, NULL);
	raft_on_update(raft, &trig);

	do {
		fiber_yield();
	} while (raft->term < data.term && !fiber_is_cancelled());

	trigger_clear(&trig);
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	return 0;
}

void
box_raft_init(void)
{
	static const struct raft_vtab box_raft_vtab = {
		.broadcast = box_raft_broadcast,
		.write = box_raft_write,
		.schedule_async = box_raft_schedule_async,
	};
	raft_create(&box_raft_global, &box_raft_vtab);
	trigger_create(&box_raft_on_update, box_raft_on_update_f, NULL, NULL);
	raft_on_update(box_raft(), &box_raft_on_update);
}

void
box_raft_free(void)
{
	struct raft *raft = box_raft();
	/*
	 * Can't join the fiber, because the event loop is stopped already, and
	 * yields are not allowed.
	 */
	box_raft_worker = NULL;
	raft_destroy(raft);
	/*
	 * Invalidate so as box_raft() would fail if any usage attempt happens.
	 */
	raft->state = 0;
}
