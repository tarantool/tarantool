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
#include "applier.h"

#include <msgpuck.h>

#include "xlog.h"
#include "fiber.h"
#include "scoped_guard.h"
#include "coio.h"
#include "coio_buf.h"
#include "xstream.h"
#include "recovery.h"
#include "wal.h"
#include "xrow.h"
#include "box/cluster.h"
#include "iproto_constants.h"
#include "version.h"
#include "trigger.h"
#include "xrow_io.h"
#include "error.h"

/* TODO: add configuration options */
static const int RECONNECT_DELAY = 1;
static const int CONNECT_TIMEOUT = 30;

STRS(applier_state, applier_STATE);

static inline void
applier_set_state(struct applier *applier, enum applier_state state)
{
	applier->state = state;
	say_debug("=> %s", applier_state_strs[state] +
		  strlen("APPLIER_"));
	trigger_run(&applier->on_state, applier);
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

	/*
	 * Forbid changing UUID dynamically on connect because
	 * applier is registered by UUID in cluster.h.
	 */
	if (!tt_uuid_is_nil(&applier->uuid) &&
	    !tt_uuid_is_equal(&applier->uuid, &greeting.uuid)) {
		Exception *e = tnt_error(ClientError, ER_SERVER_UUID_MISMATCH,
					 tt_uuid_str(&applier->uuid),
					 tt_uuid_str(&greeting.uuid));
		/* Log the error only once. */
		if (!applier->warning_said)
			e->log();
		e->raise();
	}

	/* Save the remote server version and UUID on connect. */
	applier->uuid = greeting.uuid;
	applier->version_id = greeting.version_id;

	say_info("connected to %u.%u.%u at %s\r\n",
		 version_id_major(greeting.version_id),
		 version_id_minor(greeting.version_id),
		 version_id_patch(greeting.version_id),
		 sio_strfaddr(&applier->addr, applier->addr_len));

	/* Don't display previous error messages in box.info.replication */
	diag_clear(&fiber()->diag);

	applier_set_state(applier, APPLIER_CONNECTED);

	/* Detect connection to itself */
	if (tt_uuid_is_equal(&applier->uuid, &SERVER_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/* Perform authentication if user provided at least login */
	if (!uri->login)
		return;

	/* Authenticate */
	applier_set_state(applier, APPLIER_AUTH);
	struct xrow_header row;
	xrow_encode_auth(&row, greeting.salt, greeting.salt_len, uri->login,
			 uri->login_len, uri->password,
			 uri->password_len);
	coio_write_xrow(coio, &row);
	coio_read_xrow(coio, &iobuf->in, &row);
	applier->last_row_time = ev_now(loop());
	if (row.type != IPROTO_OK)
		xrow_decode_error(&row); /* auth failed */

	/* auth successed */
	say_info("authenticated");
	applier_set_state(applier, APPLIER_CONNECTED);
}

/**
 * Execute and process JOIN request (bootstrap the server).
 */
static void
applier_join(struct applier *applier)
{
	/* Send JOIN request */
	struct ev_io *coio = &applier->io;
	struct iobuf *iobuf = applier->iobuf;
	struct xrow_header row;
	xrow_encode_join(&row, &SERVER_UUID);
	coio_write_xrow(coio, &row);

	/* Decode JOIN response */
	coio_read_xrow(coio, &iobuf->in, &row);
	if (iproto_type_is_error(row.type)) {
		return xrow_decode_error(&row); /* re-throw error */
	} else if (row.type != IPROTO_OK) {
		tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
			  (uint32_t) row.type);
	}

	applier_set_state(applier, APPLIER_INITIAL_JOIN);

	/*
	 * Receive initial data.
	 */
	assert(applier->initial_join_stream != NULL);
	while (true) {
		coio_read_xrow(coio, &iobuf->in, &row);
		applier->last_row_time = ev_now(loop());
		if (iproto_type_is_dml(row.type)) {
			xstream_write(applier->initial_join_stream, &row);
		} else if (row.type == IPROTO_OK) {
			break; /* end of stream */
		} else if (iproto_type_is_error(row.type)) {
			xrow_decode_error(&row);  /* rethrow error */
		} else {
			tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
				  (uint32_t) row.type);
		}
	}
	say_info("initial data received");

	applier_set_state(applier, APPLIER_FINAL_JOIN);

	/*
	 * Receive final data.
	 */
	assert(applier->final_join_stream != NULL);
	while (true) {
		coio_read_xrow(coio, &iobuf->in, &row);
		applier->last_row_time = ev_now(loop());
		if (iproto_type_is_dml(row.type)) {
			xstream_write(applier->final_join_stream, &row);
		} else if (row.type == IPROTO_OK) {
			break; /* end of stream */
		} else if (iproto_type_is_error(row.type)) {
			xrow_decode_error(&row);  /* rethrow error */
		} else {
			tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
				  (uint32_t) row.type);
		}
	}
	/* Decode end of stream packet */
	vclock_create(&applier->vclock);
	assert(row.type == IPROTO_OK);
	xrow_decode_vclock(&row, &applier->vclock);
	say_info("final data received");

	applier_set_state(applier, APPLIER_JOINED);
	applier_set_state(applier, APPLIER_CONNECTED);
}

/**
 * Execute and process SUBSCRIBE request (follow updates from a master).
 */
static void
applier_subscribe(struct applier *applier)
{
	assert(applier->subscribe_stream != NULL);

	/* Send SUBSCRIBE request */
	struct ev_io *coio = &applier->io;
	struct iobuf *iobuf = applier->iobuf;
	struct xrow_header row;

	/* TODO: don't use struct recovery here */
	struct recovery *r = ::recovery;
	xrow_encode_subscribe(&row, &CLUSTER_UUID, &SERVER_UUID, &r->vclock);
	coio_write_xrow(coio, &row);
	applier_set_state(applier, APPLIER_FOLLOW);
	/* Re-enable warnings after successful execution of SUBSCRIBE */
	applier->warning_said = false;
	vclock_create(&applier->vclock);

	/*
	 * Read SUBSCRIBE response
	 */
	if (applier->version_id >= version_id(1, 6, 7)) {
		coio_read_xrow(coio, &iobuf->in, &row);
		if (iproto_type_is_error(row.type)) {
			return xrow_decode_error(&row);  /* error */
		} else if (row.type != IPROTO_OK) {
			tnt_raise(ClientError, ER_PROTOCOL,
				  "Invalid response to SUBSCRIBE");
		}

		/*
		 * Don't overwrite applier->vclock before performing
		 * sanity checks for a valid server id.
		 */
		struct vclock vclock;
		vclock_create(&vclock);
		xrow_decode_vclock(&row, &vclock);

		/* Forbid changing the server_id */
		if (applier->id != 0 && applier->id != row.server_id) {
			Exception *e = tnt_error(ClientError,
						 ER_SERVER_ID_MISMATCH,
						 tt_uuid_str(&applier->uuid),
						 applier->id, row.server_id);
			/* Log the error at most once. */
			if (!applier->warning_said)
				e->log();
			e->raise();
		}

		/* Save the received server_id and vclock */
		applier->id = row.server_id;
		vclock_copy(&applier->vclock, &vclock);
	}
	/**
	 * Tarantool < 1.6.7:
	 * If there is an error in subscribe, it's sent directly
	 * in response to subscribe.  If subscribe is successful,
	 * there is no "OK" response, but a stream of rows from
	 * the binary log.
	 */

	/*
	 * Process a stream of rows from the binary log.
	 */
	while (true) {
		coio_read_xrow(coio, &iobuf->in, &row);
		applier->lag = ev_now(loop()) - row.tm;
		applier->last_row_time = ev_now(loop());

		if (iproto_type_is_error(row.type))
			xrow_decode_error(&row);  /* error */
		xstream_write(applier->subscribe_stream, &row);

		iobuf_reset(iobuf);
		fiber_gc();
	}
}

/**
 * Write a nice error message to log file on SocketError or ClientError
 * in applier_f().
 */
static inline void
applier_log_error(struct applier *applier, struct error *e)
{
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
	case APPLIER_INITIAL_JOIN:
	case APPLIER_FINAL_JOIN:
		say_info("can't read row");
		break;
	default:
		break;
	}
	error_log(e);
	if (type_cast(SocketError, e))
		say_info("will retry every %i second", RECONNECT_DELAY);
	applier->warning_said = true;
}

static inline void
applier_disconnect(struct applier *applier, enum applier_state state)
{
	coio_close(loop(), &applier->io);
	iobuf_reset(applier->iobuf);
	applier_set_state(applier, state);
	fiber_gc();
}

static int
applier_f(va_list ap)
{
	struct applier *applier = va_arg(ap, struct applier *);

	/* Re-connect loop */
	while (!fiber_is_cancelled()) {
		try {
			applier_connect(applier);
			if (tt_uuid_is_nil(&CLUSTER_UUID)) {
				/*
				 * Execute JOIN if this is a bootstrap,
				 * and there is no snapshot. The
				 * join will pause the applier
				 * until WAL is created.
				 */
				applier_join(applier);
			}
			applier_subscribe(applier);
			/*
			 * subscribe() has an infinite loop which
			 * is stoppable only with fiber_cancel().
			 */
			assert(0);
			return 0;
		} catch (ClientError *e) {
			if (e->errcode() == ER_CONNECTION_TO_SELF &&
			    tt_uuid_is_equal(&applier->uuid, &SERVER_UUID)) {
				/* Connection to itself, stop applier */
				applier_disconnect(applier, APPLIER_OFF);
				return 0;
			} else if (e->errcode() != ER_LOADING) {
				applier_log_error(applier, e);
				applier_disconnect(applier, APPLIER_STOPPED);
				throw;
			}
			assert(e->errcode() == ER_LOADING);
			/*
			 * Ignore ER_LOADING
			 */
			if (!applier->warning_said) {
				say_info("bootstrap in progress...");
				applier->warning_said = true;
			}
			applier_disconnect(applier, APPLIER_DISCONNECTED);
			/* fall through, try again later */
		} catch (FiberIsCancelled *e) {
			applier_disconnect(applier, APPLIER_OFF);
			throw;
		} catch (SocketError *e) {
			applier_log_error(applier, e);
			applier_disconnect(applier, APPLIER_DISCONNECTED);
			/* fall through */
		}
		/* Put fiber_sleep() out of catch block.
		 *
		 * This is done to avoid the case when two or more
		 * fibers yield inside their try/catch blocks and
		 * throw an exception. Seems like the exception unwinder
		 * uses global state inside the catch block.
		 *
		 * This could lead to incorrect exception processing
		 * and crash the server.
		 *
		 * See: https://github.com/tarantool/tarantool/issues/136
		*/
		fiber_sleep(RECONNECT_DELAY);
	}
	return 0;
}

void
applier_start(struct applier *applier)
{
	char name[FIBER_NAME_MAX];
	assert(applier->reader == NULL);

	const char *uri = uri_format(&applier->uri);
	snprintf(name, sizeof(name), "applier/%s", uri);

	struct fiber *f = fiber_new_xc(name, applier_f);
	/**
	 * So that we can safely grab the status of the
	 * fiber any time we want.
	 */
	fiber_set_joinable(f, true);
	applier->reader = f;
	fiber_start(f, applier);
}

void
applier_stop(struct applier *applier)
{
	struct fiber *f = applier->reader;
	if (f == NULL)
		return;
	fiber_cancel(f);
	fiber_join(f);
	applier_set_state(applier, APPLIER_OFF);
	applier->reader = NULL;
}

struct applier *
applier_new(const char *uri, struct xstream *initial_join_stream,
	    struct xstream *final_join_stream,
	    struct xstream *subscribe_stream)
{
	struct applier *applier = (struct applier *)
		calloc(1, sizeof(struct applier));
	if (applier == NULL) {
		diag_set(OutOfMemory, sizeof(*applier), "malloc",
			 "struct applier");
		return NULL;
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

	applier->initial_join_stream = initial_join_stream;
	applier->final_join_stream = final_join_stream;
	applier->subscribe_stream = subscribe_stream;
	applier->last_row_time = ev_now(loop());
	rlist_create(&applier->on_state);
	ipc_channel_create(&applier->pause, 0);

	return applier;
}

void
applier_delete(struct applier *applier)
{
	assert(applier->reader == NULL);
	iobuf_delete(applier->iobuf);
	assert(applier->io.fd == -1);
	ipc_channel_destroy(&applier->pause);
	trigger_destroy(&applier->on_state);
	free(applier);
}

void
applier_resume(struct applier *applier)
{
	assert(!fiber_is_dead(applier->reader));
	void *data = NULL;
	ipc_channel_put_xc(&applier->pause, data);
}

static inline void
applier_pause(struct applier *applier)
{
	/* Sleep until applier_resume() wake us up */
	void *data;
	ipc_channel_get_xc(&applier->pause, &data);
}

struct applier_on_state {
	struct trigger base;
	struct applier *applier;
	enum applier_state desired_state;
	struct ipc_channel wakeup;
};

/** Used by applier_connect_all() */
static void
applier_on_state_f(struct trigger *trigger, void *event)
{
	(void) event;
	struct applier_on_state *on_state =
		container_of(trigger, struct applier_on_state, base);

	struct applier *applier = on_state->applier;

	if (applier->state != APPLIER_OFF &&
	    applier->state != APPLIER_STOPPED &&
	    applier->state != on_state->desired_state)
		return;

	/* Wake up waiter */
	ipc_channel_put_xc(&on_state->wakeup, applier);

	applier_pause(applier);
}

static inline void
applier_add_on_state(struct applier *applier,
		     struct applier_on_state *trigger,
		     enum applier_state desired_state)
{
	trigger_create(&trigger->base, applier_on_state_f, NULL, NULL);
	trigger->applier = applier;
	ipc_channel_create(&trigger->wakeup, 0);
	trigger->desired_state = desired_state;
	trigger_add(&applier->on_state, &trigger->base);
}

static inline void
applier_clear_on_state(struct applier_on_state *trigger)
{
	ipc_channel_destroy(&trigger->wakeup);
	trigger_clear(&trigger->base);
}

static inline int
applier_wait_for_state(struct applier_on_state *trigger, double timeout)
{
	void *data = NULL;
	if (ipc_channel_get_timeout(&trigger->wakeup, &data, timeout) != 0)
		return -1; /* ER_TIMEOUT */

	struct applier *applier = trigger->applier;
	if (applier->state != trigger->desired_state) {
		assert(applier->state == APPLIER_OFF ||
		       applier->state == APPLIER_STOPPED);
		/* Re-throw the original error */
		assert(!diag_is_empty(&applier->reader->diag));
		diag_move(&applier->reader->diag, &fiber()->diag);
		return -1;
	}
	return 0;
}

void
applier_connect_all(struct applier **appliers, int count)
{
	if (count == 0)
		return; /* nothing to do */

	/*
	 * Simultaneously connect to remote peers to receive their UUIDs
	 * and fill the resulting set:
	 *
	 * - create a single control channel;
	 * - register a trigger in each applier to wake up our
	 *   fiber via this channel when the remote peer becomes
	 *   connected and a UUID is received;
	 * - wait up to CONNECT_TIMEOUT seconds for `count` messages;
	 * - on timeout, raise a CFG error, cancel and destroy
	 *   the freshly created appliers (done in a guard);
	 * - an success, unregister the trigger, check the UUID set
	 *   for duplicates, fill the result set, return.
	 */

	/* A channel from applier's on_state trigger is used to wake us up */
	IpcChannelGuard wakeup(count);
	/* Memory for on_state triggers registered in appliers */
	struct applier_on_state triggers[VCLOCK_MAX];
	/* Wait results until this time */
	double deadline = fiber_time() + CONNECT_TIMEOUT;

	/* Add triggers and start simulations connection to remote peers */
	for (int i = 0; i < count; i++) {
		/* Register a trigger to wake us up when peer is connected */
		applier_add_on_state(appliers[i], &triggers[i],
				     APPLIER_CONNECTED);
		/* Start background connection */
		applier_start(appliers[i]);
	}

	/* Wait for all appliers */
	for (int i = 0; i < count; i++) {
		double wait = deadline - fiber_time();
		if (wait < 0.0 ||
		    applier_wait_for_state(&triggers[i], wait) != 0) {
			goto error;
		}
	}


	for (int i = 0; i < count; i++) {
		assert(appliers[i]->state == APPLIER_CONNECTED);
		/* Unregister the temporary trigger used to wake us up */
		applier_clear_on_state(&triggers[i]);
	}

	/* Now all the appliers are connected, finish. */
	return;
error:
	/* Destroy appliers */
	for (int i = 0; i < count; i++) {
		applier_clear_on_state(&triggers[i]);
		applier_stop(appliers[i]);
	}

	/* ignore original error */
	tnt_raise(ClientError, ER_CFG, "replication_source",
		  "failed to connect to one or more servers");
}

void
applier_resume_to_state(struct applier *applier, enum applier_state state,
			double timeout)
{
	struct applier_on_state trigger;
	applier_add_on_state(applier, &trigger, state);
	applier_resume(applier);
	int rc = applier_wait_for_state(&trigger, timeout);
	applier_clear_on_state(&trigger);
	if (rc != 0)
		diag_raise();
	assert(applier->state == state);
}
