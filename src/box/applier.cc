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
#include "applier.h"

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
#include "version.h"

static const int RECONNECT_DELAY = 1.0;
STRS(applier_state, applier_STATE);

static inline void
applier_set_state(struct applier *applier, enum applier_state state)
{
	applier->state = state;
	say_debug("=> %s", applier_state_strs[state] +
		  strlen("APPLIER_"));
}

static void
applier_read_row(struct ev_io *coio, struct iobuf *iobuf,
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
applier_write_row(struct ev_io *coio, const struct xrow_header *row)
{
	struct iovec iov[XROW_IOVMAX];
	int iovcnt = xrow_to_iovec(row, iov);
	coio_writev(coio, iov, iovcnt, 0);
}

/**
 * Connect to a remote host and authenticate the client.
 */
void
applier_connect(struct applier *applier)
{
	struct ev_io *coio = &applier->io;
	struct iobuf *iobuf = applier->iobuf;
	if (coio->fd >= 0)
		return;
	char greetingbuf[IPROTO_GREETING_SIZE];

	struct uri *uri = &applier->uri;
	/*
	 * coio_connect() stores resolved address to \a &applier->addr
	 * on success. &applier->addr_len is a value-result argument which
	 * must be initialized to the size of associated buffer (addrstorage)
	 * before calling coio_connect(). Since coio_connect() performs
	 * DNS resolution under the hood it is theoretically possible that
	 * applier->addr_len will be different even for same uri.
	 */
	applier->addr_len = sizeof(applier->addrstorage);
	applier_set_state(applier, APPLIER_CONNECT);
	coio_connect(coio, uri, &applier->addr, &applier->addr_len);
	assert(coio->fd >= 0);
	coio_readn(coio, greetingbuf, IPROTO_GREETING_SIZE);
	applier->last_row_time = ev_now(loop());

	/* Decode server version and name from greeting */
	struct greeting greeting;
	if (greeting_decode(greetingbuf, &greeting) != 0)
		tnt_raise(LoggedError, ER_PROTOCOL, "Invalid greeting");

	if (strcmp(greeting.protocol, "Binary") != 0) {
		tnt_raise(LoggedError, ER_PROTOCOL,
			  "Unsupported protocol for replication");
	}

	applier->version_id = greeting.version_id;

	say_info("connected to %u.%u.%u at %s\r\n",
		 version_id_major(greeting.version_id),
		 version_id_minor(greeting.version_id),
		 version_id_patch(greeting.version_id),
		 sio_strfaddr(&applier->addr, applier->addr_len));

	/* Don't display previous error messages in box.info.replication */
	diag_clear(&fiber()->diag);

	/* Perform authentication if user provided at least login */
	if (!uri->login) {
		applier_set_state(applier, APPLIER_CONNECTED);
		return;
	}

	/* Authenticate */
	applier_set_state(applier, APPLIER_AUTH);
	struct xrow_header row;
	xrow_encode_auth(&row, greeting.salt, greeting.salt_len, uri->login,
			 uri->login_len, uri->password,
			 uri->password_len);
	applier_write_row(coio, &row);
	applier_read_row(coio, iobuf, &row);
	applier->last_row_time = ev_now(loop());
	if (row.type != IPROTO_OK)
		xrow_decode_error(&row); /* auth failed */

	/* auth successed */
	say_info("authenticated");
}

/**
 * Execute and process JOIN request (bootstrap the server).
 */
static void
applier_join(struct applier *applier, struct recovery *r)
{
	say_info("downloading a snapshot from %s",
		 sio_strfaddr(&applier->addr, applier->addr_len));

	/* Send JOIN request */
	struct ev_io *coio = &applier->io;
	struct iobuf *iobuf = applier->iobuf;
	struct xrow_header row;
	xrow_encode_join(&row, &r->server_uuid);
	applier_write_row(coio, &row);
	applier_set_state(applier, APPLIER_BOOTSTRAP);

	assert(vclock_has(&r->vclock, 0)); /* check for surrogate server_id */

	while (true) {
		applier_read_row(coio, iobuf, &row);
		applier->last_row_time = ev_now(loop());
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
	vclock_create(&applier->vclock);
	assert(row.type == IPROTO_OK);
	xrow_decode_vclock(&row, &applier->vclock);

	/* Replace server vclock using data from snapshot */
	vclock_copy(&r->vclock, &applier->vclock);

	/* Re-enable warnings after successful execution of JOIN */
	applier_set_state(applier, APPLIER_CONNECTED);
	/* keep connection */
}

/**
 * Execute and process SUBSCRIBE request (follow updates from a master).
 */
static void
applier_subscribe(struct applier *applier, struct recovery *r)
{
	/* Send SUBSCRIBE request */
	struct ev_io *coio = &applier->io;
	struct iobuf *iobuf = applier->iobuf;
	struct xrow_header row;

	xrow_encode_subscribe(&row, &cluster_id, &r->server_uuid, &r->vclock);
	applier_write_row(coio, &row);
	applier_set_state(applier, APPLIER_FOLLOW);
	/* Re-enable warnings after successful execution of SUBSCRIBE */
	applier->warning_said = false;
	vclock_create(&applier->vclock);

	/*
	 * Read SUBSCRIBE response
	 */
	if (applier->version_id >= version_id(1, 6, 7)) {
		applier_read_row(coio, iobuf, &row);
		if (iproto_type_is_error(row.type)) {
			return xrow_decode_error(&row);  /* error */
		} else if (row.type != IPROTO_OK) {
			tnt_raise(ClientError, ER_PROTOCOL,
				  "Invalid response to SUBSCRIBE");
		}

		xrow_decode_vclock(&row, &applier->vclock);
		applier->id = row.server_id;
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
		applier_read_row(coio, iobuf, &row);
		applier->lag = ev_now(loop()) - row.tm;
		applier->last_row_time = ev_now(loop());

		if (iproto_type_is_error(row.type))
			xrow_decode_error(&row);  /* error */
		recovery_apply_row(r, &row);

		iobuf_reset(iobuf);
		fiber_gc();
	}
}

/**
 * Write a nice error message to log file on SocketError or ClientError
 * in applier_f().
 */
static inline void
applier_log_exception(struct applier *applier, Exception *e)
{
	if (type_cast(FiberCancelException, e))
		return;
	if (applier->warning_said)
		return;
	switch (applier->state) {
	case APPLIER_CONNECT:
		say_info("can't connect to master");
		break;
	case APPLIER_CONNECTED:
		say_info("can't join/subscribe");
		break;
	case APPLIER_AUTH:
		say_info("failed to authenticate");
		break;
	case APPLIER_FOLLOW:
	case APPLIER_BOOTSTRAP:
		say_info("can't read row");
		break;
	default:
		break;
	}
	e->log();
	if (type_cast(SocketError, e))
		say_info("will retry every %i second", RECONNECT_DELAY);
	applier->warning_said = true;
}

static inline void
applier_disconnect(struct applier *applier, Exception *e,
		   enum applier_state state)
{
	applier_log_exception(applier, e);
	coio_close(loop(), &applier->io);
	iobuf_reset(applier->iobuf);
	applier_set_state(applier, state);
	fiber_gc();
}

static void
applier_f(va_list ap)
{
	struct applier *applier = va_arg(ap, struct applier *);
	struct recovery *r = va_arg(ap, struct recovery *);

	/* Re-connect loop */
	while (true) {
		try {
			applier_connect(applier);
			/*
			 * Execute JOIN if this is a bootstrap, and
			 * there is no snapshot, and SUBSCRIBE
			 * otherwise.
			 */
			if (r->writer == NULL) {
				applier_join(applier, r);
			} else {
				applier_subscribe(applier, r);
				/*
				 * subscribe() has an infinite
				 * loop which is stoppable only
				 * with fiber_cancel()
				 */
				assert(0);
			}
			ev_io_stop(loop(), &applier->io);
			iobuf_reset(applier->iobuf);
			/* Don't close the socket */
			return;
		} catch (ClientError *e) {
			applier_disconnect(applier, e, APPLIER_STOPPED);
			throw;
		} catch (FiberCancelException *e) {
			applier_disconnect(applier, e, APPLIER_OFF);
			throw;
		} catch (SocketError *e) {
			applier_disconnect(applier, e, APPLIER_DISCONNECTED);
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
		fiber_testcancel();
	}
}

void
applier_start(struct applier *applier, struct recovery *r)
{
	char name[FIBER_NAME_MAX];
	assert(applier->reader == NULL);

	const char *uri = uri_format(&applier->uri);
	if (applier->io.fd < 0)
		say_crit("starting replication from %s", uri);
	snprintf(name, sizeof(name), "applier/%s", uri);

	struct fiber *f = fiber_new(name, applier_f);
	/**
	 * So that we can safely grab the status of the
	 * fiber any time we want.
	 */
	fiber_set_joinable(f, true);
	applier->reader = f;
	fiber_start(f, applier, r);
}

void
applier_stop(struct applier *applier)
{
	struct fiber *f = applier->reader;
	if (f == NULL)
		return;
	const char *uri = uri_format(&applier->uri);
	say_crit("shutting down applier %s", uri);
	fiber_cancel(f);
	fiber_join(f);
	applier_set_state(applier, APPLIER_OFF);
	applier->reader = NULL;
}

void
applier_wait(struct applier *applier)
{
	assert(applier->reader != NULL);
	auto fiber_guard = make_scoped_guard([=] { applier->reader = NULL; });
	fiber_join(applier->reader);
	fiber_testerror();
}

struct applier *
applier_new(const char *uri)
{
	struct applier *applier = (struct applier *)
		calloc(1, sizeof(struct applier));
	if (applier == NULL) {
		tnt_raise(OutOfMemory, sizeof(*applier), "malloc",
			  "struct applier");
	}
	coio_init(&applier->io, -1);
	applier->iobuf = iobuf_new();
	vclock_create(&applier->vclock);

	/* uri_parse() sets pointers to applier->source buffer */
	snprintf(applier->source, sizeof(applier->source), "%s", uri);
	int rc = uri_parse(&applier->uri, applier->source);
	/* URI checked by box_check_replication_source() */
	assert(rc == 0 && applier->uri.service != NULL);
	(void) rc;

	applier->last_row_time = ev_now(loop());
	return applier;
}

void
applier_delete(struct applier *applier)
{
	assert(applier->reader == NULL);
	iobuf_delete(applier->iobuf);
	coio_close(loop(), &applier->io);
	free(applier);
}
