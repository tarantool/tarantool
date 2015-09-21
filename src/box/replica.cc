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

#include "xlog.h"
#include "fiber.h"
#include "scoped_guard.h"
#include "coio.h"
#include "coio_buf.h"
#include "recovery.h"
#include "xrow.h"
#include "msgpuck/msgpuck.h"
#include "box/cluster.h"
#include "iproto_constants.h"
#include "trivia/util.h"

static const int RECONNECT_DELAY = 1.0;
STRS(replica_state, replica_STATE);

static inline void
replica_set_state(struct replica *replica, enum replica_state state)
{
	replica->state = state;
	say_debug("=> %s", replica_state_strs[state] +
		  strlen("REPLICA_"));
}

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

/**
 * Connect to a remote host and authenticate the client.
 */
void
replica_connect(struct replica *replica)
{
	struct ev_io *coio = &replica->io;
	struct iobuf *iobuf = replica->iobuf;
	if (coio->fd >= 0)
		return;
	char greetingbuf[IPROTO_GREETING_SIZE];

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
	replica_set_state(replica, REPLICA_CONNECT);
	coio_connect(coio, uri, &replica->addr, &replica->addr_len);
	assert(coio->fd >= 0);
	coio_readn(coio, greetingbuf, IPROTO_GREETING_SIZE);
	replica->last_row_time = ev_now(loop());

	/* Decode server version and name from greeting */
	struct greeting greeting;
	if (greeting_decode(greetingbuf, &greeting) != 0)
		tnt_raise(LoggedError, ER_PROTOCOL, "Invalid greeting");

	if (strcmp(greeting.protocol, "Binary") != 0) {
		tnt_raise(LoggedError, ER_PROTOCOL,
			  "Unsupported protocol for replication");
	}

	replica->version_id = greeting.version_id;

	say_info("connected to %s at %s\r\n", greeting.version,
		 sio_strfaddr(&replica->addr, replica->addr_len));

	/* Don't display previous error messages in box.info.replication */
	diag_clear(&fiber()->diag);

	/* Perform authentication if user provided at least login */
	if (!uri->login) {
		replica_set_state(replica, REPLICA_CONNECTED);
		return;
	}

	/* Authenticate */
	replica_set_state(replica, REPLICA_AUTH);
	struct xrow_header row;
	xrow_encode_auth(&row, greeting.salt, greeting.salt_len, uri->login,
			 uri->login_len, uri->password,
			 uri->password_len);
	replica_write_row(coio, &row);
	replica_read_row(coio, iobuf, &row);
	replica->last_row_time = ev_now(loop());
	if (row.type != IPROTO_OK)
		xrow_decode_error(&row); /* auth failed */

	/* auth successed */
	say_info("authenticated");
}

/**
 * Execute and process JOIN request (bootstrap the server).
 */
static void
replica_join(struct replica *replica, struct recovery *r)
{
	say_info("downloading a snapshot from %s",
		 sio_strfaddr(&replica->addr, replica->addr_len));

	/* Send JOIN request */
	struct ev_io *coio = &replica->io;
	struct iobuf *iobuf = replica->iobuf;
	struct xrow_header row;
	xrow_encode_join(&row, &r->server_uuid);
	replica_write_row(coio, &row);
	replica_set_state(replica, REPLICA_BOOTSTRAP);

	assert(vclock_has(&r->vclock, 0)); /* check for surrogate server_id */

	while (true) {
		replica_read_row(coio, iobuf, &row);
		replica->last_row_time = ev_now(loop());
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
	vclock_create(&replica->vclock);
	assert(row.type == IPROTO_OK);
	xrow_decode_vclock(&row, &replica->vclock);

	/* Replace server vclock using data from snapshot */
	vclock_copy(&r->vclock, &replica->vclock);

	/* Re-enable warnings after successful execution of JOIN */
	replica_set_state(replica, REPLICA_CONNECTED);
	/* keep connection */
}

/**
 * Execute and process SUBSCRIBE request (follow updates from a master).
 */
static void
replica_subscribe(struct replica *replica, struct recovery *r)
{
	/* Send SUBSCRIBE request */
	struct ev_io *coio = &replica->io;
	struct iobuf *iobuf = replica->iobuf;
	struct xrow_header row;

	xrow_encode_subscribe(&row, &cluster_id, &r->server_uuid, &r->vclock);
	replica_write_row(coio, &row);
	replica_set_state(replica, REPLICA_FOLLOW);
	/* Re-enable warnings after successful execution of SUBSCRIBE */
	replica->warning_said = false;
	vclock_create(&replica->vclock);

	/*
	 * Read SUBSCRIBE response
	 */
	if (replica->version_id >= version_id(1, 6, 7)) {
		replica_read_row(coio, iobuf, &row);
		if (iproto_type_is_error(row.type)) {
			return xrow_decode_error(&row);  /* error */
		} else if (row.type != IPROTO_OK) {
			tnt_raise(ClientError, ER_PROTOCOL,
				  "Invalid response to SUBSCRIBE");
		}

		xrow_decode_vclock(&row, &replica->vclock);
		replica->id = row.server_id;
	}
	/**
	 * Tarantool < 1.6.7:
	 * If there is an error in subscribe, it's
	 * sent directly in response to subscribe.
	 * If subscribe is successful, there is no
	 * "OK" response, but a stream of rows.
	 * from the binary log.
	 */

	/*
	 * Process a stream of rows from the binary log.
	 */
	while (true) {
		replica_read_row(coio, iobuf, &row);
		replica->lag = ev_now(loop()) - row.tm;
		replica->last_row_time = ev_now(loop());

		if (iproto_type_is_error(row.type))
			xrow_decode_error(&row);  /* error */
		recovery_apply_row(r, &row);

		iobuf_reset(iobuf);
		fiber_gc();
	}
}

/**
 * Write a nice error message to log file on SocketError or ClientError
 * in replica_f().
 */
static inline void
replica_log_exception(struct replica *replica, Exception *e)
{
	if (type_cast(FiberCancelException, e))
		return;
	if (replica->warning_said)
		return;
	switch (replica->state) {
	case REPLICA_CONNECT:
		say_info("can't connect to master");
		break;
	case REPLICA_CONNECTED:
		say_info("can't join/subscribe");
		break;
	case REPLICA_AUTH:
		say_info("failed to authenticate");
		break;
	case REPLICA_FOLLOW:
	case REPLICA_BOOTSTRAP:
		say_info("can't read row");
		break;
	default:
		break;
	}
	e->log();
	if (type_cast(SocketError, e))
		say_info("will retry every %i second", RECONNECT_DELAY);
	replica->warning_said = true;
}

static inline void
replica_disconnect(struct replica *replica, Exception *e,
		   enum replica_state state)
{
	replica_log_exception(replica, e);
	coio_close(loop(), &replica->io);
	iobuf_reset(replica->iobuf);
	replica_set_state(replica, state);
	fiber_gc();
}

static void
replica_f(va_list ap)
{
	struct replica *replica = va_arg(ap, struct replica *);
	struct recovery *r = va_arg(ap, struct recovery *);

	/* Re-connect loop */
	while (true) {
		try {
			replica_connect(replica);
			/*
			 * Execute JOIN if this is a bootstrap, and
			 * there is no snapshot, and SUBSCRIBE
			 * otherwise.
			 */
			if (r->writer == NULL) {
				replica_join(replica, r);
			} else {
				replica_subscribe(replica, r);
				/*
				 * subscribe() has an infinite
				 * loop which is stoppable only
				 * with fiber_cancel()
				 */
				assert(0);
			}
			ev_io_stop(loop(), &replica->io);
			iobuf_reset(replica->iobuf);
			/* Don't close the socket */
			return;
		} catch (ClientError *e) {
			replica_disconnect(replica, e, REPLICA_STOPPED);
			throw;
		} catch (FiberCancelException *e) {
			replica_disconnect(replica, e, REPLICA_OFF);
			throw;
		} catch (SocketError *e) {
			replica_disconnect(replica, e, REPLICA_DISCONNECTED);
			/* fall through */
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
		fiber_sleep(RECONNECT_DELAY);
	}
}

void
replica_start(struct replica *replica, struct recovery *r)
{
	char name[FIBER_NAME_MAX];
	assert(replica->reader == NULL);

	const char *uri = uri_format(&replica->uri);
	if (replica->io.fd < 0)
		say_crit("starting replication from %s", uri);
	snprintf(name, sizeof(name), "replica/%s", uri);

	struct fiber *f = fiber_new(name, replica_f);
	/**
	 * So that we can safely grab the status of the
	 * fiber any time we want.
	 */
	fiber_set_joinable(f, true);
	replica->reader = f;
	fiber_start(f, replica, r);
}

void
replica_stop(struct replica *replica)
{
	struct fiber *f = replica->reader;
	if (f == NULL)
		return;
	const char *uri = uri_format(&replica->uri);
	say_crit("shutting down replica %s", uri);
	fiber_cancel(f);
	/**
	 * If the replica died from an exception, don't throw it
	 * up.
	 */
	diag_clear(&f->diag);
	fiber_join(f); /* doesn't throw due do diag_clear() */
	replica_set_state(replica, REPLICA_OFF);
	replica->reader = NULL;
}

void
replica_wait(struct replica *replica)
{
	assert(replica->reader != NULL);
	auto fiber_guard = make_scoped_guard([=] { replica->reader = NULL; });
	fiber_join(replica->reader); /* may throw */
}

struct replica *
replica_new(const char *uri)
{
	struct replica *replica = (struct replica *)
		calloc(1, sizeof(struct replica));
	if (replica == NULL) {
		tnt_raise(OutOfMemory, sizeof(*replica), "malloc",
			  "struct replica");
	}
	coio_init(&replica->io, -1);
	replica->iobuf = iobuf_new();
	vclock_create(&replica->vclock);

	/* uri_parse() sets pointers to replica->source buffer */
	snprintf(replica->source, sizeof(replica->source), "%s", uri);
	int rc = uri_parse(&replica->uri, replica->source);
	/* URI checked by box_check_replication_source() */
	assert(rc == 0 && replica->uri.service != NULL);
	(void) rc;

	replica->last_row_time = ev_now(loop());
	return replica;
}

void
replica_delete(struct replica *replica)
{
	assert(replica->reader == NULL);
	iobuf_delete(replica->iobuf);
	coio_close(loop(), &replica->io);
	free(replica);
}
