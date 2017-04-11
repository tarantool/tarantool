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
#include "trivia/util.h"
#include "cbus.h"
#include "cfg.h"
#include "errinj.h"
#include "fiber.h"
#include "say.h"
#include "scoped_guard.h"

#include "coeio.h"
#include "coio.h"
#include "engine.h"
#include "iproto_constants.h"
#include "recovery.h"
#include "replication.h"
#include "trigger.h"
#include "vclock.h"
#include "xrow.h"
#include "xrow_io.h"
#include "xstream.h"

/** Report relay status to tx thread at least once per this interval */
static const int RELAY_REPORT_INTERVAL = 1;

/**
 * Cbus message to send status updates from relay to tx thread.
 */
struct relay_status_msg {
	/** Parent */
	struct cmsg msg;
	/** Relay instance */
	struct relay *relay;
	/** New vclock */
	struct vclock vclock;
};

/**
 * Cbus message to notify tx thread that relay is stopping.
 */
struct relay_exit_msg {
	/** Parent */
	struct cmsg msg;
	/** Relay instance */
	struct relay *relay;
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
	/** Directory rescan delay for recovery */
	ev_tstamp wal_dir_rescan_delay;
	/** Remote replica id */
	uint32_t replica_id;

	/** Relay endpoint */
	struct cbus_endpoint endpoint;
	/** A pipe from 'relay' thread to 'tx' */
	struct cpipe tx_pipe;
	/** A pipe from 'tx' thread to 'relay' */
	struct cpipe relay_pipe;
	/** Status message */
	struct relay_status_msg status_msg;
	/** A condition to signal when status message is handled. */
	struct ipc_cond status_cond;
	/** Relay exit orchestration message */
	struct relay_exit_msg exit_msg;

	struct {
		/* Align to prevent false-sharing with tx thread */
		alignas(CACHELINE_SIZE)
		/** Current vclock sent by relay */
		struct vclock vclock;
		/** The condition is signaled at relay exit. */
		struct ipc_cond exit_cond;
	} tx;
};

const struct vclock *
relay_vclock(const struct relay *relay)
{
	return &relay->tx.vclock;
}

static void
relay_send_initial_join_row(struct xstream *stream, struct xrow_header *row);
static void
relay_send_row(struct xstream *stream, struct xrow_header *row);

static inline void
relay_create(struct relay *relay, int fd, uint64_t sync,
	     void (*stream_write)(struct xstream *, struct xrow_header *))
{
	memset(relay, 0, sizeof(*relay));
	xstream_create(&relay->stream, stream_write);
	coio_init(&relay->io, fd);
	relay->sync = sync;
}

static inline void
relay_destroy(struct relay *relay)
{
	(void) relay;
}

static inline void
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
	struct relay relay;
	relay_create(&relay, fd, sync, relay_send_initial_join_row);
	auto scope_guard = make_scoped_guard([&]{
		relay_destroy(&relay);
	});

	assert(relay.stream.write != NULL);
	engine_join(vclock, &relay.stream);
}

int
relay_final_join_f(va_list ap)
{
	struct relay *relay = va_arg(ap, struct relay *);
	coeio_enable();
	relay_set_cord_name(relay->io.fd);

	/* Send all WALs until stop_vclock */
	assert(relay->stream.write != NULL);
	xdir_scan_xc(&relay->r->wal_dir);
	recover_remaining_wals(relay->r, &relay->stream, &relay->stop_vclock);
	assert(vclock_compare(&relay->r->vclock, &relay->stop_vclock) == 0);
	return 0;
}

void
relay_final_join(int fd, uint64_t sync, struct vclock *start_vclock,
	         struct vclock *stop_vclock)
{
	struct relay relay;
	relay_create(&relay, fd, sync, relay_send_row);
	relay.r = recovery_new(cfg_gets("wal_dir"),
			       cfg_geti("force_recovery"),
			       start_vclock);
	vclock_copy(&relay.stop_vclock, stop_vclock);
	auto scope_guard = make_scoped_guard([&]{
		recovery_delete(relay.r);
		relay_destroy(&relay);
	});

	cord_costart(&relay.cord, "final_join", relay_final_join_f, &relay);
	if (cord_cojoin(&relay.cord) != 0)
		diag_raise();
	ERROR_INJECT(ERRINJ_RELAY_FINAL_SLEEP, {
		while (vclock_compare(stop_vclock, &replicaset_vclock) == 0)
			fiber_sleep(0.001);
	});
}

static void
feed_event_f(struct trigger *trigger, void * /* event */)
{
	ev_feed_event(loop(), (struct ev_io *) trigger->data, EV_CUSTOM);
}

/**
 * The message which updated tx thread with a new vclock has returned back
 * to the relay.
 */
static void
relay_status_update(struct cmsg *msg)
{
	msg->route = NULL;
	struct relay_status_msg *status_msg = (struct relay_status_msg *)msg;
	ipc_cond_signal(&status_msg->relay->status_cond);
}

/**
 * Deliver a fresh relay vclock to tx thread.
 */
static void
tx_status_update(struct cmsg *msg)
{
	struct relay_status_msg *status = (struct relay_status_msg *)msg;
	vclock_copy(&status->relay->tx.vclock, &status->vclock);
	static const struct cmsg_hop route[] = {
		{relay_status_update, NULL}
	};
	cmsg_init(msg, route);
	cpipe_push(&status->relay->relay_pipe, msg);
}

static void
tx_exit_cb(struct cmsg *msg)
{
	struct relay_exit_msg *m = (struct relay_exit_msg *)msg;
	ipc_cond_signal(&m->relay->tx.exit_cond);
}

static void
relay_cbus_detach(struct relay *relay)
{
	say_crit("exiting the relay loop");
	/* Check that we have no in-flight status message */
	while (relay->status_msg.msg.route != NULL) {
		ipc_cond_wait(&relay->status_cond);
	}

	static const struct cmsg_hop exit_route[] = {
		{tx_exit_cb, NULL}
	};
	cmsg_init(&relay->exit_msg.msg, exit_route);
	relay->exit_msg.relay = relay;
	cpipe_push(&relay->tx_pipe, &relay->exit_msg.msg);
	/*
	 * The relay must not destroy its endpoint until the
	 * corresponding callee's fiber/cord has destroyed its
	 * counterpart, otherwise the callee will wait
	 * indefinitely.
	 */
	cpipe_destroy(&relay->tx_pipe);
	cbus_endpoint_destroy(&relay->endpoint, cbus_process);
	ipc_cond_destroy(&relay->status_cond);
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
	coeio_enable();
	relay->stream.write = relay_send_row;
	ipc_cond_create(&relay->status_cond);
	cbus_endpoint_create(&relay->endpoint, cord_name(cord()),
			     fiber_schedule_cb, fiber());
	/*
	 * Use tx_prio router because our handler never yields and
	 * just updates struct relay members.
	 */
	cpipe_create(&relay->tx_pipe, "tx_prio");

	/* Create a guard to detach the relay from cbus on exit */
	auto cbus_guard = make_scoped_guard([&]{
		relay_cbus_detach(relay);
	});
	relay_set_cord_name(relay->io.fd);
	recovery_follow_local(r, &relay->stream, fiber_name(fiber()),
			      relay->wal_dir_rescan_delay);

	/*
	 * Init a read event: when replica closes its end
	 * of the socket, we can read EOF and shutdown the
	 * relay.
	 */
	struct ev_io read_ev;
	read_ev.data = fiber();
	ev_io_init(&read_ev, (ev_io_cb) fiber_schedule_cb,
		   relay->io.fd, EV_READ);
	/**
	 * If there is an exception in the follower fiber, it's
	 * sufficient to break the main fiber's wait on the read
	 * event.
	 * recovery_stop_local() will follow and raise the
	 * original exception in the joined fiber.  This original
	 * exception will reach cord_join() and will be raised
	 * further up, eventually reaching iproto_process(), where
	 * it'll get converted to an iproto message and sent to
	 * the client.
	 * It's safe to allocate the trigger on stack, the life of
	 * the follower fiber is enclosed into life of this fiber.
	 */
	struct trigger on_follow_error = {
		RLIST_LINK_INITIALIZER, feed_event_f, &read_ev, NULL
	};
	trigger_add(&r->watcher->on_stop, &on_follow_error);
	while (! fiber_is_dead(r->watcher)) {
		ev_io_start(loop(), &read_ev);
		fiber_yield_timeout(RELAY_REPORT_INTERVAL);
		ev_io_stop(loop(), &read_ev);
		/*
		 * The fiber can be woken by IO read event, by the timeout of
		 * status messaging or by an acknowledge to status message.
		 * Handle cbus messages first.
		 */
		cbus_process(&relay->endpoint);

		uint8_t data;
		int rc = recv(read_ev.fd, &data, sizeof(data), 0);

		if (rc == 0 || (rc < 0 && errno == ECONNRESET)) {
			say_info("the replica has closed its socket, exiting");
			break;
		}
		if (rc < 0 && errno != EINTR && errno != EAGAIN &&
		    errno != EWOULDBLOCK)
			say_syserror("recv");

		/*
		 * Check that the vclock has been updated and the previous
		 * status message is delivered
		 */
		if (relay->status_msg.msg.route != NULL ||
		    vclock_compare(&relay->status_msg.vclock,
				   &r->vclock) == 0)
			continue;
		static const struct cmsg_hop route[] = {
			{tx_status_update, NULL}
		};
		cmsg_init(&relay->status_msg.msg, route);
		vclock_copy(&relay->status_msg.vclock, &r->vclock);
		relay->status_msg.relay = relay;
		cpipe_push(&relay->tx_pipe, &relay->status_msg.msg);
	}
	/*
	 * Avoid double wakeup: both from the on_stop and fiber
	 * cancel events.
	 */
	trigger_clear(&on_follow_error);
	recovery_stop_local(r);
	return 0;
}

/** Replication acceptor fiber handler. */
void
relay_subscribe(int fd, uint64_t sync, struct replica *replica,
		struct vclock *replica_clock)
{
	assert(replica->id != REPLICA_ID_NIL);
	/* Don't allow multiple relays for the same replica */
	if (replica->relay != NULL) {
		tnt_raise(ClientError, ER_CFG, "replication",
			  "duplicate connection with the same replica UUID");
	}

	struct relay relay;
	relay_create(&relay, fd, sync, relay_send_row);
	relay.r = recovery_new(cfg_gets("wal_dir"),
			       cfg_geti("force_recovery"),
			       replica_clock);
	vclock_copy(&relay.tx.vclock, replica_clock);
	relay.replica_id = replica->id;
	relay.wal_dir_rescan_delay = cfg_getd("wal_dir_rescan_delay");
	replica_set_relay(replica, &relay);

	auto scope_guard = make_scoped_guard([&]{
		replica_clear_relay(replica);
		recovery_delete(relay.r);
		relay_destroy(&relay);
	});

	struct cord cord;
	char name[FIBER_NAME_MAX];
	snprintf(name, sizeof(name), "relay: %u", replica->id);
	ipc_cond_create(&relay.tx.exit_cond);
	cord_costart(&cord, name, relay_subscribe_f, &relay);
	cpipe_create(&relay.relay_pipe, name);
	/*
	 * When relay exits, it sends a message which signals the
	 * exit condition in tx thread.
	 */
	ipc_cond_wait(&relay.tx.exit_cond);
	ipc_cond_destroy(&relay.tx.exit_cond);
	/*
	 * Destroy the cpipe so that relay fiber can destroy
	 * the corresponding endpoint and exit.
	 */
	cpipe_destroy(&relay.relay_pipe);
	if (cord_cojoin(&cord) != 0) {
		diag_raise();
	}
}

static void
relay_send(struct relay *relay, struct xrow_header *packet)
{
	packet->sync = relay->sync;
	coio_write_xrow(&relay->io, packet);
	fiber_gc();
}

static void
relay_send_initial_join_row(struct xstream *stream, struct xrow_header *row)
{
	struct relay *relay = container_of(stream, struct relay, stream);
	relay_send(relay, row);
	ERROR_INJECT(ERRINJ_RELAY,
	{
		fiber_sleep(1000.0);
	});
}

/** Send a single row to the client. */
static void
relay_send_row(struct xstream *stream, struct xrow_header *packet)
{
	struct relay *relay = container_of(stream, struct relay, stream);
	assert(iproto_type_is_dml(packet->type));
	/*
	 * We're feeding a WAL, thus responding to SUBSCRIBE request.
	 * In that case, only send a row if it is not from the same replica
	 * (i.e. don't send replica's own rows back).
	 */
	if (packet->replica_id != relay->replica_id) {
		relay_send(relay, packet);
		ERROR_INJECT(ERRINJ_RELAY,
		{
			fiber_sleep(1000.0);
		});
	}
}
