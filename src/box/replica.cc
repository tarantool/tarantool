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
replica_read_row(struct ev_io *coio, struct iobuf *iobuf,
		 struct xrow_header *row)
{
	struct ibuf *in = &iobuf->in;

	/* Read fixed header */
	if (ibuf_used(in) < 1)
		coio_breadn(coio, in, 1);

	/* Read length */
	if (mp_typeof(*in->rpos) != MP_UINT) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "packet length");
	}
	ssize_t to_read = mp_check_uint(in->rpos, in->wpos);
	if (to_read > 0)
		coio_breadn(coio, in, to_read);

	uint32_t len = mp_decode_uint((const char **) &in->rpos);

	/* Read header and body */
	to_read = len - ibuf_used(in);
	if (to_read > 0)
		coio_breadn(coio, in, to_read);

	xrow_header_decode(row, (const char **) &in->rpos, in->rpos + len);
}

static void
replica_write_row(struct ev_io *coio, const struct xrow_header *row)
{
	struct iovec iov[XROW_IOVMAX];
	int iovcnt = xrow_to_iovec(row, iov);
	coio_writev(coio, iov, iovcnt, 0);
}

static void
replica_connect(struct recovery_state *r, struct ev_io *coio,
	        struct iobuf *iobuf)
{
	char greeting[IPROTO_GREETING_SIZE];

	struct replica *replica = &r->replica;
	struct uri *uri = &replica->uri;
	/*
	 * coio_connect() stores resolved address to \a &replica->addr
	 * on success. &replica->addr_len is a value-result argument which
	 * must be initialized to the size of associated buffer (addrstorage)
	 * before calling coio_connect(). Since coio_connect() performs
	 * DNS resolution under the hood it is theoretically possible that
	 * replica->addr_len will be different even for same uri.
	 */
	replica->addr_len = sizeof(replica->addrstorage);
	/* Prepare null-terminated strings for coio_connect() */
	coio_connect(coio, uri, &replica->addr, &replica->addr_len);
	assert(coio->fd >= 0);
	coio_readn(coio, greeting, sizeof(greeting));

	say_crit("connected to %s", sio_strfaddr(&replica->addr,
						 replica->addr_len));

	/* Perform authentication if user provided at least login */
	if (!replica->uri.login)
		return;

	/* Authenticate */
	say_debug("authenticating...");
	struct xrow_header row;
	xrow_encode_auth(&row, greeting, uri->login,
			 uri->login_len, uri->password,
			 uri->password_len);
	replica_write_row(coio, &row);
	replica_read_row(coio, iobuf, &row);
	if (row.type != IPROTO_OK)
		xrow_decode_error(&row); /* auth failed */

	/* auth successed */
	say_info("authenticated");
}

void
replica_bootstrap(struct recovery_state *r)
{
	say_info("bootstrapping a replica");
	struct replica *replica = &r->replica;
	assert(recovery_has_replica(r));

	/* Generate Server-UUID */
	tt_uuid_create(&r->server_uuid);

	struct ev_io coio;
	coio_init(&coio);
	struct iobuf *iobuf = iobuf_new();
	auto coio_guard = make_scoped_guard([&] {
		iobuf_delete(iobuf);
		evio_close(loop(), &coio);
	});

	for (;;) {
		try {
			replica_connect(r, &coio, iobuf);
			replica->warning_said = false;
			break;
		} catch (FiberCancelException *e) {
			throw;
		} catch (Exception *e) {
			if (! replica->warning_said) {
				say_error("can't connect to master");
				e->log();
				say_info("will retry every %i second",
					 RECONNECT_DELAY);
				replica->warning_said = true;
			}
			iobuf_reset(iobuf);
			evio_close(loop(), &coio);
		}
		fiber_sleep(RECONNECT_DELAY);
	}

	/* Send JOIN request */
	struct xrow_header row;
	xrow_encode_join(&row, &r->server_uuid);
	replica_write_row(&coio, &row);

	/* Add a surrogate server id for snapshot rows */
	vclock_add_server(&r->vclock, 0);

	while (true) {
		replica_read_row(&coio, iobuf, &row);

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
replica_set_status(struct replica *replica, const char *status)
{
	replica->status = status;
}

static void
pull_from_replica(va_list ap)
{
	struct recovery_state *r = va_arg(ap, struct recovery_state *);
	struct replica *replica = &r->replica;
	struct ev_io coio;
	struct iobuf *iobuf = iobuf_new();
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
			if (! evio_has_fd(&coio)) {
				replica_set_status(replica, "connecting");
				err = "can't connect to master";
				replica_connect(r, &coio, iobuf);
				/* Send SUBSCRIBE request */
				err = "can't subscribe to master";
				xrow_encode_subscribe(&row, &cluster_id,
					&r->server_uuid, &r->vclock);
				replica_write_row(&coio, &row);
				replica->warning_said = false;
				replica_set_status(replica, "connected");
			}
			err = "can't read row";
			/**
			 * If there is an error in subscribe, it's
			 * sent directly in response to subscribe.
			 * If subscribe is successful, there is no
			 * "OK" response, but a stream of rows.
			 * from the binary log.
			 */
			replica_read_row(&coio, iobuf, &row);
			err = NULL;
			replica->lag = ev_now(loop) - row.tm;
			replica->last_row_time = ev_now(loop);

			if (iproto_type_is_error(row.type))
				xrow_decode_error(&row);  /* error */
			recovery_apply_row(r, &row);

			iobuf_reset(iobuf);
			fiber_gc();
		} catch (ClientError *e) {
			replica_set_status(replica, "stopped");
			throw;
		} catch (FiberCancelException *e) {
			replica_set_status(replica, "off");
			throw;
		} catch (Exception *e) {
			replica_set_status(replica, "disconnected");
			if (!replica->warning_said) {
				if (err != NULL)
					say_info("%s", err);
				e->log();
				say_info("will retry every %i second",
					 RECONNECT_DELAY);
				replica->warning_said = true;
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
		if (! evio_has_fd(&coio))
			fiber_sleep(RECONNECT_DELAY);
	}
}

void
recovery_follow_replica(struct recovery_state *r)
{
	char name[FIBER_NAME_MAX];
	struct replica *replica = &r->replica;

	assert(replica->reader == NULL);
	assert(recovery_has_replica(r));

	const char *uri = uri_format(&replica->uri);
	say_crit("starting replication from %s", uri);
	snprintf(name, sizeof(name), "replica/%s", uri);


	struct fiber *f = fiber_new(name, pull_from_replica);
	/**
	 * So that we can safely grab the status of the
	 * fiber any time we want.
	 */
	fiber_set_joinable(f, true);

	replica->reader = f;
	fiber_start(f, r);
}

void
recovery_stop_replica(struct recovery_state *r)
{
	say_info("shutting down the replica");
	struct replica *replica = &r->replica;
	struct fiber *f = replica->reader;
	replica->reader = NULL;
	fiber_cancel(f);
	/**
	 * If the replica died from an exception, don't throw it
	 * up.
	 */
	diag_clear(&f->diag);
	fiber_join(f);
	replica->status = "off";
}

void
recovery_set_replica(struct recovery_state *r, const char *uri)
{
	/* First, stop the reader, then set the source */
	struct replica *replica = &r->replica;
	assert(replica->reader == NULL);
	if (uri == NULL) {
		replica->source[0] = '\0';
		return;
	}
	snprintf(replica->source, sizeof(replica->source), "%s", uri);
	int rc = uri_parse(&replica->uri, replica->source);
	/* URI checked by box_check_replication_source() */
	assert(rc == 0 && replica->uri.service != NULL);
	(void) rc;
}

bool
recovery_has_replica(struct recovery_state *r)
{
	return r->replica.source[0];
}

void
recovery_init_replica(struct recovery_state *r)
{
	r->replica.status = "off";
}
