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

#include "box.h"
#include "xlog.h"
#include "fiber.h"
#include "scoped_guard.h"
#include "coio.h"
#include "coio_buf.h"
#include "xstream.h"
#include "wal.h"
#include "xrow.h"
#include "replication.h"
#include "iproto_constants.h"
#include "version.h"
#include "trigger.h"
#include "xrow_io.h"
#include "error.h"
#include "session.h"

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
 * Write a nice error message to log file on SocketError or ClientError
 * in applier_f().
 */
static inline void
applier_log_error(struct applier *applier, struct error *e)
{
	uint32_t errcode = box_error_code(e);
	if (applier->last_logged_errcode == errcode)
		return;
	switch (applier->state) {
	case APPLIER_CONNECT:
		say_info("can't connect to master");
		break;
	case APPLIER_CONNECTED:
	case APPLIER_READY:
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
		say_info("will retry every %.2lf second",
			 replication_timeout);
	applier->last_logged_errcode = errcode;
}

/*
 * Fiber function to write vclock to replication master.
 */
static int
applier_writer_f(va_list ap)
{
	struct applier *applier = va_arg(ap, struct applier *);
	struct ev_io io;
	coio_create(&io, applier->io.fd);

	/* Re-connect loop */
	while (!fiber_is_cancelled()) {
		fiber_cond_wait_timeout(&applier->writer_cond,
					replication_timeout);
		/* Send ACKs only when in FINAL JOIN and FOLLOW modes */
		if (applier->state != APPLIER_FOLLOW &&
		    applier->state != APPLIER_FINAL_JOIN)
			continue;
		try {
			struct xrow_header xrow;
			xrow_encode_vclock(&xrow, &replicaset_vclock);
			coio_write_xrow(&io, &xrow);
		} catch (SocketError *e) {
			/*
			 * Do not exit, if there is a network error,
			 * the reader fiber will reconnect for us
			 * and signal our cond afterwards.
			 */
			e->log();
		} catch (Exception *e) {
			/*
			 * Out of memory encoding the message, ignore
			 * and try again after an interval.
			 */
			e->log();
		}
		fiber_gc();
	}
	return 0;
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
	applier->last_row_time = ev_monotonic_now(loop());

	/* Decode instance version and name from greeting */
	struct greeting greeting;
	if (greeting_decode(greetingbuf, &greeting) != 0)
		tnt_raise(LoggedError, ER_PROTOCOL, "Invalid greeting");

	if (strcmp(greeting.protocol, "Binary") != 0) {
		tnt_raise(LoggedError, ER_PROTOCOL,
			  "Unsupported protocol for replication");
	}

	/*
	 * Forbid changing UUID dynamically on connect because
	 * applier is registered by UUID in the replica set.
	 */
	if (!tt_uuid_is_nil(&applier->uuid) &&
	    !tt_uuid_is_equal(&applier->uuid, &greeting.uuid)) {
		Exception *e = tnt_error(ClientError, ER_INSTANCE_UUID_MISMATCH,
					 tt_uuid_str(&applier->uuid),
					 tt_uuid_str(&greeting.uuid));
		applier_log_error(applier, e);
		e->raise();
	}

	if (applier->version_id != greeting.version_id) {
		say_info("remote master is %u.%u.%u at %s\r\n",
			 version_id_major(greeting.version_id),
			 version_id_minor(greeting.version_id),
			 version_id_patch(greeting.version_id),
			 sio_strfaddr(&applier->addr, applier->addr_len));
	}

	/* Save the remote instance version and UUID on connect. */
	applier->uuid = greeting.uuid;
	applier->version_id = greeting.version_id;

	/* Don't display previous error messages in box.info.replication */
	diag_clear(&fiber()->diag);

	applier_set_state(applier, APPLIER_CONNECTED);

	/* Detect connection to itself */
	if (tt_uuid_is_equal(&applier->uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/* Perform authentication if user provided at least login */
	if (!uri->login)
		goto done;

	/* Authenticate */
	applier_set_state(applier, APPLIER_AUTH);
	struct xrow_header row;
	xrow_encode_auth_xc(&row, greeting.salt, greeting.salt_len, uri->login,
			    uri->login_len, uri->password, uri->password_len);
	coio_write_xrow(coio, &row);
	coio_read_xrow(coio, &iobuf->in, &row);
	applier->last_row_time = ev_monotonic_now(loop());
	if (row.type != IPROTO_OK)
		xrow_decode_error_xc(&row); /* auth failed */

done:
	/* auth succeeded */
	say_info("authenticated");
	applier_set_state(applier, APPLIER_READY);

	if (applier->version_id >= version_id(1, 7, 4)) {
		/* Enable replication ACKs for newer servers */
		assert(applier->writer == NULL);

		char name[FIBER_NAME_MAX];
		int pos = snprintf(name, sizeof(name), "applierw/");
		uri_format(name + pos, sizeof(name) - pos, &applier->uri, false);

		applier->writer = fiber_new_xc(name, applier_writer_f);
		fiber_set_joinable(applier->writer, true);
		fiber_start(applier->writer, applier);
	}
}

/**
 * Execute and process JOIN request (bootstrap the instance).
 */
static void
applier_join(struct applier *applier)
{
	/* Send JOIN request */
	struct ev_io *coio = &applier->io;
	struct iobuf *iobuf = applier->iobuf;
	struct xrow_header row;
	xrow_encode_join_xc(&row, &INSTANCE_UUID);
	coio_write_xrow(coio, &row);

	/**
	 * Tarantool < 1.7.0: if JOIN is successful, there is no "OK"
	 * response, but a stream of rows from snapshot.
	 */
	if (applier->version_id >= version_id(1, 7, 0)) {
		/* Decode JOIN response */
		coio_read_xrow(coio, &iobuf->in, &row);
		if (iproto_type_is_error(row.type)) {
			xrow_decode_error_xc(&row); /* re-throw error */
		} else if (row.type != IPROTO_OK) {
			tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
				  (uint32_t) row.type);
		}
		/*
		 * Start vclock. The vclock of the checkpoint
		 * the master is sending to the replica.
		 * Used to initialize the replica's initial
		 * vclock in bootstrap_from_master()
		 */
		xrow_decode_vclock_xc(&row, &replicaset_vclock);
	}

	applier_set_state(applier, APPLIER_INITIAL_JOIN);

	/*
	 * Receive initial data.
	 */
	assert(applier->join_stream != NULL);
	while (true) {
		coio_read_xrow(coio, &iobuf->in, &row);
		applier->last_row_time = ev_monotonic_now(loop());
		if (iproto_type_is_dml(row.type)) {
			xstream_write_xc(applier->join_stream, &row);
		} else if (row.type == IPROTO_OK) {
			if (applier->version_id < version_id(1, 7, 0)) {
				/*
				 * This is the start vclock if the
				 * server is 1.6. Since we have
				 * not initialized replication
				 * vclock yet, do it now. In 1.7+
				 * this vlcock is not used.
				 */
				xrow_decode_vclock_xc(&row, &replicaset_vclock);
			}
			break; /* end of stream */
		} else if (iproto_type_is_error(row.type)) {
			xrow_decode_error_xc(&row);  /* rethrow error */
		} else {
			tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
				  (uint32_t) row.type);
		}
	}
	say_info("initial data received");

	applier_set_state(applier, APPLIER_FINAL_JOIN);

	/*
	 * Tarantool < 1.7.0: there is no "final join" stage.
	 */
	if (applier->version_id < version_id(1, 7, 0))
		goto finish;

	/*
	 * Receive final data.
	 */
	while (true) {
		coio_read_xrow(coio, &iobuf->in, &row);
		applier->last_row_time = ev_monotonic_now(loop());
		if (iproto_type_is_dml(row.type)) {
			vclock_follow(&replicaset_vclock, row.replica_id,
				      row.lsn);
			xstream_write_xc(applier->subscribe_stream, &row);
		} else if (row.type == IPROTO_OK) {
			/*
			 * Current vclock. This is not used now,
			 * ignore.
			 */
			break; /* end of stream */
		} else if (iproto_type_is_error(row.type)) {
			xrow_decode_error_xc(&row);  /* rethrow error */
		} else {
			tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
				  (uint32_t) row.type);
		}
	}
finish:
	say_info("final data received");

	applier_set_state(applier, APPLIER_JOINED);
	applier_set_state(applier, APPLIER_READY);
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

	xrow_encode_subscribe_xc(&row, &REPLICASET_UUID, &INSTANCE_UUID,
				 &replicaset_vclock);
	coio_write_xrow(coio, &row);
	applier_set_state(applier, APPLIER_FOLLOW);

	/*
	 * Read SUBSCRIBE response
	 */
	if (applier->version_id >= version_id(1, 6, 7)) {
		coio_read_xrow(coio, &iobuf->in, &row);
		if (iproto_type_is_error(row.type)) {
			xrow_decode_error_xc(&row);  /* error */
		} else if (row.type != IPROTO_OK) {
			tnt_raise(ClientError, ER_PROTOCOL,
				  "Invalid response to SUBSCRIBE");
		}
		/*
		 * In case of successful subscribe, the server
		 * responds with its current vclock.
		 */
		struct vclock vclock;
		vclock_create(&vclock);
		xrow_decode_vclock_xc(&row, &vclock);
	}
	/**
	 * Tarantool < 1.6.7:
	 * If there is an error in subscribe, it's sent directly
	 * in response to subscribe.  If subscribe is successful,
	 * there is no "OK" response, but a stream of rows from
	 * the binary log.
	 */

	/* Re-enable warnings after successful execution of SUBSCRIBE */
	applier->last_logged_errcode = 0;

	/*
	 * Process a stream of rows from the binary log.
	 */
	while (true) {
		coio_read_xrow(coio, &iobuf->in, &row);
		applier->lag = ev_now(loop()) - row.tm;
		applier->last_row_time = ev_monotonic_now(loop());

		if (iproto_type_is_error(row.type))
			xrow_decode_error_xc(&row);  /* error */
		/* Replication request. */
		if (row.replica_id == REPLICA_ID_NIL ||
		    row.replica_id >= VCLOCK_MAX) {
			/*
			 * A safety net, this can only occur
			 * if we're fed a strangely broken xlog.
			 */
			tnt_raise(ClientError, ER_UNKNOWN_REPLICA,
				  int2str(row.replica_id),
				  tt_uuid_str(&REPLICASET_UUID));
		}
		if (vclock_get(&replicaset_vclock, row.replica_id) < row.lsn) {
			/**
			 * Promote the replica set vclock before
			 * applying the row. If there is an
			 * exception (conflict) applying the row,
			 * the row is skipped when the replication
			 * is resumed.
			 */
			vclock_follow(&replicaset_vclock, row.replica_id,
				      row.lsn);
			xstream_write_xc(applier->subscribe_stream, &row);
		}
		fiber_cond_signal(&applier->writer_cond);
		iobuf_reset(iobuf);
		fiber_gc();
	}
}

static inline void
applier_disconnect(struct applier *applier, enum applier_state state)
{
	if (applier->writer != NULL) {
		fiber_cancel(applier->writer);
		applier->writer = NULL;
	}

	coio_close(loop(), &applier->io);
	iobuf_reset(applier->iobuf);
	applier_set_state(applier, state);
	fiber_gc();
}

static int
applier_f(va_list ap)
{
	struct applier *applier = va_arg(ap, struct applier *);
	/*
	 * Set correct session type for use in on_replace()
	 * triggers.
	 */
	current_session()->type = SESSION_TYPE_APPLIER;

	/* Re-connect loop */
	while (!fiber_is_cancelled()) {
		try {
			applier_connect(applier);
			if (tt_uuid_is_nil(&REPLICASET_UUID)) {
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
			unreachable();
			return 0;
		} catch (ClientError *e) {
			if (e->errcode() == ER_CONNECTION_TO_SELF &&
			    tt_uuid_is_equal(&applier->uuid, &INSTANCE_UUID)) {
				/* Connection to itself, stop applier */
				applier_disconnect(applier, APPLIER_OFF);
				return 0;
			} else if (e->errcode() == ER_LOADING) {
				/* Autobootstrap */
				applier_log_error(applier, e);
				goto reconnect;
			} else if (e->errcode() == ER_ACCESS_DENIED) {
				/* Invalid configuration */
				applier_log_error(applier, e);
				goto reconnect;
			} else if (e->errcode() == ER_SYSTEM) {
				/* System error from master instance. */
				applier_log_error(applier, e);
				goto reconnect;
			} else {
				/* Unrecoverable errors */
				applier_log_error(applier, e);
				applier_disconnect(applier, APPLIER_STOPPED);
				throw;
			}
		} catch (FiberIsCancelled *e) {
			applier_disconnect(applier, APPLIER_OFF);
			throw;
		} catch (SocketError *e) {
			applier_log_error(applier, e);
			goto reconnect;
		}
		/* Put fiber_sleep() out of catch block.
		 *
		 * This is done to avoid the case when two or more
		 * fibers yield inside their try/catch blocks and
		 * throw an exception. Seems like the exception unwinder
		 * uses global state inside the catch block.
		 *
		 * This could lead to incorrect exception processing
		 * and crash the program.
		 *
		 * See: https://github.com/tarantool/tarantool/issues/136
		*/
reconnect:
		applier_disconnect(applier, APPLIER_DISCONNECTED);
		fiber_sleep(replication_timeout);
	}
	return 0;
}

void
applier_start(struct applier *applier)
{
	char name[FIBER_NAME_MAX];
	assert(applier->reader == NULL);

	int pos = snprintf(name, sizeof(name), "applier/");
	uri_format(name + pos, sizeof(name) - pos, &applier->uri, false);

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
applier_new(const char *uri, struct xstream *join_stream,
	    struct xstream *subscribe_stream)
{
	struct applier *applier = (struct applier *)
		calloc(1, sizeof(struct applier));
	if (applier == NULL) {
		diag_set(OutOfMemory, sizeof(*applier), "malloc",
			 "struct applier");
		return NULL;
	}
	coio_create(&applier->io, -1);
	applier->iobuf = iobuf_new();

	/* uri_parse() sets pointers to applier->source buffer */
	snprintf(applier->source, sizeof(applier->source), "%s", uri);
	int rc = uri_parse(&applier->uri, applier->source);
	/* URI checked by box_check_replication() */
	assert(rc == 0 && applier->uri.service != NULL);
	(void) rc;

	applier->join_stream = join_stream;
	applier->subscribe_stream = subscribe_stream;
	applier->last_row_time = ev_monotonic_now(loop());
	rlist_create(&applier->on_state);
	fiber_channel_create(&applier->pause, 0);
	fiber_cond_create(&applier->writer_cond);

	return applier;
}

void
applier_delete(struct applier *applier)
{
	assert(applier->reader == NULL && applier->writer == NULL);
	iobuf_delete(applier->iobuf);
	assert(applier->io.fd == -1);
	fiber_channel_destroy(&applier->pause);
	trigger_destroy(&applier->on_state);
	fiber_cond_destroy(&applier->writer_cond);
	free(applier);
}

void
applier_resume(struct applier *applier)
{
	assert(!fiber_is_dead(applier->reader));
	void *data = NULL;
	fiber_channel_put_xc(&applier->pause, data);
}

static inline void
applier_pause(struct applier *applier)
{
	/* Sleep until applier_resume() wake us up */
	void *data;
	fiber_channel_get_xc(&applier->pause, &data);
}

struct applier_on_state {
	struct trigger base;
	struct applier *applier;
	enum applier_state desired_state;
	struct fiber_channel wakeup;
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
	fiber_channel_put_xc(&on_state->wakeup, applier);

	applier_pause(applier);
}

static inline void
applier_add_on_state(struct applier *applier,
		     struct applier_on_state *trigger,
		     enum applier_state desired_state)
{
	trigger_create(&trigger->base, applier_on_state_f, NULL, NULL);
	trigger->applier = applier;
	fiber_channel_create(&trigger->wakeup, 0);
	trigger->desired_state = desired_state;
	trigger_add(&applier->on_state, &trigger->base);
}

static inline void
applier_clear_on_state(struct applier_on_state *trigger)
{
	fiber_channel_destroy(&trigger->wakeup);
	trigger_clear(&trigger->base);
}

static inline int
applier_wait_for_state(struct applier_on_state *trigger, double timeout)
{
	void *data = NULL;
	if (fiber_channel_get_timeout(&trigger->wakeup, &data, timeout) != 0)
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
applier_connect_all(struct applier **appliers, int count,
		    double timeout)
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
	double deadline = ev_monotonic_now(loop()) + timeout;

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
		double wait = deadline - ev_monotonic_now(loop());
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
	tnt_raise(ClientError, ER_CFG, "replication",
		  "failed to connect to one or more replicas");
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
