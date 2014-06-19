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
#include "replica.h"
#include "recovery.h"
#include "tarantool.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "log_io.h"
#include "fiber.h"
#include "coio_buf.h"
#include "recovery.h"
#include "tarantool.h"
#include "iproto.h"
#include "iproto_constants.h"
#include "msgpuck/msgpuck.h"
#include "box/cluster.h"

static void
remote_read_row(struct ev_io *coio, struct iobuf *iobuf,
		struct iproto_header *row)
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

	iproto_header_decode(row, (const char **) &in->pos, in->pos + len);
}

/* Blocked I/O  */
static void
remote_read_row_fd(int sock, struct iproto_header *row)
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
	iproto_header_decode(row, &data, data + len);
}

void
replica_bootstrap(struct recovery_state *r, const char *replication_source)
{
	say_info("bootstrapping a replica");

	/* Generate Node-UUID */
	tt_uuid_create(&r->node_uuid);

	char greeting[IPROTO_GREETING_SIZE];

	port_uri uri;
	if (!port_uri_parse(&uri, replication_source)) {
		panic("Broken replication_source url: %s", replication_source);
	}

	int master = sio_socket(uri.addr.sa_family, SOCK_STREAM, IPPROTO_TCP);
	FDGuard guard(master);

	uint64_t sync = rand();

	/* Send JOIN request */
	struct iproto_header row;
	memset(&row, 0, sizeof(struct iproto_header));
	row.type = IPROTO_JOIN;
	row.sync = sync;

	char buf[128];
	char *data = buf;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_NODE_UUID);
	data = mp_encode_strl(data, UUID_LEN);
	/* Greet the remote server with our server UUID */
	tt_uuid_enc_be(&recovery_state->node_uuid, data);
	data += UUID_LEN;

	assert(data <= buf + sizeof(buf));
	row.body[0].iov_base = buf;
	row.body[0].iov_len = (data - buf);
	row.bodycnt = 1;
	char fixheader[IPROTO_FIXHEADER_SIZE];
	struct iovec iov[IPROTO_ROW_IOVMAX];
	int iovcnt = iproto_row_encode(&row, iov, fixheader);

	sio_connect(master, &uri.addr, uri.addr_len);
	sio_readn(master, greeting, sizeof(greeting));
	sio_writev_all(master, iov, iovcnt);

	while (true) {
		remote_read_row_fd(master, &row);

		/* Recv JOIN response (= end of stream) */
		if (row.type == IPROTO_JOIN) {
			say_info("done");
			break;
		}

		recovery_process(r, &row);
	}

	recovery_end_recover_snapshot(r);
	/* master socket closed by guard */
}

static void
remote_connect(struct recovery_state *r, struct ev_io *coio,const char **err)
{
	char greeting[IPROTO_GREETING_SIZE];
	evio_socket(coio, AF_INET, SOCK_STREAM, IPPROTO_TCP);

	*err = "can't connect to master";
	coio_connect(coio, &r->remote->uri.addr, r->remote->uri.addr_len);
	coio_readn(coio, greeting, sizeof(greeting));

	/* Send SUBSCRIBE request */
	struct iproto_header row;
	memset(&row, 0, sizeof(row));
	row.type = IPROTO_SUBSCRIBE;

	uint32_t cluster_size = vclock_size(&r->vclock);
	size_t size = 128 + cluster_size *
		(mp_sizeof_uint(UINT32_MAX) + mp_sizeof_uint(UINT64_MAX));
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	char *data = buf;
	data = mp_encode_map(data, 3);
	data = mp_encode_uint(data, IPROTO_CLUSTER_UUID);
	data = mp_encode_strl(data, UUID_LEN);
	tt_uuid_enc_be(&cluster_id, data);
	data += UUID_LEN;
	data = mp_encode_uint(data, IPROTO_NODE_UUID);
	data = mp_encode_strl(data, UUID_LEN);
	tt_uuid_enc_be(&recovery_state->node_uuid, data);
	data += UUID_LEN;
	data = mp_encode_uint(data, IPROTO_LSNMAP);
	data = mp_encode_map(data, cluster_size);
	vclock_foreach(&r->vclock, server) {
		data = mp_encode_uint(data, server.id);
		data = mp_encode_uint(data, server.lsn);
	}
	assert(data <= buf + size);
	row.body[0].iov_base = buf;
	row.body[0].iov_len = (data - buf);
	row.bodycnt = 1;
	char fixheader[IPROTO_FIXHEADER_SIZE];
	struct iovec iov[IPROTO_ROW_IOVMAX];
	int iovcnt = iproto_row_encode(&row, iov, fixheader);
	coio_writev(coio, iov, iovcnt, 0);

	say_crit("connected to master");
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
			struct iproto_header row;
			remote_read_row(&coio, iobuf, &row);
			fiber_setcancellable(false);
			err = NULL;

			r->remote->recovery_lag = ev_now(loop) - row.tm;
			r->remote->recovery_last_update_tstamp =
				ev_now(loop);

			recovery_process(r, &row);

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
recovery_follow_remote(struct recovery_state *r, const char *uri)
{
	char name[FIBER_NAME_MAX];
	struct fiber *f;

	assert(r->remote == NULL);

	say_crit("initializing the replica, WAL master %s", uri);
	snprintf(name, sizeof(name), "replica/%s", uri);

	try {
		f = fiber_new(name, pull_from_remote);
	} catch (Exception *e) {
		return;
	}



	static struct remote remote;
	memset(&remote, 0, sizeof(remote));
	if (!port_uri_parse(&remote.uri, uri)) {
		say_error("Can't parse uri: %s", uri);
		return;
	}

	remote.reader = f;
	snprintf(remote.source, sizeof(remote.source), "%s", uri);
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
