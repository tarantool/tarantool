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
#include "main.h"

#include "xlog.h"
#include "fiber.h"
#include "scoped_guard.h"
#include "coio_buf.h"
#include "recovery.h"
#include "xrow.h"
#include "msgpuck/msgpuck.h"
#include "box/cluster.h"
#include "iproto_constants.h"

static const int RECONNECT_DELAY = 1.0;

static void
remote_read_row(struct ev_io *coio, struct iobuf *iobuf,
		struct xrow_header *row)
{
	struct ibuf *in = &iobuf->in;

	/* Read fixed header */
	if (ibuf_size(in) < 1)
		coio_breadn(coio, in, 1);

	/* Read length */
	if (mp_typeof(*in->pos) != MP_UINT) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "packet length");
	}
	ssize_t to_read = mp_check_uint(in->pos, in->end);
	if (to_read > 0)
		coio_breadn(coio, in, to_read);

	uint32_t len = mp_decode_uint((const char **) &in->pos);

	/* Read header and body */
	to_read = len - ibuf_size(in);
	if (to_read > 0)
		coio_breadn(coio, in, to_read);

	xrow_header_decode(row, (const char **) &in->pos, in->pos + len);
}

static void
remote_write_row(struct ev_io *coio, const struct xrow_header *row)
{
	struct iovec iov[XROW_IOVMAX];
	int iovcnt = xrow_to_iovec(row, iov);
	coio_writev(coio, iov, iovcnt, 0);
}

static void
remote_connect(struct recovery_state *r, struct ev_io *coio,
	       struct iobuf *iobuf)
{
	char greeting[IPROTO_GREETING_SIZE];

	struct remote *remote = &r->remote;
	struct uri *uri = &r->remote.uri;
	/*
	 * coio_connect() stores resolved address to \a &remote->addr
	 * on success. &remote->addr_len is a value-result argument which
	 * must be initialized to the size of associated buffer (addrstorage)
	 * before calling coio_connect(). Since coio_connect() performs
	 * DNS resolution under the hood it is theoretically possible that
	 * remote->addr_len will be different even for same uri.
	 */
	remote->addr_len = sizeof(remote->addrstorage);
	/* Prepare null-terminated strings for coio_connect() */
	coio_connect(coio, uri, &remote->addr, &remote->addr_len);
	assert(coio->fd >= 0);
	coio_readn(coio, greeting, sizeof(greeting));

	say_crit("connected to %s", sio_strfaddr(&remote->addr,
						 remote->addr_len));

	/* Perform authentication if user provided at least login */
	if (!r->remote.uri.login)
		return;

	/* Authenticate */
	say_debug("authenticating...");
	struct xrow_header row;
	xrow_encode_auth(&row, greeting, uri->login,
			 uri->login_len, uri->password,
			 uri->password_len);
	remote_write_row(coio, &row);
	remote_read_row(coio, iobuf, &row);
	if (row.type != IPROTO_OK)
		xrow_decode_error(&row); /* auth failed */

	/* auth successed */
	say_info("authenticated");
}

void
replica_bootstrap(struct recovery_state *r)
{
	say_info("bootstrapping a replica");
	assert(recovery_has_remote(r));

	/* Generate Server-UUID */
	tt_uuid_create(&r->server_uuid);

	struct ev_io coio;
	coio_init(&coio);
	struct iobuf *iobuf = iobuf_new(fiber_name(fiber()));
	auto coio_guard = make_scoped_guard([&] {
		iobuf_delete(iobuf);
		evio_close(loop(), &coio);
	});

	for (;;) {
		try {
			remote_connect(r, &coio, iobuf);
			r->remote.warning_said = false;
			break;
		} catch (FiberCancelException *e) {
			throw;
		} catch (Exception *e) {
			if (! r->remote.warning_said) {
				say_error("can't connect to master");
				e->log();
				say_info("will retry every %i second",
					 RECONNECT_DELAY);
				r->remote.warning_said = true;
			}
			iobuf_reset(iobuf);
			evio_close(loop(), &coio);
		}
		fiber_sleep(RECONNECT_DELAY);
	}

	/* Send JOIN request */
	struct xrow_header row;
	xrow_encode_join(&row, &r->server_uuid);
	remote_write_row(&coio, &row);

	/* Add a surrogate server id for snapshot rows */
	vclock_add_server(&r->vclock, 0);

	while (true) {
		remote_read_row(&coio, iobuf, &row);

		if (row.type == IPROTO_OK) {
			/* End of stream */
			say_info("done");
			break;
		} else if (iproto_type_is_dml(row.type)) {
			/* Regular snapshot row  (IPROTO_INSERT) */
			recovery_apply_row(r, &row);
		} else /* error or unexpected packet */ {
			xrow_decode_error(&row);  /* rethrow error */
		}
	}

	/* Decode end of stream packet */
	struct vclock vclock;
	vclock_create(&vclock);
	assert(row.type == IPROTO_OK);
	xrow_decode_vclock(&row, &vclock);

	/* Replace server vclock using data from snapshot */
	vclock_copy(&r->vclock, &vclock);

	/* master socket closed by guard */
}

static void
remote_set_status(struct remote *remote, const char *status)
{
	remote->status = status;
}

static void
pull_from_remote(va_list ap)
{
	struct recovery_state *r = va_arg(ap, struct recovery_state *);
	struct ev_io coio;
	struct iobuf *iobuf = iobuf_new(fiber_name(fiber()));
	ev_loop *loop = loop();

	coio_init(&coio);

	auto coio_guard = make_scoped_guard([&] {
		iobuf_delete(iobuf);
		evio_close(loop(), &coio);
	});

	while (true) {
		const char *err = NULL;
		try {
			struct xrow_header row;
			if (! evio_is_active(&coio)) {
				remote_set_status(&r->remote, "connecting");
				err = "can't connect to master";
				remote_connect(r, &coio, iobuf);
				/* Send SUBSCRIBE request */
				err = "can't subscribe to master";
				xrow_encode_subscribe(&row, &cluster_id,
					&r->server_uuid, &r->vclock);
				remote_write_row(&coio, &row);
				r->remote.warning_said = false;
				remote_set_status(&r->remote, "connected");
			}
			err = "can't read row";
			/**
			 * If there is an error in subscribe, it's
			 * sent directly in response to subscribe.
			 * If subscribe is successful, there is no
			 * "OK" response, but a stream of rows.
			 * from the binary log.
			 */
			remote_read_row(&coio, iobuf, &row);
			err = NULL;
			r->remote.lag = ev_now(loop) - row.tm;
			r->remote.last_row_time =
				ev_now(loop);

			if (iproto_type_is_error(row.type))
				xrow_decode_error(&row);  /* error */
			recovery_apply_row(r, &row);

			iobuf_reset(iobuf);
			fiber_gc();
		} catch (ClientError *e) {
			remote_set_status(&r->remote, "stopped");
			throw;
		} catch (FiberCancelException *e) {
			remote_set_status(&r->remote, "off");
			throw;
		} catch (Exception *e) {
			remote_set_status(&r->remote, "disconnected");
			if (! r->remote.warning_said) {
				if (err != NULL)
					say_info("%s", err);
				e->log();
				say_info("will retry every %i second",
					 RECONNECT_DELAY);
				r->remote.warning_said = true;
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
			fiber_sleep(RECONNECT_DELAY);
	}
}

void
recovery_follow_remote(struct recovery_state *r)
{
	char name[FIBER_NAME_MAX];

	assert(r->remote.reader == NULL);
	assert(recovery_has_remote(r));

	const char *uri = uri_format(&r->remote.uri);
	say_crit("starting replication from %s", uri);
	snprintf(name, sizeof(name), "replica/%s", uri);


	struct fiber *f = fiber_new(name, pull_from_remote);
	/**
	 * So that we can safely grab the status of the
	 * fiber any time we want.
	 */
	fiber_set_joinable(f, true);

	r->remote.reader = f;
	fiber_start(f, r);
}

void
recovery_stop_remote(struct recovery_state *r)
{
	say_info("shutting down the replica");
	struct fiber *f = r->remote.reader;
	r->remote.reader = NULL;
	fiber_cancel(f);
	/**
	 * If the remote died from an exception, don't throw it
	 * up.
	 */
	Exception::clear(&f->exception);
	fiber_join(f);
	r->remote.status = "off";
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
	snprintf(r->remote.source, sizeof(r->remote.source), "%s", uri);
	struct remote *remote = &r->remote;
	int rc = uri_parse(&remote->uri, r->remote.source);
	/* URI checked by box_check_replication_source() */
	assert(rc == 0 && remote->uri.service != NULL);
	(void) rc;
}

bool
recovery_has_remote(struct recovery_state *r)
{
	return r->remote.source[0];
}

void
recovery_init_remote(struct recovery_state *r)
{
	r->remote.status = "off";
}
