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
#include "trivia/util.h" /** static_assert */
#include "tt_static.h"
#include "scoped_guard.h"
#include "cbus.h"
#include "errinj.h"
#include "fiber.h"
#include "memory.h"
#include "say.h"

#include "coio.h"
#include "coio_task.h"
#include "engine.h"
#include "gc.h"
#include "iostream.h"
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

#include <stdlib.h>

/**
 * Cbus message to send status updates from relay to tx thread.
 */
struct relay_status_msg {
	/** Parent */
	struct cmsg msg;
	/** Relay instance */
	struct relay *relay;
	/** Replica's last known raft term. */
	uint64_t term;
	/** Replica vclock. */
	struct vclock vclock;
	/** Last replicated transaction timestamp. */
	double txn_lag;
	/** Last vclock sync received in replica's response. */
	uint64_t vclock_sync;
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

/**
 * Cbus message to push raft messages to relay.
 */
struct relay_raft_msg {
	struct cmsg base;
	struct cmsg_hop route[2];
	struct raft_request req;
	struct vclock vclock;
	struct relay *relay;
};


/** State of a replication relay. */
struct relay {
	/** Replica connection */
	struct iostream *io;
	/** Request sync */
	uint64_t sync;
	/** Last ACK sent to the replica. */
	struct relay_heartbeat last_sent_ack;
	/** Last ACK received from the replica. */
	struct applier_heartbeat last_recv_ack;
	/** Recovery instance to read xlog from the disk */
	struct recovery *r;
	/** Xstream argument to recovery */
	struct xstream stream;
	/** A region used to save rows when collecting transactions. */
	struct lsregion lsregion;
	/** A monotonically growing identifier for lsregion allocations. */
	int64_t lsr_id;
	/** The tsn of the currently read transaction. */
	int64_t read_tsn;
	/** A list of rows making up the currently read transaction. */
	struct rlist current_tx;
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
	/** Replicatoin slave version. */
	uint32_t version_id;
	/**
	 * The biggest Raft term that node has broadcasted. Used to synchronize
	 * Raft term (from tx thread) and PROMOTE (from WAL) dispatch.
	 */
	uint64_t sent_raft_term;
	/**
	 * A filter of replica ids whose rows should be ignored.
	 * Each set filter bit corresponds to a replica id whose
	 * rows shouldn't be relayed. The list of ids to ignore
	 * is passed by the replica on subscribe.
	 */
	uint32_t id_filter;
	/**
	 * Local vclock at the moment of subscribe, used to check
	 * dataset on the other side and send missing data rows if any.
	 */
	struct vclock local_vclock_at_subscribe;

	/** Endpoint to receive messages from WAL. */
	struct cbus_endpoint wal_endpoint;
	/**
	 * Endpoint to receive messages from TX. Having the 2 endpoints
	 * separated helps to synchronize the data coming from TX and WAL. Such
	 * as term bumps from TX with PROMOTE rows from WAL.
	 */
	struct cbus_endpoint tx_endpoint;
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
	/** Time when last row was sent to the peer. */
	double last_row_time;
	/** Time when last heartbeat was sent to the peer. */
	double last_heartbeat_time;
	/** Time of last communication with the tx thread. */
	double tx_seen_time;
	/**
	 * A time difference between the moment when we
	 * wrote a transaction to the local WAL and when
	 * this transaction has been replicated to remote
	 * node (ie written to node's WAL) so that ACK get
	 * received.
	 */
	double txn_lag;
	/** Relay sync state. */
	enum relay_state state;
	/** Whether relay should speed up the next heartbeat dispatch. */
	bool need_new_vclock_sync;
	struct {
		/* Align to prevent false-sharing with tx thread */
		alignas(CACHELINE_SIZE)
		/** Known relay vclock. */
		struct vclock vclock;
		/**
		 * Transaction downstream lag to be accessed
		 * from TX thread only.
		 */
		double txn_lag;
		/** Known vclock sync received in response from replica. */
		uint64_t vclock_sync;
		/**
		 * True if the relay is ready to accept messages via the cbus.
		 */
		bool is_paired;
		/**
		 * A pair of raft messages travelling between tx and relay
		 * threads. While one is en route, the other is ready to save
		 * the next incoming raft message.
		 */
		struct relay_raft_msg raft_msgs[2];
		/**
		 * Id of the raft message waiting in tx thread and ready to
		 * save Raft requests. May be either 0 or 1.
		 */
		int raft_ready_msg;
		/** Whether raft_ready_msg holds a saved Raft message */
		bool is_raft_push_pending;
		/**
		 * Whether any of the messages is en route between tx and
		 * relay.
		 */
		bool is_raft_push_sent;
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

double
relay_txn_lag(const struct relay *relay)
{
	return relay->tx.txn_lag;
}

static void
relay_send(struct relay *relay, struct xrow_header *packet);
static void
relay_send_initial_join_row(struct xstream *stream, struct xrow_header *row);

/** One iteration of the subscription loop - bump heartbeats, TX endpoints. */
static void
relay_subscribe_update(struct relay *relay);

/** Process a single row from the WAL stream. */
static void
relay_process_row(struct xstream *stream, struct xrow_header *row);

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
	/*
	 * According to posix_memalign requirements, align must be
	 * multiple of sizeof(void *).
	 */
	static_assert(alignof(struct relay) % sizeof(void *) == 0,
		      "align for posix_memalign function must be "
		      "multiple of sizeof(void *)");
	struct relay *relay = NULL;
	if (posix_memalign((void **)&relay, alignof(struct relay),
			   sizeof(struct relay)) != 0) {
		diag_set(OutOfMemory, sizeof(struct relay), "aligned_alloc",
			  "struct relay");
		return NULL;
	}
	assert(relay != NULL);

	memset(relay, 0, sizeof(struct relay));
	relay->replica = replica;
	relay->last_row_time = ev_monotonic_now(loop());
	fiber_cond_create(&relay->reader_cond);
	diag_create(&relay->diag);
	stailq_create(&relay->pending_gc);
	relay->state = RELAY_OFF;
	return relay;
}

/** A callback recovery calls every now and then to unblock the event loop. */
static void
relay_yield(struct xstream *stream)
{
	(void) stream;
	fiber_sleep(0);
}

static void
relay_send_heartbeat_on_timeout(struct relay *relay);

/** A callback for recovery to send heartbeats while scanning a WAL. */
static void
relay_subscribe_on_wal_yield_f(struct xstream *stream)
{
	struct relay *relay = container_of(stream, struct relay, stream);
	relay_subscribe_update(relay);
	fiber_sleep(0);
}

static void
relay_start(struct relay *relay, struct iostream *io, uint64_t sync,
	     void (*stream_write)(struct xstream *, struct xrow_header *),
	     void (*stream_cb)(struct xstream *), uint64_t sent_raft_term)
{
	xstream_create(&relay->stream, stream_write, stream_cb);
	/*
	 * Clear the diagnostics at start, in case it has the old
	 * error message which we keep around to display in
	 * box.info.replication.
	 */
	diag_clear(&relay->diag);
	relay->io = io;
	relay->sync = sync;
	relay->state = RELAY_FOLLOW;
	relay->sent_raft_term = sent_raft_term;
	relay->need_new_vclock_sync = false;
	relay->last_row_time = ev_monotonic_now(loop());
	relay->tx_seen_time = relay->last_row_time;
	relay->last_heartbeat_time = relay->last_row_time;
	/* Never send rows for REPLICA_ID_NIL to anyone */
	relay->id_filter = 1 << REPLICA_ID_NIL;
	memset(&relay->status_msg, 0, sizeof(relay->status_msg));
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
	lsregion_destroy(&relay->lsregion);
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
	relay->io = NULL;
	if (relay->r != NULL)
		recovery_delete(relay->r);
	relay->r = NULL;
	relay->state = RELAY_STOPPED;
	/*
	 * If relay is stopped then lag statistics should
	 * be updated on next new ACK packets obtained.
	 */
	relay->txn_lag = 0;
	relay->tx.txn_lag = 0;
	relay->tx.vclock_sync = 0;
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

static void
relay_cord_init(struct relay *relay)
{
	coio_enable();
	relay_set_cord_name(relay->io->fd);
	lsregion_create(&relay->lsregion, &runtime);
	relay->lsr_id = 0;
	relay->read_tsn = 0;
	rlist_create(&relay->current_tx);
}

void
relay_initial_join(struct iostream *io, uint64_t sync, struct vclock *vclock,
		   uint32_t replica_version_id)
{
	struct relay *relay = relay_new(NULL);
	if (relay == NULL)
		diag_raise();

	relay_start(relay, io, sync, relay_send_initial_join_row, relay_yield,
		    UINT64_MAX);
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

	struct synchro_request req;
	struct raft_request raft_req;
	struct vclock limbo_vclock;
	txn_limbo_checkpoint(&txn_limbo, &req, &limbo_vclock);
	box_raft_checkpoint_local(&raft_req);

	/* Respond to the JOIN request with the current vclock. */
	struct xrow_header row;
	RegionGuard region_guard(&fiber()->gc);
	xrow_encode_vclock(&row, vclock);
	row.sync = sync;
	coio_write_xrow(relay->io, &row);

	/*
	 * Version is present starting with 2.7.3, 2.8.2, 2.9.1
	 * All these versions know of additional META stage of initial join.
	 */
	if (replica_version_id > 0) {
		/* Mark the beginning of the metadata stream. */
		xrow_encode_type(&row, IPROTO_JOIN_META);
		xstream_write(&relay->stream, &row);

		xrow_encode_raft(&row, &fiber()->gc, &raft_req);
		xstream_write(&relay->stream, &row);

		char body[XROW_BODY_LEN_MAX];
		xrow_encode_synchro(&row, body, &req);
		row.replica_id = req.replica_id;
		xstream_write(&relay->stream, &row);

		/* Mark the end of the metadata stream. */
		xrow_encode_type(&row, IPROTO_JOIN_SNAPSHOT);
		xstream_write(&relay->stream, &row);
	}

	/* Send read view to the replica. */
	engine_join_xc(&ctx, &relay->stream);
}

int
relay_final_join_f(va_list ap)
{
	struct relay *relay = va_arg(ap, struct relay *);
	auto guard = make_scoped_guard([=] { relay_exit(relay); });

	relay_cord_init(relay);

	/* Send all WALs until stop_vclock */
	assert(relay->stream.write != NULL);
	recover_remaining_wals(relay->r, &relay->stream,
			       &relay->stop_vclock, true);
	assert(vclock_compare(&relay->r->vclock, &relay->stop_vclock) == 0);
	return 0;
}

void
relay_final_join(struct replica *replica, struct iostream *io, uint64_t sync,
		 const struct vclock *start_vclock,
		 const struct vclock *stop_vclock)
{
	/*
	 * As a new thread is started for the final join stage, its cancellation
	 * should be handled properly during an unexpected shutdown, so, we
	 * reuse the subscribe relay in order to cancel the final join thread
	 * during replication_free().
	 */
	struct relay *relay = replica->relay;
	assert(relay->state != RELAY_FOLLOW);

	relay_start(relay, io, sync, relay_process_row, relay_yield,
		    UINT64_MAX);
	auto relay_guard = make_scoped_guard([=] {
		relay_stop(relay);
	});
	/*
	 * Save the first vclock as 'received'. Because it was really received.
	 */
	vclock_copy_ignore0(&relay->last_recv_ack.vclock, start_vclock);
	relay->r = recovery_new(wal_dir(), false, start_vclock);
	vclock_copy(&relay->stop_vclock, stop_vclock);

	struct cord cord;
	int rc = cord_costart(&cord, "final_join", relay_final_join_f, relay);
	if (rc == 0)
		rc = cord_cojoin(&cord);
	if (rc != 0)
		diag_raise();

	ERROR_INJECT(ERRINJ_RELAY_FINAL_JOIN,
		     tnt_raise(ClientError, ER_INJECTION, "relay final join"));

	ERROR_INJECT(ERRINJ_RELAY_FINAL_SLEEP, {
		while (vclock_compare(stop_vclock, &replicaset.vclock) == 0)
			fiber_sleep(0.001);
	});
}

/** Check if status update is needed and send it if possible. */
static void
relay_check_status_needs_update(struct relay *relay);

/**
 * The message which updated tx thread with a new vclock has returned back
 * to the relay.
 */
static void
relay_status_update(struct cmsg *msg)
{
	msg->route = NULL;
	struct relay_status_msg *status = (struct relay_status_msg *)msg;
	struct relay *relay = status->relay;
	relay->tx_seen_time = ev_monotonic_now(loop());
	relay_check_status_needs_update(relay);
}

/**
 * Deliver a fresh relay vclock to tx thread.
 */
static void
tx_status_update(struct cmsg *msg)
{
	struct relay_status_msg *status = (struct relay_status_msg *)msg;
	struct relay *relay = status->relay;
	vclock_copy(&relay->tx.vclock, &status->vclock);
	relay->tx.txn_lag = status->txn_lag;
	relay->tx.vclock_sync = status->vclock_sync;

	struct replication_ack ack;
	ack.source = status->relay->replica->id;
	ack.vclock = &status->vclock;
	ack.vclock_sync = status->vclock_sync;
	bool anon = status->relay->replica->anon;
	/*
	 * It is important to process the term first and freeze the limbo before
	 * an ACK if the term was bumped. This is because majority of the
	 * cluster might be already living in a new term and this ACK is coming
	 * from one of such nodes. It means that the row was written on the
	 * replica but can't CONFIRM/ROLLBACK - the old term has ended, new one
	 * has no result yet, need a PROMOTE.
	 */
	raft_process_term(box_raft(), status->term, ack.source);
	/*
	 * Let pending synchronous transactions know, which of
	 * them were successfully sent to the replica. Acks are
	 * collected only by the transactions originator (which is
	 * the single master in 100% so far). Other instances wait
	 * for master's CONFIRM message instead.
	 */
	if (txn_limbo.owner_id == instance_id && !anon) {
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
	gc_consumer_update_async(&m->relay->replica->uuid, &m->vclock);
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

/** Process the last received ACK from applier. */
static void
relay_process_ack(struct relay *relay, double tm)
{
	if (tm == 0)
		return;
	/*
	 * Replica sends us last replicated transaction timestamp which is
	 * needed for relay lag monitoring. Note that this transaction has been
	 * written to WAL with our current realtime clock value, thus when it
	 * get reported back we can compute time spent regardless of the clock
	 * value on remote replica. Update the lag only when the timestamp
	 * corresponds to some transaction the replica has just applied, i.e.
	 * received vclock is bigger than the previous one.
	 */
	const struct vclock *prev_vclock = &relay->status_msg.vclock;
	const struct vclock *next_vclock = &relay->last_recv_ack.vclock;
	/*
	 * Both vclocks are confirmed by the same applier, sequentially. They
	 * can't go down.
	 */
	assert(vclock_compare(prev_vclock, next_vclock) <= 0);
	if (vclock_compare_ignore0(prev_vclock, next_vclock) < 0)
		relay->txn_lag = ev_now(loop()) - tm;
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
	ibuf_create(&ibuf, &cord()->slabc, 1024);
	struct applier_heartbeat *last_recv_ack = &relay->last_recv_ack;
	try {
		while (!fiber_is_cancelled()) {
			FiberGCChecker gc_check;
			struct xrow_header xrow;
			ERROR_INJECT_YIELD(ERRINJ_RELAY_READ_ACK_DELAY);
			coio_read_xrow_timeout_xc(relay->io, &ibuf, &xrow,
					replication_disconnect_timeout());
			xrow_decode_applier_heartbeat_xc(&xrow, last_recv_ack);
			relay_process_ack(relay, xrow.tm);
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
static inline void
relay_send_heartbeat(struct relay *relay)
{
	struct xrow_header row;
	try {
		++relay->last_sent_ack.vclock_sync;
		RegionGuard region_guard(&fiber()->gc);
		xrow_encode_relay_heartbeat(&row, &relay->last_sent_ack);
		/*
		 * Do not encode timestamp if this heartbeat is sent in between
		 * data rows so as to not affect replica's upstream lag.
		 */
		if (relay->last_row_time > relay->last_heartbeat_time)
			row.tm = 0;
		else
			row.tm = ev_now(loop());
		row.replica_id = instance_id;
		relay->last_heartbeat_time = ev_monotonic_now(loop());
		relay_send(relay, &row);
		relay->need_new_vclock_sync = false;
	} catch (Exception *e) {
		relay_set_error(relay, e);
		fiber_cancel(fiber());
	}
}

/**
 * Check whether a new heartbeat message should be sent and send it
 * in case it's required.
 */
static inline void
relay_send_heartbeat_on_timeout(struct relay *relay)
{
	double now = ev_monotonic_now(loop());
	/*
	 * Do not send a message when it was just sent or when tx thread is
	 * unresponsive.
	 * Waiting for a replication_disconnect_timeout before declaring tx
	 * thread unresponsive helps fight leader disruptions: followers start
	 * counting down replication_disconnect_timeout only when the same
	 * timeout already passes on the leader, meaning tx thread hang will be
	 * noticed twice as late compared to a usual failure, like a crash or
	 * network error. IOW transient hangs are tolerated without leader
	 * switchover.
	 */
	if (!relay->need_new_vclock_sync &&
	    (now - relay->last_heartbeat_time <= replication_timeout ||
	    now - relay->tx_seen_time >= replication_disconnect_timeout()))
		return;
	relay_send_heartbeat(relay);
}

static void
relay_push_raft_msg(struct relay *relay)
{
	bool is_raft_enabled = relay->tx.is_paired && !relay->replica->anon &&
			       relay->version_id >= version_id(2, 6, 0);
	if (!is_raft_enabled || relay->tx.is_raft_push_sent)
		return;
	struct relay_raft_msg *msg =
		&relay->tx.raft_msgs[relay->tx.raft_ready_msg];
	cpipe_push(&relay->relay_pipe, &msg->base);
	relay->tx.raft_ready_msg = (relay->tx.raft_ready_msg + 1) % 2;
	relay->tx.is_raft_push_sent = true;
	relay->tx.is_raft_push_pending = false;
}

/** A notification that relay thread is ready to process cbus messages. */
static void
relay_thread_on_start(void *arg)
{
	struct relay *relay = (struct relay *)arg;
	relay->tx.is_paired = true;
	if (!relay->replica->anon && relay->version_id >= version_id(2, 6, 0)) {
		/*
		 * Send saved raft message as soon as relay becomes operational.
		 */
		if (relay->tx.is_raft_push_pending)
			relay_push_raft_msg(relay);
	}
	trigger_run(&replicaset.on_relay_thread_start, relay->replica);
}

/** A notification about relay detach from the cbus. */
static void
relay_thread_on_stop(void *arg)
{
	struct relay *relay = (struct relay *)arg;
	relay->tx.is_paired = false;
}

/** The trigger_vclock_sync call message. */
struct relay_trigger_vclock_sync_msg {
	/** Parent cbus message. */
	struct cbus_call_msg base;
	/** The queried relay. */
	struct relay *relay;
	/** Sync value returned from relay. */
	uint64_t vclock_sync;
};

/** A callback to free the message once it returns to tx thread. */
static int
relay_trigger_vclock_sync_msg_free(struct cbus_call_msg *msg)
{
	free(msg);
	return 0;
}

/** Relay side of the trigger_vclock_sync call. */
static int
relay_trigger_vclock_sync_f(struct cbus_call_msg *msg)
{
	struct relay_trigger_vclock_sync_msg *m =
		(struct relay_trigger_vclock_sync_msg *)msg;
	m->vclock_sync = m->relay->last_sent_ack.vclock_sync + 1;
	m->relay->need_new_vclock_sync = true;
	return 0;
}

int
relay_trigger_vclock_sync(struct relay *relay, uint64_t *vclock_sync,
			  double deadline)
{
	if (!relay->tx.is_paired)
		return 0;
	struct relay_trigger_vclock_sync_msg *msg =
		(struct relay_trigger_vclock_sync_msg *)xmalloc(sizeof(*msg));
	msg->relay = relay;
	double timeout = deadline - ev_monotonic_now(loop());
	if (cbus_call_timeout(&relay->relay_pipe, &relay->tx_pipe, &msg->base,
			      relay_trigger_vclock_sync_f,
			      relay_trigger_vclock_sync_msg_free, timeout) < 0)
		return -1;
	*vclock_sync = msg->vclock_sync;
	free(msg);
	return 0;
}

static void
relay_check_status_needs_update(struct relay *relay)
{
	struct applier_heartbeat *last_recv_ack = &relay->last_recv_ack;
	struct relay_status_msg *status_msg = &relay->status_msg;
	if (status_msg->msg.route != NULL)
		return;

	struct vclock *send_vclock;
	if (relay->version_id < version_id(1, 7, 4))
		send_vclock = &relay->r->vclock;
	else
		send_vclock = &last_recv_ack->vclock;

	/* Collect xlog files received by the replica. */
	relay_schedule_pending_gc(relay, send_vclock);

	double tx_idle = ev_monotonic_now(loop()) - relay->tx_seen_time;
	if (vclock_sum(&status_msg->vclock) ==
	    vclock_sum(send_vclock) && tx_idle <= replication_timeout &&
	    status_msg->vclock_sync == last_recv_ack->vclock_sync)
		return;
	static const struct cmsg_hop route[] = {
		{tx_status_update, NULL}
	};
	cmsg_init(&status_msg->msg, route);
	vclock_copy(&status_msg->vclock, send_vclock);
	status_msg->txn_lag = relay->txn_lag;
	status_msg->relay = relay;
	status_msg->term = last_recv_ack->term;
	status_msg->vclock_sync = last_recv_ack->vclock_sync;
	cpipe_push(&relay->tx_pipe, &status_msg->msg);
}

static void
relay_subscribe_update(struct relay *relay)
{
	/*
	 * The fiber can be woken by IO cancel, by a timeout of status messaging
	 * or by an acknowledge to status message. Handle cbus messages first.
	 */
	struct errinj *inj = errinj(ERRINJ_RELAY_FROM_TX_DELAY, ERRINJ_BOOL);
	if (inj == NULL || !inj->bparam)
		cbus_process(&relay->tx_endpoint);
	relay_send_heartbeat_on_timeout(relay);
	relay_check_status_needs_update(relay);
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

	relay_cord_init(relay);

	cbus_endpoint_create(&relay->tx_endpoint,
			     tt_sprintf("relay_tx_%p", relay),
			     fiber_schedule_cb, fiber());
	cbus_pair("tx", relay->tx_endpoint.name, &relay->tx_pipe,
		  &relay->relay_pipe, relay_thread_on_start, relay,
		  cbus_process);

	cbus_endpoint_create(&relay->wal_endpoint,
			     tt_sprintf("relay_wal_%p", relay),
			     fiber_schedule_cb, fiber());

	/*
	 * Setup garbage collection trigger.
	 * Not needed for anonymous replicas, since they
	 * aren't registered with gc at all.
	 */
	struct trigger on_close_log;
	trigger_create(&on_close_log, relay_on_close_log_f, relay, NULL);
	if (!relay->replica->anon)
		trigger_add(&relay->r->on_close_log, &on_close_log);

	/* Setup WAL watcher for sending new rows to the replica. */
	struct errinj *inj = errinj(ERRINJ_RELAY_WAL_START_DELAY, ERRINJ_BOOL);
	while (inj != NULL && inj->bparam) {
		fiber_sleep(0.01);
		xstream_yield(&relay->stream);
	}
	wal_set_watcher(&relay->wal_watcher, relay->wal_endpoint.name,
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
	inj = errinj(ERRINJ_RELAY_REPORT_INTERVAL, ERRINJ_DOUBLE);
	while (!fiber_is_cancelled()) {
		FiberGCChecker gc_check;
		double timeout = replication_timeout;
		if (inj != NULL && inj->dparam != 0)
			timeout = inj->dparam;

		fiber_cond_wait_deadline(&relay->reader_cond,
					 relay->last_row_time + timeout);
		cbus_process(&relay->wal_endpoint);
		relay_subscribe_update(relay);
	}

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
		    relay_thread_on_stop, relay, cbus_process);
	cbus_endpoint_destroy(&relay->wal_endpoint, cbus_process);
	cbus_endpoint_destroy(&relay->tx_endpoint, cbus_process);

	relay_exit(relay);

	/*
	 * Log the error that caused the relay to break the loop.
	 * Don't clear the error for status reporting.
	 */
	assert(!diag_is_empty(&relay->diag));
	diag_set_error(diag_get(), diag_last_error(&relay->diag));
	diag_log();
	say_info("exiting the relay loop");

	return -1;
}

/** Replication acceptor fiber handler. */
void
relay_subscribe(struct replica *replica, struct iostream *io, uint64_t sync,
		const struct vclock *start_vclock, uint32_t replica_version_id,
		uint32_t replica_id_filter, uint64_t sent_raft_term)
{
	assert(replica->anon || replica->id != REPLICA_ID_NIL);
	struct relay *relay = replica->relay;
	assert(relay->state != RELAY_FOLLOW);
	if (replica_version_id < version_id(2, 6, 0) || replica->anon)
		sent_raft_term = UINT64_MAX;
	relay_start(relay, io, sync, relay_process_row,
		    relay_subscribe_on_wal_yield_f, sent_raft_term);
	replica_on_relay_follow(replica);
	auto relay_guard = make_scoped_guard([=] {
		relay_stop(relay);
		replica_on_relay_stop(replica);
	});

	vclock_copy(&relay->local_vclock_at_subscribe, &replicaset.vclock);
	/*
	 * Save the first vclock as 'received'. Because it was really received.
	 */
	vclock_copy_ignore0(&relay->last_recv_ack.vclock, start_vclock);
	relay->r = recovery_new(wal_dir(), false, start_vclock);
	vclock_copy_ignore0(&relay->tx.vclock, start_vclock);
	relay->version_id = replica_version_id;
	relay->id_filter |= replica_id_filter;

	struct cord cord;
	int rc = cord_costart(&cord, "subscribe", relay_subscribe_f, relay);
	if (rc == 0)
		rc = cord_cojoin(&cord);
	if (rc != 0)
		diag_raise();
}

static void
relay_send(struct relay *relay, struct xrow_header *packet)
{
	ERROR_INJECT_YIELD(ERRINJ_RELAY_SEND_DELAY);

	packet->sync = relay->sync;
	relay->last_row_time = ev_monotonic_now(loop());
	coio_write_xrow(relay->io, packet);

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
 * Send a Raft message to the peer. This is done asynchronously, out of scope
 * of recover_remaining_wals loop.
 */
static void
relay_raft_msg_push(struct cmsg *base)
{
	struct relay_raft_msg *msg = (struct relay_raft_msg *)base;
	struct xrow_header row;
	RegionGuard region_guard(&fiber()->gc);
	xrow_encode_raft(&row, &fiber()->gc, &msg->req);
	try {
		relay_send(msg->relay, &row);
		msg->relay->sent_raft_term = msg->req.term;
	} catch (Exception *e) {
		relay_set_error(msg->relay, e);
		fiber_cancel(fiber());
	}
}

static void
tx_raft_msg_return(struct cmsg *base)
{
	struct relay_raft_msg *msg = (struct relay_raft_msg *)base;
	msg->relay->tx.is_raft_push_sent = false;
	if (msg->relay->tx.is_raft_push_pending)
		relay_push_raft_msg(msg->relay);
}

void
relay_push_raft(struct relay *relay, const struct raft_request *req)
{
	struct relay_raft_msg *msg =
		&relay->tx.raft_msgs[relay->tx.raft_ready_msg];
	/*
	 * Overwrite the request in raft_ready_msg. Only the latest raft request
	 * is saved.
	 */
	msg->req = *req;
	if (req->vclock != NULL) {
		msg->req.vclock = &msg->vclock;
		vclock_copy(&msg->vclock, req->vclock);
	}
	msg->route[0].f = relay_raft_msg_push;
	msg->route[0].pipe = &relay->tx_pipe;
	msg->route[1].f = tx_raft_msg_return;
	msg->route[1].pipe = NULL;
	cmsg_init(&msg->base, msg->route);
	msg->relay = relay;
	relay->tx.is_raft_push_pending = true;
	relay_push_raft_msg(relay);
}

/** Check if a row should be sent to a remote replica. */
static bool
relay_filter_row(struct relay *relay, struct xrow_header *packet)
{
	assert(fiber()->f == relay_subscribe_f ||
	       fiber()->f == relay_final_join_f);
	bool is_subscribe = fiber()->f == relay_subscribe_f;
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
	if ((1 << packet->replica_id & relay->id_filter) != 0) {
		return false;
	} else if (is_subscribe && packet->replica_id == relay->replica->id &&
		   packet->lsn > vclock_get(&relay->local_vclock_at_subscribe,
					    packet->replica_id)) {
		/*
		 * Knowing that recovery goes for LSNs in ascending order,
		 * filter out this replica id to skip the expensive check above.
		 * It'll always be true from now on for this relay.
		 */
		relay->id_filter |= 1 << packet->replica_id;
		return false;
	}

	if (packet->group_id == GROUP_LOCAL) {
		/*
		 * All packets with REPLICA_ID_NIL are filtered out by
		 * id_filter. The remaining ones are from old tarantool
		 * versions, when local rows went by normal replica id. We have
		 * to relay them as NOPs for the sake of vclock convergence.
		 */
		assert(packet->replica_id != REPLICA_ID_NIL);
		packet->type = IPROTO_NOP;
		packet->group_id = GROUP_DEFAULT;
		packet->bodycnt = 0;
	}

	/*
	 * This is not a filter, but still seems to be the best place for this
	 * code. PROMOTE/DEMOTE should be sent only after corresponding RAFT
	 * term was already sent. We assume that PROMOTE/DEMOTE will arrive
	 * after RAFT term, otherwise something might break.
	 */
	if (iproto_type_is_promote_request(packet->type)) {
		struct synchro_request req;
		if (xrow_decode_synchro(packet, &req, NULL) != 0)
			diag_raise();
		while (relay->sent_raft_term < req.term) {
			if (fiber_is_cancelled()) {
				diag_set(FiberIsCancelled);
				diag_raise();
			}
			cbus_process(&relay->tx_endpoint);
			if (relay->sent_raft_term >= req.term)
				break;
			fiber_yield();
		}
	}
	return true;
}

/**
 * A helper struct to collect all rows to be sent in scope of a transaction
 * into a single list.
 */
struct relay_row {
	/** A transaction row. */
	struct xrow_header row;
	/** A link in all transaction rows. */
	struct rlist in_tx;
};

/** Save a single transaction row for the future use. */
static void
relay_save_row(struct relay *relay, struct xrow_header *packet)
{
	struct relay_row *tx_row = xlsregion_alloc_object(&relay->lsregion,
							  ++relay->lsr_id,
							  struct relay_row);
	struct xrow_header *row = &tx_row->row;
	*row = *packet;
	if (packet->bodycnt == 1) {
		size_t len = packet->body[0].iov_len;
		void *new_body = xlsregion_alloc(&relay->lsregion, len,
						 ++relay->lsr_id);
		memcpy(new_body, packet->body[0].iov_base, len);
		row->body[0].iov_base = new_body;
	}
	rlist_add_tail_entry(&relay->current_tx, tx_row, in_tx);
}

/** Send a full transaction to the replica. */
static void
relay_send_tx(struct relay *relay)
{
	struct relay_row *item;

	rlist_foreach_entry(item, &relay->current_tx, in_tx) {
		struct xrow_header *packet = &item->row;

		struct errinj *inj = errinj(ERRINJ_RELAY_BREAK_LSN,
					    ERRINJ_INT);
		if (inj != NULL && packet->lsn == inj->iparam) {
			packet->lsn = inj->iparam - 1;
			packet->tsn = packet->lsn;
			say_warn("injected broken lsn: %lld",
				 (long long) packet->lsn);
		}
		relay_send(relay, packet);
	}

	rlist_create(&relay->current_tx);
	lsregion_gc(&relay->lsregion, relay->lsr_id);
}

static void
relay_process_row(struct xstream *stream, struct xrow_header *packet)
{
	struct relay *relay = container_of(stream, struct relay, stream);
	struct rlist *current_tx = &relay->current_tx;

	if (relay->read_tsn == 0) {
		rlist_create(current_tx);
		relay->read_tsn = packet->tsn;
	} else if (relay->read_tsn != packet->tsn) {
		tnt_raise(ClientError, ER_PROTOCOL, "Found a new transaction "
			  "with previous one not yet committed");
	}

	if (!packet->is_commit) {
		if (relay_filter_row(relay, packet)) {
			relay_save_row(relay, packet);
		}
		return;
	}
	if (relay_filter_row(relay, packet)) {
		relay_save_row(relay, packet);
	} else if (rlist_empty(current_tx)) {
		relay->read_tsn = 0;
		return;
	} else {
		rlist_last_entry(current_tx, struct relay_row,
				 in_tx)->row.flags = packet->flags;
	}
	relay_send_tx(relay);
	relay->read_tsn = 0;
}
