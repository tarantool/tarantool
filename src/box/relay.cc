/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "relay.h"

#include "trivia/config.h"
#include "tt_static.h"
#include "scoped_guard.h"
#include "cbus.h"
#include "errinj.h"
#include "fiber.h"
#include "say.h"

#include "coio.h"
#include "coio_task.h"
#include "engine.h"
#include "gc.h"
#include "iproto_constants.h"
#include "recovery.h"
#include "replication.h"
#include "trigger.h"
#include "vclock/vclock.h"
#include "version.h"
#include "xrow.h"
#include "xrow_io.h"
#include "xstream.h"
#include "wal.h"
#include "txn_limbo.h"
#include "raft.h"

/**
 * Cbus message to send status updates from relay to tx thread.
 */
struct relay_status_msg {
	/** Parent */
	struct cmsg msg;
	/** Relay instance */
	struct relay *relay;
	/** Replica vclock. */
	struct vclock vclock;
};

/**
 * Cbus message to update replica gc state in tx thread.
 */
struct relay_gc_msg {
	/** Parent */
	struct cmsg msg;
	/**
	 * Link in the list of pending gc messages,
	 * see relay::pending_gc.
	 */
	struct stailq_entry in_pending;
	/** Relay instance */
	struct relay *relay;
	/** Vclock to advance to */
	struct vclock vclock;
};

/** State of a replication relay. */
struct relay {
	/** The thread in which we relay data to the replica. */
	struct cord cord;
	/** Replica connection */
	struct ev_io io;
	/** Request sync */
	uint64_t sync;
	/** Recovery instance to read xlog from the disk */
	struct recovery *r;
	/** Xstream argument to recovery */
	struct xstream stream;
	/** Vclock to stop playing xlogs */
	struct vclock stop_vclock;
	/** Remote replica */
	struct replica *replica;
	/** WAL event watcher. */
	struct wal_watcher wal_watcher;
	/** Relay reader cond. */
	struct fiber_cond reader_cond;
	/** Relay diagnostics. */
	struct diag diag;
	/** Vclock recieved from replica. */
	struct vclock recv_vclock;
	/** Replicatoin slave version. */
	uint32_t version_id;
	/**
	 * A filter of replica ids whose rows should be ignored.
	 * Each set filter bit corresponds to a replica id whose
	 * rows shouldn't be relayed. The list of ids to ignore
	 * is passed by the replica on subscribe.
	 */
	uint32_t id_filter;
	/**
	 * How many rows has this relay sent to the replica. Used to yield once
	 * in a while when reading a WAL to unblock the event loop.
	 */
	int64_t row_count;
	/**
	 * Local vclock at the moment of subscribe, used to check
	 * dataset on the other side and send missing data rows if any.
	 */
	struct vclock local_vclock_at_subscribe;

	/** Relay endpoint */
	struct cbus_endpoint endpoint;
	/** A pipe from 'relay' thread to 'tx' */
	struct cpipe tx_pipe;
	/** A pipe from 'tx' thread to 'relay' */
	struct cpipe relay_pipe;
	/** Status message */
	struct relay_status_msg status_msg;
	/**
	 * List of garbage collection messages awaiting
	 * confirmation from the replica.
	 */
	struct stailq pending_gc;
	/** Time when last row was sent to peer. */
	double last_row_time;
	/** Relay sync state. */
	enum relay_state state;

	struct {
		/* Align to prevent false-sharing with tx thread */
		alignas(CACHELINE_SIZE)
		/** Known relay vclock. */
		struct vclock vclock;
		/**
		 * True if the relay needs Raft updates. It can live fine
		 * without sending Raft updates, if it is a relay to an
		 * anonymous replica, for example.
		 */
		bool is_raft_enabled;
	} tx;
};

struct diag*
relay_get_diag(struct relay *relay)
{
	return &relay->diag;
}

enum relay_state
relay_get_state(const struct relay *relay)
{
	return relay->state;
}

const struct vclock *
relay_vclock(const struct relay *relay)
{
	return &relay->tx.vclock;
}

double
relay_last_row_time(const struct relay *relay)
{
	return relay->last_row_time;
}

static void
relay_send(struct relay *relay, struct xrow_header *packet);
static void
relay_send_initial_join_row(struct xstream *stream, struct xrow_header *row);
static void
relay_send_row(struct xstream *stream, struct xrow_header *row);

struct relay *
relay_new(struct replica *replica)
{
	/*
	 * We need to use aligned_alloc for this struct, because it's have
	 * specific alignas(CACHELINE_SIZE). If we use simple malloc or same
	 * functions, we will get member access within misaligned address
	 * (Use clang UB Sanitizer, to make sure of this)
	 */
	assert((sizeof(struct relay) % alignof(struct relay)) == 0);
	struct relay *relay = (struct relay *)
		aligned_alloc(alignof(struct relay), sizeof(struct relay));
	if (relay == NULL) {
		diag_set(OutOfMemory, sizeof(struct relay), "aligned_alloc",
			  "struct relay");
		return NULL;
	}

	memset(relay, 0, sizeof(struct relay));
	relay->replica = replica;
	relay->last_row_time = ev_monotonic_now(loop());
	fiber_cond_create(&relay->reader_cond);
	diag_create(&relay->diag);
	stailq_create(&relay->pending_gc);
	relay->state = RELAY_OFF;
	return relay;
}

static void
relay_start(struct relay *relay, int fd, uint64_t sync,
	     void (*stream_write)(struct xstream *, struct xrow_header *))
{
	xstream_create(&relay->stream, stream_write);
	/*
	 * Clear the diagnostics at start, in case it has the old
	 * error message which we keep around to display in
	 * box.info.replication.
	 */
	diag_clear(&relay->diag);
	coio_create(&relay->io, fd);
	relay->sync = sync;
	relay->state = RELAY_FOLLOW;
	relay->row_count = 0;
	relay->last_row_time = ev_monotonic_now(loop());
}

void
relay_cancel(struct relay *relay)
{
	/* Check that the thread is running first. */
	if (relay->cord.id != 0) {
		if (tt_pthread_cancel(relay->cord.id) == ESRCH)
			return;
		tt_pthread_join(relay->cord.id, NULL);
	}
}

/**
 * Called by a relay thread right before termination.
 */
static void
relay_exit(struct relay *relay)
{
	struct errinj *inj = errinj(ERRINJ_RELAY_EXIT_DELAY, ERRINJ_DOUBLE);
	if (inj != NULL && inj->dparam > 0)
		fiber_sleep(inj->dparam);

	/*
	 * Destroy the recovery context. We MUST do it in
	 * the relay thread, because it contains an xlog
	 * cursor, which must be closed in the same thread
	 * that opened it (it uses cord's slab allocator).
	 */
	recovery_delete(relay->r);
	relay->r = NULL;
}

static void
relay_stop(struct relay *relay)
{
	struct relay_gc_msg *gc_msg, *next_gc_msg;
	stailq_foreach_entry_safe(gc_msg, next_gc_msg,
				  &relay->pending_gc, in_pending) {
		free(gc_msg);
	}
	stailq_create(&relay->pending_gc);
	if (relay->r != NULL)
		recovery_delete(relay->r);
	relay->r = NULL;
	relay->state = RELAY_STOPPED;
	/*
	 * Needed to track whether relay thread is running or not
	 * for relay_cancel(). Id is reset to a positive value
	 * upon cord_create().
	 */
	relay->cord.id = 0;
}

void
relay_delete(struct relay *relay)
{
	if (relay->state == RELAY_FOLLOW)
		relay_stop(relay);
	fiber_cond_destroy(&relay->reader_cond);
	diag_destroy(&relay->diag);
	TRASH(relay);
	free(relay);
}

static void
relay_set_cord_name(int fd)
{
	char name[FIBER_NAME_MAX];
	struct sockaddr_storage peer;
	socklen_t addrlen = sizeof(peer);
	if (getpeername(fd, ((struct sockaddr*)&peer), &addrlen) == 0) {
		snprintf(name, sizeof(name), "relay/%s",
			 sio_strfaddr((struct sockaddr *)&peer, addrlen));
	} else {
		snprintf(name, sizeof(name), "relay/<unknown>");
	}
	cord_set_name(name);
}

void
relay_initial_join(int fd, uint64_t sync, struct vclock *vclock)
{
	struct relay *relay = relay_new(NULL);
	if (relay == NULL)
		diag_raise();

	relay_start(relay, fd, sync, relay_send_initial_join_row);
	auto relay_guard = make_scoped_guard([=] {
		relay_stop(relay);
		relay_delete(relay);
	});

	/* Freeze a read view in engines. */
	struct engine_join_ctx ctx;
	engine_prepare_join_xc(&ctx);
	auto join_guard = make_scoped_guard([&] {
		engine_complete_join(&ctx);
	});

	/*
	 * Sync WAL to make sure that all changes visible from
	 * the frozen read view are successfully committed and
	 * obtain corresponding vclock.
	 */
	if (wal_sync(vclock) != 0)
		diag_raise();

	/*
	 * Start sending data only when the latest sync
	 * transaction is confirmed.
	 */
	if (txn_limbo_wait_confirm(&txn_limbo) != 0)
		diag_raise();

	/* Respond to the JOIN request with the current vclock. */
	struct xrow_header row;
	xrow_encode_vclock_xc(&row, vclock);
	row.sync = sync;
	coio_write_xrow(&relay->io, &row);

	/* Send read view to the replica. */
	engine_join_xc(&ctx, &relay->stream);
}

int
relay_final_join_f(va_list ap)
{
	struct relay *relay = va_arg(ap, struct relay *);
	auto guard = make_scoped_guard([=] { relay_exit(relay); });

	coio_enable();
	relay_set_cord_name(relay->io.fd);

	/* Send all WALs until stop_vclock */
	assert(relay->stream.write != NULL);
	recover_remaining_wals(relay->r, &relay->stream,
			       &relay->stop_vclock, true);
	assert(vclock_compare(&relay->r->vclock, &relay->stop_vclock) == 0);
	return 0;
}

void
relay_final_join(int fd, uint64_t sync, struct vclock *start_vclock,
		 struct vclock *stop_vclock)
{
	struct relay *relay = relay_new(NULL);
	if (relay == NULL)
		diag_raise();

	relay_start(relay, fd, sync, relay_send_row);
	auto relay_guard = make_scoped_guard([=] {
		relay_stop(relay);
		relay_delete(relay);
	});

	relay->r = recovery_new(wal_dir(), false, start_vclock);
	vclock_copy(&relay->stop_vclock, stop_vclock);

	int rc = cord_costart(&relay->cord, "final_join",
			      relay_final_join_f, relay);
	if (rc == 0)
		rc = cord_cojoin(&relay->cord);
	if (rc != 0)
		diag_raise();

	ERROR_INJECT(ERRINJ_RELAY_FINAL_JOIN,
		     tnt_raise(ClientError, ER_INJECTION, "relay final join"));

	ERROR_INJECT(ERRINJ_RELAY_FINAL_SLEEP, {
		while (vclock_compare(stop_vclock, &replicaset.vclock) == 0)
			fiber_sleep(0.001);
	});
}

/**
 * The message which updated tx thread with a new vclock has returned back
 * to the relay.
 */
static void
relay_status_update(struct cmsg *msg)
{
	msg->route = NULL;
}

/**
 * Deliver a fresh relay vclock to tx thread.
 */
static void
tx_status_update(struct cmsg *msg)
{
	struct relay_status_msg *status = (struct relay_status_msg *)msg;
	vclock_copy(&status->relay->tx.vclock, &status->vclock);
	struct replication_ack ack;
	ack.source = status->relay->replica->id;
	ack.vclock = &status->vclock;
	/*
	 * Let pending synchronous transactions know, which of
	 * them were successfully sent to the replica. Acks are
	 * collected only by the transactions originator (which is
	 * the single master in 100% so far). Other instances wait
	 * for master's CONFIRM message instead.
	 */
	if (txn_limbo.owner_id == instance_id) {
		txn_limbo_ack(&txn_limbo, ack.source,
			      vclock_get(ack.vclock, instance_id));
	}
	trigger_run(&replicaset.on_ack, &ack);

	static const struct cmsg_hop route[] = {
		{relay_status_update, NULL}
	};
	cmsg_init(msg, route);
	cpipe_push(&status->relay->relay_pipe, msg);
}

/**
 * Update replica gc state in tx thread.
 */
static void
tx_gc_advance(struct cmsg *msg)
{
	struct relay_gc_msg *m = (struct relay_gc_msg *)msg;
	gc_consumer_advance(m->relay->replica->gc, &m->vclock);
	free(m);
}

static int
relay_on_close_log_f(struct trigger *trigger, void * /* event */)
{
	static const struct cmsg_hop route[] = {
		{tx_gc_advance, NULL}
	};
	struct relay *relay = (struct relay *)trigger->data;
	struct relay_gc_msg *m = (struct relay_gc_msg *)malloc(sizeof(*m));
	if (m == NULL) {
		say_warn("failed to allocate relay gc message");
		return 0;
	}
	cmsg_init(&m->msg, route);
	m->relay = relay;
	vclock_copy(&m->vclock, &relay->r->vclock);
	/*
	 * Do not invoke garbage collection until the replica
	 * confirms that it has received data stored in the
	 * sent xlog.
	 */
	stailq_add_tail_entry(&relay->pending_gc, m, in_pending);
	return 0;
}

/**
 * Invoke pending garbage collection requests.
 *
 * This function schedules the most recent gc message whose
 * vclock is less than or equal to the given one. Older
 * messages are discarded as their job will be done by the
 * scheduled message anyway.
 */
static inline void
relay_schedule_pending_gc(struct relay *relay, const struct vclock *vclock)
{
	struct relay_gc_msg *curr, *next, *gc_msg = NULL;
	stailq_foreach_entry_safe(curr, next, &relay->pending_gc, in_pending) {
		/*
		 * We may delete a WAL file only if its vclock is
		 * less than or equal to the vclock acknowledged by
		 * the replica. Even if the replica's signature is
		 * is greater, but the vclocks are incomparable, we
		 * must not delete the WAL, because there may still
		 * be rows not applied by the replica in it while
		 * the greater signatures is due to changes pulled
		 * from other members of the cluster.
		 */
		if (vclock_compare_ignore0(&curr->vclock, vclock) > 0)
			break;
		stailq_shift(&relay->pending_gc);
		free(gc_msg);
		gc_msg = curr;
	}
	if (gc_msg != NULL)
		cpipe_push(&relay->tx_pipe, &gc_msg->msg);
}

static void
relay_set_error(struct relay *relay, struct error *e)
{
	/* Don't override existing error. */
	if (diag_is_empty(&relay->diag))
		diag_set_error(&relay->diag, e);
}

static void
relay_process_wal_event(struct wal_watcher *watcher, unsigned events)
{
	struct relay *relay = container_of(watcher, struct relay, wal_watcher);
	if (fiber_is_cancelled()) {
		/*
		 * The relay is exiting. Rescanning the WAL at this
		 * point would be pointless and even dangerous,
		 * because the relay could have written a packet
		 * fragment to the socket before being cancelled
		 * so that writing another row to the socket would
		 * lead to corrupted replication stream and, as
		 * a result, permanent replication breakdown.
		 */
		return;
	}
	try {
		recover_remaining_wals(relay->r, &relay->stream, NULL,
				       (events & WAL_EVENT_ROTATE) != 0);
	} catch (Exception *e) {
		relay_set_error(relay, e);
		fiber_cancel(fiber());
	}
}

/*
 * Relay reader fiber function.
 * Read xrow encoded vclocks sent by the replica.
 */
int
relay_reader_f(va_list ap)
{
	struct relay *relay = va_arg(ap, struct relay *);
	struct fiber *relay_f = va_arg(ap, struct fiber *);

	struct ibuf ibuf;
	struct ev_io io;
	coio_create(&io, relay->io.fd);
	ibuf_create(&ibuf, &cord()->slabc, 1024);
	try {
		while (!fiber_is_cancelled()) {
			struct xrow_header xrow;
			coio_read_xrow_timeout_xc(&io, &ibuf, &xrow,
					replication_disconnect_timeout());
			/* vclock is followed while decoding, zeroing it. */
			vclock_create(&relay->recv_vclock);
			xrow_decode_vclock_xc(&xrow, &relay->recv_vclock);
			fiber_cond_signal(&relay->reader_cond);
		}
	} catch (Exception *e) {
		relay_set_error(relay, e);
		fiber_cancel(relay_f);
	}
	ibuf_destroy(&ibuf);
	return 0;
}

/**
 * Send a heartbeat message over a connected relay.
 */
static void
relay_send_heartbeat(struct relay *relay)
{
	struct xrow_header row;
	xrow_encode_timestamp(&row, instance_id, ev_now(loop()));
	try {
		relay_send(relay, &row);
	} catch (Exception *e) {
		relay_set_error(relay, e);
		fiber_cancel(fiber());
	}
}

/** A message to set Raft enabled flag in TX thread from a relay thread. */
struct relay_is_raft_enabled_msg {
	/** Base cbus message. */
	struct cmsg base;
	/**
	 * First hop - TX thread, second hop - a relay thread, to notify about
	 * the flag being set.
	 */
	struct cmsg_hop route[2];
	/** Relay pointer to set the flag in. */
	struct relay *relay;
	/** New flag value. */
	bool value;
	/** Flag to wait for the flag being set, in a relay thread. */
	bool is_finished;
};

/** TX thread part of the Raft flag setting, first hop. */
static void
tx_set_is_raft_enabled(struct cmsg *base)
{
	struct relay_is_raft_enabled_msg *msg =
		(struct relay_is_raft_enabled_msg *)base;
	msg->relay->tx.is_raft_enabled = msg->value;
}

/** Relay thread part of the Raft flag setting, second hop. */
static void
relay_set_is_raft_enabled(struct cmsg *base)
{
	struct relay_is_raft_enabled_msg *msg =
		(struct relay_is_raft_enabled_msg *)base;
	msg->is_finished = true;
}

/**
 * Set relay Raft enabled flag from a relay thread to be accessed by the TX
 * thread.
 */
static void
relay_send_is_raft_enabled(struct relay *relay,
			   struct relay_is_raft_enabled_msg *msg, bool value)
{
	msg->route[0].f = tx_set_is_raft_enabled;
	msg->route[0].pipe = &relay->relay_pipe;
	msg->route[1].f = relay_set_is_raft_enabled;
	msg->route[1].pipe = NULL;
	msg->relay = relay;
	msg->value = value;
	msg->is_finished = false;
	cmsg_init(&msg->base, msg->route);
	cpipe_push(&relay->tx_pipe, &msg->base);
	/*
	 * cbus_call() can't be used, because it works only if the sender thread
	 * is a simple cbus_process() loop. But the relay thread is not -
	 * instead it calls cbus_process() manually when ready. And the thread
	 * loop consists of the main fiber wakeup. So cbus_call() would just
	 * hang, because cbus_process() wouldn't be called by the scheduler
	 * fiber.
	 */
	while (!msg->is_finished) {
		cbus_process(&relay->endpoint);
		if (msg->is_finished)
			break;
		fiber_yield();
	}
}

/**
 * A libev callback invoked when a relay client socket is ready
 * for read. This currently only happens when the client closes
 * its socket, and we get an EOF.
 */
static int
relay_subscribe_f(va_list ap)
{
	struct relay *relay = va_arg(ap, struct relay *);
	struct recovery *r = relay->r;

	coio_enable();
	relay_set_cord_name(relay->io.fd);

	/* Create cpipe to tx for propagating vclock. */
	cbus_endpoint_create(&relay->endpoint, tt_sprintf("relay_%p", relay),
			     fiber_schedule_cb, fiber());
	cbus_pair("tx", relay->endpoint.name, &relay->tx_pipe,
		  &relay->relay_pipe, NULL, NULL, cbus_process);

	struct relay_is_raft_enabled_msg raft_enabler;
	if (!relay->replica->anon)
		relay_send_is_raft_enabled(relay, &raft_enabler, true);

	/*
	 * Setup garbage collection trigger.
	 * Not needed for anonymous replicas, since they
	 * aren't registered with gc at all.
	 */
	struct trigger on_close_log;
	trigger_create(&on_close_log, relay_on_close_log_f, relay, NULL);
	if (!relay->replica->anon)
		trigger_add(&r->on_close_log, &on_close_log);

	/* Setup WAL watcher for sending new rows to the replica. */
	wal_set_watcher(&relay->wal_watcher, relay->endpoint.name,
			relay_process_wal_event, cbus_process);

	/* Start fiber for receiving replica acks. */
	char name[FIBER_NAME_MAX];
	snprintf(name, sizeof(name), "%s:%s", fiber()->name, "reader");
	struct fiber *reader = fiber_new_xc(name, relay_reader_f);
	fiber_set_joinable(reader, true);
	fiber_start(reader, relay, fiber());

	/*
	 * If the replica happens to be up to date on subscribe,
	 * don't wait for timeout to happen - send a heartbeat
	 * message right away to update the replication lag as
	 * soon as possible.
	 */
	relay_send_heartbeat(relay);

	/*
	 * Run the event loop until the connection is broken
	 * or an error occurs.
	 */
	while (!fiber_is_cancelled()) {
		double timeout = replication_timeout;
		struct errinj *inj = errinj(ERRINJ_RELAY_REPORT_INTERVAL,
					    ERRINJ_DOUBLE);
		if (inj != NULL && inj->dparam != 0)
			timeout = inj->dparam;

		fiber_cond_wait_deadline(&relay->reader_cond,
					 relay->last_row_time + timeout);

		/*
		 * The fiber can be woken by IO cancel, by a timeout of
		 * status messaging or by an acknowledge to status message.
		 * Handle cbus messages first.
		 */
		cbus_process(&relay->endpoint);
		/* Check for a heartbeat timeout. */
		if (ev_monotonic_now(loop()) - relay->last_row_time > timeout)
			relay_send_heartbeat(relay);
		/*
		 * Check that the vclock has been updated and the previous
		 * status message is delivered
		 */
		if (relay->status_msg.msg.route != NULL)
			continue;
		struct vclock *send_vclock;
		if (relay->version_id < version_id(1, 7, 4))
			send_vclock = &r->vclock;
		else
			send_vclock = &relay->recv_vclock;

		/* Collect xlog files received by the replica. */
		relay_schedule_pending_gc(relay, send_vclock);

		if (vclock_sum(&relay->status_msg.vclock) ==
		    vclock_sum(send_vclock))
			continue;
		static const struct cmsg_hop route[] = {
			{tx_status_update, NULL}
		};
		cmsg_init(&relay->status_msg.msg, route);
		vclock_copy(&relay->status_msg.vclock, send_vclock);
		relay->status_msg.relay = relay;
		cpipe_push(&relay->tx_pipe, &relay->status_msg.msg);
	}

	if (!relay->replica->anon)
		relay_send_is_raft_enabled(relay, &raft_enabler, false);

	/*
	 * Clear garbage collector trigger and WAL watcher.
	 * trigger_clear() does nothing in case the triggers
	 * aren't set (the replica is anonymous).
	 */
	trigger_clear(&on_close_log);
	wal_clear_watcher(&relay->wal_watcher, cbus_process);

	/* Join ack reader fiber. */
	fiber_cancel(reader);
	fiber_join(reader);

	/* Destroy cpipe to tx. */
	cbus_unpair(&relay->tx_pipe, &relay->relay_pipe,
		    NULL, NULL, cbus_process);
	cbus_endpoint_destroy(&relay->endpoint, cbus_process);

	relay_exit(relay);

	/*
	 * Log the error that caused the relay to break the loop.
	 * Don't clear the error for status reporting.
	 */
	assert(!diag_is_empty(&relay->diag));
	diag_set_error(diag_get(), diag_last_error(&relay->diag));
	diag_log();
	say_crit("exiting the relay loop");

	return -1;
}

/** Replication acceptor fiber handler. */
void
relay_subscribe(struct replica *replica, int fd, uint64_t sync,
		struct vclock *replica_clock, uint32_t replica_version_id,
		uint32_t replica_id_filter)
{
	assert(replica->anon || replica->id != REPLICA_ID_NIL);
	struct relay *relay = replica->relay;
	assert(relay->state != RELAY_FOLLOW);
	/*
	 * Register the replica with the garbage collector
	 * unless it has already been registered by initial
	 * join.
	 */
	if (replica->gc == NULL && !replica->anon) {
		replica->gc = gc_consumer_register(replica_clock, "replica %s",
						   tt_uuid_str(&replica->uuid));
		if (replica->gc == NULL)
			diag_raise();
	}

	relay_start(relay, fd, sync, relay_send_row);
	auto relay_guard = make_scoped_guard([=] {
		relay_stop(relay);
		replica_on_relay_stop(replica);
	});

	vclock_copy(&relay->local_vclock_at_subscribe, &replicaset.vclock);
	relay->r = recovery_new(wal_dir(), false, replica_clock);
	vclock_copy(&relay->tx.vclock, replica_clock);
	relay->version_id = replica_version_id;

	relay->id_filter = replica_id_filter;

	int rc = cord_costart(&relay->cord, "subscribe",
			      relay_subscribe_f, relay);
	if (rc == 0)
		rc = cord_cojoin(&relay->cord);
	if (rc != 0)
		diag_raise();
}

static void
relay_send(struct relay *relay, struct xrow_header *packet)
{
	ERROR_INJECT_YIELD(ERRINJ_RELAY_SEND_DELAY);

	packet->sync = relay->sync;
	relay->last_row_time = ev_monotonic_now(loop());
	coio_write_xrow(&relay->io, packet);
	fiber_gc();

	/*
	 * It may happen that the socket is always ready for write, so yield
	 * explicitly every now and then to not block the event loop.
	 */
	if (++relay->row_count % WAL_ROWS_PER_YIELD == 0)
		fiber_sleep(0);

	struct errinj *inj = errinj(ERRINJ_RELAY_TIMEOUT, ERRINJ_DOUBLE);
	if (inj != NULL && inj->dparam > 0)
		fiber_sleep(inj->dparam);
}

static void
relay_send_initial_join_row(struct xstream *stream, struct xrow_header *row)
{
	struct relay *relay = container_of(stream, struct relay, stream);
	/*
	 * Ignore replica local requests as we don't need to promote
	 * vclock while sending a snapshot.
	 */
	if (row->group_id != GROUP_LOCAL)
		relay_send(relay, row);
}

/**
 * Recreate recovery cursor from the last confirmed point. That is
 * used by Raft, when the node becomes a leader. It may happen,
 * that it already sent some data to other nodes as a follower,
 * and they ignored the data. Now when the node is a leader, it
 * should send the not confirmed data again. Otherwise the cluster
 * will stuck, or worse - the newer data would be sent without the
 * older sent but ignored data.
 */
static void
relay_restart_recovery(struct relay *relay)
{
	/*
	 * Need to keep the 0 component unchanged. The remote vclock has it
	 * unset, because it is a local LSN, and it is not sent between the
	 * instances. Nonetheless, the recovery procedures still need the local
	 * LSN set to find a proper xlog to start from. If it is not set, the
	 * recovery process will treat it as 0, and will try to find an xlog
	 * file having the first local row. Which of course may not exist for a
	 * long time already. And then it will fail with an xlog gap error.
	 *
	 * The currently used local LSN is ok to use, even if it is outdated a
	 * bit. Because the existing recovery object keeps the current xlog file
	 * not deleted thanks to xlog GC subsystem. So by using the current
	 * local LSN and the last confirmed remote LSNs we can be sure such an
	 * xlog file still exists.
	 */
	struct vclock restart_vclock;
	vclock_copy(&restart_vclock, &relay->recv_vclock);
	vclock_reset(&restart_vclock, 0, vclock_get(&relay->r->vclock, 0));
	struct recovery *r = recovery_new(wal_dir(), false, &restart_vclock);
	rlist_swap(&relay->r->on_close_log, &r->on_close_log);
	recovery_delete(relay->r);
	relay->r = r;
	recover_remaining_wals(relay->r, &relay->stream, NULL, true);
}

struct relay_raft_msg {
	struct cmsg base;
	struct cmsg_hop route;
	struct raft_request req;
	struct vclock vclock;
	struct relay *relay;
};

static void
relay_raft_msg_push(struct cmsg *base)
{
	struct relay_raft_msg *msg = (struct relay_raft_msg *)base;
	struct xrow_header row;
	xrow_encode_raft(&row, &fiber()->gc, &msg->req);
	try {
		/*
		 * Send the message before restarting the recovery. Otherwise
		 * all the rows would be sent from under a non-leader role and
		 * would be ignored again.
		 */
		relay_send(msg->relay, &row);
		if (msg->req.state == RAFT_STATE_LEADER)
			relay_restart_recovery(msg->relay);
	} catch (Exception *e) {
		relay_set_error(msg->relay, e);
		fiber_cancel(fiber());
	}
	free(msg);
}

void
relay_push_raft(struct relay *relay, const struct raft_request *req)
{
	/*
	 * Raft updates don't stack. They are thrown away if can't be pushed
	 * now. This is fine, as long as relay's live much longer that the
	 * timeouts in Raft are set.
	 */
	if (!relay->tx.is_raft_enabled)
		return;
	/*
	 * XXX: the message should be preallocated. It should
	 * work like Kharon in IProto. Relay should have 2 raft
	 * messages rotating. When one is sent, the other can be
	 * updated and a flag is set. When the first message is
	 * sent, the control returns to TX thread, sees the set
	 * flag, rotates the buffers, and sends it again. And so
	 * on. This is how it can work in future, with 0 heap
	 * allocations. Current solution with alloc-per-update is
	 * good enough as a start. Another option - wait until all
	 * is moved to WAL thread, where this will all happen
	 * in one thread and will be much simpler.
	 */
	struct relay_raft_msg *msg =
		(struct relay_raft_msg *)malloc(sizeof(*msg));
	if (msg == NULL) {
		panic("Couldn't allocate raft message");
		return;
	}
	msg->req = *req;
	if (req->vclock != NULL) {
		msg->req.vclock = &msg->vclock;
		vclock_copy(&msg->vclock, req->vclock);
	}
	msg->route.f = relay_raft_msg_push;
	msg->route.pipe = NULL;
	cmsg_init(&msg->base, &msg->route);
	msg->relay = relay;
	cpipe_push(&relay->relay_pipe, &msg->base);
}

/** Send a single row to the client. */
static void
relay_send_row(struct xstream *stream, struct xrow_header *packet)
{
	struct relay *relay = container_of(stream, struct relay, stream);
	if (packet->group_id == GROUP_LOCAL) {
		/*
		 * We do not relay replica-local rows to other
		 * instances, since we started signing them with
		 * a zero instance id. However, if replica-local
		 * rows, signed with a non-zero id are present in
		 * our WAL, we still need to relay them as NOPs in
		 * order to correctly promote the vclock on the
		 * replica.
		 */
		if (packet->replica_id == REPLICA_ID_NIL)
			return;
		packet->type = IPROTO_NOP;
		packet->group_id = GROUP_DEFAULT;
		packet->bodycnt = 0;
	}
	assert(iproto_type_is_dml(packet->type) ||
	       iproto_type_is_synchro_request(packet->type));
	/* Check if the rows from the instance are filtered. */
	if ((1 << packet->replica_id & relay->id_filter) != 0)
		return;
	/*
	 * We're feeding a WAL, thus responding to FINAL JOIN or SUBSCRIBE
	 * request. If this is FINAL JOIN (i.e. relay->replica is NULL),
	 * we must relay all rows, even those originating from the replica
	 * itself (there may be such rows if this is rebootstrap). If this
	 * SUBSCRIBE, only send a row if it is not from the same replica
	 * (i.e. don't send replica's own rows back) or if this row is
	 * missing on the other side (i.e. in case of sudden power-loss,
	 * data was not written to WAL, so remote master can't recover
	 * it). In the latter case packet's LSN is less than or equal to
	 * local master's LSN at the moment it received 'SUBSCRIBE' request.
	 */
	if (relay->replica == NULL ||
	    packet->replica_id != relay->replica->id ||
	    packet->lsn <= vclock_get(&relay->local_vclock_at_subscribe,
				      packet->replica_id)) {
		struct errinj *inj = errinj(ERRINJ_RELAY_BREAK_LSN,
					    ERRINJ_INT);
		if (inj != NULL && packet->lsn == inj->iparam) {
			packet->lsn = inj->iparam - 1;
			say_warn("injected broken lsn: %lld",
				 (long long) packet->lsn);
		}
		relay_send(relay, packet);
	}
}
