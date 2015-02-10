/*
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
#include "replication.h"
#include <say.h>
#include <fiber.h>

#include "recovery.h"
#include "xlog.h"
#include "evio.h"
#include "iproto_constants.h"
#include "box/cluster.h"
#include "box/schema.h"
#include "box/vclock.h"
#include "scoped_guard.h"
#include "xrow.h"
#include "coeio.h"
#include "coio.h"
#include "cfg.h"

static void
replication_send_row(struct recovery_state *r, void * /* param */,
			   struct xrow_header *packet);

/** Replication acceptor fiber handler. */
static void *
replication_join_thread(void *arg)
{
	struct recovery_state *r = (struct recovery_state *) arg;

	/* Turn off the non-blocking mode, if any. */
	int nonblock = sio_getfl(r->relay.sock) & O_NONBLOCK;
	sio_setfl(r->relay.sock, O_NONBLOCK, 0);
	auto socket_guard = make_scoped_guard([=]{
		/* Restore non-blocking mode */
		sio_setfl(r->relay.sock, O_NONBLOCK, nonblock);
	});

	/* Send snapshot */
	recover_snap(r);

	/* Send response to JOIN command = end of stream */
	struct xrow_header row;
	xrow_encode_vclock(&row, &r->vclock);
	row.sync = r->relay.sync;
	struct iovec iov[XROW_IOVMAX];
	int iovcnt = xrow_to_iovec(&row, iov);
	sio_writev_all(r->relay.sock, iov, iovcnt);

	say_info("snapshot sent");
	return NULL;
}

void
replication_join(int fd, struct xrow_header *packet)
{
	struct recovery_state *r;
	r = recovery_new(cfg_gets("snap_dir"), cfg_gets("wal_dir"),
			 replication_send_row, NULL);
	auto recovery_guard = make_scoped_guard([&]{
		recovery_delete(r);
	});
	r->relay.sock = fd;
	r->relay.sync = packet->sync;

	char name[FIBER_NAME_MAX];
	struct sockaddr_storage peer;
	socklen_t addrlen = sizeof(peer);
	getpeername(r->relay.sock, ((struct sockaddr*)&peer), &addrlen);
	snprintf(name, sizeof(name), "relay/%s",
		 sio_strfaddr((struct sockaddr *)&peer, addrlen));

	struct cord cord;
	cord_start(&cord, name, replication_join_thread, r);
	cord_cojoin(&cord);
}

/**
 * A libev callback invoked when a relay client socket is ready
 * for read. This currently only happens when the client closes
 * its socket, and we get an EOF.
 */
static void
replication_subscribe_f(va_list ap)
{
	struct recovery_state *r = va_arg(ap, struct recovery_state *);

	recovery_follow_local(r, 0.1);
	/*
	 * Init a read event: when replica closes its end
	 * of the socket, we can read EOF and shutdown the
	 * relay.
	 */
	struct ev_io sock_read_ev;
	sock_read_ev.data = fiber();
	ev_io_init(&sock_read_ev, (ev_io_cb) fiber_schedule,
		   r->relay.sock, EV_READ);

	while (true) {
		ev_io_start(loop(), &sock_read_ev);
		fiber_yield();
		ev_io_stop(loop(), &sock_read_ev);

		uint8_t data;
		int rc = recv(r->relay.sock, &data, sizeof(data), 0);

		if (rc == 0 || (rc < 0 && errno == ECONNRESET)) {
			say_info("the replica has closed its socket, exiting");
			goto end;
		}
		if (rc < 0)
			say_syserror("recv");
	}
end:
	recovery_stop_local(r);
	ev_break(loop(), EVBREAK_ALL);
}

static void *
replication_subscribe_thread(void *arg)
{
	struct recovery_state *r = (struct recovery_state *) arg;

	/* Turn off the non-blocking mode, if any. */
	int nonblock = sio_getfl(r->relay.sock) & O_NONBLOCK;
	sio_setfl(r->relay.sock, O_NONBLOCK, 0);
	auto socket_guard = make_scoped_guard([=]{
		/* Restore non-blocking mode */
		sio_setfl(r->relay.sock, O_NONBLOCK, nonblock);
	});

	struct fiber *f = fiber_new("subscribe", replication_subscribe_f);
	fiber_start(f, r);
	ev_run(loop(), 0);

	say_crit("exiting the relay loop");
	return 0;
}

/** Replication acceptor fiber handler. */
void
replication_subscribe(int fd, struct xrow_header *packet)
{
	struct recovery_state *r;
	r = recovery_new(cfg_gets("snap_dir"), cfg_gets("wal_dir"),
			 replication_send_row, NULL);

	auto recovery_guard = make_scoped_guard([&]{
		recovery_delete(r);
	});

	r->relay.sock = fd;
	r->relay.sync = packet->sync;

	struct tt_uuid uu = uuid_nil, server_uuid = uuid_nil;

	xrow_decode_subscribe(packet, &uu, &server_uuid, &r->vclock);

	/**
	 * Check that the given UUID matches the UUID of the
	 * cluster this server belongs to. Used to handshake
	 * replica connect, and refuse a connection from a replica
	 * which belongs to a different cluster.
	 */
	if (!tt_uuid_is_equal(&uu, &cluster_id)) {
		tnt_raise(ClientError, ER_CLUSTER_ID_MISMATCH,
			  tt_uuid_str(&uu), tt_uuid_str(&cluster_id));
	}

	/* Check server uuid */
	r->server_id = schema_find_id(SC_CLUSTER_ID, 1,
				   tt_uuid_str(&server_uuid), UUID_STR_LEN);
	if (r->server_id == SC_ID_NIL) {
		tnt_raise(ClientError, ER_UNKNOWN_SERVER,
			  tt_uuid_str(&server_uuid));
	}

	char name[FIBER_NAME_MAX];
	struct sockaddr_storage peer;
	socklen_t addrlen = sizeof(peer);
	getpeername(r->relay.sock, ((struct sockaddr*)&peer), &addrlen);
	snprintf(name, sizeof(name), "relay/%s",
		 sio_strfaddr((struct sockaddr *)&peer, addrlen));

	struct cord cord;
	cord_start(&cord, name, replication_subscribe_thread, r);
	cord_cojoin(&cord);
}

/** Send a single row to the client. */
static void
replication_send_row(struct recovery_state *r, void * /* param */,
			   struct xrow_header *packet)
{
	assert(iproto_type_is_dml(packet->type));

	/*
	 * If packet->server_id == 0 this is a snapshot packet.
	 * (JOIN request). In this case, send every row.
	 * Otherwise, we're feeding a WAL, thus responding to
	 * SUBSCRIBE request. In that case, only send a row if
	 * it not from the same server (i.e. don't send
	 * replica's own rows back).
	 */
	if (packet->server_id == 0 || packet->server_id != r->server_id)  {
		packet->sync = r->relay.sync;
		struct iovec iov[XROW_IOVMAX];
		int iovcnt = xrow_to_iovec(packet, iov);
		sio_writev_all(r->relay.sock, iov, iovcnt);
	}
	/*
	 * Update local vclock. During normal operation wal_write()
	 * updates local vclock. In relay mode we have to update
	 * it here.
	 */
	vclock_follow(&r->vclock, packet->server_id, packet->lsn);
}
