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
#include "fiber_cond.h"
#include "coio.h"
#include "coio_buf.h"
#include "wal.h"
#include "xrow.h"
#include "replication.h"
#include "iproto_constants.h"
#include "version.h"
#include "trigger.h"
#include "xrow_io.h"
#include "error.h"
#include "errinj.h"
#include "session.h"
#include "cfg.h"
#include "schema.h"
#include "txn.h"
#include "box.h"
#include "xrow.h"
#include "scoped_guard.h"
#include "txn_limbo.h"
#include "journal.h"
#include "raft.h"

STRS(applier_state, applier_STATE);

enum {
	/**
	 * How often to log received row count. Used during join and register.
	 */
	ROWS_PER_LOG = 100000,
};

static inline void
applier_set_state(struct applier *applier, enum applier_state state)
{
	applier->state = state;
	say_debug("=> %s", applier_state_strs[state] +
		  strlen("APPLIER_"));
	trigger_run_xc(&applier->on_state, applier);
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
	case APPLIER_SYNC:
	case APPLIER_FOLLOW:
	case APPLIER_INITIAL_JOIN:
	case APPLIER_FINAL_JOIN:
		say_info("can't read row");
		break;
	default:
		break;
	}
	error_log(e);
	switch (errcode) {
	case ER_LOADING:
	case ER_CFG:
	case ER_ACCESS_DENIED:
	case ER_NO_SUCH_USER:
	case ER_SYSTEM:
	case ER_UNKNOWN_REPLICA:
	case ER_PASSWORD_MISMATCH:
	case ER_XLOG_GAP:
	case ER_TOO_EARLY_SUBSCRIBE:
	case ER_SYNC_QUORUM_TIMEOUT:
	case ER_SYNC_ROLLBACK:
		say_info("will retry every %.2lf second",
			 replication_reconnect_interval());
		break;
	default:
		break;
	}
	applier->last_logged_errcode = errcode;
}

/**
 * A helper function which switches the applier to FOLLOW state
 * if it has synchronized with its master.
 */
static inline void
applier_check_sync(struct applier *applier)
{
	/*
	 * Stay 'orphan' until appliers catch up with
	 * the remote vclock at the time of SUBSCRIBE
	 * and the lag is less than configured.
	 */
	if (applier->state == APPLIER_SYNC &&
	    applier->lag <= replication_sync_lag &&
	    vclock_compare_ignore0(&applier->remote_vclock_at_subscribe,
				   &replicaset.vclock) <= 0) {
		/* Applier is synced, switch to "follow". */
		applier_set_state(applier, APPLIER_FOLLOW);
	}
}

/*
 * Fiber function to write vclock to replication master.
 * To track connection status, replica answers master
 * with encoded vclock. In addition to DML requests,
 * master also sends heartbeat messages every
 * replication_timeout seconds (introduced in 1.7.7).
 * On such requests replica also responds with vclock.
 */
static int
applier_writer_f(va_list ap)
{
	struct applier *applier = va_arg(ap, struct applier *);
	struct ev_io io;
	coio_create(&io, applier->io.fd);

	/* ID is permanent while applier is alive */
	uint32_t replica_id = applier->instance_id;

	while (!fiber_is_cancelled()) {
		/*
		 * Tarantool >= 1.7.7 sends periodic heartbeat
		 * messages so we don't need to send ACKs every
		 * replication_timeout seconds any more.
		 */
		if (!applier->has_acks_to_send) {
			if (applier->version_id >= version_id(1, 7, 7))
				fiber_cond_wait_timeout(&applier->writer_cond,
							TIMEOUT_INFINITY);
			else
				fiber_cond_wait_timeout(&applier->writer_cond,
							replication_timeout);
		}
		/*
		 * A writer fiber is going to be awaken after a commit or
		 * a heartbeat message. So this is an appropriate place to
		 * update an applier status because the applier state could
		 * yield and doesn't fit into a commit trigger.
		 */
		applier_check_sync(applier);

		/* Send ACKs only when in FOLLOW mode ,*/
		if (applier->state != APPLIER_SYNC &&
		    applier->state != APPLIER_FOLLOW)
			continue;
		try {
			applier->has_acks_to_send = false;
			struct xrow_header xrow;
			xrow_encode_vclock(&xrow, &replicaset.vclock);
			/*
			 * For relay lag statistics we report last
			 * written transaction timestamp in tm field.
			 *
			 * If user delete the node from _cluster space,
			 * we obtain a nil pointer here.
			 */
			struct replica *r = replica_by_id(replica_id);
			if (likely(r != NULL))
				xrow.tm = r->applier_txn_last_tm;
			coio_write_xrow(&io, &xrow);
			ERROR_INJECT(ERRINJ_APPLIER_SLOW_ACK, {
				fiber_sleep(0.01);
			});
			/*
			 * Even if new ACK is requested during the
			 * write, don't send it again right away.
			 * Otherwise risk to stay in this loop for
			 * a long time.
			 */
		} catch (SocketError *e) {
			/*
			 * There is no point trying to send ACKs if
			 * the master closed its end - we would only
			 * spam the log - so exit immediately.
			 */
			if (e->saved_errno == EPIPE)
				break;
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

static int
apply_snapshot_row(struct xrow_header *row)
{
	int rc;
	struct request request;
	if (xrow_decode_dml(row, &request, dml_request_key_map(row->type)) != 0)
		return -1;
	struct space *space = space_cache_find(request.space_id);
	if (space == NULL)
		return -1;
	struct txn *txn = txn_begin();
	if (txn == NULL)
		return -1;
	/*
	 * Do not wait for confirmation when fetching a snapshot.
	 * Master only sends confirmed rows during join.
	 */
	txn_set_flags(txn, TXN_FORCE_ASYNC);
	if (txn_begin_stmt(txn, space, request.type) != 0)
		goto rollback;
	/* no access checks here - applier always works with admin privs */
	struct tuple *unused;
	if (space_execute_dml(space, txn, &request, &unused) != 0)
		goto rollback_stmt;
	if (txn_commit_stmt(txn, &request))
		goto rollback;
	rc = txn_commit(txn);
	fiber_gc();
	return rc;
rollback_stmt:
	txn_rollback_stmt(txn);
rollback:
	txn_abort(txn);
	fiber_gc();
	return -1;
}

/**
 * Process a no-op request.
 *
 * A no-op request does not affect any space, but it
 * promotes vclock and is written to WAL.
 */
static int
process_nop(struct request *request)
{
	assert(request->type == IPROTO_NOP);
	struct txn *txn = in_txn();
	if (txn_begin_stmt(txn, NULL, request->type) != 0)
		return -1;
	return txn_commit_stmt(txn, request);
}

static int
apply_row(struct xrow_header *row)
{
	struct request request;
	assert(!iproto_type_is_synchro_request(row->type));
	if (xrow_decode_dml(row, &request, dml_request_key_map(row->type)) != 0)
		return -1;
	if (request.type == IPROTO_NOP)
		return process_nop(&request);
	struct space *space = space_cache_find(request.space_id);
	if (space == NULL)
		return -1;
	if (box_process_rw(&request, space, NULL) != 0) {
		say_error("error applying row: %s", request_str(&request));
		return -1;
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
	struct ibuf *ibuf = &applier->ibuf;
	if (coio->fd >= 0)
		return;
	char greetingbuf[IPROTO_GREETING_SIZE];
	struct xrow_header row;

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

	if (applier->version_id != greeting.version_id) {
		say_info("remote master %s at %s running Tarantool %u.%u.%u",
			 tt_uuid_str(&greeting.uuid),
			 sio_strfaddr(&applier->addr, applier->addr_len),
			 version_id_major(greeting.version_id),
			 version_id_minor(greeting.version_id),
			 version_id_patch(greeting.version_id));
	}

	/* Save the remote instance version and UUID on connect. */
	applier->uuid = greeting.uuid;
	applier->version_id = greeting.version_id;

	/* Don't display previous error messages in box.info.replication */
	diag_clear(&fiber()->diag);

	/*
	 * Send an IPROTO_VOTE request to fetch the master's ballot
	 * before proceeding to "join". It will be used for leader
	 * election on bootstrap.
	 */
	xrow_encode_vote(&row);
	coio_write_xrow(coio, &row);
	coio_read_xrow(coio, ibuf, &row);
	if (row.type == IPROTO_OK) {
		xrow_decode_ballot_xc(&row, &applier->ballot);
	} else try {
		xrow_decode_error_xc(&row);
	} catch (ClientError *e) {
		if (e->errcode() != ER_UNKNOWN_REQUEST_TYPE)
			e->raise();
		/*
		 * Master isn't aware of IPROTO_VOTE request.
		 * It's OK - we can proceed without it.
		 */
	}

	applier_set_state(applier, APPLIER_CONNECTED);

	/* Detect connection to itself */
	if (tt_uuid_is_equal(&applier->uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/* Perform authentication if user provided at least login */
	if (!uri->login)
		goto done;

	/* Authenticate */
	applier_set_state(applier, APPLIER_AUTH);
	xrow_encode_auth_xc(&row, greeting.salt, greeting.salt_len, uri->login,
			    uri->login_len,
			    uri->password != NULL ? uri->password : "",
			    uri->password_len);
	coio_write_xrow(coio, &row);
	coio_read_xrow(coio, ibuf, &row);
	applier->last_row_time = ev_monotonic_now(loop());
	if (row.type != IPROTO_OK)
		xrow_decode_error_xc(&row); /* auth failed */

	/* auth succeeded */
	say_info("authenticated");
done:
	applier_set_state(applier, APPLIER_READY);
}

static uint64_t
applier_wait_snapshot(struct applier *applier)
{
	struct ev_io *coio = &applier->io;
	struct ibuf *ibuf = &applier->ibuf;
	struct xrow_header row;

	/**
	 * Tarantool < 1.7.0: if JOIN is successful, there is no "OK"
	 * response, but a stream of rows from checkpoint.
	 */
	if (applier->version_id >= version_id(1, 7, 0)) {
		/* Decode JOIN/FETCH_SNAPSHOT response */
		coio_read_xrow(coio, ibuf, &row);
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
		xrow_decode_vclock_xc(&row, &replicaset.vclock);
	}

	/*
	 * Receive initial data.
	 */
	uint64_t row_count = 0;
	while (true) {
		coio_read_xrow(coio, ibuf, &row);
		applier->last_row_time = ev_monotonic_now(loop());
		if (iproto_type_is_dml(row.type)) {
			if (apply_snapshot_row(&row) != 0)
				diag_raise();
			if (++row_count % ROWS_PER_LOG == 0)
				say_info("%.1fM rows received", row_count / 1e6);
		} else if (row.type == IPROTO_OK) {
			if (applier->version_id < version_id(1, 7, 0)) {
				/*
				 * This is the start vclock if the
				 * server is 1.6. Since we have
				 * not initialized replication
				 * vclock yet, do it now. In 1.7+
				 * this vclock is not used.
				 */
				xrow_decode_vclock_xc(&row, &replicaset.vclock);
			}
			break; /* end of stream */
		} else if (iproto_type_is_error(row.type)) {
			xrow_decode_error_xc(&row);  /* rethrow error */
		} else {
			tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
				  (uint32_t) row.type);
		}
	}

	return row_count;
}

static void
applier_fetch_snapshot(struct applier *applier)
{
	/* Send FETCH SNAPSHOT request */
	struct ev_io *coio = &applier->io;
	struct xrow_header row;

	memset(&row, 0, sizeof(row));
	row.type = IPROTO_FETCH_SNAPSHOT;
	coio_write_xrow(coio, &row);

	applier_set_state(applier, APPLIER_FETCH_SNAPSHOT);
	applier_wait_snapshot(applier);
	applier_set_state(applier, APPLIER_FETCHED_SNAPSHOT);
	applier_set_state(applier, APPLIER_READY);
}

static uint64_t
applier_read_tx(struct applier *applier, struct stailq *rows, double timeout);

static int
apply_final_join_tx(uint32_t replica_id, struct stailq *rows);

/**
 * A helper struct to link xrow objects in a list.
 */
struct applier_tx_row {
	/* Next transaction row. */
	struct stailq_entry next;
	/* xrow_header struct for the current transaction row. */
	struct xrow_header row;
};

static uint64_t
applier_wait_register(struct applier *applier, uint64_t row_count)
{
	/*
	 * Tarantool < 1.7.0: there is no "final join" stage.
	 * Proceed to "subscribe" and do not finish bootstrap
	 * until replica id is received.
	 */
	if (applier->version_id < version_id(1, 7, 0))
		return row_count;

	uint64_t next_log_cnt =
		row_count + ROWS_PER_LOG - row_count % ROWS_PER_LOG;
	/*
	 * Receive final data.
	 */
	while (true) {
		struct stailq rows;
		row_count += applier_read_tx(applier, &rows, TIMEOUT_INFINITY);
		while (row_count >= next_log_cnt) {
			say_info("%.1fM rows received", next_log_cnt / 1e6);
			next_log_cnt += ROWS_PER_LOG;
		}
		struct xrow_header *first_row =
			&stailq_first_entry(&rows, struct applier_tx_row,
					    next)->row;
		if (first_row->type == IPROTO_OK) {
			/* Current vclock. This is not used now, ignore. */
			assert(first_row ==
			       &stailq_last_entry(&rows, struct applier_tx_row,
						  next)->row);
			break;
		}
		if (apply_final_join_tx(applier->instance_id, &rows) != 0)
			diag_raise();
	}

	return row_count;
}

static void
applier_register(struct applier *applier, bool was_anon)
{
	/* Send REGISTER request */
	struct ev_io *coio = &applier->io;
	struct xrow_header row;

	memset(&row, 0, sizeof(row));
	/*
	 * Send this instance's current vclock together
	 * with REGISTER request.
	 */
	xrow_encode_register(&row, &INSTANCE_UUID, box_vclock);
	row.type = IPROTO_REGISTER;
	coio_write_xrow(coio, &row);

	/*
	 * Register may serve as a retry for final join. Set corresponding
	 * states to unblock anyone who's waiting for final join to start or
	 * end.
	 */
	applier_set_state(applier, was_anon ? APPLIER_REGISTER :
					      APPLIER_FINAL_JOIN);
	applier_wait_register(applier, 0);
	applier_set_state(applier, was_anon ? APPLIER_REGISTERED :
					      APPLIER_JOINED);
	applier_set_state(applier, APPLIER_READY);
}

/**
 * Execute and process JOIN request (bootstrap the instance).
 */
static void
applier_join(struct applier *applier)
{
	/* Send JOIN request */
	struct ev_io *coio = &applier->io;
	struct xrow_header row;
	uint64_t row_count;

	xrow_encode_join_xc(&row, &INSTANCE_UUID);
	coio_write_xrow(coio, &row);

	applier_set_state(applier, APPLIER_INITIAL_JOIN);

	row_count = applier_wait_snapshot(applier);

	say_info("initial data received");

	applier_set_state(applier, APPLIER_FINAL_JOIN);

	if (applier_wait_register(applier, row_count) == row_count) {
		/*
		 * We didn't receive any rows during registration.
		 * Proceed to "subscribe" and do not finish bootstrap
		 * until replica id is received.
		 */
		return;
	}

	say_info("final data received");

	applier_set_state(applier, APPLIER_JOINED);
	applier_set_state(applier, APPLIER_READY);
}

static struct applier_tx_row *
applier_read_tx_row(struct applier *applier, double timeout)
{
	struct ev_io *coio = &applier->io;
	struct ibuf *ibuf = &applier->ibuf;
	size_t size;
	struct applier_tx_row *tx_row =
		region_alloc_object(&fiber()->gc, typeof(*tx_row), &size);

	if (tx_row == NULL)
		tnt_raise(OutOfMemory, size, "region_alloc_object", "tx_row");

	struct xrow_header *row = &tx_row->row;

	ERROR_INJECT_YIELD(ERRINJ_APPLIER_READ_TX_ROW_DELAY);

	coio_read_xrow_timeout_xc(coio, ibuf, row, timeout);

	applier->lag = ev_now(loop()) - row->tm;
	applier->last_row_time = ev_monotonic_now(loop());
	return tx_row;
}

static int64_t
set_next_tx_row(struct stailq *rows, struct applier_tx_row *tx_row, int64_t tsn)
{
	struct xrow_header *row = &tx_row->row;

	if (iproto_type_is_error(row->type))
		xrow_decode_error_xc(row);

	/* Replication request. */
	if (row->replica_id >= VCLOCK_MAX) {
		/*
		 * A safety net, this can only occur if we're fed a strangely
		 * broken xlog. row->replica_id == 0, when reading heartbeats
		 * from an anonymous instance.
		 */
		tnt_raise(ClientError, ER_UNKNOWN_REPLICA,
			  int2str(row->replica_id),
			  tt_uuid_str(&REPLICASET_UUID));
	}
	if (tsn == 0) {
		/*
		 * Transaction id must be derived from the log sequence number
		 * of the first row in the transaction.
		 */
		tsn = row->tsn;
		if (row->lsn != tsn)
			tnt_raise(ClientError, ER_PROTOCOL,
				  "Transaction id must be equal to LSN of the "
				  "first row in the transaction.");
	} else if (tsn != row->tsn) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "replication",
			  "interleaving transactions");
	}

	assert(row->bodycnt <= 1);
	if (row->is_commit) {
		/* Signal the caller that we've reached the tx end. */
		tsn = 0;
	} else if (row->bodycnt == 1) {
		/*
		 * Save row body to gc region. Not done for single-statement
		 * transactions and the last row of multi-statement transactions
		 * knowing that the input buffer will not be used while the
		 * transaction is applied.
		 */
		void *new_base = region_alloc(&fiber()->gc, row->body->iov_len);
		if (new_base == NULL)
			tnt_raise(OutOfMemory, row->body->iov_len, "region",
				  "xrow body");
		memcpy(new_base, row->body->iov_base, row->body->iov_len);
		/* Adjust row body pointers. */
		row->body->iov_base = new_base;
	}

	stailq_add_tail(rows, &tx_row->next);
	return tsn;
}

/**
 * Read one transaction from network using applier's input buffer.
 * Transaction rows are placed onto fiber gc region.
 * We could not use applier input buffer to store rows because
 * rpos is adjusted as xrow is decoded and the corresponding
 * network input space is reused for the next xrow.
 */
static uint64_t
applier_read_tx(struct applier *applier, struct stailq *rows, double timeout)
{
	int64_t tsn = 0;
	uint64_t row_count = 0;

	stailq_create(rows);
	do {
		struct applier_tx_row *tx_row = applier_read_tx_row(applier,
								    timeout);
		tsn = set_next_tx_row(rows, tx_row, tsn);
		++row_count;
	} while (tsn != 0);
	return row_count;
}

static void
applier_rollback_by_wal_io(int64_t signature)
{
	/*
	 * Setup shared applier diagnostic area.
	 *
	 * FIXME: We should consider redesign this
	 * moment and instead of carrying one shared
	 * diag use per-applier diag instead all the time
	 * (which actually already present in the structure).
	 *
	 * But remember that WAL writes are asynchronous and
	 * rollback may happen a way later after it was passed to
	 * the journal engine.
	 */
	diag_set_txn_sign(signature);
	diag_set_error(&replicaset.applier.diag,
		       diag_last_error(diag_get()));

	/* Broadcast the rollback across all appliers. */
	trigger_run(&replicaset.applier.on_rollback, NULL);

	/* Rollback applier vclock to the committed one. */
	vclock_copy(&replicaset.applier.vclock, &replicaset.vclock);
}

static int
applier_txn_rollback_cb(struct trigger *trigger, void *event)
{
	(void) trigger;
	struct txn *txn = (struct txn *) event;
	/*
	 * Synchronous transaction rollback due to receiving a
	 * ROLLBACK entry is a normal event and requires no
	 * special handling.
	 */
	if (txn->signature != TXN_SIGNATURE_SYNC_ROLLBACK)
		applier_rollback_by_wal_io(txn->signature);
	return 0;
}

struct replica_cb_data {
	/** Replica ID the data belongs to. */
	uint32_t replica_id;
	/**
	 * Timestamp of a transaction to be accounted
	 * for relay lag. Usually it is a last row in
	 * a transaction.
	 */
	double txn_last_tm;
};

/** Update replica associated data once write is complete. */
static void
replica_txn_wal_write_cb(struct replica_cb_data *rcb)
{
	struct replica *r = replica_by_id(rcb->replica_id);
	if (likely(r != NULL))
		r->applier_txn_last_tm = rcb->txn_last_tm;
}

static int
applier_txn_wal_write_cb(struct trigger *trigger, void *event)
{
	(void) event;

	struct replica_cb_data *rcb =
		(struct replica_cb_data *)trigger->data;
	replica_txn_wal_write_cb(rcb);

	/* Broadcast the WAL write across all appliers. */
	trigger_run(&replicaset.applier.on_wal_write, NULL);
	return 0;
}

struct synchro_entry {
	/** Request to process when WAL write is done. */
	struct synchro_request *req;
	/** Fiber created the entry. To wakeup when WAL write is done. */
	struct fiber *owner;
	/** Replica associated data. */
	struct replica_cb_data *rcb;
	/**
	 * The base journal entry. It has unsized array and then must be the
	 * last entry in the structure. But can workaround it via a union
	 * adding the needed tail as char[].
	 */
	union {
		struct journal_entry base;
		char base_buf[sizeof(base) + sizeof(base.rows[0])];
	};
};

/**
 * Async write journal completion.
 */
static void
apply_synchro_row_cb(struct journal_entry *entry)
{
	assert(entry->complete_data != NULL);
	struct synchro_entry *synchro_entry =
		(struct synchro_entry *)entry->complete_data;
	if (entry->res < 0) {
		applier_rollback_by_wal_io(entry->res);
	} else {
		replica_txn_wal_write_cb(synchro_entry->rcb);
		txn_limbo_process(&txn_limbo, synchro_entry->req);
		trigger_run(&replicaset.applier.on_wal_write, NULL);
	}
	fiber_wakeup(synchro_entry->owner);
}

/** Process a synchro request. */
static int
apply_synchro_row(uint32_t replica_id, struct xrow_header *row)
{
	assert(iproto_type_is_synchro_request(row->type));

	struct synchro_request req;
	if (xrow_decode_synchro(row, &req) != 0)
		goto err;

	struct replica_cb_data rcb_data;
	struct synchro_entry entry;
	/*
	 * Rows array is cast from *[] to **, because otherwise g++ complains
	 * about out of array bounds access.
	 */
	struct xrow_header **rows;
	rows = entry.base.rows;
	rows[0] = row;
	journal_entry_create(&entry.base, 1, xrow_approx_len(row),
			     apply_synchro_row_cb, &entry);
	entry.req = &req;
	entry.owner = fiber();

	rcb_data.replica_id = replica_id;
	rcb_data.txn_last_tm = row->tm;
	entry.rcb = &rcb_data;

	/*
	 * The WAL write is blocking. Otherwise it might happen that a CONFIRM
	 * or ROLLBACK is sent to WAL, and it would empty the limbo, but before
	 * it is written, more transactions arrive with a different owner. They
	 * won't be able to enter the limbo due to owner ID mismatch. Hence the
	 * synchro rows must block receipt of new transactions.
	 *
	 * Don't forget to return -1 both if the journal write failed right
	 * away, and if it failed inside of WAL thread (res < 0). Otherwise the
	 * caller would propagate committed vclock to this row thinking it was
	 * a success.
	 *
	 * XXX: in theory it could be done vice-versa. The write could be made
	 * non-blocking, and instead the potentially conflicting transactions
	 * could try to wait for all the current synchro WAL writes to end
	 * before trying to commit. But that requires extra steps from the
	 * transactions side, including the async ones.
	 */
	if (journal_write(&entry.base) != 0)
		goto err;
	if (entry.base.res < 0) {
		diag_set_journal_res(entry.base.res);
		goto err;
	}
	return 0;
err:
	diag_log();
	return -1;
}

static int
applier_handle_raft(struct applier *applier, struct xrow_header *row)
{
	assert(iproto_type_is_raft_request(row->type));
	if (applier->instance_id == 0) {
		diag_set(ClientError, ER_PROTOCOL, "Can't apply a Raft request "
			 "from an instance without an ID");
		return -1;
	}

	struct raft_request req;
	struct vclock candidate_clock;
	if (xrow_decode_raft(row, &req, &candidate_clock) != 0)
		return -1;
	return box_raft_process(&req, applier->instance_id);
}

static int
apply_plain_tx(uint32_t replica_id, struct stailq *rows,
	       bool skip_conflict, bool use_triggers)
{
	/*
	 * Explicitly begin the transaction so that we can
	 * control fiber->gc life cycle and, in case of apply
	 * conflict safely access failed xrow object and allocate
	 * IPROTO_NOP on gc.
	 */
	struct txn *txn = txn_begin();
	struct applier_tx_row *item;
	if (txn == NULL)
		 return -1;

	stailq_foreach_entry(item, rows, next) {
		struct xrow_header *row = &item->row;
		int res = apply_row(row);
		if (res != 0 && skip_conflict) {
			struct error *e = diag_last_error(diag_get());
			/*
			 * In case of ER_TUPLE_FOUND error and enabled
			 * replication_skip_conflict configuration
			 * option, skip applying the foreign row and
			 * replace it with NOP in the local write ahead
			 * log.
			 */
			if (e->type == &type_ClientError &&
			    box_error_code(e) == ER_TUPLE_FOUND) {
				diag_clear(diag_get());
				row->type = IPROTO_NOP;
				row->bodycnt = 0;
				res = apply_row(row);
			}
		}
		if (res != 0)
			goto fail;
	}

	/*
	 * We are going to commit so it's a high time to check if
	 * the current transaction has non-local effects.
	 */
	if (txn_is_distributed(txn)) {
		/*
		 * A transaction mixes remote and local rows.
		 * Local rows must be replicated back, which
		 * doesn't make sense since the master likely has
		 * new changes which local rows may overwrite.
		 * Raise an error.
		 */
		diag_set(ClientError, ER_UNSUPPORTED, "Replication",
			 "distributed transactions");
		goto fail;
	}

	if (use_triggers) {
		/* We are ready to submit txn to wal. */
		struct trigger *on_rollback, *on_wal_write;
		size_t size;
		on_rollback = region_alloc_object(&txn->region, typeof(*on_rollback),
						  &size);
		on_wal_write = region_alloc_object(&txn->region, typeof(*on_wal_write),
						   &size);
		if (on_rollback == NULL || on_wal_write == NULL) {
			diag_set(OutOfMemory, size, "region_alloc_object",
				 "on_rollback/on_wal_write");
			goto fail;
		}

		struct replica_cb_data *rcb;
		rcb = region_alloc_object(&txn->region, typeof(*rcb), &size);
		if (rcb == NULL) {
			diag_set(OutOfMemory, size, "region_alloc_object", "rcb");
			goto fail;
		}

		trigger_create(on_rollback, applier_txn_rollback_cb, NULL, NULL);
		txn_on_rollback(txn, on_rollback);

		/*
		 * We use *last* entry timestamp because ack comes up to
		 * last entry in transaction. Same time this shows more
		 * precise result because we're interested in how long
		 * transaction traversed network + remote WAL bundle before
		 * ack get received.
		 */
		item = stailq_last_entry(rows, struct applier_tx_row, next);
		rcb->replica_id = replica_id;
		rcb->txn_last_tm = item->row.tm;

		trigger_create(on_wal_write, applier_txn_wal_write_cb, rcb, NULL);
		txn_on_wal_write(txn, on_wal_write);
	}

	return txn_commit_try_async(txn);
fail:
	txn_abort(txn);
	return -1;
}

/** A simpler version of applier_apply_tx() for final join stage. */
static int
apply_final_join_tx(uint32_t replica_id, struct stailq *rows)
{
	struct xrow_header *first_row =
		&stailq_first_entry(rows, struct applier_tx_row, next)->row;
	struct xrow_header *last_row =
		&stailq_last_entry(rows, struct applier_tx_row, next)->row;
	int rc = 0;
	/* WAL isn't enabled yet, so follow vclock manually. */
	vclock_follow_xrow(&replicaset.vclock, last_row);
	if (unlikely(iproto_type_is_synchro_request(first_row->type))) {
		assert(first_row == last_row);
		rc = apply_synchro_row(replica_id, first_row);
	} else {
		rc = apply_plain_tx(replica_id, rows, false, false);
	}
	fiber_gc();
	return rc;
}

/**
 * When elections are enabled we must filter out synchronous rows coming
 * from an instance that fell behind the current leader. This includes
 * both synchronous tx rows and rows for txs following unconfirmed
 * synchronous transactions.
 * The rows are replaced with NOPs to preserve the vclock consistency.
 */
static void
applier_synchro_filter_tx(struct stailq *rows)
{
	/*
	 * XXX: in case raft is disabled, synchronous replication still works
	 * but without any filtering. That might lead to issues with
	 * unpredictable confirms after rollbacks which are supposed to be
	 * fixed by the filtering.
	 */
	if (!raft_is_enabled(box_raft()))
		return;
	struct xrow_header *row;
	/*
	 * It  may happen that we receive the instance's rows via some third
	 * node, so cannot check for applier->instance_id here.
	 */
	row = &stailq_first_entry(rows, struct applier_tx_row, next)->row;
	if (!txn_limbo_is_replica_outdated(&txn_limbo, row->replica_id))
		return;

	if (stailq_last_entry(rows, struct applier_tx_row, next)->row.wait_sync)
		goto nopify;

	/*
	 * Not waiting for sync and not a synchro request - this make it already
	 * NOP or an asynchronous transaction not depending on any synchronous
	 * ones - let it go as is.
	 */
	if (!iproto_type_is_synchro_request(row->type))
		return;
	/*
	 * Do not NOPify promotion, otherwise won't even know who is the limbo
	 * owner now.
	 */
	if (iproto_type_is_promote_request(row->type))
		return;
nopify:;
	struct applier_tx_row *item;
	stailq_foreach_entry(item, rows, next) {
		row = &item->row;
		row->type = IPROTO_NOP;
		/*
		 * Row body is saved to fiber's region and will be freed
		 * on next fiber_gc() call.
		 */
		row->bodycnt = 0;
	}
}

/**
 * Apply all rows in the rows queue as a single transaction.
 *
 * Return 0 for success or -1 in case of an error.
 */
static int
applier_apply_tx(struct applier *applier, struct stailq *rows)
{
	/*
	 * Initially we've been filtering out data if it came from
	 * an applier which instance_id doesn't match raft->leader,
	 * but this prevents from obtaining valid leader's data when
	 * it comes from intermediate node. For example a series of
	 * replica hops
	 *
	 *  master -> replica 1 -> replica 2
	 *
	 * where each replica carries master's initiated transaction
	 * in xrow->replica_id field and master's data get propagated
	 * indirectly.
	 *
	 * Finally we dropped such "sender" filtration and use transaction
	 * "initiator" filtration via xrow->replica_id only.
	 */
	struct xrow_header *first_row = &stailq_first_entry(rows,
					struct applier_tx_row, next)->row;
	struct xrow_header *last_row;
	last_row = &stailq_last_entry(rows, struct applier_tx_row, next)->row;
	struct replica *replica = replica_by_id(first_row->replica_id);
	int rc = 0;
	/*
	 * In a full mesh topology, the same set of changes
	 * may arrive via two concurrently running appliers.
	 * Hence we need a latch to strictly order all changes
	 * that belong to the same server id.
	 */
	struct latch *latch = (replica ? &replica->order_latch :
			       &replicaset.applier.order_latch);
	latch_lock(latch);
	if (vclock_get(&replicaset.applier.vclock,
		       last_row->replica_id) >= last_row->lsn) {
		goto finish;
	} else if (vclock_get(&replicaset.applier.vclock,
			      first_row->replica_id) >= first_row->lsn) {
		/*
		 * We've received part of the tx from an old
		 * instance not knowing of tx boundaries.
		 * Skip the already applied part.
		 */
		struct xrow_header *tmp;
		while (true) {
			tmp = &stailq_first_entry(rows,
						  struct applier_tx_row,
						  next)->row;
			if (tmp->lsn <= vclock_get(&replicaset.applier.vclock,
						   tmp->replica_id)) {
				stailq_shift(rows);
			} else {
				break;
			}
		}
	}
	applier_synchro_filter_tx(rows);
	if (unlikely(iproto_type_is_synchro_request(first_row->type))) {
		/*
		 * Synchro messages are not transactions, in terms
		 * of DML. Always sent and written isolated from
		 * each other.
		 */
		assert(first_row == last_row);
		rc = apply_synchro_row(applier->instance_id, first_row);
	} else {
		rc = apply_plain_tx(applier->instance_id, rows,
				    replication_skip_conflict, true);
	}
	if (rc != 0)
		goto finish;

	vclock_follow(&replicaset.applier.vclock, last_row->replica_id,
		      last_row->lsn);
finish:
	latch_unlock(latch);
	fiber_gc();
	return rc;
}

/**
 * Notify the applier's write fiber that there are more ACKs to
 * send to master.
 */
static inline void
applier_signal_ack(struct applier *applier)
{
	fiber_cond_signal(&applier->writer_cond);
	applier->has_acks_to_send = true;
}

/*
 * A trigger to update an applier state after WAL write.
 */
static int
applier_on_wal_write(struct trigger *trigger, void *event)
{
	(void) event;
	struct applier *applier = (struct applier *)trigger->data;
	applier_signal_ack(applier);
	return 0;
}

/*
 * A trigger to update an applier state after a replication rollback.
 */
static int
applier_on_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct applier *applier = (struct applier *)trigger->data;
	/* Setup a shared error. */
	if (!diag_is_empty(&replicaset.applier.diag)) {
		diag_set_error(&applier->diag,
			       diag_last_error(&replicaset.applier.diag));
	}
	/* Stop the applier fiber. */
	fiber_cancel(applier->reader);
	return 0;
}

/**
 * Execute and process SUBSCRIBE request (follow updates from a master).
 */
static void
applier_subscribe(struct applier *applier)
{
	/* Send SUBSCRIBE request */
	struct ev_io *coio = &applier->io;
	struct ibuf *ibuf = &applier->ibuf;
	struct xrow_header row;
	struct tt_uuid cluster_id = uuid_nil;

	struct vclock vclock;
	vclock_create(&vclock);
	vclock_copy(&vclock, &replicaset.vclock);
	/*
	 * Stop accepting local rows coming from a remote
	 * instance as soon as local WAL starts accepting writes.
	 */
	uint32_t id_filter = box_is_orphan() ? 0 : 1 << instance_id;
	xrow_encode_subscribe_xc(&row, &REPLICASET_UUID, &INSTANCE_UUID,
				 &vclock, replication_anon, id_filter);
	coio_write_xrow(coio, &row);

	/* Read SUBSCRIBE response */
	if (applier->version_id >= version_id(1, 6, 7)) {
		coio_read_xrow(coio, ibuf, &row);
		if (iproto_type_is_error(row.type)) {
			xrow_decode_error_xc(&row);  /* error */
		} else if (row.type != IPROTO_OK) {
			tnt_raise(ClientError, ER_PROTOCOL,
				  "Invalid response to SUBSCRIBE");
		}
		/*
		 * In case of successful subscribe, the server
		 * responds with its current vclock.
		 *
		 * Tarantool > 2.1.1 also sends its cluster id to
		 * the replica, and replica has to check whether
		 * its and master's cluster ids match.
		 */
		vclock_create(&applier->remote_vclock_at_subscribe);
		xrow_decode_subscribe_response_xc(&row, &cluster_id,
					&applier->remote_vclock_at_subscribe);
		applier->instance_id = row.replica_id;
		/*
		 * If master didn't send us its cluster id
		 * assume that it has done all the checks.
		 * In this case cluster_id will remain zero.
		 */
		if (!tt_uuid_is_nil(&cluster_id) &&
		    !tt_uuid_is_equal(&cluster_id, &REPLICASET_UUID)) {
			tnt_raise(ClientError, ER_REPLICASET_UUID_MISMATCH,
				  tt_uuid_str(&cluster_id),
				  tt_uuid_str(&REPLICASET_UUID));
		}

		say_info("subscribed");
		say_info("remote vclock %s local vclock %s",
			 vclock_to_string(&applier->remote_vclock_at_subscribe),
			 vclock_to_string(&vclock));
	}
	/*
	 * Tarantool < 1.6.7:
	 * If there is an error in subscribe, it's sent directly
	 * in response to subscribe.  If subscribe is successful,
	 * there is no "OK" response, but a stream of rows from
	 * the binary log.
	 */

	if (applier->state == APPLIER_READY) {
		/*
		 * Tarantool < 1.7.7 does not send periodic heartbeat
		 * messages so we cannot enable applier synchronization
		 * for it without risking getting stuck in the 'orphan'
		 * mode until a DML operation happens on the master.
		 */
		if (applier->version_id >= version_id(1, 7, 7))
			applier_set_state(applier, APPLIER_SYNC);
		else
			applier_set_state(applier, APPLIER_FOLLOW);
	} else {
		/*
		 * Tarantool < 1.7.0 sends replica id during
		 * "subscribe" stage. We can't finish bootstrap
		 * until it is received.
		 */
		assert(applier->state == APPLIER_FINAL_JOIN);
		assert(applier->version_id < version_id(1, 7, 0));
	}

	/* Re-enable warnings after successful execution of SUBSCRIBE */
	applier->last_logged_errcode = 0;
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

	applier->lag = TIMEOUT_INFINITY;

	/*
	 * Register triggers to handle WAL writes and rollbacks.
	 *
	 * Note we use them for syncronous packets handling as well
	 * thus when changing make sure that synchro handling won't
	 * be broken.
	 */
	struct trigger on_wal_write;
	trigger_create(&on_wal_write, applier_on_wal_write, applier, NULL);
	trigger_add(&replicaset.applier.on_wal_write, &on_wal_write);

	struct trigger on_rollback;
	trigger_create(&on_rollback, applier_on_rollback, applier, NULL);
	trigger_add(&replicaset.applier.on_rollback, &on_rollback);

	auto trigger_guard = make_scoped_guard([&] {
		trigger_clear(&on_wal_write);
		trigger_clear(&on_rollback);
	});

	/*
	 * Process a stream of rows from the binary log.
	 */
	while (true) {
		if (applier->state == APPLIER_FINAL_JOIN &&
		    instance_id != REPLICA_ID_NIL) {
			say_info("final data received");
			applier_set_state(applier, APPLIER_JOINED);
			applier_set_state(applier, APPLIER_READY);
			applier_set_state(applier, APPLIER_FOLLOW);
		}

		/*
		 * Tarantool < 1.7.7 does not send periodic heartbeat
		 * messages so we can't assume that if we haven't heard
		 * from the master for quite a while the connection is
		 * broken - the master might just be idle.
		 */
		double timeout = applier->version_id < version_id(1, 7, 7) ?
				 TIMEOUT_INFINITY :
				 replication_disconnect_timeout();

		struct stailq rows;
		applier_read_tx(applier, &rows, timeout);

		/*
		 * In case of an heartbeat message wake a writer up
		 * and check applier state.
		 */
		struct xrow_header *first_row =
			&stailq_first_entry(&rows, struct applier_tx_row,
					    next)->row;
		raft_process_heartbeat(box_raft(), applier->instance_id);
		if (first_row->lsn == 0) {
			if (unlikely(iproto_type_is_raft_request(
							first_row->type))) {
				if (applier_handle_raft(applier,
							first_row) != 0)
					diag_raise();
			}
			applier_signal_ack(applier);
		} else if (applier_apply_tx(applier, &rows) != 0) {
			diag_raise();
		}

		if (ibuf_used(ibuf) == 0)
			ibuf_reset(ibuf);
	}
}

static inline void
applier_disconnect(struct applier *applier, enum applier_state state)
{
	applier_set_state(applier, state);
	if (applier->writer != NULL) {
		fiber_cancel(applier->writer);
		fiber_join(applier->writer);
		applier->writer = NULL;
	}

	coio_close_io(loop(), &applier->io);
	/* Clear all unparsed input. */
	ibuf_reinit(&applier->ibuf);
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
	struct session *session = session_create_on_demand();
	if (session == NULL)
		return -1;
	session_set_type(session, SESSION_TYPE_APPLIER);

	/*
	 * The instance saves replication_anon value on bootstrap.
	 * If a freshly started instance sees it has received
	 * REPLICASET_UUID but hasn't yet registered, it must be an
	 * anonymous replica, hence the default value 'true'.
	 */
	bool was_anon = true;

	/* Re-connect loop */
	while (!fiber_is_cancelled()) {
		try {
			applier_connect(applier);
			if (tt_uuid_is_nil(&REPLICASET_UUID)) {
				/*
				 * Execute JOIN if this is a bootstrap.
				 * In case of anonymous replication, don't
				 * join but just fetch master's snapshot.
				 *
				 * The join will pause the applier
				 * until WAL is created.
				 */
				was_anon = replication_anon;
				if (replication_anon)
					applier_fetch_snapshot(applier);
				else
					applier_join(applier);
			}
			if (instance_id == REPLICA_ID_NIL &&
			    !replication_anon) {
				/*
				 * The instance transitioned from anonymous or
				 * is retrying final join.
				 */
				applier_register(applier, was_anon);
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
				applier_disconnect(applier, APPLIER_LOADING);
				goto reconnect;
			} else if (e->errcode() == ER_TOO_EARLY_SUBSCRIBE) {
				/*
				 * The instance is not anonymous, and is
				 * registered, but its ID is not delivered to
				 * all the nodes in the cluster yet, and some
				 * nodes may ask to retry connection later,
				 * until they receive _cluster record of this
				 * instance. From some third node, for example.
				 */
				applier_log_error(applier, e);
				applier_disconnect(applier, APPLIER_LOADING);
				goto reconnect;
			} else if (e->errcode() == ER_SYNC_QUORUM_TIMEOUT ||
				   e->errcode() == ER_SYNC_ROLLBACK) {
				/*
				 * Join failure due to synchronous
				 * transaction rollback.
				 */
				applier_log_error(applier, e);
				applier_disconnect(applier, APPLIER_LOADING);
				goto reconnect;
			} else if (e->errcode() == ER_CFG ||
				   e->errcode() == ER_ACCESS_DENIED ||
				   e->errcode() == ER_NO_SUCH_USER ||
				   e->errcode() == ER_PASSWORD_MISMATCH) {
				/* Invalid configuration */
				applier_log_error(applier, e);
				applier_disconnect(applier, APPLIER_LOADING);
				goto reconnect;
			} else if (e->errcode() == ER_SYSTEM) {
				/* System error from master instance. */
				applier_log_error(applier, e);
				applier_disconnect(applier, APPLIER_DISCONNECTED);
				goto reconnect;
			} else {
				/* Unrecoverable errors */
				applier_log_error(applier, e);
				applier_disconnect(applier, APPLIER_STOPPED);
				return -1;
			}
		} catch (XlogGapError *e) {
			/*
			 * Xlog gap error can't be a critical error. Because it
			 * is totally normal during bootstrap. Consider the
			 * case: node1 is a leader, it is booted with vclock
			 * {1: 3}. Node2 connects and fetches snapshot of node1,
			 * it also gets vclock {1: 3}. Then node1 writes
			 * something and its vclock becomes {1: 4}. Now node3
			 * boots from node1, and gets the same vclock. Vclocks
			 * now look like this:
			 *
			 * - node1: {1: 4}, leader, has {1: 3} snap.
			 * - node2: {1: 3}, booted from node1, has only snap.
			 * - node3: {1: 4}, booted from node1, has only snap.
			 *
			 * If the cluster is a fullmesh, node2 will send
			 * subscribe requests with vclock {1: 3}. If node3
			 * receives it, it will respond with xlog gap error,
			 * because it only has a snap with {1: 4}, nothing else.
			 * In that case node2 should retry connecting to node3,
			 * and in the meantime try to get newer changes from
			 * node1.
			 */
			applier_log_error(applier, e);
			applier_disconnect(applier, APPLIER_LOADING);
			goto reconnect;
		} catch (FiberIsCancelled *e) {
			if (!diag_is_empty(&applier->diag)) {
				diag_move(&applier->diag, &fiber()->diag);
				applier_disconnect(applier, APPLIER_STOPPED);
				break;
			}
			applier_disconnect(applier, APPLIER_OFF);
			break;
		} catch (SocketError *e) {
			applier_log_error(applier, e);
			applier_disconnect(applier, APPLIER_DISCONNECTED);
			goto reconnect;
		} catch (SystemError *e) {
			applier_log_error(applier, e);
			applier_disconnect(applier, APPLIER_DISCONNECTED);
			goto reconnect;
		} catch (Exception *e) {
			applier_log_error(applier, e);
			applier_disconnect(applier, APPLIER_STOPPED);
			return -1;
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
		fiber_sleep(replication_reconnect_interval());
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
applier_new(const char *uri)
{
	struct applier *applier = (struct applier *)
		calloc(1, sizeof(struct applier));
	if (applier == NULL) {
		diag_set(OutOfMemory, sizeof(*applier), "malloc",
			 "struct applier");
		return NULL;
	}
	coio_create(&applier->io, -1);
	ibuf_create(&applier->ibuf, &cord()->slabc, 1024);

	/* uri_parse() sets pointers to applier->source buffer */
	snprintf(applier->source, sizeof(applier->source), "%s", uri);
	int rc = uri_parse(&applier->uri, applier->source);
	/* URI checked by box_check_replication() */
	assert(rc == 0 && applier->uri.service != NULL);
	(void) rc;

	applier->last_row_time = ev_monotonic_now(loop());
	rlist_create(&applier->on_state);
	fiber_cond_create(&applier->resume_cond);
	fiber_cond_create(&applier->writer_cond);
	diag_create(&applier->diag);

	return applier;
}

void
applier_delete(struct applier *applier)
{
	assert(applier->reader == NULL && applier->writer == NULL);
	ibuf_destroy(&applier->ibuf);
	assert(applier->io.fd == -1);
	trigger_destroy(&applier->on_state);
	diag_destroy(&applier->diag);
	free(applier);
}

void
applier_resume(struct applier *applier)
{
	assert(!fiber_is_dead(applier->reader));
	applier->is_paused = false;
	fiber_cond_signal(&applier->resume_cond);
}

void
applier_pause(struct applier *applier)
{
	/* Sleep until applier_resume() wake us up */
	assert(fiber() == applier->reader);
	assert(!applier->is_paused);
	applier->is_paused = true;
	while (applier->is_paused && !fiber_is_cancelled())
		fiber_cond_wait(&applier->resume_cond);
}

struct applier_on_state {
	struct trigger base;
	struct applier *applier;
	enum applier_state desired_state;
	struct fiber_cond wakeup;
};

static int
applier_on_state_f(struct trigger *trigger, void *event)
{
	(void) event;
	struct applier_on_state *on_state =
		container_of(trigger, struct applier_on_state, base);

	struct applier *applier = on_state->applier;

	if (applier->state != APPLIER_OFF &&
	    applier->state != APPLIER_STOPPED &&
	    applier->state != on_state->desired_state)
		return 0;

	/* Wake up waiter */
	fiber_cond_signal(&on_state->wakeup);

	applier_pause(applier);
	return 0;
}

static inline void
applier_add_on_state(struct applier *applier,
		     struct applier_on_state *trigger,
		     enum applier_state desired_state)
{
	trigger_create(&trigger->base, applier_on_state_f, NULL, NULL);
	trigger->applier = applier;
	fiber_cond_create(&trigger->wakeup);
	trigger->desired_state = desired_state;
	trigger_add(&applier->on_state, &trigger->base);
}

static inline void
applier_clear_on_state(struct applier_on_state *trigger)
{
	fiber_cond_destroy(&trigger->wakeup);
	trigger_clear(&trigger->base);
}

static inline int
applier_wait_for_state(struct applier_on_state *trigger, double timeout)
{
	struct applier *applier = trigger->applier;
	double deadline = ev_monotonic_now(loop()) + timeout;
	while (applier->state != APPLIER_OFF &&
	       applier->state != APPLIER_STOPPED &&
	       applier->state != trigger->desired_state) {
		if (fiber_cond_wait_deadline(&trigger->wakeup, deadline) != 0)
			return -1; /* ER_TIMEOUT */
	}
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
