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
replica_bootstrap(struct recovery_state *r)
{
	say_info("bootstrapping a replica");
	assert(recovery_has_remote(r));

	/* Generate Server-UUID */
	tt_uuid_create(&r->server_uuid);

	char greeting[IPROTO_GREETING_SIZE];

	uint64_t sync = rand();
	struct iovec iov[IPROTO_ROW_IOVMAX];
	struct iproto_header row;

	int master = sio_socket(r->remote.uri.addr.sa_family,
				SOCK_STREAM, IPPROTO_TCP);
	FDGuard guard(master);

	sio_connect(master, &r->remote.uri.addr, r->remote.uri.addr_len);
	sio_readn(master, greeting, sizeof(greeting));

	if (*r->remote.uri.login) {
		/* Authenticate */
		iproto_encode_auth(&row, greeting, r->remote.uri.login,
				   r->remote.uri.password);
		int iovcnt = iproto_row_encode(&row, iov);
		sio_writev_all(master, iov, iovcnt);
		remote_read_row_fd(master, &row);
		iproto_decode_error(&row); /* auth failed */
		/* auth successed */
		say_info("authenticated with master");
	}

	/* Send JOIN request */
	iproto_encode_join(&row, &recovery_state->server_uuid);
	row.sync = sync;
	int iovcnt = iproto_row_encode(&row, iov);

	sio_writev_all(master, iov, iovcnt);

	/* Add a surrogate server id for snapshot rows */
	vclock_add_server(&r->vclock, 0);

	while (true) {
		remote_read_row_fd(master, &row);

		if (iproto_request_is_dml(row.type)) {
			/* Regular snapshot row  (IPROTO_INSERT) */
			recovery_process(r, &row);
		} else if (row.type == IPROTO_JOIN) {
			/* End of stream */
			say_info("done");
			break;
		} else {
			iproto_decode_error(&row);  /* rethrow error */
		}
	}

	/* Decode end of stream packet */
	struct vclock vclock;
	vclock_create(&vclock);
	assert(row.type == IPROTO_JOIN);
	iproto_decode_eos(&row, &vclock);

	/* Replace server vclock using data from snapshot */
	vclock_copy(&r->vclock, &vclock);

	/* master socket closed by guard */
}

static void
remote_connect(struct recovery_state *r, struct ev_io *coio,
	       struct iobuf *iobuf, const char **err)
{
	char greeting[IPROTO_GREETING_SIZE];
	struct iovec iov[IPROTO_ROW_IOVMAX];

	evio_socket(coio, AF_INET, SOCK_STREAM, IPPROTO_TCP);

	struct port_uri *uri = &r->remote.uri;

	*err = "can't connect to master";
	coio_connect(coio, &uri->addr, uri->addr_len);
	coio_readn(coio, greeting, sizeof(greeting));

	if (*r->remote.uri.login) {
		/* Authenticate */
		say_debug("authenticating...");
		struct iproto_header row;
		iproto_encode_auth(&row, greeting, uri->login, uri->password);
		int iovcnt = iproto_row_encode(&row, iov);
		coio_writev(coio, iov, iovcnt, 0);
		remote_read_row(coio, iobuf, &row);
		iproto_decode_error(&row); /* auth failed */
		/* auth successed */
		say_info("authenticated");
	}

	/* Send SUBSCRIBE request */
	struct iproto_header row;
	iproto_encode_subscribe(&row, &cluster_id, &r->server_uuid, &r->vclock);
	int iovcnt = iproto_row_encode(&row, iov);
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
				title("replica", "%s/%s", r->remote.source,
				      "connecting");
				if (iobuf == NULL)
					iobuf = iobuf_new(fiber_name(fiber()));
				remote_connect(r, &coio, iobuf, &err);
				warning_said = false;
				title("replica", "%s/%s", r->remote.source,
				      "connected");
			}
			err = "can't read row";
			struct iproto_header row;
			remote_read_row(&coio, iobuf, &row);
			if (!iproto_request_is_dml(row.type))
				iproto_decode_error(&row);  /* error */
			fiber_setcancellable(false);
			err = NULL;

			r->remote.recovery_lag = ev_now(loop) - row.tm;
			r->remote.recovery_last_update_tstamp =
				ev_now(loop);

			recovery_process(r, &row);

			iobuf_reset(iobuf);
			fiber_gc();
		} catch (FiberCancelException *e) {
			title("replica", "%s/%s", r->remote.source, "failed");
			iobuf_delete(iobuf);
			evio_close(loop, &coio);
			throw;
		} catch (Exception *e) {
			title("replica", "%s/%s", r->remote.source, "failed");
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
recovery_follow_remote(struct recovery_state *r)
{
	char name[FIBER_NAME_MAX];
	struct fiber *f;

	assert(r->remote.reader == NULL);
	assert(recovery_has_remote(r));

	say_crit("starting replication from %s", r->remote.source);
	snprintf(name, sizeof(name), "replica/%s", r->remote.source);

	try {
		f = fiber_new(name, pull_from_remote);
	} catch (Exception *e) {
		return;
	}

	r->remote.reader = f;
	fiber_call(f, r);
}

void
recovery_stop_remote(struct recovery_state *r)
{
	say_info("shutting down the replica");
	fiber_cancel(r->remote.reader);
	r->remote.reader = NULL;
}

void
recovery_set_remote(struct recovery_state *r, const char *uri)
{
	/* First, stop the reader, then set the source */
	assert(r->remote.reader == NULL);
	if (uri == NULL) {
		r->remote.source[0] = '\0';
		return;
	}
	/*
	 * @todo: as long as DNS is involved, this may fail even
	 * on a valid uri. Don't panic in this case.
	 */
	if (port_uri_parse(&r->remote.uri, uri))
		panic("Can't parse uri: %s", uri);
	snprintf(r->remote.source,
		 sizeof(r->remote.source), "%s", uri);
}

bool
recovery_has_remote(struct recovery_state *r)
{
	return r->remote.source[0];
}

