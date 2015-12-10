/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include <say.h>
#include "scoped_guard.h"

#include "recovery.h"
#include "xlog.h"
#include "iproto_constants.h"
#include "engine.h"
#include "cluster.h"
#include "vclock.h"
#include "xrow.h"
#include "coeio.h"
#include "coio.h"
#include "cfg.h"
#include "trigger.h"
#include "errinj.h"
#include "xrow_io.h"

void
relay_send_row(struct recovery *r, void *param, struct xrow_header *packet);

static inline void
relay_create(struct relay *relay, int fd, uint64_t sync)
{
	memset(relay, 0, sizeof(*relay));
	relay->r = recovery_new(cfg_gets("snap_dir"), cfg_gets("wal_dir"),
			 relay_send_row, relay);
	recovery_setup_panic(relay->r, cfg_geti("panic_on_snap_error"),
			     cfg_geti("panic_on_wal_error"));

	coio_init(&relay->io, fd);
	relay->sync = sync;
	relay->wal_dir_rescan_delay = cfg_getd("wal_dir_rescan_delay");
}

static inline void
relay_destroy(struct relay *relay)
{
	recovery_delete(relay->r);
}

static inline void
relay_set_cord_name(int fd)
{
	char name[FIBER_NAME_MAX];
	struct sockaddr_storage peer;
	socklen_t addrlen = sizeof(peer);
	getpeername(fd, ((struct sockaddr*)&peer), &addrlen);
	snprintf(name, sizeof(name), "relay/%s",
		 sio_strfaddr((struct sockaddr *)&peer, addrlen));
	cord_set_name(name);
}

void
relay_join_f(va_list ap)
{
	struct relay *relay = va_arg(ap, struct relay *);

	relay_set_cord_name(relay->io.fd);

	/* Send snapshot */
	engine_join(relay);

	say_info("snapshot sent");
}

void
relay_join(int fd, uint64_t sync, struct vclock *join_vclock)
{
	struct relay relay;
	relay_create(&relay, fd, sync);
	auto scope_guard = make_scoped_guard([&]{
		relay_destroy(&relay);
	});
	struct recovery *r = relay.r;

	cord_costart(&relay.cord, "join", relay_join_f, &relay);
	cord_cojoin(&relay.cord);
	diag_raise();

	vclock_copy(join_vclock, vclockset_last(&r->snap_dir.index));
}

static void
feed_event_f(struct trigger *trigger, void * /* event */)
{
	ev_feed_event(loop(), (struct ev_io *) trigger->data, EV_CUSTOM);
}

/**
 * A libev callback invoked when a relay client socket is ready
 * for read. This currently only happens when the client closes
 * its socket, and we get an EOF.
 */
static void
relay_subscribe_f(va_list ap)
{
	struct relay *relay = va_arg(ap, struct relay *);
	struct recovery *r = relay->r;

	relay_set_cord_name(relay->io.fd);
	recovery_follow_local(r, fiber_name(fiber()),
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
		fiber_yield();
		ev_io_stop(loop(), &read_ev);

		uint8_t data;
		int rc = recv(read_ev.fd, &data, sizeof(data), 0);

		if (rc == 0 || (rc < 0 && errno == ECONNRESET)) {
			say_info("the replica has closed its socket, exiting");
			break;
		}
		if (rc < 0 && errno != EINTR && errno != EAGAIN &&
		    errno != EWOULDBLOCK)
			say_syserror("recv");
	}
	recovery_stop_local(r);
	say_crit("exiting the relay loop");
}

/** Replication acceptor fiber handler. */
void
relay_subscribe(int fd, uint64_t sync, struct server *server,
		struct vclock *replica_clock)
{
	assert(server->id != SERVER_ID_NIL);
	assert(server->relay == NULL);

	struct relay relay;
	relay_create(&relay, fd, sync);
	server_set_relay(server, &relay);
	auto scope_guard = make_scoped_guard([&]{
		server_clear_relay(server);
		relay_destroy(&relay);
	});

	struct recovery *r = relay.r;
	r->server_id = server->id;
	/* Initialize the relay with replica's current clock. */
	vclock_copy(&r->vclock, replica_clock);

	struct cord cord;
	cord_costart(&cord, "subscribe", relay_subscribe_f, &relay);
	cord_cojoin(&cord);
	diag_raise();
}

void
relay_send(struct relay *relay, struct xrow_header *packet)
{
	packet->sync = relay->sync;
	coio_write_xrow(&relay->io, packet);
}

/** Send a single row to the client. */
void
relay_send_row(struct recovery *r, void *param, struct xrow_header *packet)
{
	struct relay *relay = (struct relay *) param;
	assert(iproto_type_is_dml(packet->type));

	/*
	 * If packet->server_id == 0 this is a snapshot packet.
	 * (JOIN request). In this case, send every row.
	 * Otherwise, we're feeding a WAL, thus responding to
	 * SUBSCRIBE request. In that case, only send a row if
	 * it is not from the same server (i.e. don't send
	 * replica's own rows back).
	 */
	if (packet->server_id == 0 || packet->server_id != r->server_id) {
		relay_send(relay, packet);
		ERROR_INJECT(ERRINJ_RELAY,
		{
			fiber_sleep(1000.0);
		});
	}
	/*
	 * Update local vclock. During normal operation wal_write()
	 * updates local vclock. In relay mode we have to update
	 * it here.
	 */
	vclock_follow(&r->vclock, packet->server_id, packet->lsn);
}
