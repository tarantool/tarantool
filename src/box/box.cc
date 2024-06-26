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
#include "box/box.h"

#include "trivia/config.h"

#include <sys/utsname.h>
#include <spawn.h>

#include "lua/utils.h" /* lua_hash() */
#include "fiber_pool.h"
#include <say.h>
#include <scoped_guard.h>
#include "identifier.h"
#include "iproto.h"
#include "iproto_constants.h"
#include "recovery.h"
#include "wal.h"
#include "relay.h"
#include "applier.h"
#include <rmean.h>
#include "main.h"
#include "tuple.h"
#include "tuple_format.h"
#include "session.h"
#include "schema.h"
#include "engine.h"
#include "memtx_engine.h"
#include "memtx_space.h"
#include "memcs_engine.h"
#include "sysview.h"
#include "blackhole.h"
#include "service_engine.h"
#include "vinyl.h"
#include "space.h"
#include "index.h"
#include "port.h"
#include "txn.h"
#include "txn_limbo.h"
#include "user.h"
#include "cfg.h"
#include "coio.h"
#include "replication.h" /* replica */
#include "title.h"
#include "xrow.h"
#include "xrow_io.h"
#include "xstream.h"
#include "authentication.h"
#include "security.h"
#include "path_lock.h"
#include "gc.h"
#include "sql.h"
#include "systemd.h"
#include "call.h"
#include "crash.h"
#include "func.h"
#include "sequence.h"
#include "sql_stmt_cache.h"
#include "msgpack.h"
#include "raft.h"
#include "watcher.h"
#include "audit.h"
#include "trivia/util.h"
#include "version.h"
#include "mp_uuid.h"
#include "flightrec.h"
#include "wal_ext.h"
#include "mp_util.h"
#include "small/static.h"
#include "memory.h"
#include "node_name.h"
#include "tt_sort.h"
#include "event.h"
#include "tweaks.h"

static char status[64] = "unconfigured";

/** box.stat rmean */
struct rmean *rmean_box;

double on_shutdown_trigger_timeout = 3.0;

double txn_timeout_default;

struct rlist box_on_shutdown_trigger_list =
	RLIST_HEAD_INITIALIZER(box_on_shutdown_trigger_list);

struct event *box_on_shutdown_event = NULL;

const struct vclock *box_vclock = &replicaset.vclock;

const char *box_auth_type;

const char *box_ballot_event_key = "internal.ballot";

struct tt_uuid bootstrap_leader_uuid;

bool box_is_force_recovery = false;

/**
 * Set if backup is in progress, i.e. box_backup_start() was
 * called but box_backup_stop() hasn't been yet.
 */
static bool backup_is_in_progress;

/**
 * If backup is in progress, this points to the gc reference
 * object that prevents the garbage collector from deleting
 * the checkpoint files that are currently being backed up.
 */
static struct gc_checkpoint_ref backup_gc;

bool box_read_ffi_is_disabled;

/**
 * Counter behind box_read_ffi_is_disabled: FFI is re-enabled
 * when it hits zero.
 */
static int box_read_ffi_disable_count;

/**
 * The instance is in read-write mode: the local checkpoint
 * and all write ahead logs are processed. For a replica,
 * it also means we've successfully connected to the master
 * and began receiving updates from it.
 */
static bool is_box_configured = false;
static bool is_storage_initialized = false;
/** Set if storage shutdown is started. */
static bool is_storage_shutdown = false;
static bool is_ro = true;
static fiber_cond ro_cond;

/**
 * The following flag is set if the instance failed to
 * synchronize to a sufficient number of replicas to form
 * a quorum and so was forced to switch to read-only mode.
 */
static bool is_orphan;

/**
 * Summary flag incorporating all the instance attributes,
 * affecting ability to write. Currently these are:
 * - is_ro;
 * - is_orphan;
 */
static bool is_ro_summary = true;

/**
 * The pool of fibers in the transaction processor thread
 * working on incoming messages from net, wal and other
 * threads.
 */
static struct fiber_pool tx_fiber_pool;
/**
 * A separate endpoint for WAL wakeup messages, to
 * ensure that WAL messages are delivered even
 * if all fibers in tx_fiber_pool are used. Without
 * this endpoint, tx thread could deadlock when there
 * are too many messages in flight (gh-1892).
 */
static struct cbus_endpoint tx_prio_endpoint;

struct event *box_on_recovery_state_event;

/** Cached event 'tarantool.trigger.on_change'. */
static struct event *tarantool_trigger_on_change_event;

/**
 * Recovery states supported by on_recovery_state triggers.
 * Are positioned in the order of appearance during initial box.cfg().
 * The only exception are WAL_RECOVERED and INDEXES_BUILT, which might come in
 * any order, since the moment when secondary indexes are built depends on
 * box.cfg.force_recovery and box.cfg.memtx_use_mvcc_engine.
 */
enum box_recovery_state {
	/**
	 * The node has either recovered the snapshot from the disk, received
	 * the snapshot from the remote master as part of initial join stage or
	 * has bootstrapped the cluster.
	 */
	RECOVERY_STATE_SNAPSHOT_RECOVERED,
	/**
	 * The node has either recovered the local WAL files, received the WALs
	 * from the remote master as part of final join or has bootstrapped the
	 * cluster.
	 */
	RECOVERY_STATE_WAL_RECOVERED,
	/**
	 * The node has built secondary indexes for memtx spaces.
	 */
	RECOVERY_STATE_INDEXES_BUILT,
	/**
	 * The node has synced with remote peers. IOW, it has transitioned from
	 * "orphan" to "running".
	 */
	RECOVERY_STATE_SYNCED,
	box_recovery_state_MAX,
};

static const char *box_recovery_state_strs[box_recovery_state_MAX] = {
	/* [RECOVERY_STATE_SNAPSHOT_RECOVERED] = */
	"snapshot_recovered",
	/* [RECOVERY_STATE_WAL_RECOVERED] = */
	"wal_recovered",
	/* [RECOVERY_STATE_INDEXES_BUILT] = */
	"indexes_built",
	/* [RECOVERY_STATE_SYNCED] = */
	"synced",
};

/** Whether the triggers for "synced" recovery stage have already run. */
static bool recovery_state_synced_is_reached;

/** PATH_MAX is too big and 2K is recommended limit for web address. */
#define BOX_FEEDBACK_HOST_MAX 2048

/** Feedback host URL to send crash info to. */
static char box_feedback_host[BOX_FEEDBACK_HOST_MAX];

/** Whether sending crash info to feedback URL is enabled. */
static bool box_feedback_crash_enabled;

#ifdef TEST_BUILD
/**
 * Set timeout to infinity in test build because first not all CI tests treat
 * non zero exit code of Tarantool instance as failure currently. Also in
 * luatest currently it is easier to test for no hanging rather then for
 * Tarantool instance exit code.
 */
#define BOX_SHUTDOWN_TIMEOUT_DEFAULT TIMEOUT_INFINITY
#else
#define BOX_SHUTDOWN_TIMEOUT_DEFAULT 3.0
#endif

/** Timeout on waiting client related fibers to finish. */
static double box_shutdown_timeout = BOX_SHUTDOWN_TIMEOUT_DEFAULT;
TWEAK_DOUBLE(box_shutdown_timeout);

/** Idle timeout for box fiber pool. */
static double box_fiber_pool_idle_timeout = FIBER_POOL_IDLE_TIMEOUT;
TWEAK_DOUBLE(box_fiber_pool_idle_timeout);

static int
box_run_on_recovery_state(enum box_recovery_state state)
{
	assert(state >= 0 && state < box_recovery_state_MAX);
	const char *state_str = box_recovery_state_strs[state];
	struct port args;
	port_c_create(&args);
	port_c_add_str0(&args, state_str);
	int rc = event_run_triggers(box_on_recovery_state_event, &args);
	port_destroy(&args);
	return rc;
}

static void
box_storage_init(void);

/**
 * Broadcast the current instance status
 */
static void
box_broadcast_status(void);

/**
 * A timer to broadcast the updated vclock. Doing this on each vclock update
 * would be too expensive.
 */
static ev_timer box_broadcast_ballot_timer;

/** Set a new interval for vclock updates in ballot. */
static void
box_update_broadcast_ballot_interval(double interval)
{
	static double ballot_broadcast_interval;
	/* Do the broadcast at least once a second. */
	interval = MIN(interval, 1.0);
	if (interval == ballot_broadcast_interval)
		return;
	double timeout = ev_timer_remaining(loop(),
					    &box_broadcast_ballot_timer);
	timeout -= ballot_broadcast_interval;
	timeout += interval;
	ev_timer_stop(loop(), &box_broadcast_ballot_timer);
	ev_timer_set(&box_broadcast_ballot_timer, timeout, interval);
	ev_timer_start(loop(), &box_broadcast_ballot_timer);
	ballot_broadcast_interval = interval;
}

/** A callback to broadcast updated vclock in ballot by timeout. */
static void
box_broadcast_ballot_on_timeout(ev_loop *loop, ev_timer *timer, int events)
{
	(void)loop;
	(void)timer;
	(void)events;
	static struct vclock broadcast_vclock;
	if (vclock_compare_ignore0(&broadcast_vclock, &replicaset.vclock) == 0)
		return;
	box_broadcast_ballot();
	vclock_copy(&broadcast_vclock, &replicaset.vclock);
}

/**
 * Generate and update the instance status title
 */
static void
title(const char *new_status)
{
	snprintf(status, sizeof(status), "%s", new_status);
	title_set_status(new_status);
	title_update();
	systemd_snotify("STATUS=%s", status);
	box_broadcast_status();
}

void
box_update_ro_summary(void)
{
	bool old_is_ro_summary = is_ro_summary;
	is_ro_summary = is_ro || is_orphan || raft_is_ro(box_raft()) ||
			txn_limbo_is_ro(&txn_limbo);
	/* In 99% nothing changes. Filter this out first. */
	if (is_ro_summary == old_is_ro_summary)
		return;

	if (is_ro_summary)
		engine_switch_to_ro();
	fiber_cond_broadcast(&ro_cond);
	box_broadcast_status();
	box_broadcast_election();
	box_broadcast_ballot();
}

const char *
box_ro_reason(void)
{
	if (raft_is_ro(box_raft()))
		return "election";
	if (txn_limbo_is_ro(&txn_limbo))
		return "synchro";
	if (is_ro)
		return "config";
	if (is_orphan)
		return "orphan";
	return NULL;
}

int
box_check_slice_slow(void)
{
	return fiber_check_slice();
}

int
box_check_writable(void)
{
	if (!is_ro_summary)
		return 0;
	struct error *e = diag_set(ClientError, ER_READONLY);
	struct raft *raft = box_raft();
	error_append_msg(e, " - ");
	error_set_str(e, "reason", box_ro_reason());
	/*
	 * In case of multiple reasons at the same time only one is reported.
	 * But the order is important. For example, if the instance has election
	 * enabled, for the client it is better to see that it is a 'follower'
	 * and who is the leader than just see cfg 'read_only' is true.
	 */
	if (raft_is_ro(raft)) {
		const char *state = raft_state_str(raft->state);
		uint64_t term = raft->volatile_term;
		error_set_str(e, "state", state);
		error_set_uint(e, "term", term);
		error_append_msg(e, "state is election %s with term %llu",
				 state, (unsigned long long)term);
		uint32_t id = raft->leader;
		if (id != REPLICA_ID_NIL) {
			error_set_uint(e, "leader_id", id);
			error_append_msg(e, ", leader is %u", id);
			struct replica *r = replica_by_id(id);
			/*
			 * XXX: when the leader is dropped from _cluster, it
			 * is not reported to Raft.
			 */
			if (r != NULL) {
				error_set_uuid(e, "leader_uuid", &r->uuid);
				error_append_msg(e, " (%s)",
						 tt_uuid_str(&r->uuid));
			}
		}
	} else if (txn_limbo_is_ro(&txn_limbo)) {
		uint32_t id = txn_limbo.owner_id;
		uint64_t term = txn_limbo.promote_greatest_term;
		error_set_uint(e, "queue_owner_id", id);
		error_set_uint(e, "term", term);
		error_append_msg(e, "synchro queue with term %llu belongs "
				 "to %u", (unsigned long long)term,
				 (unsigned)id);
		struct replica *r = replica_by_id(id);
		/*
		 * XXX: when an instance is deleted from _cluster, its limbo's
		 * ownership is not cleared.
		 */
		if (r != NULL) {
			error_set_uuid(e, "queue_owner_uuid", &r->uuid);
			error_append_msg(e, " (%s)", tt_uuid_str(&r->uuid));
		}
		if (txn_limbo.owner_id == instance_id) {
			if (txn_limbo.is_frozen_due_to_fencing) {
				error_append_msg(e, " and is frozen due to "
						    "fencing");
			} else if (txn_limbo.is_frozen_until_promotion) {
				error_append_msg(e, " and is frozen until "
						    "promotion");
			}
		}
	} else {
		if (is_ro)
			error_append_msg(e, "box.cfg.read_only is true");
		else if (is_orphan)
			error_append_msg(e, "it is an orphan");
		else
			assert(false);
	}
	diag_log();
	return -1;
}

static void
box_check_writable_xc(void)
{
	if (box_check_writable() != 0)
		diag_raise();
}

static void
box_check_memtx_min_tuple_size(ssize_t memtx_min_tuple_size)
{

	if (memtx_min_tuple_size < 8 || memtx_min_tuple_size > 1048280)
	tnt_raise(ClientError, ER_CFG, "memtx_min_tuple_size",
		  "specified value is out of bounds");
}

int
box_process_rw(struct request *request, struct space *space,
	       struct tuple **result)
{
	struct tuple *tuple = NULL;
	bool return_tuple = false;
	struct txn *txn = in_txn();
	bool is_autocommit = txn == NULL;
	if (is_autocommit && (txn = txn_begin()) == NULL)
		return -1;
	assert(iproto_type_is_dml(request->type));
	rmean_collect(rmean_box, request->type, 1);
	if (access_check_space(space, PRIV_W) != 0)
		goto rollback;
	if (txn_begin_stmt(txn, space, request->type) != 0)
		goto rollback;
	if (space_execute_dml(space, txn, request, &tuple) != 0) {
		txn_rollback_stmt(txn);
		goto rollback;
	}
	if (result != NULL)
		*result = tuple;

	return_tuple = result != NULL && tuple != NULL;
	if (return_tuple) {
		/*
		 * Pin the tuple locally before the commit,
		 * otherwise it may go away during yield in
		 * when WAL is written in autocommit mode.
		 */
		tuple_ref(tuple);
	}

	if (txn_commit_stmt(txn, request))
		goto rollback;

	if (is_autocommit && txn_commit(txn) < 0)
		goto error;
	if (return_tuple) {
		tuple_bless(tuple);
		tuple_unref(tuple);
	}
	return 0;

rollback:
	if (is_autocommit)
		txn_abort(txn);
error:
	if (return_tuple)
		tuple_unref(tuple);
	return -1;
}

static bool
box_check_ro(void);

void
box_set_ro(void)
{
	is_ro = box_check_ro();
	box_update_ro_summary();
}

bool
box_is_ro(void)
{
	return is_ro_summary;
}

bool
box_is_orphan(void)
{
	return is_orphan;
}

bool
box_is_anon(void)
{
	return instance_id == REPLICA_ID_NIL;
}

int
box_wait_ro(bool ro, double timeout)
{
	double deadline = ev_monotonic_now(loop()) + timeout;
	while (is_box_configured == false || box_is_ro() != ro) {
		if (fiber_cond_wait_deadline(&ro_cond, deadline) != 0)
			return -1;
	}
	return 0;
}

void
box_do_set_orphan(bool orphan)
{
	is_orphan = orphan;
	box_update_ro_summary();
	if (!is_orphan && !recovery_state_synced_is_reached) {
		box_run_on_recovery_state(RECOVERY_STATE_SYNCED);
		recovery_state_synced_is_reached = true;
	}
}

void
box_set_orphan(bool orphan)
{
	box_do_set_orphan(orphan);
	/* Update the title to reflect the new status. */
	if (is_orphan) {
		say_info("entering orphan mode");
		title("orphan");
	} else {
		say_info("leaving orphan mode");
		title("running");
	}
}

struct wal_stream {
	/** Base class. */
	struct xstream base;
	/** The lsregion for allocating rows. */
	struct lsregion lsr;
	/** The array of lists keeping rows from xlog. */
	struct rlist nodes_rows[VCLOCK_MAX];
	/** Current transaction ID. 0 when no transaction. */
	int64_t tsn;
	/**
	 * LSN of the first row saved to check TSN and LSN match in case all
	 * rows of the tx appeared to be local.
	 */
	int64_t first_row_lsn;
	/**
	 * Flag whether there is a pending yield to do when the current
	 * transaction is finished. It can't always be done right away because
	 * would abort the current transaction if it is memtx.
	 */
	bool has_yield;
	/**
	 * True if any row in the transaction was global. Saved to check if TSN
	 * matches LSN of a first global row.
	 */
	bool has_global_row;
};

/**
 * A stub used in txn_commit() during local recovery. We "replay"
 * transactions during local recovery, with WAL turned off.
 * Since each transaction attempts to write itself to WAL at
 * commit, we need an implementation which would fake WAL write.
 */
struct recovery_journal {
	struct journal base;
	struct vclock *vclock;
};

/**
 * Use the current row LSN as commit LSN - vinyl needs to see the
 * exact same signature during local recovery to properly mark
 * min/max LSN of created LSM levels.
 */
static int
recovery_journal_write(struct journal *base,
		       struct journal_entry *entry)
{
	struct recovery_journal *journal = (struct recovery_journal *) base;
	entry->res = vclock_sum(journal->vclock);
	/*
	 * Since there're no actual writes, fire a
	 * journal_async_complete callback right away.
	 */
	journal_async_complete(entry);
	return 0;
}

static void
recovery_journal_create(struct vclock *v)
{
	static struct recovery_journal journal;
	journal_create(&journal.base, recovery_journal_write,
		       recovery_journal_write);
	journal.vclock = v;
	journal_set(&journal.base);
}

/**
 * Drop the stream to the initial state. It is supposed to be done when an error
 * happens. Because in case of force recovery the stream will continue getting
 * tuples. For that it must stay in a valid state and must handle them somehow.
 *
 * Now the stream simply drops the current transaction like it never happened,
 * even if its commit-row wasn't met yet. Should be good enough for
 * force-recovery when the consistency is already out of the game.
 */
static void
wal_stream_abort(struct wal_stream *stream)
{
	struct txn *tx = in_txn();
	if (tx != NULL)
		txn_abort(tx);
	stream->tsn = 0;
}

/**
 * The wrapper exists only for the debug purposes, to ensure tsn being non-0 is
 * in sync with the fiber's txn being non-NULL. It has nothing to do with the
 * journal content, and therefore can use assertions instead of rigorous error
 * checking even in release.
 */
static bool
wal_stream_has_tx_in_progress(const struct wal_stream *stream)
{
	bool has = stream->tsn != 0;
	assert(has == (in_txn() != NULL));
	return has;
}

/**
 * The function checks that the tail of the transaction from the journal
 * has been read. A mixed transaction consists of multiple transactions
 * that are mixed together. Need to make sure that after the restoration
 * is completed, there are no unfinished transactions left in the
 * nodes_rows array. wal_stream_has_tx_in_progress is a transaction which
 * has all the data, but simply isn't committed yet.
 */
static bool
wal_stream_has_unfinished_tx(const struct wal_stream *stream)
{
	if (wal_stream_has_tx_in_progress(stream))
		return true;
	const struct rlist *nodes_rows = stream->nodes_rows;
	for (int i = 0; i < VCLOCK_MAX; i++) {
		if (!rlist_empty((struct rlist *)&nodes_rows[i]))
			return true;
	}
	return false;
}

static int
wal_stream_apply_synchro_row(struct wal_stream *stream, struct xrow_header *row)
{
	assert(iproto_type_is_synchro_request(row->type));
	if (wal_stream_has_tx_in_progress(stream)) {
		diag_set(XlogError, "found synchro request in a transaction");
		return -1;
	}
	struct synchro_request syn_req;
	if (xrow_decode_synchro(row, &syn_req, NULL) != 0) {
		say_error("couldn't decode a synchro request");
		return -1;
	}
	return txn_limbo_process(&txn_limbo, &syn_req);
}

static int
wal_stream_apply_raft_row(struct wal_stream *stream, struct xrow_header *row)
{
	assert(iproto_type_is_raft_request(row->type));
	if (wal_stream_has_tx_in_progress(stream)) {
		diag_set(XlogError, "found raft request in a transaction");
		return -1;
	}
	struct raft_request raft_req;
	/* Vclock is never persisted in WAL by Raft. */
	if (xrow_decode_raft(row, &raft_req, NULL) != 0) {
		say_error("couldn't decode a raft request");
		return -1;
	}
	box_raft_recover(&raft_req);
	return 0;
}

/**
 * Rows of the same transaction are wrapped into begin/commit. Mostly for the
 * sake of synchronous replication, when the log can contain rolled back
 * transactions, which must be entirely reverted during recovery when ROLLBACK
 * records are met. Row-by-row recovery wouldn't work for multi-statement
 * synchronous transactions.
 */
static int
wal_stream_apply_dml_row(struct wal_stream *stream, struct xrow_header *row)
{
	struct request request;
	uint64_t req_type = dml_request_key_map(row->type);
	if (xrow_decode_dml(row, &request, req_type) != 0) {
		say_error("couldn't decode a DML request");
		return -1;
	}
	/*
	 * Note that all the information which came from the log is validated
	 * and the errors are handled. Not asserted or paniced. That is for the
	 * sake of force recovery, which must be able to recover just everything
	 * what possible instead of terminating the instance.
	 */
	struct txn *txn;
	if (stream->tsn == 0) {
		if (row->tsn == 0) {
			diag_set(XlogError, "found a row without TSN");
			goto end_diag_request;
		}
		stream->tsn = row->tsn;
		stream->first_row_lsn = row->lsn;
		stream->has_global_row = false;
		/*
		 * Rows are not stacked into a list like during replication,
		 * because recovery does not yield while reading the rows. All
		 * the yields are controlled by the stream, and therefore no
		 * need to wait for all the rows to start a transaction. Can
		 * start now, apply the rows, and make a yield after commit if
		 * necessary. Helps to avoid a lot of copying.
		 */
		txn = txn_begin();
		if (txn == NULL) {
			say_error("couldn't begin a recovery transaction");
			return -1;
		}
	} else if (row->tsn != stream->tsn) {
		diag_set(XlogError, "found a next transaction with the "
			 "previous one not yet committed");
		goto end_diag_request;
	} else {
		txn = in_txn();
	}
	/* Ensure TSN is equal to LSN of the first global row. */
	if (!stream->has_global_row && row->group_id != GROUP_LOCAL) {
		if (row->tsn != row->lsn) {
			diag_set(XlogError, "found a first global row in a "
				 "transaction with LSN/TSN mismatch");
			goto end_diag_request;
		}
		stream->has_global_row = true;
	}
	assert(wal_stream_has_tx_in_progress(stream));
	/* Nops might appear at least after before_replace skipping rows. */
	if (request.type != IPROTO_NOP) {
		struct space *space = space_cache_find(request.space_id);
		if (space == NULL) {
			say_error("couldn't find space by ID");
			goto end_diag_request;
		}
		if (box_process_rw(&request, space, NULL) != 0) {
			say_error("couldn't apply the request");
			goto end_diag_request;
		}
	}
	assert(txn != NULL);
	if (!row->is_commit)
		return 0;
	/*
	 * For fully local transactions the TSN check won't work like for global
	 * transactions, because it is not known if there are global rows until
	 * commit arrives.
	 */
	if (!stream->has_global_row && stream->tsn != stream->first_row_lsn) {
		diag_set(XlogError, "fully local transaction's TSN does not "
			 "match LSN of the first row");
		return -1;
	}
	stream->tsn = 0;
	/*
	 * During local recovery the commit procedure should be async, otherwise
	 * the only fiber processing recovery will get stuck on the first
	 * synchronous tx it meets until confirm timeout is reached and the tx
	 * is rolled back, yielding an error.
	 * Moreover, txn_commit_try_async() doesn't hurt at all during local
	 * recovery, since journal_write is faked at this stage and returns
	 * immediately.
	 */
	if (txn_commit_try_async(txn) != 0) {
		/* Commit fail automatically leads to rollback. */
		assert(in_txn() == NULL);
		say_error("couldn't commit a recovery transaction");
		return -1;
	}
	assert(in_txn() == NULL);
	return 0;

end_diag_request:
	/*
	 * The label must be used only for the errors related directly to the
	 * request. Errors like txn_begin() fail has nothing to do with it, and
	 * therefore don't log the request as the fault reason.
	 */
	say_error("error at request: %s", request_str(&request));
	return -1;
}

/**
 * Keeping added rows in a rlist to separate mixed transactions
 */
struct wal_row {
	/** Base class. */
	struct xrow_header row;
	/** A link on the list of rows stacked in the nodes_rows array. */
	struct rlist in_row_list;
	/** A growing identifier to track lsregion allocations. */
	int64_t lsr_id;
};

/**
 * Callback to stash row and row bodies upon receipt, used to recover
 * mixed transactions.
 */
static struct wal_row *
wal_stream_save_row(struct wal_stream *stream,
		    const struct xrow_header *row)
{
	static int64_t lsr_id = 0;
	struct lsregion *lsr = &stream->lsr;
	struct wal_row *new_row = xlsregion_alloc_object(lsr, ++lsr_id,
							 typeof(*new_row));
	new_row->lsr_id = lsr_id;
	new_row->row = *row;

	assert(new_row->row.bodycnt <= 1);
	if (new_row->row.bodycnt == 1) {
		size_t len = new_row->row.body[0].iov_len;
		char *new_base = (char *)xlsregion_alloc(lsr, len, lsr_id);
		memcpy(new_base, new_row->row.body[0].iov_base, len);
		/* Adjust row body pointers. */
		new_row->row.body[0].iov_base = new_base;
	}
	return new_row;
}

/**
 * Find the min lsr_id that is still needed.
 */
static int64_t
wal_stream_find_min_lsr_id(struct wal_stream *stream)
{
	int64_t min_lsr_id = INT64_MAX;
	struct rlist *nodes_rows = stream->nodes_rows;
	struct wal_row *item;
	/*
	 * For each new row, lsr_id is incremented by 1. Only row from
	 * different replica_id can get mixed, so for each replica_id,
	 * the very first row has the smallest lsr_id of all row with that
	 * replica_id. Thus, for each transaction read to the end, to
	 * iterate through the nodes_rows array of lists and see which of
	 * the first `row` for each `replica_id` has the lowest lsr_id.
	 * This lsr_id is still needed, but anything less than this is not.
	 * So can free everything up to this lsr_id.
	 */
	for (uint32_t i = 0; i < VCLOCK_MAX; i++) {
		if (!rlist_empty(&nodes_rows[i])) {
			item = rlist_first_entry(&nodes_rows[i], wal_row,
						 in_row_list);
			if (item->lsr_id <= min_lsr_id)
				min_lsr_id = item->lsr_id - 1;
		}
	}
	return min_lsr_id;
}

/**
 * Deallocating memory for the wal_row
 */
static void
wal_stream_gc(struct wal_stream *stream)
{
	struct lsregion *lsr = &stream->lsr;
	int64_t lsr_id = wal_stream_find_min_lsr_id(stream);
	lsregion_gc(lsr, lsr_id);
}

/**
 * When restoring the log is read row-by-row. However, it is necessary
 * to store the rows for correct further recovery. For example, with
 * mixed transactions. The function saves the again coming string to the
 * rlist. This rlist is stored in nodes_rows array in replica_id cell.
 * As soon as a row arrives with the is_commit flag set, the corresponding
 * transaction tries to apply. If an error occurs the transaction is
 * rolled back. And we continue to work with the next transaction.
 */
static int
wal_stream_apply_mixed_dml_row(struct wal_stream *stream,
			       struct xrow_header *row)
{
	/*
	 * A local row can be part of any node's transaction. Try to find the
	 * right transaction by tsn. If this fails, assume the row belongs to
	 * this node.
	 */
	uint32_t id;
	struct rlist *nodes_rows = stream->nodes_rows;
	if (row->replica_id == 0) {
		id = instance_id;
		for (uint32_t i = 0; i < VCLOCK_MAX; i++) {
			if (rlist_empty(&nodes_rows[i]))
				continue;
			struct wal_row *tmp =
				rlist_last_entry(&nodes_rows[i], typeof(*tmp),
						 in_row_list);
			if (tmp->row.tsn == row->tsn) {
				id = i;
				break;
			}
		}
	} else {
		id = row->replica_id;
	}

	struct wal_row *save_row = wal_stream_save_row(stream, row);
	rlist_add_tail_entry(&nodes_rows[id], save_row, in_row_list);

	if (!row->is_commit)
		return 0;

	int rc = 0;
	struct wal_row *item, *next_row;
	rlist_foreach_entry_safe(item, &nodes_rows[id],
				 in_row_list, next_row) {
		rlist_del_entry(item, in_row_list);
		if (rc != 0)
			continue;
		rc = wal_stream_apply_dml_row(stream, &item->row);
	}
	/* deallocating the memory */
	wal_stream_gc(stream);
	assert(rlist_empty(&nodes_rows[id]));

	return rc;
}

/**
 * Yield once in a while, but not too often, mostly to allow signal handling to
 * take place.
 */
static void
wal_stream_try_yield(struct wal_stream *stream)
{
	if (wal_stream_has_tx_in_progress(stream) || !stream->has_yield)
		return;
	stream->has_yield = false;
	fiber_sleep(0);
}

static void
wal_stream_apply_row(struct xstream *base, struct xrow_header *row)
{
	struct wal_stream *stream =
		container_of(base, struct wal_stream, base);
	if (iproto_type_is_synchro_request(row->type)) {
		if (wal_stream_apply_synchro_row(stream, row) != 0)
			goto end_error;
	} else if (iproto_type_is_raft_request(row->type)) {
		if (wal_stream_apply_raft_row(stream, row) != 0)
			goto end_error;
	} else if (box_is_force_recovery) {
		if (wal_stream_apply_mixed_dml_row(stream, row) != 0)
			goto end_error;
	} else if (wal_stream_apply_dml_row(stream, row) != 0) {
		goto end_error;
	}
	wal_stream_try_yield(stream);
	return;

end_error:
	wal_stream_abort(stream);
	wal_stream_try_yield(stream);
	diag_raise();
}

/**
 * Plan a yield in recovery stream. Wal stream will execute it as soon as it's
 * ready.
 */
static void
wal_stream_schedule_yield(struct xstream *base)
{
	struct wal_stream *stream = container_of(base, struct wal_stream, base);
	stream->has_yield = true;
	wal_stream_try_yield(stream);
}

static void
wal_stream_create(struct wal_stream *ctx)
{
	xstream_create(&ctx->base, wal_stream_apply_row,
		       wal_stream_schedule_yield);
	lsregion_create(&ctx->lsr, &runtime);
	for (int i = 0; i < VCLOCK_MAX; i++) {
		rlist_create(&ctx->nodes_rows[i]);
	}
	ctx->tsn = 0;
	ctx->first_row_lsn = 0;
	ctx->has_yield = false;
	ctx->has_global_row = false;
}

static void
wal_stream_destroy(struct wal_stream *ctx)
{
	lsregion_destroy(&ctx->lsr);
	TRASH(ctx);
}

/* {{{ configuration bindings */

/*
 * Check log configuration validity.
 *
 * Used thru Lua FFI.
 */
extern "C" int
say_check_cfg(const char *log,
	      MAYBE_UNUSED int level,
	      int nonblock,
	      const char *format_str)
{
	enum say_logger_type type = SAY_LOGGER_STDERR;
	if (log != NULL && say_parse_logger_type(&log, &type) < 0) {
		diag_set(ClientError, ER_CFG, "log",
			 diag_last_error(diag_get())->errmsg);
		return -1;
	}
	if (type == SAY_LOGGER_SYSLOG) {
		struct say_syslog_opts opts;
		if (say_parse_syslog_opts(log, &opts) < 0) {
			if (diag_last_error(diag_get())->type ==
			    &type_IllegalParams)
				diag_set(ClientError, ER_CFG, "log",
					 diag_last_error(diag_get())->errmsg);
			return -1;
		}
		say_free_syslog_opts(&opts);
	}

	enum say_format format = say_format_by_name(format_str);
	if (format == say_format_MAX) {
		diag_set(ClientError, ER_CFG, "log_format",
			 "expected 'plain' or 'json'");
		return -1;
	}
	if (nonblock == 1 &&
	    (type == SAY_LOGGER_FILE || type == SAY_LOGGER_STDERR)) {
		diag_set(ClientError, ER_CFG, "log_nonblock",
			 "the option is incompatible with file/stderr logger");
		return -1;
	}
	return 0;
}

/**
 * Returns the authentication method corresponding to box.cfg.auth_type.
 * If not found, sets diag and returns NULL.
 */
static const struct auth_method *
box_check_auth_type(void)
{
	const char *auth_type = cfg_gets("auth_type");
	const struct auth_method *method =
		auth_method_by_name(auth_type, strlen(auth_type));
	if (method == NULL) {
		diag_set(ClientError, ER_CFG, "auth_type", auth_type);
		return NULL;
	}
	return method;
}

static enum election_mode
box_check_election_mode(void)
{
	const char *mode = cfg_gets("election_mode");
	if (strcmp(mode, "off") == 0)
		return ELECTION_MODE_OFF;
	else if (strcmp(mode, "voter") == 0)
		return ELECTION_MODE_VOTER;
	else if (strcmp(mode, "manual") == 0)
		return ELECTION_MODE_MANUAL;
	else if (strcmp(mode, "candidate") == 0)
		return ELECTION_MODE_CANDIDATE;

	diag_set(ClientError, ER_CFG, "election_mode",
		"the value must be one of the following strings: "
		"'off', 'voter', 'candidate', 'manual'");
	return ELECTION_MODE_INVALID;
}

static double
box_check_election_timeout(void)
{
	double d = cfg_getd("election_timeout");
	if (d <= 0) {
		diag_set(ClientError, ER_CFG, "election_timeout",
			 "the value must be a positive number");
		return -1;
	}
	return d;
}

/**
 * Raises error if election_fencing_mode configuration is incorrect.
 */
static election_fencing_mode
box_check_election_fencing_mode(void)
{
	const char *mode = cfg_gets("election_fencing_mode");
	if (strcmp(mode, "off") == 0)
		return ELECTION_FENCING_MODE_OFF;
	else if (strcmp(mode, "soft") == 0)
		return ELECTION_FENCING_MODE_SOFT;
	else if (strcmp(mode, "strict") == 0)
		return ELECTION_FENCING_MODE_STRICT;

	diag_set(ClientError, ER_CFG, "election_fencing_mode",
		 "the value must be one of the following strings: "
		 "'off', 'soft', 'strict'");
	return ELECTION_FENCING_MODE_INVALID;
}

/** A helper to check validity of a single uri. */
static int
check_uri(const struct uri *uri, const char *option_name, bool set_diag)
{
	const char *auth_type = uri_param(uri, "auth_type", 0);
	const char *errmsg;
	if (uri->service == NULL) {
		errmsg = "expected host:service or /unix.socket";
		goto bad_uri;
	}
	if (auth_type != NULL &&
	    auth_method_by_name(auth_type, strlen(auth_type)) == NULL) {
		errmsg = "unknown authentication method";
		goto bad_uri;
	}
	return 0;
bad_uri:;
	if (set_diag) {
		char *uristr = tt_static_buf();
		uri_format(uristr, TT_STATIC_BUF_LEN, uri, false);
		diag_set(ClientError, ER_CFG, option_name,
			 tt_sprintf("bad URI '%s': %s", uristr, errmsg));
	}
	return -1;
}

/**
 * Check validity of a uri passed in a configuration option.
 * On success stores the uri in @a uri.
 */
static int
box_check_uri(struct uri *uri, const char *option_name, bool set_diag)
{
	const char *source = cfg_gets(option_name);
	if (source == NULL) {
		uri_create(uri, NULL);
		return 0;
	}
	if (uri_create(uri, source) == 0)
		return check_uri(uri, option_name, set_diag);
	if (set_diag) {
		diag_set(ClientError, ER_CFG, option_name,
			 tt_sprintf("bad URI '%s': expected host:service or "
				    "/unix.socket", source));
	}
	return -1;
}

/**
 * Check validity of a uri set passed in a configuration option.
 * On success stores the uri set in @a uri_set.
 */
static int
box_check_uri_set(struct uri_set *uri_set, const char *option_name)
{
	if (cfg_get_uri_set(option_name, uri_set) != 0) {
		diag_set(ClientError, ER_CFG, option_name,
			 diag_last_error(diag_get())->errmsg);
		return -1;
	}
	for (int i = 0; i < uri_set->uri_count; i++) {
		const struct uri *uri = &uri_set->uris[i];
		if (check_uri(uri, option_name, true) != 0) {
			uri_set_destroy(uri_set);
			return -1;
		}
	}
	return 0;
}

static int
box_check_replication(struct uri_set *uri_set)
{
	return box_check_uri_set(uri_set, "replication");
}

static int
box_check_replication_threads(void)
{
	int count = cfg_geti("replication_threads");
	if (count <= 0 || count > REPLICATION_THREADS_MAX) {
		diag_set(ClientError, ER_CFG, "replication_threads",
			 tt_sprintf("must be greater than 0, less than or "
				    "equal to %d", REPLICATION_THREADS_MAX));
		return -1;
	}
	return 0;
}

/** Check bootstrap_strategy option validity. */
static enum bootstrap_strategy
box_check_bootstrap_strategy(void)
{
	const char *strategy = cfg_gets("bootstrap_strategy");
	if (strcmp(strategy, "auto") == 0)
		return BOOTSTRAP_STRATEGY_AUTO;
	if (strcmp(strategy, "legacy") == 0)
		return BOOTSTRAP_STRATEGY_LEGACY;
	if (strcmp(strategy, "config") == 0)
		return BOOTSTRAP_STRATEGY_CONFIG;
	if (strcmp(strategy, "supervised") == 0)
		return BOOTSTRAP_STRATEGY_SUPERVISED;
	diag_set(ClientError, ER_CFG, "bootstrap_strategy",
		 "the value should be one of the following: "
		 "'auto', 'config', 'supervised', 'legacy'");
	return BOOTSTRAP_STRATEGY_INVALID;
}

static int
box_check_listen(struct uri_set *uri_set)
{
	return box_check_uri_set(uri_set, "listen");
}

static double
box_check_replication_timeout(void)
{
	double timeout = cfg_getd("replication_timeout");
	if (timeout <= 0) {
		tnt_raise(ClientError, ER_CFG, "replication_timeout",
			  "the value must be greather than 0");
	}
	return timeout;
}

static double
box_check_replication_connect_timeout(void)
{
	double timeout = cfg_getd("replication_connect_timeout");
	if (timeout <= 0) {
		tnt_raise(ClientError, ER_CFG, "replication_connect_timeout",
			  "the value must be greather than 0");
	}
	return timeout;
}

static int
box_check_replication_connect_quorum(void)
{
	int quorum = cfg_geti_default("replication_connect_quorum",
				      REPLICATION_CONNECT_QUORUM_ALL);
	if (quorum < 0) {
		tnt_raise(ClientError, ER_CFG, "replication_connect_quorum",
			  "the value must be greater or equal to 0");
	}
	return quorum;
}

static double
box_check_replication_sync_lag(void)
{
	double lag = cfg_getd_default("replication_sync_lag", TIMEOUT_INFINITY);
	if (lag <= 0) {
		tnt_raise(ClientError, ER_CFG, "replication_sync_lag",
			  "the value must be greater than 0");
	}
	return lag;
}

/**
 * Evaluate replication syncro quorum number from a formula.
 */
static int
box_eval_replication_synchro_quorum(int nr_replicas)
{
	assert(nr_replicas > 0 && nr_replicas < VCLOCK_MAX);

	const char loadable[] =
		"local expr, N = ...\n"
		"local f, err = loadstring('return ('..expr..')')\n"
		"if not f then "
			"error(string.format('Failed to load \%\%s:"
			"\%\%s', expr, err)) "
		"end\n"
		"setfenv(f, {N = N, math = {"
			"ceil = math.ceil,"
			"floor = math.floor,"
			"abs = math.abs,"
			"random = math.random,"
			"min = math.min,"
			"max = math.max,"
			"sqrt = math.sqrt,"
			"fmod = math.fmod,"
		"}})\n"
		"local res = f()\n"
		"if type(res) ~= 'number' then\n"
			"error('Expression should return a number')\n"
		"end\n"
		"return math.floor(res)\n";
	const char *expr = cfg_gets("replication_synchro_quorum");

	luaL_loadstring(tarantool_L, loadable);
	lua_pushstring(tarantool_L, expr);
	lua_pushinteger(tarantool_L, nr_replicas);

	if (lua_pcall(tarantool_L, 2, 1, 0) != 0) {
		diag_set(ClientError, ER_CFG,
			 "replication_synchro_quorum",
			 lua_tostring(tarantool_L, -1));
		return -1;
	}

	int64_t quorum = -1;
	if (lua_isnumber(tarantool_L, -1))
		quorum = luaL_toint64(tarantool_L, -1);
	lua_pop(tarantool_L, 1);

	/*
	 * At least we should have 1 node to sync, the weird
	 * formulas such as N-2 do not guarantee quorums thus
	 * return an error.
	 */
	if (quorum <= 0 || quorum >= VCLOCK_MAX) {
		const char *msg =
			tt_sprintf("the formula is evaluated "
				   "to the quorum %lld for replica "
				   "number %d, which is out of range "
				   "[%d;%d]", (long long)quorum,
				   nr_replicas, 1, VCLOCK_MAX - 1);
		diag_set(ClientError, ER_CFG,
			 "replication_synchro_quorum", msg);
		return -1;
	}

	return quorum;
}

static int
box_check_replication_synchro_quorum(void)
{
	if (!cfg_isnumber("replication_synchro_quorum")) {
		/*
		 * The formula uses symbolic name 'N' as
		 * a number of currently registered replicas.
		 *
		 * When we're in "checking" mode we should walk
		 * over all possible number of replicas to make
		 * sure the formula is correct.
		 *
		 * Note that currently VCLOCK_MAX is pretty small
		 * value but if we gonna increase this limit make
		 * sure that the cycle won't take too much time.
		 */
		for (int i = 1; i < VCLOCK_MAX; i++) {
			if (box_eval_replication_synchro_quorum(i) < 0)
				return -1;
		}
		return 0;
	}

	int64_t quorum = cfg_geti64("replication_synchro_quorum");
	if (quorum <= 0 || quorum >= VCLOCK_MAX) {
		diag_set(ClientError, ER_CFG, "replication_synchro_quorum",
			 "the value must be greater than zero and less than "
			 "maximal number of replicas");
		return -1;
	}
	return 0;
}

static double
box_check_replication_synchro_timeout(void)
{
	double timeout = cfg_getd("replication_synchro_timeout");
	if (timeout <= 0) {
		diag_set(ClientError, ER_CFG, "replication_synchro_timeout",
			 "the value must be greater than zero");
		return -1;
	}
	return timeout;
}

static double
box_check_replication_sync_timeout(void)
{
	double timeout = cfg_getd("replication_sync_timeout");
	if (timeout < 0) {
		tnt_raise(ClientError, ER_CFG, "replication_sync_timeout",
			  "the value must be greater than or equal to 0");
	}
	return timeout;
}

/** Check validity of a uuid passed in a configuration option. */
static inline int
box_check_uuid(struct tt_uuid *uuid, const char *name, bool set_diag)
{
	*uuid = uuid_nil;
	const char *uuid_str = cfg_gets(name);
	if (uuid_str == NULL)
		return 0;
	if (tt_uuid_from_string(uuid_str, uuid) != 0) {
		if (set_diag)
			diag_set(ClientError, ER_CFG, name, uuid_str);
		return -1;
	}
	if (tt_uuid_is_nil(uuid)) {
		if (set_diag) {
			diag_set(ClientError, ER_CFG, name,
				 tt_sprintf("nil UUID is reserved"));
		}
		return -1;
	}
	return 0;
}

static bool
box_check_ro(void)
{
	bool ro = cfg_geti("read_only") != 0;
	bool anon = cfg_geti("replication_anon") != 0;
	if (anon && !ro) {
		tnt_raise(ClientError, ER_CFG, "read_only",
			  "the value may be set to false only when "
			  "replication_anon is false");
	}
	return ro;
}

static bool
box_check_replication_anon(void)
{
	bool anon = cfg_geti("replication_anon") != 0;
	bool ro = cfg_geti("read_only") != 0;
	if (anon && !ro) {
		tnt_raise(ClientError, ER_CFG, "replication_anon",
			  "the value may be set to true only when "
			  "the instance is read-only");
	}
	return anon;
}

static int
box_check_instance_uuid(struct tt_uuid *uuid)
{
	return box_check_uuid(uuid, "instance_uuid", true);
}

/** Fetch an optional node name from the config. */
static int
box_check_node_name(char *out, const char *cfg_name, bool set_diag)
{
	const char *name = cfg_gets(cfg_name);
	if (name == NULL) {
		*out = 0;
		return 0;
	}
	/* Nil name is allowed as Lua box.NULL or nil. Not as "". */
	if (!node_name_is_valid(name)) {
		if (set_diag) {
			diag_set(ClientError, ER_CFG, cfg_name,
				 "expected a valid name");
		}
		return -1;
	}
	strlcpy(out, name, NODE_NAME_SIZE_MAX);
	return 0;
}

static int
box_check_instance_name(char *out)
{
	return box_check_node_name(out, "instance_name", true);
}

static int
box_check_replicaset_uuid(struct tt_uuid *uuid)
{
	return box_check_uuid(uuid, "replicaset_uuid", true);
}

/** Check bootstrap_leader option validity. */
static int
box_check_bootstrap_leader(struct uri *uri, struct tt_uuid *uuid, char *name)
{
	*uuid = uuid_nil;
	uri_create(uri, NULL);
	*name = '\0';
	const char *source = cfg_gets("bootstrap_leader");
	enum bootstrap_strategy strategy = box_check_bootstrap_strategy();
	if (strategy != BOOTSTRAP_STRATEGY_CONFIG) {
		if (source == NULL) {
			/* Nothing to do. */
			return 0;
		}
		diag_set(ClientError, ER_CFG, "bootstrap_leader",
			 "the option takes no effect when bootstrap strategy "
			 "is not 'config'");
		return -1;
	} else if (source == NULL) {
		diag_set(ClientError, ER_CFG, "bootstrap_leader",
			 "the option can't be empty when bootstrap strategy "
			 "is 'config'");
		return -1;
	}
	if (box_check_uri(uri, "bootstrap_leader", false) == 0)
		return 0;
	/* Not a uri. Try uuid then. */
	if (box_check_uuid(uuid, "bootstrap_leader", false) == 0)
		return 0;
	if (box_check_node_name(name, "bootstrap_leader", false) == 0)
		return 0;
	diag_set(ClientError, ER_CFG, "bootstrap_leader",
		 "the value must be either a uri, a uuid or a name");
	return -1;
}

static int
box_check_replicaset_name(char *out)
{
	return box_check_node_name(out, "replicaset_name", true);
}

static int
box_check_cluster_name(char *out)
{
	return box_check_node_name(out, "cluster_name", true);
}

static enum wal_mode
box_check_wal_mode(const char *mode_name)
{
	assert(mode_name != NULL); /* checked in Lua */
	int mode = strindex(wal_mode_STRS, mode_name, WAL_MODE_MAX);
	if (mode == WAL_MODE_MAX)
		tnt_raise(ClientError, ER_CFG, "wal_mode", mode_name);
	return (enum wal_mode) mode;
}

static int64_t
box_check_wal_queue_max_size(void)
{
	int64_t size = cfg_geti64("wal_queue_max_size");
	if (size < 0) {
		diag_set(ClientError, ER_CFG, "wal_queue_max_size",
			 "wal_queue_max_size must be >= 0");
	}
	/* Unlimited. */
	if (size == 0)
		size = INT64_MAX;
	return size;
}

/**
 * If set, raise an error on any attempt to set wal_cleanup_delay to
 * non-zero value.
 */
static bool wal_cleanup_delay_is_disabled = false;
TWEAK_BOOL(wal_cleanup_delay_is_disabled);

static double
box_check_wal_cleanup_delay(void)
{
	const double default_value = 4 * 3600;
	double value = cfg_getd("wal_cleanup_delay");
	if (value < 0) {
		diag_set(ClientError, ER_CFG, "wal_cleanup_delay",
			 "value must be >= 0");
		return -1;
	}
	/* Non-zero and non-default value is detected - say a warning. */
	if (value > 0 && value != default_value)
		say_warn_once("Option wal_cleanup_delay is deprecated.");

	if (value > 0 && wal_cleanup_delay_is_disabled) {
		diag_set(ClientError, ER_DEPRECATED,
			 "Option wal_cleanup_delay");
		return -1;
	}

	return value;
}

static void
box_check_readahead(int readahead)
{
	enum { READAHEAD_MIN = 128, READAHEAD_MAX = 2147483647 };
	if (readahead < (int) READAHEAD_MIN ||
	    readahead > (int) READAHEAD_MAX) {
		tnt_raise(ClientError, ER_CFG, "readahead",
			  "specified value is out of bounds");
	}
}

static void
box_check_checkpoint_count(int checkpoint_count)
{
	if (checkpoint_count < 1) {
		tnt_raise(ClientError, ER_CFG, "checkpoint_count",
			  "the value must not be less than one");
	}
}

static int64_t
box_check_wal_max_size(int64_t wal_max_size)
{
	/* check wal_max_bytes configuration */
	if (wal_max_size <= 1) {
		tnt_raise(ClientError, ER_CFG, "wal_max_size",
			  "the value must be greater than one");
	}
	return wal_max_size;
}

/** Validate that wal_retention_period is >= 0. */
static double
box_check_wal_retention_period()
{
	double value = cfg_getd("wal_retention_period");
	if (value < 0) {
		diag_set(ClientError, ER_CFG, "wal_retention_period",
			 "the value must be >= 0");
		return -1;
	}
	return value;
}

/** Validate wal_retention_period and raise error, if needed. */
static double
box_check_wal_retention_period_xc()
{
	double value = box_check_wal_retention_period();
	if (value < 0)
		diag_raise();
	return value;
}

static ssize_t
box_check_memory_quota(const char *quota_name)
{
	int64_t size = cfg_geti64(quota_name);
	if (size >= 0 && (size_t) size <= QUOTA_MAX)
		return size;
	diag_set(ClientError, ER_CFG, quota_name,
		 tt_sprintf("must be >= 0 and <= %zu, but it is %lld",
			    QUOTA_MAX, (long long)size));
	return -1;
}

static void
box_check_vinyl_options(void)
{
	int read_threads = cfg_geti("vinyl_read_threads");
	int write_threads = cfg_geti("vinyl_write_threads");
	int64_t range_size = cfg_geti64("vinyl_range_size");
	int64_t page_size = cfg_geti64("vinyl_page_size");
	int run_count_per_level = cfg_geti("vinyl_run_count_per_level");
	double run_size_ratio = cfg_getd("vinyl_run_size_ratio");
	double bloom_fpr = cfg_getd("vinyl_bloom_fpr");

	if (box_check_memory_quota("vinyl_memory") < 0)
		diag_raise();

	if (read_threads < 1) {
		tnt_raise(ClientError, ER_CFG, "vinyl_read_threads",
			  "must be greater than or equal to 1");
	}
	if (write_threads < 2) {
		tnt_raise(ClientError, ER_CFG, "vinyl_write_threads",
			  "must be greater than or equal to 2");
	}
	if (page_size <= 0 || (range_size > 0 && page_size > range_size)) {
		tnt_raise(ClientError, ER_CFG, "vinyl_page_size",
			  "must be greater than 0 and less than "
			  "or equal to vinyl_range_size");
	}
	if (run_count_per_level <= 0) {
		tnt_raise(ClientError, ER_CFG, "vinyl_run_count_per_level",
			  "must be greater than 0");
	}
	if (run_size_ratio <= 1) {
		tnt_raise(ClientError, ER_CFG, "vinyl_run_size_ratio",
			  "must be greater than 1");
	}
	if (bloom_fpr <= 0 || bloom_fpr > 1) {
		tnt_raise(ClientError, ER_CFG, "vinyl_bloom_fpr",
			  "must be greater than 0 and less than or equal to 1");
	}
}

static int
box_check_sql_cache_size(int size)
{
	if (size < 0) {
		diag_set(ClientError, ER_CFG, "sql_cache_size",
			 "must be non-negative");
		return -1;
	}
	return 0;
}

static int
box_check_allocator(void)
{
	const char *allocator = cfg_gets("memtx_allocator");
	if (strcmp(allocator, "small") && strcmp(allocator, "system")) {
		diag_set(ClientError, ER_CFG, "memtx_allocator",
			 tt_sprintf("must be small or system, "
				    "but was set to %s", allocator));
		return -1;
	}
	return 0;
}

static void
box_check_small_alloc_options(void)
{
	/*
	 * If we use the int type, we may get an incorrect
	 * result if the user enters a large value.
	 */
	int64_t granularity = cfg_geti64("slab_alloc_granularity");
	/*
	 * Granularity must be exponent of two and >= 4.
	 * We can use granularity value == 4 only because we used small
	 * memory allocator only for struct tuple, which doesn't require
	 * aligment. Also added an upper bound for granularity, since if
	 * the user enters too large value, he will get incomprehensible
	 * errors later.
	 */
	if (granularity < 4 || granularity > 1024 * 16 ||
	    ! is_exp_of_two(granularity))
		tnt_raise(ClientError, ER_CFG, "slab_alloc_granularity",
			  "must be greater than or equal to 4,"
			  " less than or equal"
			  " to 1024 * 16 and exponent of two");
}

static int
box_check_iproto_options(void)
{
	int iproto_threads = cfg_geti("iproto_threads");
	if (iproto_threads <= 0 || iproto_threads > IPROTO_THREADS_MAX) {
		diag_set(ClientError, ER_CFG, "iproto_threads",
			  tt_sprintf("must be greater than or equal to 0,"
				     " less than or equal to %d",
				     IPROTO_THREADS_MAX));
		return -1;
	}
	return 0;
}

static double
box_check_txn_timeout(void)
{
	double timeout = cfg_getd_default("txn_timeout", TIMEOUT_INFINITY);
	if (timeout <= 0) {
		diag_set(ClientError, ER_CFG, "txn_timeout",
			 "the value must be greather than 0");
		return -1;
	}
	return timeout;
}

/**
 * Get and check isolation level from config, converting number or string to
 * enum txn_isolation_level.
 * @return isolation level or txn_isolation_level_MAX is case of error.
 */
static enum txn_isolation_level
box_check_txn_isolation(void)
{
	uint32_t level;
	if (cfg_isnumber("txn_isolation")) {
		level = cfg_geti("txn_isolation");
	} else {
		const char *str_level = cfg_gets("txn_isolation");
		level = strindex(txn_isolation_level_strs, str_level,
				 txn_isolation_level_MAX);
		if (level == txn_isolation_level_MAX)
			level = strindex(txn_isolation_level_aliases, str_level,
					 txn_isolation_level_MAX);
	}
	if (level >= txn_isolation_level_MAX) {
		diag_set(ClientError, ER_CFG, "txn_isolation",
			 "must be one of "
			 "box.txn_isolation_level (keys or values)");
		return txn_isolation_level_MAX;
	}
	if (level == TXN_ISOLATION_DEFAULT) {
		diag_set(ClientError, ER_CFG, "txn_isolation",
			 "cannot set default transaction isolation "
			 "to 'default'");
		return txn_isolation_level_MAX;
	}
	if (level == TXN_ISOLATION_LINEARIZABLE) {
		diag_set(ClientError, ER_CFG, "txn_isolation",
			 "cannot set default transaction isolation "
			 "to 'linearizable'");
		return txn_isolation_level_MAX;
	}
	return (enum txn_isolation_level)level;
}

static void
box_check_say()
{
	if (luaT_dostring(tarantool_L,
			  "require('log').box_api.cfg_check()") != 0)
		diag_raise();
}

int
box_init_say()
{
	if (luaT_dostring(tarantool_L, "require('log').box_api.cfg()") != 0)
		return -1;

	if (cfg_geti("background") && say_set_background() != 0)
		return -1;

	return 0;
}

/**
 * Checks whether memtx_sort_threads configuration parameter is correct.
 */
static void
box_check_memtx_sort_threads(void)
{
	int num = cfg_geti("memtx_sort_threads");
	/*
	 * After high level checks this parameter is either nil or has
	 * type 'number'.
	 */
	if (cfg_isnumber("memtx_sort_threads") &&
	    (num <= 0 || num > TT_SORT_THREADS_MAX))
		tnt_raise(ClientError, ER_CFG, "memtx_sort_threads",
			  tt_sprintf("must be greater than 0 and less than or"
				     " equal to %d", TT_SORT_THREADS_MAX));
}

void
box_check_config(void)
{
	struct tt_uuid uuid;
	struct uri uri;
	struct uri_set uri_set;
	char name[NODE_NAME_SIZE_MAX];
	box_check_say();
	if (audit_log_check_cfg() != 0)
		diag_raise();
	if (box_check_flightrec() != 0)
		diag_raise();
	if (box_check_listen(&uri_set) != 0)
		diag_raise();
	uri_set_destroy(&uri_set);
	if (box_check_auth_type() == NULL)
		diag_raise();
	if (box_check_instance_uuid(&uuid) != 0)
		diag_raise();
	if (box_check_replicaset_uuid(&uuid) != 0)
		diag_raise();
	if (box_check_election_mode() == ELECTION_MODE_INVALID)
		diag_raise();
	if (box_check_election_timeout() < 0)
		diag_raise();
	if (box_check_election_fencing_mode() == ELECTION_FENCING_MODE_INVALID)
		diag_raise();
	if (box_check_replication(&uri_set) != 0)
		diag_raise();
	uri_set_destroy(&uri_set);
	box_check_replication_timeout();
	box_check_replication_connect_timeout();
	box_check_replication_connect_quorum();
	box_check_replication_sync_lag();
	if (box_check_replication_synchro_quorum() != 0)
		diag_raise();
	if (box_check_replication_synchro_timeout() < 0)
		diag_raise();
	if (box_check_replication_threads() < 0)
		diag_raise();
	box_check_replication_sync_timeout();
	if (box_check_bootstrap_strategy() == BOOTSTRAP_STRATEGY_INVALID)
		diag_raise();
	if (box_check_bootstrap_leader(&uri, &uuid, name) != 0)
		diag_raise();
	uri_destroy(&uri);
	box_check_readahead(cfg_geti("readahead"));
	box_check_checkpoint_count(cfg_geti("checkpoint_count"));
	box_check_wal_max_size(cfg_geti64("wal_max_size"));
	box_check_wal_mode(cfg_gets("wal_mode"));
	if (box_check_wal_queue_max_size() < 0)
		diag_raise();
	if (box_check_wal_cleanup_delay() < 0)
		diag_raise();
	if (box_check_wal_retention_period() < 0)
		diag_raise();
	if (box_check_memory_quota("memtx_memory") < 0)
		diag_raise();
	box_check_memtx_min_tuple_size(cfg_geti64("memtx_min_tuple_size"));
	if (box_check_allocator() != 0)
		diag_raise();
	box_check_small_alloc_options();
	box_check_vinyl_options();
	if (box_check_iproto_options() != 0)
		diag_raise();
	if (box_check_sql_cache_size(cfg_geti("sql_cache_size")) != 0)
		diag_raise();
	if (box_check_txn_timeout() < 0)
		diag_raise();
	if (box_check_txn_isolation() == txn_isolation_level_MAX)
		diag_raise();
	box_check_memtx_sort_threads();
}

int
box_set_auth_type(void)
{
	const struct auth_method *method = box_check_auth_type();
	if (method == NULL)
		return -1;
	box_auth_type = method->name;
	return 0;
}

int
box_set_election_mode(void)
{
	enum election_mode mode = box_check_election_mode();
	if (mode == ELECTION_MODE_INVALID)
		return -1;
	box_raft_cfg_election_mode(mode);
	box_broadcast_ballot();
	return 0;
}

int
box_set_election_timeout(void)
{
	double d = box_check_election_timeout();
	if (d < 0)
		return -1;
	raft_cfg_election_timeout(box_raft(), d);
	return 0;
}

int
box_set_election_fencing_mode(void)
{
	enum election_fencing_mode mode = box_check_election_fencing_mode();
	if (mode == ELECTION_FENCING_MODE_INVALID)
		return -1;
	box_raft_set_election_fencing_mode(mode);
	return 0;
}

/*
 * Sync box.cfg.replication with the cluster registry, but
 * don't start appliers.
 */
static void
box_sync_replication(bool do_quorum, bool do_reuse)
{
	struct uri_set uri_set;
	int rc = cfg_get_uri_set("replication", &uri_set);
	assert(rc == 0);
	(void)rc;
	auto uri_set_guard = make_scoped_guard([&]{
		uri_set_destroy(&uri_set);
	});
	replicaset_connect(&uri_set, do_quorum, do_reuse);
}

static inline void
box_restart_replication(void)
{
	const bool do_quorum = true;
	const bool do_reuse = false;
	box_sync_replication(do_quorum, do_reuse);
}

static inline void
box_update_replication(void)
{
	/*
	 * In legacy mode proceed as soon as `replication_connect_quorum` remote
	 * peers are connected.
	 * In every other mode, try to connect to everyone during the given time
	 * period, but do not fail even if no connections were established.
	 */
	const bool do_quorum = bootstrap_strategy != BOOTSTRAP_STRATEGY_LEGACY;
	const bool do_reuse = true;
	box_sync_replication(do_quorum, do_reuse);
}

void
box_set_replication(void)
{
	if (!is_box_configured) {
		/*
		 * Do nothing, we're in local hot standby mode, this instance
		 * will automatically begin following the replica when local
		 * hot standby mode is finished, see box_cfg().
		 */
		return;
	}
	struct uri_set uri_set;
	if (box_check_replication(&uri_set) != 0)
		diag_raise();
	bool unchanged = uri_set_is_equal(&uri_set, &replication_uris);
	uri_set_destroy(&uri_set);
	if (unchanged) {
		/*
		 * No need to reconnect or sync in case the configuration is
		 * the same. However, we should still reload the URIs because
		 * a URI parameter may store a path to a file (for example,
		 * an SSL certificate), which could change.
		 */
		replicaset_reload_uris();
		return;
	}
	/*
	 * Try to connect to all replicas within the timeout period.
	 * Stay in orphan mode in case we fail to connect to at least
	 * 'replication_connect_quorum' remote instances.
	 */
	box_update_replication();
	/* Follow replica */
	replicaset_follow();
	/* Wait until appliers are in sync */
	replicaset_sync();
}

void
box_set_replication_timeout(void)
{
	replication_timeout = box_check_replication_timeout();
	raft_cfg_death_timeout(box_raft(), replication_disconnect_timeout());
	box_update_broadcast_ballot_interval(replication_timeout);
}

void
box_set_replication_connect_timeout(void)
{
	replication_connect_timeout = box_check_replication_connect_timeout();
}

void
box_set_replication_connect_quorum(void)
{
	replication_connect_quorum = box_check_replication_connect_quorum();
	if (is_box_configured)
		replicaset_check_quorum();
}

int
box_set_bootstrap_strategy(void)
{
	enum bootstrap_strategy strategy = box_check_bootstrap_strategy();
	if (strategy == BOOTSTRAP_STRATEGY_INVALID)
		return -1;
	bootstrap_strategy = strategy;
	return 0;
}

static int
box_set_bootstrap_leader(void)
{
	return box_check_bootstrap_leader(&cfg_bootstrap_leader_uri,
					  &cfg_bootstrap_leader_uuid,
					  cfg_bootstrap_leader_name);
}

/** Persist this instance as the bootstrap leader in _schema space. */
static int
box_set_bootstrap_leader_record(void)
{
	assert(instance_id != REPLICA_ID_NIL);
	assert(!tt_uuid_is_nil(&INSTANCE_UUID));
	return boxk(IPROTO_REPLACE, BOX_SCHEMA_ID,
		    "[%s%s%" PRIu64 "%" PRIu32 "]", "bootstrap_leader_uuid",
		    tt_uuid_str(&INSTANCE_UUID), fiber_time64(), instance_id);
}

int
box_make_bootstrap_leader(void)
{
	if (tt_uuid_is_nil(&INSTANCE_UUID)) {
		diag_set(ClientError, ER_UNSUPPORTED,
			 "box.ctl.make_bootstrap_leader()",
			 "promoting this instance before box.cfg() is called");
		return -1;
	}
	/* Bootstrap strategy is read by the time instance uuid is known. */
	assert(bootstrap_strategy != BOOTSTRAP_STRATEGY_INVALID);
	if (bootstrap_strategy != BOOTSTRAP_STRATEGY_SUPERVISED) {
		diag_set(ClientError, ER_UNSUPPORTED,
			 tt_sprintf("bootstrap_strategy = '%s'",
				    cfg_gets("bootstrap_strategy")),
			 "promoting the bootstrap leader via "
			 "box.ctl.make_bootstrap_leader()");
		return -1;
	}
	if (is_box_configured) {
		if (box_check_writable() != 0)
			return -1;
		/* Ballot broadcast will happen in an on_commit trigger. */
		return box_set_bootstrap_leader_record();
	} else {
		bootstrap_leader_uuid = INSTANCE_UUID;
		box_broadcast_ballot();
		return 0;
	}
}

void
box_set_replication_sync_lag(void)
{
	replication_sync_lag = box_check_replication_sync_lag();
}

void
box_update_replication_synchro_quorum(void)
{
	int quorum = -1;

	if (!cfg_isnumber("replication_synchro_quorum")) {
		/*
		 * The formula has been verified already. For bootstrap
		 * stage pass 1 as a number of replicas to sync because
		 * we're at early stage and registering a new replica.
		 *
		 * This should cover the valid case where formula is plain
		 * "N", ie all replicas are to be synchro mode.
		 */
		int value = MAX(1, replicaset.registered_count);
		quorum = box_eval_replication_synchro_quorum(value);
		say_info("update replication_synchro_quorum = %d", quorum);
	} else {
		quorum = cfg_geti("replication_synchro_quorum");
	}

	/*
	 * This should never happen because the values were
	 * validated already but just to prevent from
	 * unexpected changes and because the value is too
	 * important for qsync, lets re-check (this is cheap).
	 */
	if (quorum <= 0 || quorum >= VCLOCK_MAX)
		panic("failed to eval/fetch replication_synchro_quorum");

	/*
	 * Extending replicaset pause fencing until quorum is obtained.
	 */
	if (quorum > replication_synchro_quorum &&
	    replicaset.healthy_count < quorum)
		box_raft_election_fencing_pause();

	replication_synchro_quorum = quorum;
	txn_limbo_on_parameters_change(&txn_limbo);
	box_raft_update_election_quorum();
	replicaset_on_health_change();
}

int
box_set_replication_synchro_quorum(void)
{
	if (box_check_replication_synchro_quorum() != 0)
		return -1;
	box_update_replication_synchro_quorum();
	return 0;
}

int
box_set_replication_synchro_timeout(void)
{
	double value = box_check_replication_synchro_timeout();
	if (value < 0)
		return -1;
	replication_synchro_timeout = value;
	txn_limbo_on_parameters_change(&txn_limbo);
	return 0;
}

void
box_set_replication_sync_timeout(void)
{
	replication_sync_timeout = box_check_replication_sync_timeout();
}

void
box_set_replication_skip_conflict(void)
{
	replication_skip_conflict = cfg_geti("replication_skip_conflict");
}

/** Register on the master instance. Could be initial join or a name change. */
static void
box_register_on_master(void)
{
	/*
	 * Restart the appliers so as they would notice the change (need an ID,
	 * need a new name).
	 */
	box_restart_replication();
	struct replica *master = replicaset_find_join_master();
	if (master == NULL || master->applier == NULL ||
	    master->applier->state != APPLIER_CONNECTED) {
		tnt_raise(ClientError, ER_CANNOT_REGISTER);
	} else {
		struct applier *master_applier = master->applier;
		applier_resume_to_state(master_applier, APPLIER_REGISTERED,
					TIMEOUT_INFINITY);
		applier_resume_to_state(master_applier, APPLIER_READY,
					TIMEOUT_INFINITY);
	}
	replicaset_follow();
	replicaset_sync();
}

void
box_set_replication_anon(void)
{
	assert(is_box_configured);
	assert(cfg_replication_anon == box_is_anon());
	bool new_anon = box_check_replication_anon();
	if (new_anon == cfg_replication_anon)
		return;
	auto guard = make_scoped_guard([&]{
		cfg_replication_anon = !new_anon;
		box_broadcast_ballot();
	});
	cfg_replication_anon = new_anon;
	box_broadcast_ballot();
	if (!new_anon) {
		box_register_on_master();
		assert(!box_is_anon());
	} else {
		/*
		 * It is forbidden to turn a normal replica into
		 * an anonymous one.
		 */
		tnt_raise(ClientError, ER_CFG, "replication_anon",
			  "cannot be turned on after bootstrap"
			  " has finished");
	}
	guard.is_active = false;
}

/**
 * Set the cluster name record in _schema, bypassing all checks like whether the
 * instance is writable. It makes the function usable by bootstrap master when
 * it is read-only but has to make the first registration.
 */
static void
box_set_cluster_name_record(const char *name)
{
	int rc;
	if (*name == 0) {
		rc = boxk(IPROTO_DELETE, BOX_SCHEMA_ID, "[%s]", "cluster_name");
	} else {
		rc = boxk(IPROTO_REPLACE, BOX_SCHEMA_ID, "[%s%s]",
			  "cluster_name", name);
	}
	if (rc != 0)
		diag_raise();
}

void
box_set_cluster_name(void)
{
	char name[NODE_NAME_SIZE_MAX];
	if (box_check_cluster_name(name) != 0)
		diag_raise();
	/* Nil means the config doesn't care, allows to use any name. */
	if (*name == 0)
		return;
	if (strcmp(CLUSTER_NAME, name) == 0)
		return;
	box_check_writable_xc();
	box_set_cluster_name_record(name);
}

/**
 * Set the new replicaset name record in _schema, bypassing all checks like
 * whether the instance is writable. It makes the function usable by bootstrap
 * master when it is read-only but has to make the first registration.
 */
static void
box_set_replicaset_name_record(const char *name)
{
	int rc;
	if (*name == 0) {
		rc = boxk(IPROTO_DELETE, BOX_SCHEMA_ID, "[%s]",
			  "replicaset_name");
	} else {
		rc = boxk(IPROTO_REPLACE, BOX_SCHEMA_ID, "[%s%s]",
			  "replicaset_name", name);
	}
	if (rc != 0)
		diag_raise();
}

void
box_set_replicaset_name(void)
{
	char name[NODE_NAME_SIZE_MAX];
	if (box_check_replicaset_name(name) != 0)
		diag_raise();
	/* Nil means the config doesn't care, allows to use any name. */
	if (*name == 0)
		return;
	if (strcmp(REPLICASET_NAME, name) == 0)
		return;
	box_check_writable_xc();
	box_set_replicaset_name_record(name);
}

/**
 * Register a new replica if not already registered. Update its name if needed.
 */
static void
box_register_replica(const struct tt_uuid *uuid,
		     const char *name);

void
box_set_instance_name(void)
{
	char name[NODE_NAME_SIZE_MAX];
	if (box_check_instance_name(name) != 0)
		diag_raise();
	if (strcmp(cfg_instance_name, name) == 0)
		return;
	/**
	 * It's possible, that the name is set on master by the manual replace.
	 * Don't make all appliers to resubscribe in such case. Just update
	 * the saved cfg_instance_name.
	 */
	if (strcmp(INSTANCE_NAME, name) == 0) {
		strlcpy(cfg_instance_name, name, NODE_NAME_SIZE_MAX);
		return;
	}
	char old_cfg_name[NODE_NAME_SIZE_MAX];
	strlcpy(old_cfg_name, cfg_instance_name, NODE_NAME_SIZE_MAX);
	auto guard = make_scoped_guard([&]{
		strlcpy(cfg_instance_name, old_cfg_name, NODE_NAME_SIZE_MAX);
		try {
			box_restart_replication();
			replicaset_follow();
		} catch (Exception *exc) {
			exc->log();
		} catch (...) {
			panic("Unknown exception on instance name set failure");
		}
	});
	strlcpy(cfg_instance_name, name, NODE_NAME_SIZE_MAX);
	/* Nil means the config doesn't care, allows to use any name. */
	if (*name != 0) {
		if (box_is_ro())
			box_register_on_master();
		else
			box_register_replica(&INSTANCE_UUID, name);
	}
	guard.is_active = false;
}

/** Trigger to catch ACKs from all nodes when need to wait for quorum. */
struct box_quorum_trigger {
	/** Inherit trigger. */
	struct trigger base;
	/** Minimal number of nodes who should confirm the target LSN. */
	int quorum;
	/** Target LSN to wait for. */
	int64_t target_lsn;
	/** Replica ID whose LSN is being waited. */
	uint32_t replica_id;
	/**
	 * All versions of the given replica's LSN as seen by other nodes. The
	 * same as in the txn limbo.
	 */
	struct vclock vclock;
	/** Number of nodes who confirmed the LSN. */
	int ack_count;
	/** Fiber to wakeup when quorum is reached. */
	struct fiber *waiter;
};

static int
box_quorum_on_ack_f(struct trigger *trigger, void *event)
{
	struct replication_ack *ack = (struct replication_ack *)event;
	struct box_quorum_trigger *t = (struct box_quorum_trigger *)trigger;
	int64_t new_lsn = vclock_get(ack->vclock, t->replica_id);
	int64_t old_lsn = vclock_get(&t->vclock, ack->source);
	if (new_lsn < t->target_lsn || old_lsn >= t->target_lsn)
		return 0;

	vclock_follow(&t->vclock, ack->source, new_lsn);
	++t->ack_count;
	if (t->ack_count >= t->quorum) {
		fiber_wakeup(t->waiter);
		trigger_clear(trigger);
	}
	return 0;
}

/**
 * Wait until at least @a quorum of nodes confirm @a target_lsn from the node
 * with id @a lead_id.
 */
static int
box_wait_quorum(uint32_t lead_id, int64_t target_lsn, int quorum,
		double timeout)
{
#ifndef NDEBUG
    ++errinj(ERRINJ_WAIT_QUORUM_COUNT, ERRINJ_INT)->iparam;
#endif

	struct box_quorum_trigger t;
	memset(&t, 0, sizeof(t));
	vclock_create(&t.vclock);

	/* Take this node into account immediately. */
	int ack_count = vclock_get(box_vclock, lead_id) >= target_lsn;
	replicaset_foreach(replica) {
		if (relay_get_state(replica->relay) != RELAY_FOLLOW ||
		    replica->anon)
			continue;

		assert(replica->id != REPLICA_ID_NIL);
		assert(!tt_uuid_is_equal(&INSTANCE_UUID, &replica->uuid));

		int64_t lsn = vclock_get(relay_vclock(replica->relay), lead_id);
		/*
		 * The replica might not yet received anything from the old
		 * leader. Easily can happen with a newly added replica. Vclock
		 * can't be followed then because would assert on lsn > old lsn
		 * whereas they are both 0.
		 */
		if (lsn == 0)
			continue;
		vclock_follow(&t.vclock, replica->id, lsn);
		if (lsn >= target_lsn) {
			ack_count++;
			continue;
		}
	}
	if (ack_count < quorum) {
		t.quorum = quorum;
		t.target_lsn = target_lsn;
		t.replica_id = lead_id;
		t.ack_count = ack_count;
		t.waiter = fiber();
		trigger_create(&t.base, box_quorum_on_ack_f, NULL, NULL);
		trigger_add(&replicaset.on_ack, &t.base);
		double deadline = ev_monotonic_now(loop()) + timeout;
		do {
			if (fiber_yield_deadline(deadline))
				break;
		} while (!fiber_is_cancelled() && t.ack_count < t.quorum);
		trigger_clear(&t.base);
		ack_count = t.ack_count;
	}
	/*
	 * No point to proceed after cancellation even if got the quorum. The
	 * quorum is waited by limbo clear function. Emptying the limbo involves
	 * a pair of blocking WAL writes, making the fiber sleep even longer,
	 * which isn't appropriate when it's canceled.
	 */
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	if (ack_count < quorum) {
		diag_set(TimedOut);
		return -1;
	}
	return 0;
}

/**
 * The pool is used by box_cc to allocate sync_trigger_data that is used in
 * box_collect_confirmed_vclock and relay_get_sync_on_start. We allocate
 * sync_trigger_data dynamically because these functions are running in
 * different fibers. The lifetime of sync_trigger_data is not limited by the
 * execution time of box_collect_confirmed_vclock.
 */
static struct mempool sync_trigger_data_pool;

/** A structure holding trigger data to collect syncs. */
struct sync_trigger_data {
	/** Syncs to wait for. */
	uint64_t vclock_syncs[VCLOCK_MAX];
	/**
	 * A bitmap holding replica ids whose vclocks were already collected.
	 */
	vclock_map_t collected_vclock_map;
	/** The fiber waiting for vclock. */
	struct fiber *waiter;
	/** Collected vclock. */
	struct vclock *vclock;
	/** The request deadline. */
	double deadline;
	/** How many vclocks are needed. */
	int count;
	/** Whether the request is timed out. */
	bool is_timed_out;
	/** Count of fibers that are using data. */
	int ref_count;
};

/** Let others know we need data. */
void
sync_trigger_data_ref(struct sync_trigger_data *data)
{
	++data->ref_count;
}

/**
 * Let others know that we no longer need the data.
 * If no one else needs the data, free it.
 */
void
sync_trigger_data_unref(struct sync_trigger_data *data)
{
	--data->ref_count;
	assert(data->ref_count >= 0);
	if (data->ref_count == 0)
		mempool_free(&sync_trigger_data_pool, data);
}

/**
 * A trigger executed on each ack to collect up to date remote node vclocks.
 * When an ack comes with requested sync for some replica id, ack vclock is
 * accounted.
 */
static int
check_vclock_sync_on_ack(struct trigger *trigger, void *event)
{
	struct replication_ack *ack = (struct replication_ack *)event;
	struct sync_trigger_data *data =
		(struct sync_trigger_data *)trigger->data;
	uint32_t id = ack->source;
	/*
	 * Anonymous replica acks are not counted for synchronous transactions,
	 * so linearizable read shouldn't count them as well.
	 */
	if (id == 0)
		return 0;
	uint64_t sync = data->vclock_syncs[id];
	int accounted_count = bit_count_u32(data->collected_vclock_map);
	if (!bit_test(&data->collected_vclock_map, id) && sync > 0 &&
	    ack->vclock_sync >= sync && accounted_count < data->count) {
		vclock_max_ignore0(data->vclock, ack->vclock);
		bit_set(&data->collected_vclock_map, id);
		++accounted_count;
		if (accounted_count >= data->count)
			fiber_wakeup(data->waiter);
	}
	return 0;
}

/** A trigger querying relay's next sync value once it becomes operational. */
static int
relay_get_sync_on_start(struct trigger *trigger, void *event)
{
	struct replica *replica = (struct replica *)event;
	if (replica->anon)
		return 0;
	struct relay *relay = replica->relay;
	struct sync_trigger_data *data =
		(struct sync_trigger_data *)trigger->data;
	uint32_t id = replica->id;
	/* Already accounted. */
	if (bit_test(&data->collected_vclock_map, id))
		return 0;

	sync_trigger_data_ref(data);
	if (relay_trigger_vclock_sync(relay, &data->vclock_syncs[id],
				      data->deadline) != 0) {
		diag_clear(diag_get());
		data->is_timed_out = true;
		fiber_wakeup(data->waiter);
	}
	sync_trigger_data_unref(data);
	return 0;
}

/** Find the minimal vclock which has all the data confirmed on a quorum. */
static int
box_collect_confirmed_vclock(struct vclock *confirmed_vclock, double deadline)
{
	/*
	 * How many vclocks we should see to be sure that at least one of them
	 * contains all data present on any real quorum.
	 */
	int vclock_count = MAX(1, replicaset.registered_count) -
			   replication_synchro_quorum + 1;
	/*
	 * We should check the vclock on self plus vclock_count - 1 remote
	 * instances.
	 */
	vclock_copy(confirmed_vclock, &replicaset.vclock);
	if (vclock_count <= 1)
		return 0;

	struct sync_trigger_data *data = (sync_trigger_data *)
		xmempool_alloc(&sync_trigger_data_pool);
	memset(data->vclock_syncs, 0, sizeof(data->vclock_syncs));
	data->collected_vclock_map = 0;
	data->waiter = fiber();
	data->vclock = confirmed_vclock;
	data->deadline = deadline;
	data->count = vclock_count;
	data->is_timed_out = false;
	data->ref_count = 0;

	sync_trigger_data_ref(data);
	bit_set(&data->collected_vclock_map, instance_id);
	struct trigger on_relay_thread_start;
	trigger_create(&on_relay_thread_start, relay_get_sync_on_start, data,
		       NULL);
	trigger_add(&replicaset.on_relay_thread_start, &on_relay_thread_start);
	struct trigger on_ack;
	trigger_create(&on_ack, check_vclock_sync_on_ack, data, NULL);
	trigger_add(&replicaset.on_ack, &on_ack);

	auto guard = make_scoped_guard([&] {
		trigger_clear(&on_ack);
		trigger_clear(&on_relay_thread_start);
		sync_trigger_data_unref(data);
	});

	replicaset_foreach(replica) {
		if (relay_get_state(replica->relay) != RELAY_FOLLOW ||
		    replica->anon) {
			continue;
		}
		/* Might be already filled by on_relay_thread_start trigger. */
		if (data->vclock_syncs[replica->id] != 0)
			continue;
		if (relay_trigger_vclock_sync(replica->relay,
					      &data->vclock_syncs[replica->id],
					      deadline) != 0) {
			/* Timed out. */
			return -1;
		}
	}

	while (bit_count_u32(data->collected_vclock_map) < vclock_count &&
	       !data->is_timed_out && !fiber_is_cancelled()) {
		if (fiber_yield_deadline(deadline))
			break;
	}

	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	if (bit_count_u32(data->collected_vclock_map) < vclock_count) {
		diag_set(TimedOut);
		return -1;
	}
	return 0;
}

/** box_wait_vclock trigger data. */
struct box_wait_vclock_data {
	/** Whether the request is finished. */
	bool is_ready;
	/** The vclock to wait for. */
	const struct vclock *vclock;
	/** The fiber waiting for vclock. */
	struct fiber *waiter;
};

static int
box_wait_vclock_f(struct trigger *trigger, void *event)
{
	(void)event;
	struct box_wait_vclock_data *data =
		(struct box_wait_vclock_data *)trigger->data;
	if (vclock_compare_ignore0(data->vclock, &replicaset.vclock) <= 0) {
		data->is_ready = true;
		fiber_wakeup(data->waiter);
	}
	return 0;
}

/**
 * Wait until this instance's vclock reaches @a vclock or @a deadline is
 * reached.
 */
static int
box_wait_vclock(const struct vclock *vclock, double deadline)
{
	if (vclock_compare_ignore0(vclock, &replicaset.vclock) <= 0)
		return 0;
	struct trigger on_wal_write;
	struct box_wait_vclock_data data = {
		.is_ready = false,
		.vclock = vclock,
		.waiter = fiber(),
	};
	trigger_create(&on_wal_write, box_wait_vclock_f, &data, NULL);
	trigger_add(&wal_on_write, &on_wal_write);
	do {
		if (fiber_yield_deadline(deadline))
			break;
	} while (!data.is_ready && !fiber_is_cancelled());
	trigger_clear(&on_wal_write);
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	if (!data.is_ready) {
		diag_set(TimedOut);
		return -1;
	}
	return 0;
}

int
box_wait_linearization_point(double timeout)
{
	double deadline = ev_monotonic_now(loop()) + timeout;
	struct vclock confirmed_vclock;
	vclock_create(&confirmed_vclock);
	/*
	 * First find out the vclock which might be confirmed on remote
	 * instances.
	 */
	if (box_collect_confirmed_vclock(&confirmed_vclock, deadline) != 0)
		return -1;
	/* Then wait until all the rows up to this vclock are received. */
	if (box_wait_vclock(&confirmed_vclock, deadline) != 0)
		return -1;
	/*
	 * Finally, wait until all the synchronous transactions, which should be
	 * visible to this tx, become visible.
	 */
	bool is_rollback;
	timeout = deadline - ev_monotonic_now(loop());
	if (!txn_limbo_is_empty(&txn_limbo) &&
	    txn_limbo_wait_last_txn(&txn_limbo, &is_rollback, timeout) != 0)
		return -1;
	return 0;
}

/**
 * Check whether the greatest promote term has changed since it was last read.
 * IOW check that a foreign PROMOTE arrived while we were sleeping.
 */
static int
box_check_promote_term_intact(uint64_t promote_term)
{
	if (txn_limbo.promote_greatest_term != promote_term) {
		diag_set(ClientError, ER_INTERFERING_PROMOTE,
			 txn_limbo.owner_id);
		return -1;
	}
	return 0;
}

/**
 * Check whether the raft term has changed since it was last read.
 */
static int
box_check_election_term_intact(uint64_t term)
{
	if (box_raft()->volatile_term != term) {
		diag_set(ClientError, ER_INTERFERING_ELECTIONS);
		return -1;
	}
	return 0;
}

/** Trigger a new election round but don't wait for its result. */
static int
box_trigger_elections(void)
{
	uint64_t promote_term = txn_limbo.promote_greatest_term;
	raft_new_term(box_raft());
	if (box_raft_wait_term_persisted() < 0)
		return -1;
	return box_check_promote_term_intact(promote_term);
}

/** Try waiting until limbo is emptied up to given timeout. */
static int
box_try_wait_confirm(double timeout)
{
	uint64_t promote_term = txn_limbo.promote_greatest_term;
	txn_limbo_wait_empty(&txn_limbo, timeout);
	return box_check_promote_term_intact(promote_term);
}

/**
 * A helper to wait until all limbo entries are ready to be confirmed, i.e.
 * written to WAL and have gathered a quorum of ACKs from replicas.
 * Return lsn of the last limbo entry on success, -1 on error.
 */
static int64_t
box_wait_limbo_acked(double timeout)
{
	if (txn_limbo_is_empty(&txn_limbo))
		return txn_limbo.confirmed_lsn;

	uint64_t promote_term = txn_limbo.promote_greatest_term;
	int quorum = replication_synchro_quorum;
	struct txn_limbo_entry *last_entry;
	last_entry = txn_limbo_last_synchro_entry(&txn_limbo);
	/* Wait for the last entries WAL write. */
	if (last_entry->lsn < 0) {
		int64_t tid = last_entry->txn->id;

		journal_queue_flush();
		if (wal_sync(NULL) != 0)
			return -1;

		if (box_check_promote_term_intact(promote_term) != 0)
			return -1;
		if (txn_limbo_is_empty(&txn_limbo))
			return txn_limbo.confirmed_lsn;
		if (tid != txn_limbo_last_synchro_entry(&txn_limbo)->txn->id) {
			diag_set(ClientError, ER_QUORUM_WAIT, quorum,
				 "new synchronous transactions appeared");
			return -1;
		}
	}
	assert(last_entry->lsn > 0);
	int64_t wait_lsn = last_entry->lsn;

	if (box_wait_quorum(txn_limbo.owner_id, wait_lsn, quorum, timeout) != 0)
		return -1;

	if (box_check_promote_term_intact(promote_term) != 0)
		return -1;

	if (txn_limbo_is_empty(&txn_limbo))
		return txn_limbo.confirmed_lsn;

	if (quorum < replication_synchro_quorum) {
		diag_set(ClientError, ER_QUORUM_WAIT, quorum,
			 "quorum was increased while waiting");
		return -1;
	}
	if (wait_lsn < txn_limbo_last_synchro_entry(&txn_limbo)->lsn) {
		diag_set(ClientError, ER_QUORUM_WAIT, quorum,
			 "new synchronous transactions appeared");
		return -1;
	}

	return wait_lsn;
}

/** Write and process a PROMOTE request. */
static int
box_issue_promote(int64_t promote_lsn)
{
	int rc = 0;
	uint64_t term = box_raft()->term;
	uint64_t promote_term = txn_limbo.promote_greatest_term;
	assert(promote_lsn >= 0);
	rc = box_check_election_term_intact(term);
	if (rc != 0)
		return rc;

	txn_limbo_begin(&txn_limbo);
	rc = box_check_election_term_intact(term);
	if (rc != 0)
		goto end;
	rc = box_check_promote_term_intact(promote_term);
	if (rc != 0)
		goto end;
	rc = txn_limbo_write_promote(&txn_limbo, promote_lsn, term);

end:
	if (rc == 0) {
		txn_limbo_commit(&txn_limbo);
		assert(txn_limbo_is_empty(&txn_limbo));
	} else {
		txn_limbo_rollback(&txn_limbo);
	}
	return rc;
}

/** A guard to block multiple simultaneous promote()/demote() invocations. */
static bool is_in_box_promote = false;

/** Write and process a DEMOTE request. */
static int
box_issue_demote(int64_t promote_lsn)
{
	int rc = 0;
	uint64_t term = box_raft()->term;
	uint64_t promote_term = txn_limbo.promote_greatest_term;
	assert(promote_lsn >= 0);

	rc = box_check_election_term_intact(term);
	if (rc != 0)
		return rc;

	txn_limbo_begin(&txn_limbo);
	rc = box_check_election_term_intact(term);
	if (rc != 0)
		goto end;
	rc = box_check_promote_term_intact(promote_term);
	if (rc != 0)
		goto end;
	rc = txn_limbo_write_demote(&txn_limbo, promote_lsn, term);

end:
	if (rc == 0) {
		txn_limbo_commit(&txn_limbo);
		assert(txn_limbo_is_empty(&txn_limbo));
	} else {
		txn_limbo_rollback(&txn_limbo);
	}
	return rc;
}

int
box_promote_qsync(void)
{
	if (is_in_box_promote) {
		diag_set(ClientError, ER_IN_ANOTHER_PROMOTE);
		return -1;
	}
	assert(is_box_configured);
	struct raft *raft = box_raft();
	is_in_box_promote = true;
	auto promote_guard = make_scoped_guard([&] {
		is_in_box_promote = false;
	});
	assert(raft->state == RAFT_STATE_LEADER);
	if (txn_limbo_replica_term(&txn_limbo, instance_id) == raft->term)
		return 0;
	int64_t wait_lsn = box_wait_limbo_acked(TIMEOUT_INFINITY);
	if (wait_lsn < 0)
		return -1;
	if (raft->state != RAFT_STATE_LEADER) {
		diag_set(ClientError, ER_NOT_LEADER, raft->leader);
		return -1;
	}
	return box_issue_promote(wait_lsn);
}

int
box_promote(void)
{
	if (is_in_box_promote) {
		diag_set(ClientError, ER_UNSUPPORTED, "box.ctl.promote",
			 "simultaneous invocations");
		return -1;
	}
	struct raft *raft = box_raft();
	is_in_box_promote = true;
	auto promote_guard = make_scoped_guard([&] {
		is_in_box_promote = false;
	});

	if (!is_box_configured)
		return 0;
	/*
	 * Currently active leader (the instance that is seen as leader by both
	 * raft and txn_limbo) can't issue another PROMOTE.
	 */
	bool is_leader =
		txn_limbo_replica_term(&txn_limbo, instance_id) == raft->term &&
		txn_limbo.owner_id == instance_id &&
		!txn_limbo.is_frozen_until_promotion;
	if (box_election_mode != ELECTION_MODE_OFF)
		is_leader = is_leader && raft->state == RAFT_STATE_LEADER;

	if (is_leader)
		return 0;
	switch (box_election_mode) {
	case ELECTION_MODE_OFF:
		if (box_try_wait_confirm(2 * replication_synchro_timeout) != 0)
			return -1;
		if (box_trigger_elections() != 0)
			return -1;
		break;
	case ELECTION_MODE_VOTER:
		assert(raft->state == RAFT_STATE_FOLLOWER);
		diag_set(ClientError, ER_UNSUPPORTED, "election_mode='voter'",
			 "manual elections");
		return -1;
	case ELECTION_MODE_MANUAL:
	case ELECTION_MODE_CANDIDATE:
		if (raft->state == RAFT_STATE_LEADER)
			return 0;
		is_in_box_promote = false;
		return box_raft_try_promote();
	default:
		unreachable();
	}

	int64_t wait_lsn = box_wait_limbo_acked(replication_synchro_timeout);
	if (wait_lsn < 0)
		return -1;

	return box_issue_promote(wait_lsn);
}

int
box_demote(void)
{
	if (is_in_box_promote) {
		diag_set(ClientError, ER_UNSUPPORTED, "box.ctl.demote",
			 "simultaneous invocations");
		return -1;
	}
	is_in_box_promote = true;
	auto promote_guard = make_scoped_guard([&] {
		is_in_box_promote = false;
	});

	if (!is_box_configured)
		return 0;

	const struct raft *raft = box_raft();
	if (box_election_mode != ELECTION_MODE_OFF) {
		if (txn_limbo_replica_term(&txn_limbo, instance_id) !=
		    raft->term)
			return 0;
		if (txn_limbo.owner_id != instance_id)
			return 0;
		box_raft_leader_step_off();
		return 0;
	}

	assert(raft->state == RAFT_STATE_FOLLOWER);
	if (raft->leader != REPLICA_ID_NIL) {
		diag_set(ClientError, ER_NOT_LEADER, raft->leader);
		return -1;
	}
	if (txn_limbo.owner_id == REPLICA_ID_NIL)
		return 0;
	/*
	 * If the limbo term is up to date with Raft, then it might have
	 * a valid owner right now. Demotion would disrupt it. In this
	 * case the user has to explicitly overthrow the old owner with
	 * local promote(), or call demote() on the actual owner.
	 */
	if (txn_limbo.promote_greatest_term == raft->term &&
	    txn_limbo.owner_id != instance_id)
		return 0;
	if (box_trigger_elections() != 0)
		return -1;
	if (box_try_wait_confirm(2 * replication_synchro_timeout) < 0)
		return -1;
	int64_t wait_lsn = box_wait_limbo_acked(replication_synchro_timeout);
	if (wait_lsn < 0)
		return -1;
	return box_issue_demote(wait_lsn);
}

int
box_listen(void)
{
	struct uri_set uri_set;
	if (box_check_listen(&uri_set) != 0)
		return -1;
	int rc = iproto_listen(&uri_set);
	uri_set_destroy(&uri_set);
	return rc;
}

void
box_set_io_collect_interval(void)
{
	ev_set_io_collect_interval(loop(), cfg_getd("io_collect_interval"));
}

void
box_set_snap_io_rate_limit(void)
{
	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");
	assert(memtx != NULL);
	memtx_engine_set_snap_io_rate_limit(memtx,
			cfg_getd("snap_io_rate_limit"));
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_snap_io_rate_limit(vinyl,
			cfg_getd("snap_io_rate_limit"));
}

void
box_set_memtx_memory(void)
{
	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");
	assert(memtx != NULL);
	ssize_t size = box_check_memory_quota("memtx_memory");
	if (size < 0)
		diag_raise();
	memtx_engine_set_memory_xc(memtx, size);
}

void
box_set_memtx_max_tuple_size(void)
{
	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");
	assert(memtx != NULL);
	memtx_engine_set_max_tuple_size(memtx,
			cfg_geti("memtx_max_tuple_size"));
}

void
box_set_too_long_threshold(void)
{
	too_long_threshold = cfg_getd("too_long_threshold");

	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_too_long_threshold(vinyl, too_long_threshold);
}

void
box_set_readahead(void)
{
	int readahead = cfg_geti("readahead");
	box_check_readahead(readahead);
	iproto_readahead = readahead;
}

void
box_set_checkpoint_count(void)
{
	int checkpoint_count = cfg_geti("checkpoint_count");
	box_check_checkpoint_count(checkpoint_count);
	gc_set_min_checkpoint_count(checkpoint_count);
}

void
box_set_checkpoint_interval(void)
{
	double interval = cfg_getd("checkpoint_interval");
	gc_set_checkpoint_interval(interval);
}

void
box_set_checkpoint_wal_threshold(void)
{
	int64_t threshold = cfg_geti64("checkpoint_wal_threshold");
	wal_set_checkpoint_threshold(threshold);
}

int
box_set_wal_queue_max_size(void)
{
	int64_t size = box_check_wal_queue_max_size();
	if (size < 0)
		return -1;
	wal_set_queue_max_size(size);
	return 0;
}

int
box_set_wal_cleanup_delay(void)
{
	double delay = box_check_wal_cleanup_delay();
	if (delay < 0)
		return -1;
	/*
	 * Anonymous replicas do not require
	 * delay since they can't be a source
	 * of replication.
	 */
	if (box_is_anon())
		delay = 0;
	gc_set_wal_cleanup_delay(delay);
	return 0;
}

int
box_set_wal_retention_period(void)
{
	double delay = box_check_wal_retention_period();
	if (delay < 0)
		return -1;
	wal_set_retention_period(delay);
	return 0;
}

void
box_set_vinyl_memory(void)
{
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	ssize_t size = box_check_memory_quota("vinyl_memory");
	if (size < 0)
		diag_raise();
	vinyl_engine_set_memory_xc(vinyl, size);
}

void
box_set_vinyl_max_tuple_size(void)
{
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_max_tuple_size(vinyl,
			cfg_geti("vinyl_max_tuple_size"));
}

void
box_set_vinyl_cache(void)
{
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_cache(vinyl, cfg_geti64("vinyl_cache"));
}

void
box_set_vinyl_timeout(void)
{
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_timeout(vinyl,	cfg_getd("vinyl_timeout"));
}

void
box_set_force_recovery(void)
{
	box_is_force_recovery = cfg_geti("force_recovery");
}

void
box_set_net_msg_max(void)
{
	int new_iproto_msg_max = cfg_geti("net_msg_max");
	if (iproto_set_msg_max(new_iproto_msg_max) != 0)
		diag_raise();
	fiber_pool_set_max_size(&tx_fiber_pool,
				new_iproto_msg_max *
				IPROTO_FIBER_POOL_SIZE_FACTOR);
}

int
box_set_prepared_stmt_cache_size(void)
{
	int cache_sz = cfg_geti("sql_cache_size");
	if (box_check_sql_cache_size(cache_sz) != 0)
		return -1;
	if (sql_stmt_cache_set_size(cache_sz) != 0)
		return -1;
	return 0;
}

/**
 * Report crash information to the feedback daemon
 * (ie send it to feedback daemon).
 */
static int
box_feedback_report_crash(struct crash_info *cinfo)
{
	/*
	 * Update to a new number if the format get changed.
	 */
	const int crashinfo_version = 1;

	char *p = (char *)static_alloc(SMALL_STATIC_SIZE);
	char *tail = &p[SMALL_STATIC_SIZE];
	char *e = &p[SMALL_STATIC_SIZE];
	char *head = p;

	int total = 0;
	(void)total;
	int size = 0;

#define snprintf_safe(...) SNPRINT(total, snprintf, p, size, __VA_ARGS__)
#define jnprintf_safe(str) SNPRINT(total, json_escape, p, size, str)

	/*
	 * Lets reuse tail of the buffer as a temp space.
	 */
	struct utsname *uname_ptr =
		(struct utsname *)&tail[-sizeof(struct utsname)];
	if (p >= (char *)uname_ptr)
		return -1;

	if (uname(uname_ptr) != 0) {
		say_syserror("uname call failed, ignore");
		memset(uname_ptr, 0, sizeof(struct utsname));
	}

	/*
	 * Start filling the script. The "data" key value is
	 * filled as a separate code block for easier
	 * modifications in future.
	 */
	size = (char *)uname_ptr - p;
	snprintf_safe("{");
	snprintf_safe("\"crashdump\":{");
	snprintf_safe("\"version\":\"%d\",", crashinfo_version);
	snprintf_safe("\"data\":");

	/* The "data" key value */
	snprintf_safe("{");
	snprintf_safe("\"uname\":{");
	snprintf_safe("\"sysname\":\"");
	jnprintf_safe(uname_ptr->sysname);
	snprintf_safe("\",");
	/*
	 * nodename might contain a sensitive information, skip.
	 */
	snprintf_safe("\"release\":\"");
	jnprintf_safe(uname_ptr->release);
	snprintf_safe("\",");

	snprintf_safe("\"version\":\"");
	jnprintf_safe(uname_ptr->version);
	snprintf_safe("\",");

	snprintf_safe("\"machine\":\"");
	jnprintf_safe(uname_ptr->machine);
	snprintf_safe("\"");
	snprintf_safe("},");

	/* Extend size, because now uname_ptr is not needed. */
	size = e - p;

	/*
	 * Instance block requires uuid encoding so take it
	 * from the tail of the buffer.
	 */
	snprintf_safe("\"instance\":{");
	char *uuid_buf = &tail[-(UUID_STR_LEN + 1)];
	if (p >= uuid_buf)
		return -1;
	size = uuid_buf - p;

	tt_uuid_to_string(&INSTANCE_UUID, uuid_buf);
	snprintf_safe("\"server_id\":\"%s\",", uuid_buf);
	tt_uuid_to_string(&REPLICASET_UUID, uuid_buf);
	snprintf_safe("\"cluster_id\":\"%s\",", uuid_buf);

	/* No need for uuid_buf anymore. */
	size = e - p;

	struct timespec ts;
	time_t uptime = 0;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
		uptime = ts.tv_sec - tarantool_start_time;
	snprintf_safe("\"uptime\":\"%llu\"", (unsigned long long)uptime);
	snprintf_safe("},");

	snprintf_safe("\"build\":{");
	snprintf_safe("\"version\":\"%s\",", PACKAGE_VERSION);
	snprintf_safe("\"cmake_type\":\"%s\"", BUILD_INFO);
	snprintf_safe("},");

	snprintf_safe("\"signal\":{");
	snprintf_safe("\"signo\":%d,", cinfo->signo);
	snprintf_safe("\"si_code\":%d,", cinfo->sicode);
	if (cinfo->signo == SIGSEGV) {
		if (cinfo->sicode == SEGV_MAPERR) {
			snprintf_safe("\"si_code_str\":\"%s\",",
				      "SEGV_MAPERR");
		} else if (cinfo->sicode == SEGV_ACCERR) {
			snprintf_safe("\"si_code_str\":\"%s\",",
				      "SEGV_ACCERR");
		}
	}
	snprintf_safe("\"si_addr\":\"0x%llx\",",
		      (long long)cinfo->siaddr);

#ifdef ENABLE_BACKTRACE
	snprintf_safe("\"backtrace\":\"");
	jnprintf_safe(cinfo->backtrace_buf);
	snprintf_safe("\",");
#endif

	/* 64 bytes should be enough for longest localtime */
	const int ts_size = 64;
	char *timestamp_rt_str = &tail[-ts_size];
	if (p >= timestamp_rt_str)
		return -1;

	struct tm tm;
	localtime_r(&cinfo->timestamp_rt, &tm);
	strftime(timestamp_rt_str, ts_size, "%F %T %Z", &tm);
	timestamp_rt_str[ts_size - 1] = '\0';

	size = timestamp_rt_str - p;
	snprintf_safe("\"timestamp\":\"");
	jnprintf_safe(timestamp_rt_str);
	snprintf_safe("\"");
	snprintf_safe("}");
	snprintf_safe("}");

	/* Finalize the "data" key and the whole dump. */
	size = e - p;
	snprintf_safe("}");
	snprintf_safe("}");

#undef snprintf_safe
#undef jnprintf_safe

	say_debug("crash dump: %s", head);

	/* Timeout 1 sec is taken from the feedback daemon. */
	const char *expr =
		"require('http.client').post(arg[1],arg[2],{timeout=1});"
		"os.exit(1);";
	const char *exec_argv[7] = {
		tarantool_path,
		"-e", expr,
		"-",
		box_feedback_host,
		head,
		NULL,
	};

	extern char **environ;
	int rc = posix_spawn(NULL, exec_argv[0], NULL, NULL,
			     (char **)exec_argv, environ);
	if (rc != 0) {
		fprintf(stderr,
			"posix_spawn with "
			"exec(%s,[%s,%s,%s,%s,%s,%s,%s]) failed: %s\n",
			exec_argv[0], exec_argv[0], exec_argv[1], exec_argv[2],
			exec_argv[3], exec_argv[4], exec_argv[5], exec_argv[6],
			tt_strerror(rc));
		return -1;
	}

	return 0;
}

/**
 * Box callback to handle crashes.
 */
static void
box_crash_callback(struct crash_info *cinfo)
{
	if (cinfo->signo == SIGBUS &&
	    flightrec_is_mmapped_address(cinfo->siaddr)) {
		fprintf(stderr, "error accessing flightrec file\n");
		fflush(stderr);
		_exit(EXIT_FAILURE);
	}

	crash_report_stderr(cinfo);

	if (box_feedback_crash_enabled &&
	    box_feedback_report_crash(cinfo) != 0)
		fprintf(stderr, "unable to send a crash report\n");
}

int
box_set_feedback(void)
{
	const char *host = cfg_gets("feedback_host");

	if (host != NULL && strlen(host) >= BOX_FEEDBACK_HOST_MAX) {
		diag_set(ClientError, ER_CFG, "feedback_host",
			 "the address is too long");
		return -1;
	}

	if (cfg_getb("feedback_enabled") &&
	    cfg_getb("feedback_crashinfo") &&
	    host != NULL) {
		box_feedback_crash_enabled = true;
		strlcpy(box_feedback_host, host, sizeof(box_feedback_host));
		say_debug("enable sending crashinfo feedback");
	} else {
		box_feedback_crash_enabled = false;
		box_feedback_host[0] = '\0';
		say_debug("disable sending crashinfo feedback");
	}

	return 0;
}

int
box_set_txn_timeout(void)
{
	double timeout = box_check_txn_timeout();
	if (timeout < 0)
		return -1;
	txn_timeout_default = timeout;
	return 0;
}

int
box_set_txn_isolation(void)
{
	enum txn_isolation_level level = box_check_txn_isolation();
	if (level == txn_isolation_level_MAX)
		return -1;
	txn_default_isolation = level;
	return 0;
}

/* }}} configuration bindings */

/**
 * Execute a request against a given space id with
 * a variable-argument tuple described in format.
 *
 * @example: you want to insert 5 into space 1:
 * boxk(IPROTO_INSERT, 1, "[%u]", 5);
 *
 * @example: you want to set field 3 (base 0) of
 * a tuple with key [10, 20] in space 1 to 1000:
 * boxk(IPROTO_UPDATE, 1, "[%u%u][[%s%u%u]]", 10, 20, "=", 3, 1000);
 *
 * @note Since this is for internal use, it has
 * no boundary or misuse checks.
 */
int
boxk(int type, uint32_t space_id, const char *format, ...)
{
	va_list ap;
	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = type;
	request.space_id = space_id;
	va_start(ap, format);
	struct region *region = &fiber()->gc;
	size_t size = 0;
	RegionGuard region_guard(region);
	const char *data = mp_vformat_on_region(region, &size, format, ap);
	va_end(ap);
	const char *data_end = data + size;
	switch (type) {
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
		request.tuple = data;
		request.tuple_end = data_end;
		break;
	case IPROTO_DELETE:
		request.key = data;
		request.key_end = data_end;
		break;
	case IPROTO_UPDATE:
		request.key = data;
		mp_next(&data);
		request.key_end = data;
		request.tuple = data;
		mp_next(&data);
		request.tuple_end = data;
		request.index_base = 0;
		break;
	default:
		unreachable();
	}
	struct space *space = space_cache_find(space_id);
	if (space == NULL)
		return -1;
	return box_process_rw(&request, space, NULL);
}

API_EXPORT int
box_return_tuple(box_function_ctx_t *ctx, box_tuple_t *tuple)
{
	port_c_add_tuple(ctx->port, tuple);
	return 0;
}

API_EXPORT int
box_return_mp(box_function_ctx_t *ctx, const char *mp, const char *mp_end)
{
	port_c_add_mp(ctx->port, mp, mp_end);
	return 0;
}

/* schema_find_id()-like method using only public API */
API_EXPORT uint32_t
box_space_id_by_name(const char *name, uint32_t len)
{
	if (len > BOX_NAME_MAX)
		return BOX_ID_NIL;
	uint32_t size = mp_sizeof_array(1) + mp_sizeof_str(len);
	RegionGuard region_guard(&fiber()->gc);
	char *begin = (char *) region_alloc(&fiber()->gc, size);
	if (begin == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "begin");
		return BOX_ID_NIL;
	}
	char *end = mp_encode_array(begin, 1);
	end = mp_encode_str(end, name, len);

	/* NOTE: error and missing key cases are indistinguishable */
	box_tuple_t *tuple;
	if (box_index_get(BOX_VSPACE_ID, 2, begin, end, &tuple) != 0)
		return BOX_ID_NIL;
	if (tuple == NULL)
		return BOX_ID_NIL;
	uint32_t result = BOX_ID_NIL;
	(void) tuple_field_u32(tuple, BOX_SPACE_FIELD_ID, &result);
	return result;
}

API_EXPORT uint32_t
box_index_id_by_name(uint32_t space_id, const char *name, uint32_t len)
{
	if (len > BOX_NAME_MAX)
		return BOX_ID_NIL;
	uint32_t size = mp_sizeof_array(2) + mp_sizeof_uint(space_id) +
			mp_sizeof_str(len);
	RegionGuard region_guard(&fiber()->gc);
	char *begin = (char *) region_alloc(&fiber()->gc, size);
	if (begin == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "begin");
		return BOX_ID_NIL;
	}
	char *end = mp_encode_array(begin, 2);
	end = mp_encode_uint(end, space_id);
	end = mp_encode_str(end, name, len);

	/* NOTE: error and missing key cases are indistinguishable */
	box_tuple_t *tuple;
	if (box_index_get(BOX_VINDEX_ID, 2, begin, end, &tuple) != 0)
		return BOX_ID_NIL;
	if (tuple == NULL)
		return BOX_ID_NIL;
	uint32_t result = BOX_ID_NIL;
	(void) tuple_field_u32(tuple, BOX_INDEX_FIELD_ID, &result);
	return result;
}

int
box_process1(struct request *request, box_tuple_t **result)
{
	if (box_check_slice() != 0)
		return -1;
	struct space *space = space_cache_find(request->space_id);
	if (space == NULL)
		return -1;
	/*
	 * Allow to write to data-temporary and local spaces in the read-only
	 * mode. To handle space truncation and/or ddl operations on temporary
	 * spaces, we postpone the read-only check for the _truncate, _space &
	 * _index system spaces till the on_replace trigger is called, when
	 * we know which spaces are concerned.
	 */
	uint32_t id = space_id(space);
	if (is_ro_summary &&
	    id != BOX_TRUNCATE_ID &&
	    id != BOX_SPACE_ID &&
	    id != BOX_INDEX_ID &&
	    !space_is_data_temporary(space) &&
	    !space_is_local(space) &&
	    box_check_writable() != 0)
		return -1;
	if (space_is_memtx(space)) {
		/*
		 * Due to on_init_schema triggers set on system spaces,
		 * we can insert data during recovery to local and
		 * data-temporary spaces. However, until recovery is finished,
		 * we can't check key uniqueness (since indexes are still not
		 * yet built). So reject any attempts to write into these
		 * spaces.
		 */
		if (memtx_space_is_recovering(space)) {
			diag_set(ClientError, ER_UNSUPPORTED, "Snapshot recovery",
				"write requests, use "
				"box.ctl.is_recovery_finished() "
				"to check that snapshot recovery was completed");
			diag_log();
			return -1;
		}
	}

	return box_process_rw(request, space, result);
}

void
box_iterator_position_pack(const char *pos, const char *pos_end,
			   uint32_t found, const char **packed_pos,
			   const char **packed_pos_end)
{
	assert(packed_pos != NULL);
	assert(packed_pos_end != NULL);
	if (found > 0 && pos != NULL) {
		uint32_t buf_size =
			iterator_position_pack_bufsize(pos, pos_end);
		char *buf =
			(char *)xregion_alloc(&fiber()->gc, buf_size);
		iterator_position_pack(pos, pos_end, buf, buf_size,
				       packed_pos, packed_pos_end);
	} else {
		*packed_pos = NULL;
		*packed_pos_end = NULL;
	}
}

int
box_iterator_position_unpack(const char *packed_pos,
			     const char *packed_pos_end,
			     struct key_def *cmp_def, const char *key,
			     uint32_t key_part_count, int iterator,
			     const char **pos, const char **pos_end)
{
	assert(pos != NULL);
	assert(pos_end != NULL);
	if (packed_pos != NULL && packed_pos != packed_pos_end) {
		uint32_t buf_size =
			iterator_position_unpack_bufsize(packed_pos,
							 packed_pos_end);
		char *buf = (char *)xregion_alloc(&fiber()->gc, buf_size);
		if (iterator_position_unpack(packed_pos, packed_pos_end,
					     buf, buf_size,
					     pos, pos_end) != 0)
			return -1;
		uint32_t pos_part_count = mp_decode_array(pos);
		enum iterator_type type = (enum iterator_type)iterator;
		if (iterator_position_validate(*pos, pos_part_count, key,
					       key_part_count, cmp_def,
					       type) != 0)
			return -1;
	} else {
		*pos = NULL;
		*pos_end = NULL;
	}
	return 0;
}

int
box_select(uint32_t space_id, uint32_t index_id,
	   int iterator, uint32_t offset, uint32_t limit,
	   const char *key, const char *key_end,
	   const char **packed_pos, const char **packed_pos_end,
	   bool update_pos, struct port *port)
{
	(void)key_end;
	assert(!update_pos || (packed_pos != NULL && packed_pos_end != NULL));
	assert(packed_pos == NULL || packed_pos_end != NULL);

	rmean_collect(rmean_box, IPROTO_SELECT, 1);

	if (iterator < 0 || iterator >= iterator_type_MAX) {
		diag_set(IllegalParams, "Invalid iterator type");
		diag_log();
		return -1;
	}

	struct space *space = space_cache_find(space_id);
	if (space == NULL)
		return -1;
	if (access_check_space(space, PRIV_R) != 0)
		return -1;
	struct index *index = index_find(space, index_id);
	if (index == NULL)
		return -1;

	enum iterator_type type = (enum iterator_type) iterator;
	const char *key_array = key;
	uint32_t part_count = key ? mp_decode_array(&key) : 0;
	if (key_validate(index->def, type, key, part_count))
		return -1;
	const char *pos, *pos_end;
	if (box_iterator_position_unpack(*packed_pos, *packed_pos_end,
					 index->def->cmp_def, key, part_count,
					 type, &pos, &pos_end) != 0)
		return -1;

	box_run_on_select(space, index, type, key_array);

	ERROR_INJECT(ERRINJ_TESTING, {
		diag_set(ClientError, ER_INJECTION, "ERRINJ_TESTING");
		return -1;
	});

	struct txn *txn;
	struct txn_ro_savepoint svp;
	if (txn_begin_ro_stmt(space, &txn, &svp) != 0)
		return -1;

	struct iterator *it = index_create_iterator_after(index, type, key,
							  part_count, pos);
	if (it == NULL) {
		txn_end_ro_stmt(txn, &svp);
		return -1;
	}

	int rc = 0;
	uint32_t found = 0;
	struct tuple *tuple;
	port_c_create(port);
	while (found < limit) {
		rc = box_check_slice();
		if (rc != 0)
			break;
		rc = iterator_next(it, &tuple);
		if (rc != 0 || tuple == NULL)
			break;
		if (offset > 0) {
			offset--;
			continue;
		}
		port_c_add_tuple(port, tuple);
		found++;
		/*
		 * Refresh the pointer to the space, because the space struct
		 * could be freed if the iterator yielded.
		 */
		space = index_weak_ref_get_space(&it->index_ref);
	}

	txn_end_ro_stmt(txn, &svp);
	if (rc != 0)
		goto fail;

	if (update_pos) {
		uint32_t pos_size;
		/*
		 * Iterator position is extracted even if no tuples were found
		 * to check if pagination is supported by index.
		 */
		if (iterator_position(it, &pos, &pos_size) != 0)
			goto fail;
		box_iterator_position_pack(pos, pos + pos_size, found,
					   packed_pos, packed_pos_end);
	}
	iterator_delete(it);
	return 0;
fail:
	iterator_delete(it);
	port_destroy(port);
	return -1;
}

/**
 * A special wrapper for FFI - workaround for M1.
 * Use 64-bit integers beyond the 8th argument.
 * See https://github.com/LuaJIT/LuaJIT/issues/205 for details.
 */
extern "C" int
box_select_ffi(uint32_t space_id, uint32_t index_id, const char *key,
	       const char *key_end, const char **packed_pos,
	       const char **packed_pos_end, bool update_pos, struct port *port,
	       int64_t iterator, uint64_t offset, uint64_t limit)
{
	return box_select(space_id, index_id, iterator, offset, limit, key,
			  key_end, packed_pos, packed_pos_end, update_pos,
			  port);
}

API_EXPORT int
box_insert(uint32_t space_id, const char *tuple, const char *tuple_end,
	   box_tuple_t **result)
{
	mp_tuple_assert(tuple, tuple_end);
	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_INSERT;
	request.space_id = space_id;
	request.tuple = tuple;
	request.tuple_end = tuple_end;
	return box_process1(&request, result);
}

API_EXPORT int
box_replace(uint32_t space_id, const char *tuple, const char *tuple_end,
	    box_tuple_t **result)
{
	mp_tuple_assert(tuple, tuple_end);
	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_REPLACE;
	request.space_id = space_id;
	request.tuple = tuple;
	request.tuple_end = tuple_end;
	return box_process1(&request, result);
}

API_EXPORT int
box_delete(uint32_t space_id, uint32_t index_id, const char *key,
	   const char *key_end, box_tuple_t **result)
{
	mp_tuple_assert(key, key_end);
	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_DELETE;
	request.space_id = space_id;
	request.index_id = index_id;
	request.key = key;
	request.key_end = key_end;
	return box_process1(&request, result);
}

API_EXPORT int
box_update(uint32_t space_id, uint32_t index_id, const char *key,
	   const char *key_end, const char *ops, const char *ops_end,
	   int index_base, box_tuple_t **result)
{
	mp_tuple_assert(key, key_end);
	mp_tuple_assert(ops, ops_end);
	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_UPDATE;
	request.space_id = space_id;
	request.index_id = index_id;
	request.key = key;
	request.key_end = key_end;
	request.index_base = index_base;
	/** Legacy: in case of update, ops are passed in in request tuple */
	request.tuple = ops;
	request.tuple_end = ops_end;
	return box_process1(&request, result);
}

API_EXPORT int
box_upsert(uint32_t space_id, uint32_t index_id, const char *tuple,
	   const char *tuple_end, const char *ops, const char *ops_end,
	   int index_base, box_tuple_t **result)
{
	mp_tuple_assert(ops, ops_end);
	mp_tuple_assert(tuple, tuple_end);
	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_UPSERT;
	request.space_id = space_id;
	request.index_id = index_id;
	request.ops = ops;
	request.ops_end = ops_end;
	request.tuple = tuple;
	request.tuple_end = tuple_end;
	request.index_base = index_base;
	return box_process1(&request, result);
}

/**
 * Trigger space truncation by bumping a counter
 * in _truncate space.
 */
static void
space_truncate(struct space *space)
{
	size_t buf_size = 3 * mp_sizeof_array(UINT32_MAX) +
			  4 * mp_sizeof_uint(UINT64_MAX) + mp_sizeof_str(1);
	RegionGuard region_guard(&fiber()->gc);
	char *buf = (char *)xregion_alloc(&fiber()->gc, buf_size);

	char *tuple_buf = buf;
	char *tuple_buf_end = tuple_buf;
	tuple_buf_end = mp_encode_array(tuple_buf_end, 2);
	tuple_buf_end = mp_encode_uint(tuple_buf_end, space_id(space));
	tuple_buf_end = mp_encode_uint(tuple_buf_end, 1);
	assert(tuple_buf_end < buf + buf_size);

	char *ops_buf = tuple_buf_end;
	char *ops_buf_end = ops_buf;
	ops_buf_end = mp_encode_array(ops_buf_end, 1);
	ops_buf_end = mp_encode_array(ops_buf_end, 3);
	ops_buf_end = mp_encode_str(ops_buf_end, "+", 1);
	ops_buf_end = mp_encode_uint(ops_buf_end, 1);
	ops_buf_end = mp_encode_uint(ops_buf_end, 1);
	assert(ops_buf_end < buf + buf_size);

	if (box_upsert(BOX_TRUNCATE_ID, 0, tuple_buf, tuple_buf_end,
		       ops_buf, ops_buf_end, 0, NULL) != 0)
		diag_raise();
}

API_EXPORT int
box_truncate(uint32_t space_id)
{
	try {
		struct space *space = space_cache_find_xc(space_id);
		space_truncate(space);
		return 0;
	} catch (Exception *exc) {
		return -1;
	}
}

/** Update a record in _sequence_data space. */
static int
sequence_data_update(uint32_t seq_id, int64_t value)
{
	size_t tuple_buf_size = (mp_sizeof_array(2) +
				 2 * mp_sizeof_uint(UINT64_MAX));
	RegionGuard region_guard(&fiber()->gc);
	char *tuple_buf = (char *) region_alloc(&fiber()->gc, tuple_buf_size);
	if (tuple_buf == NULL) {
		diag_set(OutOfMemory, tuple_buf_size, "region", "tuple");
		return -1;
	}
	char *tuple_buf_end = tuple_buf;
	tuple_buf_end = mp_encode_array(tuple_buf_end, 2);
	tuple_buf_end = mp_encode_uint(tuple_buf_end, seq_id);
	tuple_buf_end = (value < 0 ?
			 mp_encode_int(tuple_buf_end, value) :
			 mp_encode_uint(tuple_buf_end, value));
	assert(tuple_buf_end < tuple_buf + tuple_buf_size);

	struct credentials *orig_credentials = effective_user();
	fiber_set_user(fiber(), &admin_credentials);

	int rc = box_replace(BOX_SEQUENCE_DATA_ID,
			     tuple_buf, tuple_buf_end, NULL);

	fiber_set_user(fiber(), orig_credentials);
	return rc;
}

/** Delete a record from _sequence_data space. */
static int
sequence_data_delete(uint32_t seq_id)
{
	size_t key_buf_size = mp_sizeof_array(1) + mp_sizeof_uint(UINT64_MAX);
	RegionGuard region_guard(&fiber()->gc);
	char *key_buf = (char *) region_alloc(&fiber()->gc, key_buf_size);
	if (key_buf == NULL) {
		diag_set(OutOfMemory, key_buf_size, "region", "key");
		return -1;
	}
	char *key_buf_end = key_buf;
	key_buf_end = mp_encode_array(key_buf_end, 1);
	key_buf_end = mp_encode_uint(key_buf_end, seq_id);
	assert(key_buf_end < key_buf + key_buf_size);

	struct credentials *orig_credentials = effective_user();
	fiber_set_user(fiber(), &admin_credentials);

	int rc = box_delete(BOX_SEQUENCE_DATA_ID, 0,
			    key_buf, key_buf_end, NULL);

	fiber_set_user(fiber(), orig_credentials);
	return rc;
}

API_EXPORT int
box_sequence_next(uint32_t seq_id, int64_t *result)
{
	struct sequence *seq = sequence_cache_find(seq_id);
	if (seq == NULL)
		return -1;
	if (access_check_sequence(seq) != 0)
		return -1;
	int64_t value;
	if (sequence_next(seq, &value) != 0)
		return -1;
	if (sequence_data_update(seq_id, value) != 0)
		return -1;
	*result = value;
	return 0;
}

API_EXPORT int
box_sequence_current(uint32_t seq_id, int64_t *result)
{
	struct sequence *seq = sequence_cache_find(seq_id);
	if (seq == NULL)
		return -1;
	if (access_check_sequence(seq) != 0)
		return -1;
	if (sequence_get_value(seq, result) != 0)
		return -1;
	return 0;
}

API_EXPORT int
box_sequence_set(uint32_t seq_id, int64_t value)
{
	struct sequence *seq = sequence_cache_find(seq_id);
	if (seq == NULL)
		return -1;
	if (access_check_sequence(seq) != 0)
		return -1;
	if (sequence_set(seq, value) != 0)
		return -1;
	return sequence_data_update(seq_id, value);
}

API_EXPORT int
box_sequence_reset(uint32_t seq_id)
{
	struct sequence *seq = sequence_cache_find(seq_id);
	if (seq == NULL)
		return -1;
	if (access_check_sequence(seq) != 0)
		return -1;
	sequence_reset(seq);
	return sequence_data_delete(seq_id);
}

API_EXPORT int
box_session_push(const char *data, const char *data_end)
{
	struct session *session = current_session();
	if (session == NULL)
		return -1;
	if (session_push_check_deprecation() != 0)
		return -1;
	struct port_msgpack port;
	struct port *base = (struct port *)&port;
	port_msgpack_create(base, data, data_end - data);
	int rc = session_push(session, base);
	port_msgpack_destroy(base);
	return rc;
}

API_EXPORT uint64_t
box_session_id(void)
{
	return current_session()->id;
}

API_EXPORT int
box_iproto_send(uint64_t sid,
		const char *header, const char *header_end,
		const char *body, const char *body_end)
{
	struct session *session = session_find(sid);
	if (session == NULL) {
		diag_set(ClientError, ER_NO_SUCH_SESSION, sid);
		return -1;
	}
	if (session->type != SESSION_TYPE_BINARY) {
		diag_set(ClientError, ER_WRONG_SESSION_TYPE,
			 session_type_strs[session->type]);
		return -1;
	}
	return iproto_session_send(session, header, header_end, body, body_end);
}

API_EXPORT int
box_iproto_override(uint32_t req_type, iproto_handler_t handler,
		    iproto_handler_destroy_t destroy, void *ctx)
{
	return iproto_override(req_type, handler, destroy, ctx);
}

/**
 * Insert replica record into _cluster space, bypassing all checks like whether
 * the instance is writable. It makes the function usable by bootstrap master
 * when it is read-only but has to register self.
 */
static void
box_insert_replica_record(uint32_t id, const struct tt_uuid *uuid,
			  const char *name)
{
	bool ok;
	if (*name == 0) {
		ok = boxk(IPROTO_INSERT, BOX_CLUSTER_ID, "[%u%s]",
			  (unsigned)id, tt_uuid_str(uuid)) == 0;
	} else {
		ok = boxk(IPROTO_INSERT, BOX_CLUSTER_ID, "[%u%s%s]",
			  (unsigned)id, tt_uuid_str(uuid), name) == 0;
	}
	if (!ok)
		diag_raise();
	struct replica *new_replica = replica_by_uuid(uuid);
	if (new_replica == NULL || new_replica->id != id)
		say_warn("Replica ID is changed by a trigger");
}

static void
box_register_replica(const struct tt_uuid *uuid,
		     const char *name)
{
	struct replica *replica = replica_by_uuid(uuid);
	/*
	 * Find required changes.
	 */
	bool need_name = false;
	bool need_id = false;
	if (replica == NULL) {
		need_name = *name != 0;
		need_id = true;
	} else {
		need_name = strcmp(replica->name, name) != 0;
		need_id = replica->id == REPLICA_ID_NIL;
	}
	if (!need_name && !need_id)
		return;
	/*
	 * Apply the changes.
	 */
	box_check_writable_xc();
	if (!need_id) {
		int rc;
		if (*name == 0) {
			rc = boxk(IPROTO_UPDATE, BOX_CLUSTER_ID,
				  "[%u][[%s%dNIL]]", (unsigned)replica->id, "=",
				  BOX_CLUSTER_FIELD_NAME);
		} else {
			rc = boxk(IPROTO_UPDATE, BOX_CLUSTER_ID,
				  "[%u][[%s%d%s]]", (unsigned)replica->id, "=",
				  BOX_CLUSTER_FIELD_NAME, name);
		}
		if (rc != 0)
			diag_raise();
		return;
	}
	struct replica *other = replica_by_name(name);
	if (other != NULL && other != replica) {
		if (boxk(IPROTO_UPDATE, BOX_CLUSTER_ID, "[%u][[%s%d%s]]",
			 (unsigned)other->id, "=", BOX_CLUSTER_FIELD_UUID,
			 tt_uuid_str(uuid)) != 0)
			diag_raise();
		return;
	}
	uint32_t replica_id;
	if (replica_find_new_id(&replica_id) != 0)
		diag_raise();
	box_insert_replica_record(replica_id, uuid, name);
}

int
box_process_auth(struct auth_request *request,
		 const char *salt, uint32_t salt_len)
{
	assert(salt_len >= AUTH_SALT_SIZE);
	(void)salt_len;

	rmean_collect(rmean_box, IPROTO_AUTH, 1);

	/* Check that bootstrap has been finished */
	if (!is_box_configured) {
		diag_set(ClientError, ER_LOADING);
		return -1;
	}

	const char *user = request->user_name;
	uint32_t len = mp_decode_strl(&user);
	if (authenticate(user, len, salt, request->scramble) != 0)
		return -1;
	return 0;
}

void
box_process_fetch_snapshot(struct iostream *io,
			   const struct xrow_header *header)
{
	assert(header->type == IPROTO_FETCH_SNAPSHOT);

	struct fetch_snapshot_request req;
	xrow_decode_fetch_snapshot_xc(header, &req);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	/* Check permissions */
	access_check_universe_xc(PRIV_R);

	/* Forbid replication with disabled WAL */
	if (wal_mode() == WAL_NONE) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Replication",
			  "wal_mode = 'none'");
	}

	say_info("sending current read-view to replica at %s", sio_socketname(io->fd));

	/* Send the snapshot data to the instance. */
	struct vclock start_vclock;
	relay_initial_join(io, header->sync, &start_vclock, req.version_id);
	say_info("read-view sent.");

	/* Remember master's vclock after the last request */
	struct vclock stop_vclock;
	vclock_copy(&stop_vclock, &replicaset.vclock);

	/* Send end of snapshot data marker */
	struct xrow_header row;
	RegionGuard region_guard(&fiber()->gc);
	xrow_encode_vclock(&row, &stop_vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);
}

/**
 * Replica vclock is used in gc state and recovery initialization - need to
 * replace the remote 0-th component with the own one. This doesn't break
 * recovery: it finds the WAL with a vclock strictly less than replia clock in
 * all components except the 0th one.
 *
 * Note, that it would be bad to set 0-th component to a smaller value (like
 * zero) - it would unnecessarily require additional WALs, which may have
 * already been deleted.
 *
 * Speaking of gc, remote instances' local vclock components are not used by
 * consumers at all.
 */
static void
box_localize_vclock(const struct vclock *remote, struct vclock *local)
{
	vclock_copy(local, remote);
	vclock_reset(local, 0, vclock_get(&replicaset.vclock, 0));
}

void
box_process_register(struct iostream *io, const struct xrow_header *header)
{
	assert(header->type == IPROTO_REGISTER);

	struct register_request req;
	xrow_decode_register_xc(header, &req);

	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	if (tt_uuid_is_equal(&req.instance_uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	access_check_universe_xc(PRIV_R);
	/*
	 * We only get register requests from instances which need some actual
	 * registration - name, id.
	 */
	struct replica *replica = replica_by_uuid(&req.instance_uuid);
	bool need_id = false;
	bool need_name = false;
	if (replica == NULL) {
		need_id = true;
		need_name = *req.instance_name != 0;
	} else {
		need_id = replica->id == REPLICA_ID_NIL;
		need_name = strcmp(req.instance_name, replica->name) != 0;
	}
	if (!need_id && !need_name) {
		tnt_raise(ClientError, ER_REPLICA_NOT_ANON,
			  tt_uuid_str(&req.instance_uuid));
	}
	if (box_is_anon()) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Anonymous replica",
			  "registration of non-anonymous nodes.");
	}

	/* Don't allow multiple relays for the same replica */
	if (replica != NULL &&
	    relay_get_state(replica->relay) == RELAY_FOLLOW) {
		tnt_raise(ClientError, ER_CFG, "replication",
			  "duplicate connection with the same replica UUID");
	}

	/* See box_process_join() */
	box_check_writable_xc();
	struct space *space = space_cache_find_xc(BOX_CLUSTER_ID);
	access_check_space_xc(space, PRIV_W);

	/* Forbid replication with disabled WAL */
	if (wal_mode() == WAL_NONE) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Replication",
			  "wal_mode = 'none'");
	}

	struct vclock start_vclock;
	box_localize_vclock(&req.vclock, &start_vclock);
	/*
	 * Register an anonymous consumer (not associated with replica) to hold
	 * xlogs until replica is successfully registered.
	 */
	struct gc_consumer *gc_temporary =
		gc_consumer_register_anonymous(&start_vclock,
					       "replica %s register",
					       tt_uuid_str(&req.instance_uuid));
	auto gc_guard = make_scoped_guard([&] {
		gc_consumer_unregister_anonymous(gc_temporary);
	});

	say_info("registering replica %s at %s",
		 tt_uuid_str(&req.instance_uuid), sio_socketname(io->fd));
	box_register_replica(&req.instance_uuid, req.instance_name);
	/*
	 * Replica was registered as a WAL GC consumer on insert to _cluster,
	 * so just advance its consumer now.
	 */
	if (gc_consumer_update(&req.instance_uuid, &start_vclock) != 0)
		diag_raise();
	gc_consumer_unregister_anonymous(gc_temporary);
	gc_guard.is_active = false;

	ERROR_INJECT_YIELD(ERRINJ_REPLICA_JOIN_DELAY);

	replica = replica_by_uuid(&req.instance_uuid);
	if (replica == NULL)
		tnt_raise(ClientError, ER_CANNOT_REGISTER);
	/* Remember master's vclock after the last request */
	struct vclock stop_vclock;
	vclock_copy(&stop_vclock, &replicaset.vclock);
	/*
	 * Feed replica with WALs up to the REGISTER itself so that it gets own
	 * registration entry.
	 */
	relay_final_join(replica, io, header->sync, &start_vclock,
			 &stop_vclock);
	say_info("final data sent.");

	RegionGuard region_guard(&fiber()->gc);
	struct xrow_header row;
	/* Send end of WAL stream marker */
	xrow_encode_vclock(&row, &replicaset.vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);

	gc_consumer_update_async(&replica->uuid, &stop_vclock);
}

void
box_process_join(struct iostream *io, const struct xrow_header *header)
{
	/*
	 * Tarantool 1.7 JOIN protocol diagram (gh-1113)
	 * =============================================
	 *
	 * Replica => Master
	 *
	 * => JOIN { INSTANCE_UUID: replica_uuid }
	 * <= OK { VCLOCK: start_vclock }
	 *    Replica has enough permissions and master is ready for JOIN.
	 *     - start_vclock - master's vclock at the time of join.
	 *
	 * <= INSERT
	 *    ...
	 *    Initial data: a stream of engine-specifc rows, e.g. snapshot
	 *    rows for memtx or dirty cursor data for Vinyl fed from a
	 *    read-view. Engine can use REPLICA_ID, LSN and other fields
	 *    for internal purposes.
	 *    ...
	 * <= INSERT
	 * <= OK { VCLOCK: stop_vclock } - end of initial JOIN stage.
	 *     - `stop_vclock` - master's vclock when it's done
	 *     done sending rows from the snapshot (i.e. vclock
	 *     for the end of final join).
	 *
	 * <= INSERT/REPLACE/UPDATE/UPSERT/DELETE { REPLICA_ID, LSN }
	 *    ...
	 *    Final data: a stream of WAL rows from `start_vclock` to
	 *    `stop_vclock`, inclusive. REPLICA_ID and LSN fields are
	 *    original values from WAL and master-master replication.
	 *    ...
	 * <= INSERT/REPLACE/UPDATE/UPSERT/DELETE { REPLICA_ID, LSN }
	 * <= OK { VCLOCK: current_vclock } - end of final JOIN stage.
	 *      - `current_vclock` - master's vclock after final stage.
	 *
	 * All packets must have the same SYNC value as initial JOIN request.
	 * Master can send ERROR at any time. Replica doesn't confirm rows
	 * by OKs. Either initial or final stream includes:
	 *  - Cluster UUID in _schema space
	 *  - Registration of master in _cluster space
	 *  - Registration of the new replica in _cluster space
	 */

	assert(header->type == IPROTO_JOIN);

	struct join_request req;
	xrow_decode_join_xc(header, &req);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	/* Forbid connection to itself */
	if (tt_uuid_is_equal(&req.instance_uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/* Check permissions */
	access_check_universe_xc(PRIV_R);

	if (box_is_anon()) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Anonymous replica",
			  "registration of non-anonymous nodes.");
	}

	/*
	 * Unless already registered, the new replica will be added to _cluster
	 * space once the initial join stage is complete. Fail early if the
	 * caller does not have appropriate access privileges or has some other
	 * obvious errors. So as not to waste time on the long join process
	 * then.
	 */
	struct replica *replica = replica_by_uuid(&req.instance_uuid);
	if (replica == NULL || replica->id == REPLICA_ID_NIL ||
	    strcmp(replica->name, req.instance_name) != 0) {
		box_check_writable_xc();
		struct space *space = space_cache_find_xc(BOX_CLUSTER_ID);
		access_check_space_xc(space, PRIV_W);
	}
	/* Forbid replication with disabled WAL */
	if (wal_mode() == WAL_NONE) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Replication",
			  "wal_mode = 'none'");
	}
	if ((replica == NULL && *req.instance_name != 0) ||
	    (replica != NULL &&
	     strcmp(replica->name, req.instance_name) != 0)) {
		struct replica *other = replica_by_name(req.instance_name);
		if (other != NULL && other != replica &&
		    replica_has_connections(other)) {
			tnt_raise(ClientError, ER_INSTANCE_NAME_DUPLICATE,
				  node_name_str(req.instance_name),
				  tt_uuid_str(&other->uuid));
		}
	}
	/*
	 * Register an anonymous consumer (not associated with replica) to hold
	 * xlogs until replica is successfully joined the replicaset.
	 */
	struct gc_consumer *gc_temporary =
		gc_consumer_register_anonymous(&replicaset.vclock,
					       "replica %s join",
					       tt_uuid_str(&req.instance_uuid));
	auto gc_guard = make_scoped_guard([&] {
		gc_consumer_unregister_anonymous(gc_temporary);
	});

	say_info("joining replica %s at %s",
		 tt_uuid_str(&req.instance_uuid), sio_socketname(io->fd));

	/*
	 * Initial stream: feed replica with dirty data from engines.
	 */
	struct vclock start_vclock;
	relay_initial_join(io, header->sync, &start_vclock, req.version_id);
	say_info("initial data sent.");
	/**
	 * Register the replica after sending the last row but before sending
	 * OK - if the registration fails, the error reaches the client.
	 */
	box_register_replica(&req.instance_uuid, req.instance_name);
	/*
	 * Replica was registered as a WAL GC consumer on insert to _cluster,
	 * so just advance its consumer now.
	 */
	if (gc_consumer_update(&req.instance_uuid, &start_vclock) != 0)
		diag_raise();
	gc_consumer_unregister_anonymous(gc_temporary);
	gc_guard.is_active = false;

	ERROR_INJECT_YIELD(ERRINJ_REPLICA_JOIN_DELAY);

	replica = replica_by_uuid(&req.instance_uuid);
	if (replica == NULL)
		tnt_raise(ClientError, ER_CANNOT_REGISTER);

	/* Remember master's vclock after the last request */
	struct vclock stop_vclock;
	vclock_copy(&stop_vclock, &replicaset.vclock);
	/* Send end of initial stage data marker */
	struct xrow_header row;
	RegionGuard region_guard(&fiber()->gc);
	xrow_encode_vclock(&row, &stop_vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);

	/*
	 * Final stage: feed replica with WALs in range
	 * (start_vclock, stop_vclock).
	 */
	relay_final_join(replica, io, header->sync, &start_vclock,
			 &stop_vclock);
	say_info("final data sent.");

	/* Send end of WAL stream marker */
	xrow_encode_vclock(&row, &replicaset.vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);

	gc_consumer_update_async(&replica->uuid, &stop_vclock);
}

void
box_process_subscribe(struct iostream *io, const struct xrow_header *header)
{
	assert(header->type == IPROTO_SUBSCRIBE);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	struct subscribe_request req;
	xrow_decode_subscribe_xc(header, &req);

	/* Forbid connection to itself */
	if (tt_uuid_is_equal(&req.instance_uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);
	/*
	 * The peer should have bootstrapped from somebody since it tries to
	 * subscribe already. If it belongs to a different replicaset, it won't
	 * be ever found here, and would try to reconnect thinking its replica
	 * ID wasn't replicated here yet. Prevent it right away.
	 */
	if (!tt_uuid_is_equal(&req.replicaset_uuid, &REPLICASET_UUID)) {
		tnt_raise(ClientError, ER_REPLICASET_UUID_MISMATCH,
			  tt_uuid_str(&REPLICASET_UUID),
			  tt_uuid_str(&req.replicaset_uuid));
	}
	/*
	 * Replicaset name mismatch is not considered a critical error. It can
	 * happen if rename happened and then some replicas reconnected. They
	 * won't ever be able to fetch the new name if the master rejects them.
	 */
	if (strcmp(req.replicaset_name, REPLICASET_NAME) != 0) {
		say_warn("Replicaset name mismatch on subscribe. Peer name - %s, "
			 "local name - %s", node_name_str(req.replicaset_name),
			 node_name_str(REPLICASET_NAME));
	}
	/*
	 * Do not allow non-anonymous followers for anonymous
	 * instances.
	 */
	if (box_is_anon() && !req.is_anon) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Anonymous replica",
			  "non-anonymous followers.");
	}

	/* Check permissions */
	access_check_universe_xc(PRIV_R);

	/* Check replica uuid */
	struct replica *replica = replica_by_uuid(&req.instance_uuid);

	if (!req.is_anon &&
	    (replica == NULL || replica->id == REPLICA_ID_NIL ||
	     !gc_consumer_is_registered(&replica->uuid))) {
		/*
		 * The instance is not anonymous, and is registered (at least it
		 * claims so), but its ID is not delivered to the current
		 * instance yet. Need to wait until its _cluster record arrives
		 * from some third node. Likely to happen on bootstrap, when
		 * there is a fullmesh and 1 leader doing all the _cluster
		 * registrations. Not all of them are delivered to the other
		 * nodes yet.
		 * Also can happen when the replica is deleted from _cluster,
		 * but still tries to subscribe. It won't have an ID here.
		 * Moreover, non-anonymous replica having no consumer (and our
		 * protocol implies that any registered replica has one) means
		 * that record in _cluster wasn't committed yet because the
		 * consumer is created on commit, so this case is considered as
		 * too early subscribe as well.
		 */
		tnt_raise(ClientError, ER_TOO_EARLY_SUBSCRIBE,
			  tt_uuid_str(&req.instance_uuid));
	}
	if (req.is_anon && replica != NULL && replica->id != REPLICA_ID_NIL) {
		tnt_raise(ClientError, ER_PROTOCOL, "Can't subscribe an "
			  "anonymous replica having an ID assigned");
	}
	/*
	 * Replica name mismatch is not considered a critical error. It can
	 * happen if rename happened and then the replica reconnected. It won't
	 * ever be able to fetch the new name if the master rejects the
	 * connection.
	 */
	if (replica != NULL &&
	    strcmp(replica->name, req.instance_name) != 0) {
		say_warn("Instance name mismatch on subscribe. "
			 "Peer claims name - %s, its stored name - %s",
			 node_name_str(req.instance_name),
			 node_name_str(replica->name));
	}
	if (replica == NULL)
		replica = replicaset_add_anon(&req.instance_uuid);

	/* Don't allow multiple relays for the same replica */
	if (relay_get_state(replica->relay) == RELAY_FOLLOW) {
		tnt_raise(ClientError, ER_CFG, "replication",
			  "duplicate connection with the same replica UUID");
	}

	/* Forbid replication with disabled WAL */
	if (wal_mode() == WAL_NONE) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Replication",
			  "wal_mode = 'none'");
	}
	struct vclock start_vclock;
	box_localize_vclock(&req.vclock, &start_vclock);
	/*
	 * Register the replica with the garbage collector.
	 * In case some of the replica's WAL files were deleted, it might
	 * subscribe with a smaller vclock than the master remembers, so
	 * recreate the gc consumer unconditionally to make sure it holds
	 * the correct vclock.
	 */
	if (!replica->anon &&
	    gc_consumer_update(&replica->uuid, &start_vclock) != 0)
		diag_raise();
	/*
	 * Send a response to SUBSCRIBE request, tell
	 * the replica how many rows we have in stock for it,
	 * and identify ourselves with our own replica id.
	 *
	 * Master not only checks the replica has the same replicaset UUID, but
	 * also sends the UUID to the replica so both Tarantools could perform
	 * any checks they want depending on their version and supported
	 * features.
	 *
	 * Older versions not supporting replicaset UUID in the response will
	 * just ignore the additional field (these are < 2.1.1).
	 */
	struct subscribe_response rsp;
	memset(&rsp, 0, sizeof(rsp));
	vclock_copy(&rsp.vclock, &replicaset.vclock);
	rsp.replicaset_uuid = REPLICASET_UUID;
	strlcpy(rsp.replicaset_name, REPLICASET_NAME, NODE_NAME_SIZE_MAX);
	struct xrow_header row;
	RegionGuard region_guard(&fiber()->gc);
	xrow_encode_subscribe_response(&row, &rsp);
	/*
	 * Identify the message with the replica id of this
	 * instance, this is the only way for a replica to find
	 * out the id of the instance it has connected to.
	 */
	struct replica *self = replica_by_uuid(&INSTANCE_UUID);
	assert(self != NULL); /* the local registration is read-only */
	row.replica_id = self->id;
	row.sync = header->sync;
	coio_write_xrow(io, &row);

	say_info("subscribed replica %s at %s",
		 tt_uuid_str(&req.instance_uuid), sio_socketname(io->fd));
	say_info("remote vclock %s local vclock %s",
		 vclock_to_string(&req.vclock), vclock_to_string(&rsp.vclock));
	uint64_t sent_raft_term = 0;
	if (req.version_id >= version_id(2, 6, 0) && !req.is_anon) {
		/*
		 * Send out the current raft state of the instance. Don't do
		 * that if the remote instance is old. It can be that a part of
		 * the cluster still contains old versions, which can't handle
		 * Raft messages. Raft's network footprint should be 0 as seen
		 * by such instances.
		 */
		struct raft_request req;
		box_raft_checkpoint_remote(&req);
		xrow_encode_raft(&row, &fiber()->gc, &req);
		coio_write_xrow(io, &row);
		sent_raft_term = req.term;
	}
	/*
	 * Process SUBSCRIBE request via replication relay
	 * Send current recovery vector clock as a marker
	 * of the "current" state of the master. When
	 * replica fetches rows up to this position,
	 * it enters read-write mode.
	 *
	 * @todo: this is not implemented, this is imperfect, and
	 * this is buggy in case there is rollback followed by
	 * a stall in updates (in this case replica may hang
	 * indefinitely).
	 */
	relay_subscribe(replica, io, header->sync, &start_vclock,
			req.version_id, req.id_filter, sent_raft_term);
}

void
box_process_vote(struct ballot *ballot)
{
	ballot->is_ro_cfg = cfg_geti("read_only") != 0;
	enum election_mode mode = box_check_election_mode();
	ballot->can_lead = mode == ELECTION_MODE_CANDIDATE ||
			   mode == ELECTION_MODE_MANUAL;
	ballot->is_anon = cfg_replication_anon;
	ballot->is_ro = is_ro_summary;
	ballot->is_booted = is_box_configured;
	vclock_copy(&ballot->vclock, &replicaset.vclock);
	vclock_copy(&ballot->gc_vclock, &gc.vclock);
	ballot->bootstrap_leader_uuid = bootstrap_leader_uuid;
	if (*INSTANCE_NAME != '\0') {
		strlcpy(ballot->instance_name, INSTANCE_NAME,
			NODE_NAME_SIZE_MAX);
	} else if (*cfg_instance_name != '\0') {
		strlcpy(ballot->instance_name, cfg_instance_name,
			NODE_NAME_SIZE_MAX);
	} else {
		*ballot->instance_name = '\0';
	}
	int i = 0;
	replicaset_foreach(replica) {
		if (replica->id != 0)
			ballot->registered_replica_uuids[i++] = replica->uuid;
	}
	assert(i < VCLOCK_MAX);

	ballot->registered_replica_uuids_size = i;
}

/** Fill _schema space with initial data on bootstrap. */
static void
box_populate_schema_space(void)
{
	struct tt_uuid replicaset_uuid;
	if (box_check_replicaset_uuid(&replicaset_uuid) != 0)
		diag_raise();
	char replicaset_name[NODE_NAME_SIZE_MAX];
	if (box_check_replicaset_name(replicaset_name) != 0)
		diag_raise();
	char cluster_name[NODE_NAME_SIZE_MAX];
	if (box_check_cluster_name(cluster_name) != 0)
		diag_raise();

	if (tt_uuid_is_nil(&replicaset_uuid))
		tt_uuid_create(&replicaset_uuid);
	if (boxk(IPROTO_INSERT, BOX_SCHEMA_ID, "[%s%s]", "replicaset_uuid",
		 tt_uuid_str(&replicaset_uuid)))
		diag_raise();
	box_set_cluster_name_record(cluster_name);
	box_set_replicaset_name_record(replicaset_name);
	if (bootstrap_strategy == BOOTSTRAP_STRATEGY_SUPERVISED)
		box_set_bootstrap_leader_record();
}

static void
box_on_indexes_built(void)
{
	box_run_on_recovery_state(RECOVERY_STATE_INDEXES_BUILT);
}

/**
 * Runs all triggers from event 'tarantool.trigger.on_change' with one
 * argument: name of the changed event.
 * Each returned value is ignored, all thrown errors are logged.
 */
static int
box_trigger_on_change(struct trigger *trigger, void *data)
{
	(void)trigger;
	assert(tarantool_trigger_on_change_event != NULL);
	struct event *on_change_event = tarantool_trigger_on_change_event;
	struct event *event = (struct event *)data;

	if (!event_has_triggers(on_change_event))
		return 0;

	struct port args;
	port_c_create(&args);
	port_c_add_str0(&args, event->name);
	event_run_triggers_no_fail(on_change_event, &args);
	port_destroy(&args);
	return 0;
}

static TRIGGER(box_trigger_on_change_trigger, box_trigger_on_change);

static void
engine_init()
{
	/*
	 * Sic: order is important here, since
	 * memtx must be the first to participate
	 * in checkpoints (in enigne_foreach order),
	 * so it must be registered first.
	 */
	struct memtx_engine *memtx;
	memtx = memtx_engine_new_xc(cfg_gets("memtx_dir"),
				    box_is_force_recovery,
				    cfg_getd("memtx_memory"),
				    cfg_geti("memtx_min_tuple_size"),
				    cfg_geti("strip_core"),
				    cfg_geti("slab_alloc_granularity"),
				    cfg_gets("memtx_allocator"),
				    cfg_getd("slab_alloc_factor"),
				    cfg_geti("memtx_sort_threads"),
				    box_on_indexes_built);
	engine_register((struct engine *)memtx);
	box_set_memtx_max_tuple_size();

	memcs_engine_register();

	struct sysview_engine *sysview = sysview_engine_new_xc();
	engine_register((struct engine *)sysview);

	struct engine *service_engine = service_engine_new_xc();
	engine_register(service_engine);

	struct engine *blackhole = blackhole_engine_new_xc();
	engine_register(blackhole);

	struct engine *vinyl;
	vinyl = vinyl_engine_new_xc(cfg_gets("vinyl_dir"),
				    cfg_geti64("vinyl_memory"),
				    cfg_geti("vinyl_read_threads"),
				    cfg_geti("vinyl_write_threads"),
				    box_is_force_recovery);
	engine_register((struct engine *)vinyl);
	box_set_vinyl_max_tuple_size();
	box_set_vinyl_cache();
	box_set_vinyl_timeout();
}

/**
 * Blindly apply whatever comes from bootstrap. This is either a
 * local snapshot, or received from a remote master. In both cases
 * it is not WAL - records are not sorted by their commit order,
 * and don't have headers.
 */
static int
bootstrap_journal_write(struct journal *base, struct journal_entry *entry)
{
	(void)base;

	entry->res = 0;
	journal_async_complete(entry);
	return 0;
}

/**
 * Wait until every remote peer that managed to connect chooses this node as its
 * bootstrap leader and fail otherwise.
 */
static void
check_bootstrap_unanimity(void)
{
	replicaset_foreach(replica) {
		struct applier *applier = replica->applier;
		if (applier == NULL || applier->state != APPLIER_CONNECTED)
			continue;
		struct ballot *ballot = &applier->ballot;
		const char *replica_str =
			tt_sprintf("%s at %s", tt_uuid_str(&applier->uuid),
				   applier_uri_str(applier));
		say_info("Checking if %s chose this instance as bootstrap "
			 "leader", replica_str);
		if (tt_uuid_is_nil(&ballot->bootstrap_leader_uuid))
			applier_wait_bootstrap_leader_uuid_is_set(applier);
		if (tt_uuid_compare(&ballot->bootstrap_leader_uuid,
				    &INSTANCE_UUID) != 0) {
			tnt_raise(ClientError, ER_BOOTSTRAP_NOT_UNANIMOUS,
				  tt_uuid_str(&replica->uuid),
				  tt_uuid_str(&ballot->bootstrap_leader_uuid));
		}
	}
}

/** Ensure the configured and stored global identifiers (UUID) match. */
static int
check_global_ids_integrity(void)
{
	char cluster_name[NODE_NAME_SIZE_MAX];
	char replicaset_name[NODE_NAME_SIZE_MAX];
	char instance_name[NODE_NAME_SIZE_MAX];
	struct tt_uuid replicaset_uuid;
	if (box_check_cluster_name(cluster_name) != 0 ||
	    box_check_replicaset_name(replicaset_name) != 0 ||
	    box_check_replicaset_uuid(&replicaset_uuid) != 0 ||
	    box_check_instance_name(instance_name) != 0)
		return -1;

	if (*cluster_name != 0 && strcmp(cluster_name, CLUSTER_NAME) != 0) {
		diag_set(ClientError, ER_CLUSTER_NAME_MISMATCH,
			 node_name_str(cluster_name),
			 node_name_str(CLUSTER_NAME));
		return -1;
	}
	if (*replicaset_name != 0 &&
	    strcmp(replicaset_name, REPLICASET_NAME) != 0) {
		diag_set(ClientError, ER_REPLICASET_NAME_MISMATCH,
			 node_name_str(replicaset_name),
			 node_name_str(REPLICASET_NAME));
		return -1;
	}
	if (!tt_uuid_is_nil(&replicaset_uuid) &&
	    !tt_uuid_is_equal(&replicaset_uuid, &REPLICASET_UUID)) {
		diag_set(ClientError, ER_REPLICASET_UUID_MISMATCH,
			 tt_uuid_str(&replicaset_uuid),
			 tt_uuid_str(&REPLICASET_UUID));
		return -1;
	}
	if (*instance_name != 0 && strcmp(instance_name, INSTANCE_NAME) != 0) {
		diag_set(ClientError, ER_INSTANCE_NAME_MISMATCH,
			 node_name_str(instance_name),
			 node_name_str(INSTANCE_NAME));
		return -1;
	}
	return 0;
}

/**
 * Initialize the first replica of a new replica set.
 */
static void
bootstrap_master(void)
{
	/* Do not allow to bootstrap a readonly instance as master. */
	if (cfg_geti("read_only") == 1) {
		tnt_raise(ClientError, ER_BOOTSTRAP_READONLY);
	}
	/*
	 * With "auto" bootstrap strategy refuse to boot unless everyone agrees
	 * this node is the bootstrap leader.
	 */
	if (bootstrap_strategy == BOOTSTRAP_STRATEGY_AUTO)
		check_bootstrap_unanimity();
	engine_bootstrap_xc();

	uint32_t replica_id = 1;
	box_insert_replica_record(replica_id, &INSTANCE_UUID,
				  cfg_instance_name);
	assert(replica_by_uuid(&INSTANCE_UUID)->id == 1);
	assert(strcmp(cfg_instance_name, INSTANCE_NAME) == 0);
	box_populate_schema_space();

	/* Enable WAL subsystem. */
	if (wal_enable() != 0)
		diag_raise();

	/* Make the initial checkpoint */
	if (gc_checkpoint() != 0)
		panic("failed to create a checkpoint");

	box_run_on_recovery_state(RECOVERY_STATE_SNAPSHOT_RECOVERED);
	box_run_on_recovery_state(RECOVERY_STATE_WAL_RECOVERED);
	assert(!recovery_state_synced_is_reached);
	box_run_on_recovery_state(RECOVERY_STATE_SYNCED);
	recovery_state_synced_is_reached = true;
}

/**
 * Bootstrap from the remote master
 * \pre  master->applier->state == APPLIER_CONNECTED
 * \post master->applier->state == APPLIER_READY
 *
 * \throws an exception in case an unrecoverable error
 * \return false in case of a transient error
 *         true in case everything is fine
 */
static bool
bootstrap_from_master(struct replica *master)
{
	struct applier *applier = master->applier;
	assert(applier != NULL);
	try {
		applier_resume_to_state(applier, APPLIER_READY,
					TIMEOUT_INFINITY);
	} catch (FiberIsCancelled *e) {
		throw e;
	} catch (...) {
		return false;
	}
	assert(applier->state == APPLIER_READY);

	say_info("bootstrapping replica from %s at %s",
		 tt_uuid_str(&master->uuid),
		 sio_strfaddr(&applier->addr, applier->addr_len));

	/*
	 * Send JOIN request to master
	 * See box_process_join().
	 */
	assert(!tt_uuid_is_nil(&INSTANCE_UUID));
	try {
		applier_resume_to_state(applier, APPLIER_FETCH_SNAPSHOT,
					TIMEOUT_INFINITY);
	} catch (FiberIsCancelled *e) {
		throw e;
	} catch (...) {
		return false;
	}

	/*
	 * Process initial data (snapshot or dirty disk data).
	 */
	engine_begin_initial_recovery_xc(NULL);
	enum applier_state wait_state = cfg_replication_anon ?
					APPLIER_FETCHED_SNAPSHOT :
					APPLIER_FINAL_JOIN;
	applier_resume_to_state(applier, wait_state, TIMEOUT_INFINITY);

	box_run_on_recovery_state(RECOVERY_STATE_SNAPSHOT_RECOVERED);

	/*
	 * Process final data (WALs).
	 */
	engine_begin_final_recovery_xc();
	recovery_journal_create(&replicaset.vclock);

	if (!cfg_replication_anon) {
		applier_resume_to_state(applier, APPLIER_JOINED,
					TIMEOUT_INFINITY);
	}
	/* Finalize the new replica */
	engine_end_recovery_xc();

	/* Switch applier to initial state */
	applier_resume_to_state(applier, APPLIER_READY, TIMEOUT_INFINITY);
	assert(applier->state == APPLIER_READY);

	/*
	 * An engine may write to WAL on its own during the join
	 * stage (e.g. Vinyl's deferred DELETEs). That's OK - those
	 * records will pass through the recovery journal and wind
	 * up in the initial checkpoint. However, we must enable
	 * the WAL right before starting checkpointing so that
	 * records written during and after the initial checkpoint
	 * go to the real WAL and can be recovered after restart.
	 * This also clears the recovery journal created on stack.
	 */
	if (wal_enable() != 0)
		diag_raise();

	/* Make the initial checkpoint */
	if (gc_checkpoint() != 0)
		panic("failed to create a checkpoint");

	box_run_on_recovery_state(RECOVERY_STATE_WAL_RECOVERED);

	return true;
}

/**
 * Bootstrap a new instance either as the first master in a
 * replica set or as a replica of an existing master.
 *
 * @param[out] is_bootstrap_leader  set if this instance is
 *                                  the leader of a new cluster
 */
static void
bootstrap(bool *is_bootstrap_leader)
{
	struct tt_uuid instance_uuid;
	if (box_check_instance_uuid(&instance_uuid) != 0)
		diag_raise();

	assert(tt_uuid_is_nil(&INSTANCE_UUID));
	if (!tt_uuid_is_nil(&instance_uuid))
		INSTANCE_UUID = instance_uuid;
	else
		tt_uuid_create(&INSTANCE_UUID);

	replicaset_state = REPLICASET_BOOTSTRAP;
	box_broadcast_id();
	box_broadcast_ballot();
	say_info("instance uuid %s", tt_uuid_str(&INSTANCE_UUID));

	/*
	 * Begin listening on the socket to enable
	 * master-master replication leader election.
	 */
	if (box_listen() != 0)
		diag_raise();
	/*
	 * Wait for the cluster to start up.
	 *
	 * Note, when bootstrapping a new instance, we try to
	 * connect to all masters during timeout to make sure
	 * all replicas recieve the same replica set UUID when
	 * a new cluster is deployed. If we fail to do so, settle
	 * with connecting to 'replication_connect_quorum' masters.
	 * If this also fails, throw an error.
	 */
	struct replica *master;
	/*
	 * Rebootstrap
	 *
	 * 1) Try to connect to all nodes in the replicaset during
	 *    the waiting period in order to update their ballot.
	 *
	 * 2) After updated the ballot of all nodes, it are looking
	 *    for a new master.
	 *
	 * 3) Wait until bootstrap_from_master succeeds.
	 */
	double timeout = replication_timeout;
	bool say_once = false;

	while (true) {
		box_restart_replication();
		master = replicaset_find_join_master();
		bootstrap_leader_uuid = master == NULL ? INSTANCE_UUID :
							 master->uuid;
		box_broadcast_ballot();

		if (master == NULL ||
		    tt_uuid_is_equal(&master->uuid, &INSTANCE_UUID)) {
			bootstrap_master();
			*is_bootstrap_leader = true;
			break;
		}
		if (bootstrap_from_master(master)) {
			if (check_global_ids_integrity() != 0)
				diag_raise();
			*is_bootstrap_leader = false;
			break;
		}
		if (!say_once) {
			say_info("rebootstrap failed, will retry "
				 "every %.2lf second", timeout);
		}

		say_once = true;
		fiber_sleep(timeout);
	}
}

/**
 * Recover the instance from the local directory.
 * Enter hot standby if the directory is locked.
 * Invoke rebootstrap if the instance fell too much
 * behind its peers in the replica set and needs
 * to be rebootstrapped.
 */
static void
local_recovery(const struct vclock *checkpoint_vclock)
{
	assert(!tt_uuid_is_nil(&INSTANCE_UUID));
	struct tt_uuid instance_uuid;
	if (box_check_instance_uuid(&instance_uuid) != 0)
		diag_raise();

	replicaset_state = REPLICASET_RECOVERY;
	if (!tt_uuid_is_nil(&instance_uuid) &&
	    !tt_uuid_is_equal(&instance_uuid, &INSTANCE_UUID)) {
		tnt_raise(ClientError, ER_INSTANCE_UUID_MISMATCH,
			  tt_uuid_str(&instance_uuid),
			  tt_uuid_str(&INSTANCE_UUID));
	}

	say_info("instance uuid %s", tt_uuid_str(&INSTANCE_UUID));

	struct wal_stream wal_stream;
	wal_stream_create(&wal_stream);
	auto stream_guard = make_scoped_guard([&]{
		wal_stream_abort(&wal_stream);
		wal_stream_destroy(&wal_stream);
	});
	struct recovery *recovery = recovery_new(
		wal_dir(), box_is_force_recovery, checkpoint_vclock);
	/*
	 * Make sure we report the actual recovery position
	 * in box.info while local recovery is in progress.
	 */
	box_vclock = &recovery->vclock;
	auto guard = make_scoped_guard([&]{
		box_vclock = &replicaset.vclock;
		recovery_delete(recovery);
	});

	/*
	 * Initialize the replica set vclock from recovery.
	 * The local WAL may contain rows from remote masters,
	 * so we must reflect this in replicaset vclock to
	 * not attempt to apply these rows twice.
	 */
	recovery_scan(recovery, &replicaset.vclock, &gc.vclock,
		      &wal_stream.base);
	box_broadcast_ballot();
	say_info("instance vclock %s", vclock_to_string(&replicaset.vclock));

	if (wal_dir_lock >= 0) {
		if (box_listen() != 0)
			diag_raise();
		box_update_replication();

		struct replica *master;
		double timeout = replication_timeout;
		bool say_once = false;

		while (replicaset_needs_rejoin(&master)) {
			bootstrap_leader_uuid = master->uuid;
			box_broadcast_ballot();
			if (!say_once) {
				say_crit("replica is too old, initiating "
					 "rebootstrap");
			}

			if (bootstrap_from_master(master))
				return;
			box_restart_replication();

			if (!say_once) {
				say_info("rebootstrap failed, will retry "
					 "every %.2lf second", timeout);
			}

			say_once = true;
			fiber_sleep(timeout);
		}
	}

	/*
	 * recovery->vclock is needed by Vinyl to filter
	 * WAL rows that were dumped before restart.
	 *
	 * XXX: Passing an internal member of the recovery
	 * object to an engine is an ugly hack. Instead we
	 * should introduce space_vtab::apply_wal_row method
	 * and explicitly pass the statement LSN to it.
	 */
	engine_begin_initial_recovery_xc(&recovery->vclock);

	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");
	assert(memtx != NULL);

	recovery_journal_create(&recovery->vclock);

	/*
	 * We explicitly request memtx to recover its
	 * snapshot as a separate phase since it contains
	 * data for system spaces, and triggers on
	 * recovery of system spaces issue DDL events in
	 * other engines.
	 */
	memtx_engine_recover_snapshot_xc(memtx, checkpoint_vclock);

	box_run_on_recovery_state(RECOVERY_STATE_SNAPSHOT_RECOVERED);

	engine_begin_final_recovery_xc();
	recover_remaining_wals(recovery, &wal_stream.base, NULL, false);
	if (wal_stream_has_unfinished_tx(&wal_stream)) {
		diag_set(XlogError, "found a not finished transaction "
			 "in the log");
		wal_stream_abort(&wal_stream);
		if (!box_is_force_recovery)
			diag_raise();
		diag_log();
	}
	/*
	 * Leave hot standby mode, if any, only after
	 * acquiring the lock.
	 */
	if (wal_dir_lock < 0) {
		title("hot_standby");
		say_info("Entering hot standby mode");
		engine_begin_hot_standby_xc();
		recovery_follow_local(recovery, &wal_stream.base, "hot_standby",
				      cfg_getd("wal_dir_rescan_delay"));
		while (!fiber_is_cancelled()) {
			if (path_lock(wal_dir(), &wal_dir_lock))
				diag_raise();
			if (wal_dir_lock >= 0)
				break;
			fiber_sleep(0.1);
		}
		recovery_stop_local(recovery);
		fiber_testcancel();
		recover_remaining_wals(recovery, &wal_stream.base, NULL, true);
		if (wal_stream_has_unfinished_tx(&wal_stream)) {
			diag_set(XlogError, "found a not finished transaction "
				 "in the log in hot standby mode");
			wal_stream_abort(&wal_stream);
			if (!box_is_force_recovery)
				diag_raise();
			diag_log();
		}
		/*
		 * Advance replica set vclock to reflect records
		 * applied in hot standby mode.
		 */
		vclock_copy(&replicaset.vclock, &recovery->vclock);
		if (box_listen() != 0)
			diag_raise();
		box_update_replication();
	} else if (vclock_compare(&replicaset.vclock, &recovery->vclock) != 0) {
		/*
		 * There are several reasons for a node to recover a vclock not
		 * matching the one scanned initially:
		 *
		 * 1) someone else might append the files we are reading.
		 *    This shouldn't typically happen with Tarantool >= 1.7.x,
		 *    because it takes a lock on WAL directory.
		 *    But it's still possible in some crazy set up, for example
		 *    one could recover from another instance's symlinked xlogs.
		 *
		 * 2) Non-matching (by signature) snaps and xlogs, i.e.:
		 *    a) snaps from a remote instance together with local
		 *       istance's xlogs.
		 *    b) snaps/xlogs from Tarantool 1.6.
		 *
		 * The second case could be distinguished from the first one,
		 * but this would require unnecessarily re-reading an xlog
		 * preceding the latest snap, just to make sure the WAL doesn't
		 * span over the snap creation signature.
		 *
		 * Allow both cases, if the user has set force_recovery.
		 */
		const char *mismatch_str =
			tt_sprintf("Replicaset vclock %s doesn't match "
				   "recovered data %s",
				   vclock_to_string(&replicaset.vclock),
				   vclock_to_string(&recovery->vclock));
		if (box_is_force_recovery) {
			say_warn("%s: ignoring, because 'force_recovery' "
				 "configuration option is set.", mismatch_str);
			vclock_copy(&replicaset.vclock, &recovery->vclock);
		} else {
			panic("Can't proceed. %s.", mismatch_str);
		}
	}
	stream_guard.is_active = false;
	recovery_finalize(recovery);
	wal_stream_destroy(&wal_stream);

	/*
	 * We must enable WAL before finalizing engine recovery,
	 * because an engine may start writing to WAL right after
	 * this point (e.g. deferred DELETE statements in Vinyl).
	 * This also clears the recovery journal created on stack.
	 */
	if (wal_enable() != 0)
		diag_raise();

	engine_end_recovery_xc();
	if (check_global_ids_integrity() != 0)
		diag_raise();
	box_run_on_recovery_state(RECOVERY_STATE_WAL_RECOVERED);
}

static void
tx_prio_cb(struct ev_loop *loop, ev_watcher *watcher, int events)
{
	(void) loop;
	(void) events;
	struct cbus_endpoint *endpoint = (struct cbus_endpoint *)watcher->data;
#ifndef NDEBUG
	/*
	 * The sleep is legal because it is not a fiber sleep. It puts the
	 * entire thread to sleep to simulate it being slow. It can happen in
	 * reality if the thread somewhy isn't scheduled for too long.
	 */
	struct errinj *inj = errinj(ERRINJ_TX_DELAY_PRIO_ENDPOINT,
				    ERRINJ_DOUBLE);
	if (inj->dparam != 0)
		usleep(inj->dparam * 1000000);
#endif
	cbus_process(endpoint);
}

static void
on_wal_garbage_collection(const struct vclock *vclock)
{
	gc_advance(vclock);
}

static void
on_wal_checkpoint_threshold(void)
{
	say_info("WAL threshold exceeded, triggering checkpoint");
	gc_trigger_checkpoint();
}

bool
box_is_configured(void)
{
	return is_box_configured;
}

int
box_check_configured(void)
{
	if (!is_box_configured) {
		diag_set(ClientError, ER_UNCONFIGURED);
		return -1;
	}
	return 0;
}

static void
box_cfg_xc(void)
{
	box_set_force_recovery();
	box_storage_init();
	title("loading");

	if (box_set_prepared_stmt_cache_size() != 0)
		diag_raise();
	box_set_net_msg_max();
	box_set_readahead();
	box_set_too_long_threshold();
	box_set_replication_timeout();
	if (box_set_bootstrap_strategy() != 0)
		diag_raise();
	if (box_set_bootstrap_leader() != 0)
		diag_raise();
	box_set_replication_connect_timeout();
	if (bootstrap_strategy == BOOTSTRAP_STRATEGY_LEGACY)
		box_set_replication_connect_quorum();
	box_set_replication_sync_lag();
	if (box_set_replication_synchro_quorum() != 0)
		diag_raise();
	if (box_set_replication_synchro_timeout() != 0)
		diag_raise();
	box_set_replication_sync_timeout();
	box_set_replication_skip_conflict();
	if (box_check_instance_name(cfg_instance_name) != 0)
		diag_raise();
	if (box_set_wal_queue_max_size() != 0)
		diag_raise();
	cfg_replication_anon = box_check_replication_anon();
	box_broadcast_ballot();
	/*
	 * Must be set before opening the server port, because it may be
	 * requested by a client before the configuration is completed.
	 */
	box_set_auth_type();

	struct gc_checkpoint *checkpoint = gc_last_checkpoint();

	/*
	 * Lock the write ahead log directory to avoid multiple
	 * instances running in the same dir.
	 */
	if (path_lock(wal_dir(), &wal_dir_lock) < 0)
		diag_raise();
	if (wal_dir_lock < 0) {
		/**
		 * The directory is busy and hot standby mode is off:
		 * refuse to start. In hot standby mode, a busy
		 * WAL dir must contain at least one xlog.
		 */
		if (!cfg_geti("hot_standby") || checkpoint == NULL)
			tnt_raise(ClientError, ER_ALREADY_RUNNING, wal_dir());
	}

	struct journal bootstrap_journal;
	journal_create(&bootstrap_journal, NULL, bootstrap_journal_write);
	journal_set(&bootstrap_journal);
	auto bootstrap_journal_guard = make_scoped_guard([] {
		journal_set(NULL);
	});

	bool is_bootstrap_leader = false;
	if (checkpoint != NULL) {
		/* Recover the instance from the local directory */
		local_recovery(&checkpoint->vclock);
	} else {
		/* Bootstrap a new instance */
		bootstrap(&is_bootstrap_leader);
	}
	replicaset_state = REPLICASET_READY;

	/*
	 * replicaset.applier.vclock is filled with real
	 * value where local restore has already completed
	 */
	vclock_copy(&replicaset.applier.vclock, &replicaset.vclock);

	/*
	 * Exclude self from GC delay because we care
	 * about remote replicas only, still for ref/unref
	 * balance we do reference self node initially and
	 * downgrade it to zero when there is no replication
	 * set at all.
	 */
	gc_delay_unref();

	bootstrap_journal_guard.is_active = false;
	assert(current_journal != &bootstrap_journal);

	/*
	 * Check for correct registration of the instance in _cluster
	 * The instance won't exist in _cluster space if it is an
	 * anonymous replica, add it manually.
	 */
	if (cfg_replication_anon != box_is_anon())
		panic("'replication_anon' cfg didn't work");
	struct replica *self = replica_by_uuid(&INSTANCE_UUID);
	if (!cfg_replication_anon) {
		if (self == NULL || self->id == REPLICA_ID_NIL) {
			tnt_raise(ClientError, ER_UNKNOWN_REPLICA,
				  tt_uuid_str(&INSTANCE_UUID),
				  tt_uuid_str(&REPLICASET_UUID));
		}
	} else if (self == NULL) {
		replicaset_add_anon(&INSTANCE_UUID);
	}

	rmean_cleanup(rmean_box);

	/* Follow replica */
	replicaset_follow();

	is_box_configured = true;
	box_broadcast_ballot();
	/*
	 * Fill in leader election parameters after bootstrap. Before it is not
	 * possible - there may be relevant data to recover from WAL and
	 * snapshot. Also until recovery is done, it is not possible to write
	 * new records into WAL. Another reason - before recovery is done,
	 * instance_id is not known, so Raft simply can't work.
	 */
	struct raft *raft = box_raft();
	if (!cfg_replication_anon)
		raft_cfg_instance_id(raft, instance_id);
	raft_cfg_vclock(raft, &replicaset.vclock);

	if (box_set_election_timeout() != 0)
		diag_raise();
	if (box_set_election_fencing_mode() != 0)
		diag_raise();
	/*
	 * Election is enabled last. So as all the parameters are installed by
	 * that time.
	 */
	if (box_set_election_mode() != 0)
		diag_raise();

	/*
	 * Enable split brain detection once node is fully recovered or
	 * bootstrapped. No split brain could happen during bootstrap or local
	 * recovery. Only do so in an upgraded cluster. Unfortunately, schema
	 * version 2.10.1 was used in 2.10.0 release, while split-brain
	 * detection appeared in 2.10.1. So use the schema version after 2.10.1.
	 */
	if (dd_version_id > version_id(2, 10, 1))
		txn_limbo_filter_enable(&txn_limbo);

	/*
	 * If we have no space _gc_consumers, wal_cleanup_delay is set to zero
	 * and we have registered replicas (except for self), we should set
	 * wal_cleanup_delay to non-zero value to prevent xlogs from being
	 * deleted.
	 * Note that we do this after recovery - space _gc_consumers is
	 * recovered then if it was in snapshot.
	 */
	if (!gc_consumer_is_persistent() &&
	    box_check_wal_cleanup_delay() == 0 &&
	    replicaset.registered_count > 1) {
		int wal_cleanup_delay_default = 4 * 3600;
		say_warn("Current schema does not support persistent WAL GC "
			 "state, so wal_cleanup_delay option is automatically "
			 "set to %d to prevent xlogs needed for replicas from "
			 "being cleaned up. Please re-configure it after "
			 "schema upgrade if it is needed.",
			 wal_cleanup_delay_default);
		gc_set_wal_cleanup_delay(4 * 3600);
	} else {
		if (box_set_wal_cleanup_delay() != 0)
			diag_raise();
	}

	title("running");
	say_info("ready to accept requests");

	if (!is_bootstrap_leader) {
		replicaset_sync();
	} else if (box_election_mode == ELECTION_MODE_CANDIDATE ||
		   box_election_mode == ELECTION_MODE_MANUAL) {
		/*
		 * When the cluster is just bootstrapped and this instance is a
		 * leader, it makes no sense to wait for a leader appearance.
		 * There is no one. Moreover this node *is* a leader, so it
		 * should take the control over the situation and start a new
		 * term immediately.
		 */
		int rc = box_raft_try_promote();
		if (raft->leader != instance_id && raft->leader != 0) {
			/*
			 * It was promoted and is a single registered node -
			 * there can't be another leader or a new term bump.
			 */
			panic("Bootstrap master couldn't elect self as a "
			      "leader. Leader is %u, term is %llu",
			      raft->leader, (long long)raft->volatile_term);
		}
		assert(rc == 0);
		(void)rc;
	}

	/* box.cfg.read_only is not read yet. */
	assert(box_is_ro());
	/* If anyone is waiting for ro mode. */
	fiber_cond_broadcast(&ro_cond);
	/*
	 * Yield to let ro condition waiters to handle the event.
	 * Without yield it may happen there won't be a context
	 * switch until the ro state is changed again, and as a
	 * result, some ro waiters may sleep forever. For example,
	 * when Tarantool is just started, it is expected it will
	 * enter ro=true state, and then ro=false. Without the
	 * yield the ro=true event may be lost. This affects
	 * box.ctl.wait_ro() call.
	 */
	fiber_sleep(0);
}

void
box_cfg(void)
{
	try {
		box_cfg_xc();
	} catch (Exception *e) {
		e->log();
		panic("can't initialize storage: %s", e->get_errmsg());
	}
}

int
box_checkpoint(void)
{
	assert(is_box_configured);
	return gc_checkpoint();
}

void
box_checkpoint_async(void)
{
	if (!is_box_configured || is_storage_shutdown)
		return;
	gc_trigger_checkpoint();
}

int
box_backup_start(int checkpoint_idx, box_backup_cb cb, void *cb_arg)
{
	assert(checkpoint_idx >= 0);
	if (backup_is_in_progress) {
		diag_set(ClientError, ER_BACKUP_IN_PROGRESS);
		return -1;
	}
	struct gc_checkpoint *checkpoint;
	gc_foreach_checkpoint_reverse(checkpoint) {
		if (checkpoint_idx-- == 0)
			break;
	}
	if (checkpoint_idx >= 0) {
		diag_set(ClientError, ER_MISSING_SNAPSHOT);
		return -1;
	}
	backup_is_in_progress = true;
	gc_ref_checkpoint(checkpoint, &backup_gc, "backup");
	int rc = engine_backup(&checkpoint->vclock, cb, cb_arg);
	if (rc != 0) {
		gc_unref_checkpoint(&backup_gc);
		backup_is_in_progress = false;
	}
	return rc;
}

void
box_backup_stop(void)
{
	if (backup_is_in_progress) {
		gc_unref_checkpoint(&backup_gc);
		backup_is_in_progress = false;
	}
}

const char *
box_status(void)
{
    return status;
}

static int
box_reset_space_stat(struct space *space, void *arg)
{
	(void)arg;
	for (uint32_t i = 0; i < space->index_count; i++)
		index_reset_stat(space->index[i]);
	return 0;
}

void
box_reset_stat(void)
{
	rmean_cleanup(rmean_box);
	rmean_cleanup(rmean_error);
	engine_reset_stat();
	space_foreach(box_reset_space_stat, NULL);
}

static void
builtin_events_init(void)
{
	box_broadcast_fmt("box.id", "{}");
	box_broadcast_fmt("box.schema", "{}");
	box_broadcast_fmt("box.status", "{}");
	box_broadcast_fmt("box.election", "{}");
	box_broadcast_fmt("box.wal_error", "{}");
	box_broadcast_fmt(box_ballot_event_key, "{}");
	ev_timer_init(&box_broadcast_ballot_timer,
		      box_broadcast_ballot_on_timeout, 0, 0);
}

static void
builtin_events_free(void)
{
	ev_timer_stop(loop(), &box_broadcast_ballot_timer);
}

void
box_broadcast_id(void)
{
	char buf[1024];
	char *w = buf;
	w = mp_encode_map(w, 6);
	w = mp_encode_str0(w, "id");
	w = mp_encode_uint(w, instance_id);
	w = mp_encode_str0(w, "instance_uuid");
	w = mp_encode_uuid(w, &INSTANCE_UUID);
	w = mp_encode_str0(w, "instance_name");
	if (*INSTANCE_NAME == 0)
		w = mp_encode_nil(w);
	else
		w = mp_encode_str0(w, INSTANCE_NAME);
	w = mp_encode_str0(w, "replicaset_uuid");
	w = mp_encode_uuid(w, &REPLICASET_UUID);
	w = mp_encode_str0(w, "replicaset_name");
	if (*REPLICASET_NAME == 0)
		w = mp_encode_nil(w);
	else
		w = mp_encode_str0(w, REPLICASET_NAME);
	w = mp_encode_str0(w, "cluster_name");
	if (*CLUSTER_NAME == 0)
		w = mp_encode_nil(w);
	else
		w = mp_encode_str0(w, CLUSTER_NAME);

	box_broadcast("box.id", strlen("box.id"), buf, w);

	assert((size_t)(w - buf) < sizeof(buf));
}

static void
box_broadcast_status(void)
{
	char buf[1024];
	char *w = buf;
	w = mp_encode_map(w, 3);
	w = mp_encode_str0(w, "is_ro");
	w = mp_encode_bool(w, box_is_ro());
	w = mp_encode_str0(w, "is_ro_cfg");
	w = mp_encode_bool(w, cfg_geti("read_only"));
	w = mp_encode_str0(w, "status");
	w = mp_encode_str0(w, box_status());

	box_broadcast("box.status", strlen("box.status"), buf, w);

	assert((size_t)(w - buf) < sizeof(buf));
}

void
box_broadcast_election(void)
{
	struct raft *raft = box_raft();

	char buf[1024];
	char *w = buf;
	w = mp_encode_map(w, 4);
	w = mp_encode_str0(w, "term");
	w = mp_encode_uint(w, raft->term);
	w = mp_encode_str0(w, "role");
	w = mp_encode_str0(w, raft_state_str(raft->state));
	w = mp_encode_str0(w, "is_ro");
	w = mp_encode_bool(w, box_is_ro());
	w = mp_encode_str0(w, "leader");
	w = mp_encode_uint(w, raft->leader);

	box_broadcast("box.election", strlen("box.election"), buf, w);

	assert((size_t)(w - buf) < sizeof(buf));
}

void
box_broadcast_schema(void)
{
	char buf[1024];
	char *w = buf;
	w = mp_encode_map(w, 1);
	w = mp_encode_str0(w, "version");
	w = mp_encode_uint(w, box_schema_version());

	box_broadcast("box.schema", strlen("box.schema"), buf, w);

	assert((size_t)(w - buf) < sizeof(buf));
}

void
box_broadcast_ballot(void)
{
	char buf[2048];
	char *w = buf;
	struct ballot ballot;
	box_process_vote(&ballot);
	w = mp_encode_ballot(w, &ballot);

	box_broadcast(box_ballot_event_key, strlen(box_ballot_event_key), buf,
		      w);

	assert(mp_sizeof_ballot_max(&ballot) < sizeof(buf));
}

void
box_read_ffi_disable(void)
{
	assert(box_read_ffi_is_disabled == (box_read_ffi_disable_count > 0));
	box_read_ffi_disable_count++;
	box_read_ffi_is_disabled = true;
}

void
box_read_ffi_enable(void)
{
	assert(box_read_ffi_is_disabled);
	assert(box_read_ffi_disable_count > 0);
	if (--box_read_ffi_disable_count == 0)
		box_read_ffi_is_disabled = false;
}

int
box_generate_space_id(uint32_t *new_space_id, bool is_temporary)
{
	assert(new_space_id != NULL);
	uint32_t id_range_begin = !is_temporary ?
		BOX_SYSTEM_ID_MAX + 1 : BOX_SPACE_ID_TEMPORARY_MIN;
	uint32_t id_range_end = !is_temporary ?
		(uint32_t)BOX_SPACE_ID_TEMPORARY_MIN :
		(uint32_t)BOX_SPACE_MAX + 1;
	char key_buf[16];
	char *key_end = key_buf;
	key_end = mp_encode_array(key_end, 1);
	key_end = mp_encode_uint(key_end, id_range_end);
	struct credentials *orig_credentials = effective_user();
	fiber_set_user(fiber(), &admin_credentials);
	auto guard = make_scoped_guard([=] {
		fiber_set_user(fiber(), orig_credentials);
	});
	box_iterator_t *it = box_index_iterator(BOX_SPACE_ID, 0, ITER_LT,
						key_buf, key_end);
	if (it == NULL)
		return -1;
	struct tuple *res = NULL;
	int rc = box_iterator_next(it, &res);
	box_iterator_free(it);
	if (rc != 0)
		return -1;
	assert(res != NULL);
	uint32_t max_id = 0;
	rc = tuple_field_u32(res, 0, &max_id);
	assert(rc == 0);
	if (max_id < id_range_begin)
		max_id = id_range_begin - 1;
	*new_space_id = space_cache_find_next_unused_id(max_id);
	/* Try again if overflowed. */
	if (*new_space_id >= id_range_end) {
		*new_space_id =
			space_cache_find_next_unused_id(id_range_begin - 1);
		/*
		 * The second overflow means all ids are occupied.
		 * This situation cannot happen in real world with limited
		 * memory, and its pretty hard to test it, so let's just panic
		 * if we've run out of ids.
		 */
		if (*new_space_id >= id_range_end)
			panic("Space id limit is reached");
	}
	return 0;
}

static void
on_garbage_collection(void)
{
	box_broadcast_ballot();
}

static void
box_storage_init(void)
{
	assert(!is_storage_initialized);
	/* Join the cord interconnect as "tx" endpoint. */
	fiber_pool_create(&tx_fiber_pool, "tx",
			  IPROTO_MSG_MAX_MIN * IPROTO_FIBER_POOL_SIZE_FACTOR,
			  box_fiber_pool_idle_timeout);
	/* Add an extra endpoint for WAL wake up/rollback messages. */
	cbus_endpoint_create(&tx_prio_endpoint, "tx_prio", tx_prio_cb,
			     &tx_prio_endpoint);

	rmean_box = rmean_new(iproto_type_strs, IPROTO_TYPE_STAT_MAX);
	rmean_error = rmean_new(rmean_error_strings, RMEAN_ERROR_LAST);

	gc_init(on_garbage_collection);
	engine_init();
	schema_init();
	replication_init(cfg_geti_default("replication_threads", 1));
	iproto_init(cfg_geti("iproto_threads"));
	sql_init();
	audit_log_init();
	security_cfg();

	int64_t wal_max_size = box_check_wal_max_size(
		cfg_geti64("wal_max_size"));
	enum wal_mode wal_mode = box_check_wal_mode(cfg_gets("wal_mode"));
	double wal_retention_period = box_check_wal_retention_period_xc();
	if (wal_init(wal_mode, cfg_gets("wal_dir"), wal_max_size,
		     wal_retention_period, &INSTANCE_UUID,
		     on_wal_garbage_collection,
		     on_wal_checkpoint_threshold) != 0) {
		diag_raise();
	}
	is_storage_initialized = true;
}

static void
box_storage_free(void)
{
	if (!is_storage_initialized)
		return;
	iproto_free();
	replication_free();
	gc_free();
	engine_free();
	/* schema_free(); */
	wal_free();
	flightrec_free();
	audit_log_free();
	sql_built_in_functions_cache_free();
	/* fiber_pool_destroy(&tx_fiber_pool); */
	is_storage_initialized = false;
}

void
box_init(void)
{
	iproto_constants_init();
	port_init();
	box_on_recovery_state_event =
		event_get("box.ctl.on_recovery_state", true);
	event_ref(box_on_recovery_state_event);
	box_on_shutdown_event = event_get("box.ctl.on_shutdown", true);
	event_ref(box_on_shutdown_event);
	tarantool_trigger_on_change_event =
		event_get("tarantool.trigger.on_change", true);
	event_ref(tarantool_trigger_on_change_event);
	event_on_change(&box_trigger_on_change_trigger);
	txn_event_trigger_init();
	msgpack_init();
	fiber_cond_create(&ro_cond);
	auth_init();
	security_init();
	space_cache_init();
	user_cache_init();
	/*
	 * The order is important: to initialize sessions, we need to access the
	 * admin user, which is used as a default session user when running
	 * triggers.
	 */
	session_init();
	schema_module_init();
	if (tuple_init(lua_hash) != 0)
		diag_raise();
	txn_limbo_init();
	sequence_init();
	box_watcher_init();
	box_raft_init();
	wal_ext_init();
	/*
	 * Default built-in events to help users distinguish an event being not
	 * supported from box.cfg not being called yet.
	 */
	builtin_events_init();
	crash_callback = box_crash_callback;
	mempool_create(&sync_trigger_data_pool, &cord()->slabc,
		       sizeof(struct sync_trigger_data));
}

/** Shutdown box storage i.e. stop parts that need TX loop running. */
static void
box_storage_shutdown()
{
	if (!is_storage_initialized)
		return;
	is_storage_shutdown = true;
	if (iproto_shutdown(box_shutdown_timeout) != 0) {
		diag_log();
		panic("cannot gracefully shutdown iproto");
	}
	box_watcher_shutdown();
	/*
	 * Finish client fibers after iproto_shutdown otherwise new fibers
	 * can be started through new iproto requests. Also we should
	 * finish client fibers before other subsystems shutdown so that
	 * we won't need to handle requests from client fibers after/during
	 * subsystem shutdown.
	 */
	if (fiber_shutdown(box_shutdown_timeout) != 0) {
		diag_log();
		panic("cannot gracefully shutdown client fibers");
	}
	replication_shutdown();
	gc_shutdown();
	engine_shutdown();
}

void
box_shutdown(void)
{
	box_storage_shutdown();
}

void
box_free(void)
{
	box_storage_free();
	builtin_events_free();
	security_free();
	auth_free();
	wal_ext_free();
	box_watcher_free();
	box_raft_free();
	sequence_free();
	trigger_clear(&box_trigger_on_change_trigger);
	event_unref(box_on_recovery_state_event);
	box_on_recovery_state_event = NULL;
	event_unref(tarantool_trigger_on_change_event);
	tarantool_trigger_on_change_event = NULL;
	txn_event_trigger_free();
	tuple_free();
	port_free();
	iproto_constants_free();
	mempool_destroy(&sync_trigger_data_pool);
	/* schema_module_free(); */
	/* session_free(); */
	/* user_cache_free(); */
	/* space_cache_destroy(); */
}
