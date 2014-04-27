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
#include "recovery.h"
#include "tarantool.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "log_io.h"
#include "fiber.h"
#include "pickle.h"
#include "coio_buf.h"
#include "recovery.h"
#include "tarantool.h"
#include "iproto.h"
#include "iproto_constants.h"
#include "msgpuck/msgpuck.h"
#include "replica.h"

static void
remote_read_row(struct ev_io *coio, struct iobuf *iobuf,
		struct iproto_packet *packet)
{
	struct ibuf *in = &iobuf->in;

	/* Read fixed header */
	if (ibuf_size(in) < IPROTO_FIXHEADER_SIZE)
		coio_breadn(coio, in, IPROTO_FIXHEADER_SIZE - ibuf_size(in));

	/* Read length */
	if (mp_typeof(*in->pos) != MP_UINT) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "invalid fixed header");
	}

	const char *data = in->pos;
	uint32_t len = mp_decode_uint(&data);
	if (len > IPROTO_BODY_LEN_MAX) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "received packet is too big");
	}
	in->pos += IPROTO_FIXHEADER_SIZE;

	/* Read header and body */
	ssize_t to_read = len - ibuf_size(in);
	if (to_read > 0)
		coio_breadn(coio, in, to_read);

	iproto_packet_decode(packet, (const char **) &in->pos, in->pos + len);
}

/* Blocked I/O  */
static void
remote_read_row_fd(int sock, struct iproto_packet *packet)
{
	const char *data;

	/* Read fixed header */
	char fixheader[IPROTO_FIXHEADER_SIZE];
	if (sio_read(sock, fixheader, sizeof(fixheader)) != sizeof(fixheader)) {
error:
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "invalid fixed header");
	}
	data = fixheader;
	if (mp_check(&data, data + sizeof(fixheader)) != 0)
		goto error;
	data = fixheader;

	/* Read length */
	if (mp_typeof(*data) != MP_UINT)
		goto error;
	uint32_t len = mp_decode_uint(&data);
	if (len > IPROTO_BODY_LEN_MAX) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "received packet is too big");
	}

	/* Read header and body */
	char *bodybuf = (char *) region_alloc(&fiber()->gc, len);
	if (sio_read(sock, bodybuf, len) != len) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "invalid row - can't read");
	}

	data = bodybuf;
	iproto_packet_decode(packet, &data, data + len);
}

void
replica_bootstrap(struct recovery_state *r, const char *replication_source)
{
	say_info("bootstrapping replica");

	/* Generate Node-UUID */
	tt_uuid_create(&r->node_uuid);

	char ip_addr[32];
	char greeting[IPROTO_GREETING_SIZE];
	int port;
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));

	int rc = sscanf(replication_source, "%31[^:]:%i",
			ip_addr, &port);

	assert(rc == 2);
	(void)rc;

	addr.sin_family = AF_INET;
	if (inet_aton(ip_addr, (in_addr*)&addr.sin_addr.s_addr) < 0)
		panic_syserror("inet_aton: %s", ip_addr);

	addr.sin_port = htons(port);

	int master = sio_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	FDGuard guard(master);

	uint64_t sync = rand();

	/* Send JOIN request */
	struct iproto_packet packet;
	memset(&packet, 0, sizeof(packet));
	packet.code = IPROTO_JOIN;
	packet.sync = sync;

	char buf[128];
	char *data = buf;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_NODE_UUID);
	data = mp_encode_str(data, (const char *) &recovery_state->node_uuid,
			     sizeof(tt_uuid));

	assert(data <= buf + sizeof(buf));
	packet.body[0].iov_base = buf;
	packet.body[0].iov_len = (data - buf);
	packet.bodycnt = 1;
	char fixheader[IPROTO_FIXHEADER_SIZE];
	struct iovec iov[IPROTO_ROW_IOVMAX];
	int iovcnt = iproto_encode_row(&packet, iov, fixheader);

	sio_connect(master, &addr, sizeof(addr));
	sio_readn(master, greeting, sizeof(greeting));
	sio_writev_all(master, iov, iovcnt);

	while (true) {
		remote_read_row_fd(master, &packet);
		if (packet.sync != sync) {
			tnt_raise(ClientError, ER_INVALID_MSGPACK,
				  "unexpected packet sync");
		}

		/* Recv JOIN response (= end of stream) */
		if (packet.code == IPROTO_JOIN) {
			if (packet.bodycnt != 0)
				tnt_raise(IllegalParams, "JOIN body");
			say_info("done");
			break;
		}

		recovery_process(r, &packet);
	}
	say_info("done");
	/* master socket closed by guard */
}

static void
remote_connect(struct recovery_state *r, struct ev_io *coio,const char **err)
{
	char greeting[IPROTO_GREETING_SIZE];
	evio_socket(coio, AF_INET, SOCK_STREAM, IPPROTO_TCP);

	*err = "can't connect to master";
	coio_connect(coio, &r->remote->addr);
	coio_readn(coio, greeting, sizeof(greeting));

	/* Send SUBSCRIBE request */
	struct iproto_packet packet;
	memset(&packet, 0, sizeof(packet));
	packet.code = IPROTO_SUBSCRIBE;

	uint32_t cluster_size = mh_size(r->cluster);
	size_t size = 128 + cluster_size *
		(mp_sizeof_uint(UINT32_MAX) + mp_sizeof_uint(UINT64_MAX));
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	char *data = buf;
	data = mp_encode_map(data, 3);
	data = mp_encode_uint(data, IPROTO_CLUSTER_UUID);
	data = mp_encode_str(data, (const char *) &r->cluster_uuid,
			     sizeof(tt_uuid));
	data = mp_encode_uint(data, IPROTO_NODE_UUID);
	data = mp_encode_str(data, (const char *) &r->node_uuid,
			     sizeof(tt_uuid));
	data = mp_encode_uint(data, IPROTO_LSNMAP);
	data = mp_encode_map(data, cluster_size);
	uint32_t k;
	mh_foreach(r->cluster, k) {
		struct node *node = *mh_cluster_node(r->cluster, k);
		data = mp_encode_uint(data, node->id);
		data = mp_encode_uint(data, node->current_lsn);
	}
	assert(data <= buf + size);
	packet.body[0].iov_base = buf;
	packet.body[0].iov_len = (data - buf);
	packet.bodycnt = 1;
	char fixheader[IPROTO_FIXHEADER_SIZE];
	struct iovec iov[IPROTO_ROW_IOVMAX];
	int iovcnt = iproto_encode_row(&packet, iov, fixheader);
	coio_writev(coio, iov, iovcnt, 0);

	say_crit("successfully connected to master");
}

static void
pull_from_remote(va_list ap)
{
	struct recovery_state *r = va_arg(ap, struct recovery_state *);
	struct ev_io coio;
	struct iobuf *iobuf = NULL;
	bool warning_said = false;
	const int reconnect_delay = 1;
	ev_loop *loop = loop();

	coio_init(&coio);

	for (;;) {
		const char *err = NULL;
		try {
			fiber_setcancellable(true);
			if (! evio_is_active(&coio)) {
				title("replica", "%s/%s", r->remote->source,
				      "connecting");
				if (iobuf == NULL)
					iobuf = iobuf_new(fiber_name(fiber()));
				remote_connect(r, &coio, &err);
				warning_said = false;
				title("replica", "%s/%s", r->remote->source,
				      "connected");
			}
			err = "can't read row";
			struct iproto_packet packet;
			remote_read_row(&coio, iobuf, &packet);
			fiber_setcancellable(false);
			err = NULL;

			r->remote->recovery_lag = ev_now(loop) - packet.tm;
			r->remote->recovery_last_update_tstamp =
				ev_now(loop);

			recovery_process(r, &packet);

			iobuf_reset(iobuf);
			fiber_gc();
		} catch (FiberCancelException *e) {
			title("replica", "%s/%s", r->remote->source, "failed");
			iobuf_delete(iobuf);
			evio_close(loop, &coio);
			throw;
		} catch (Exception *e) {
			title("replica", "%s/%s", r->remote->source, "failed");
			e->log();
			if (! warning_said) {
				if (err != NULL)
					say_info("%s", err);
				say_info("will retry every %i second", reconnect_delay);
				warning_said = true;
			}
			evio_close(loop, &coio);
		}

		/* Put fiber_sleep() out of catch block.
		 *
		 * This is done to avoid situation, when two or more
		 * fibers yield's inside their try/catch blocks and
		 * throws an exceptions. Seems like exception unwinder
		 * stores some global state while being inside a catch
		 * block.
		 *
		 * This could lead to incorrect exception processing
		 * and crash the server.
		 *
		 * See: https://github.com/tarantool/tarantool/issues/136
		*/
		if (! evio_is_active(&coio))
			fiber_sleep(reconnect_delay);
	}
}

void
recovery_follow_remote(struct recovery_state *r, const char *addr)
{
	char name[FIBER_NAME_MAX];
	char ip_addr[32];
	int port;
	int rc;
	struct fiber *f;
	struct in_addr server;

	assert(r->remote == NULL);

	say_crit("initializing the replica, WAL master %s", addr);
	snprintf(name, sizeof(name), "replica/%s", addr);

	try {
		f = fiber_new(name, pull_from_remote);
	} catch (Exception *e) {
		return;
	}

	rc = sscanf(addr, "%31[^:]:%i", ip_addr, &port);
	assert(rc == 2);
	(void)rc;

	if (inet_aton(ip_addr, &server) < 0) {
		say_syserror("inet_aton: %s", ip_addr);
		return;
	}

	static struct remote remote;
	memset(&remote, 0, sizeof(remote));
	remote.addr.sin_family = AF_INET;
	memcpy(&remote.addr.sin_addr.s_addr, &server, sizeof(server));
	remote.addr.sin_port = htons(port);
	memcpy(&remote.cookie, &remote.addr, MIN(sizeof(remote.cookie), sizeof(remote.addr)));
	remote.reader = f;
	snprintf(remote.source, sizeof(remote.source), "%s", addr);
	r->remote = &remote;
	fiber_call(f, r);
}

void
recovery_stop_remote(struct recovery_state *r)
{
	say_info("shutting down the replica");
	fiber_cancel(r->remote->reader);
	r->remote = NULL;
}
