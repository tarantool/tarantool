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
#include "recovery.h"

#include "scoped_guard.h"
#include "fiber.h"
#include "bootstrap.h"
#include "xlog.h"
#include "xrow.h"

#include "cluster.h"
#include "session.h"

/*
 * Recovery subsystem
 * ------------------
 *
 * A facade of the recovery subsystem is struct recovery.
 *
 * Depending on the configuration, start-up parameters, the
 * actual task being performed, the recovery can be
 * in a different state.
 *
 * The main factors influencing recovery state are:
 * - temporal: whether or not the instance is just booting
 *   from a snapshot, is in 'local hot standby mode', or
 *   is already accepting requests
 * - task based: whether it's a master process,
 *   snapshot saving process or a replication relay.
 *
 * Depending on the above factors, recovery can be in two main
 * operation modes: "read mode", recovering in-memory state
 * from existing data, and "write mode", i.e. recording on
 * disk changes of the in-memory state.
 *
 * Let's enumerate all possible distinct states of recovery:
 *
 * Read mode
 * ---------
 * IR - initial recovery, initiated right after server start:
 * reading data from the snapshot and existing WALs
 * and restoring the in-memory state
 * IRR - initial replication relay mode, reading data from
 * existing WALs (xlogs) and sending it to the client.
 *
 * HS - standby mode, entered once all existing WALs are read:
 * following the WAL directory for all changes done by the master
 * and updating the in-memory state
 * RR - replication relay, following the WAL directory for all
 * changes done by the master and sending them to the
 * replica
 *
 * Write mode
 * ----------
 * M - master mode, recording in-memory state changes in the WAL
 * R - replica mode, receiving changes from the master and
 * recording them in the WAL
 *
 * The following state transitions are possible/supported:
 *
 * recovery_init() -> IR | IRR # recover()
 * IR -> HS         # recovery_follow_local()
 * IRR -> RR        # recovery_follow_local()
 * HS -> M          # recovery_finalize()
 * M -> R           # remote_start()
 * R -> M           # remote_stop()
 */

/* {{{ LSN API */

void
recovery_fill_lsn(struct recovery *r, struct xrow_header *row)
{
	if (row->server_id == 0) {
		/* Local request. */
		row->server_id = r->server_id;
		row->lsn = vclock_inc(&r->vclock, r->server_id);
	} else {
		/* Replication request. */
		if (!vclock_has(&r->vclock, row->server_id)) {
			/*
			 * A safety net, this can only occur
			 * if we're fed a strangely broken xlog.
			 */
			tnt_raise(ClientError, ER_UNKNOWN_SERVER,
				  int2str(row->server_id));
		}
		vclock_follow(&r->vclock,  row->server_id, row->lsn);
	}
}

int64_t
recovery_last_checkpoint(struct recovery *r)
{
	/* recover last snapshot lsn */
	struct vclock *vclock = vclockset_last(&r->snap_dir.index);
	return vclock ? vclock_sum(vclock) : -1;
}

/* }}} */

/* {{{ Initial recovery */

/**
 * Throws an exception in  case of error.
 */
struct recovery *
recovery_new(const char *snap_dirname, const char *wal_dirname,
	     apply_row_f apply_row, void *apply_row_param)
{
	struct recovery *r = (struct recovery *)
			calloc(1, sizeof(*r));

	if (r == NULL) {
		tnt_raise(OutOfMemory, sizeof(*r), "malloc",
			  "struct recovery");
	}

	auto guard = make_scoped_guard([=]{
		free(r);
	});

	recovery_update_mode(r, WAL_NONE);

	r->apply_row = apply_row;
	r->apply_row_param = apply_row_param;
	r->snap_io_rate_limit = UINT64_MAX;

	xdir_create(&r->snap_dir, snap_dirname, SNAP, &r->server_uuid);

	xdir_create(&r->wal_dir, wal_dirname, XLOG, &r->server_uuid);

	vclock_create(&r->vclock);

	xdir_scan(&r->snap_dir);
	/**
	 * Avoid scanning WAL dir before we recovered
	 * the snapshot and know server UUID - this will
	 * make sure the scan skips files with wrong
	 * UUID, see replication/cluster.test for
	 * details.
	 */
	xdir_check(&r->wal_dir);

	r->watcher = NULL;

	guard.is_active = false;
	return r;
}

void
recovery_update_mode(struct recovery *r, enum wal_mode mode)
{
	assert(mode < WAL_MODE_MAX);
	r->wal_mode = mode;
}

void
recovery_update_io_rate_limit(struct recovery *r, double new_limit)
{
	r->snap_io_rate_limit = new_limit * 1024 * 1024;
	if (r->snap_io_rate_limit == 0)
		r->snap_io_rate_limit = UINT64_MAX;
}

void
recovery_setup_panic(struct recovery *r, bool on_snap_error,
		     bool on_wal_error)
{
	r->wal_dir.panic_if_error = on_wal_error;
	r->snap_dir.panic_if_error = on_snap_error;
}

static inline void
recovery_close_log(struct recovery *r)
{
	if (r->current_wal == NULL)
		return;
	if (r->current_wal->eof_read) {
		say_info("done `%s'", r->current_wal->filename);
	} else {
		say_warn("file `%s` wasn't correctly closed",
			 r->current_wal->filename);
	}
	xlog_close(r->current_wal);
	r->current_wal = NULL;
}

void
recovery_delete(struct recovery *r)
{
	recovery_stop_local(r);

	if (r->writer)
		wal_writer_stop(r);

	xdir_destroy(&r->snap_dir);
	xdir_destroy(&r->wal_dir);
	if (r->current_wal) {
		/*
		 * Possible if shutting down a replication
		 * relay or if error during startup.
		 */
		xlog_close(r->current_wal);
	}
	free(r);
}

void
recovery_exit(struct recovery *r)
{
	/* Avoid fibers, there is no event loop */
	r->watcher = NULL;
	recovery_delete(r);
}

void
recovery_atfork(struct recovery *r)
{
       xlog_atfork(&r->current_wal);
       /*
        * Make sure that atexit() handlers in the child do
        * not try to stop the non-existent thread.
        * The writer is not used in the child.
        */
       r->writer = NULL;
}

void
recovery_apply_row(struct recovery *r, struct xrow_header *row)
{
	/* Check lsn */
	int64_t current_lsn = vclock_get(&r->vclock, row->server_id);
	if (row->lsn > current_lsn)
		r->apply_row(r, r->apply_row_param, row);
}

/**
 * Read all rows in a file starting from the last position.
 * Advance the position. If end of file is reached,
 * set l.eof_read.
 */
void
recover_xlog(struct recovery *r, struct xlog *l)
{
	struct xlog_cursor i;

	xlog_cursor_open(&i, l);

	auto guard = make_scoped_guard([&]{
		xlog_cursor_close(&i);
	});

	struct xrow_header row;
	/*
	 * xlog_cursor_next() returns 1 when
	 * it can not read more rows. This doesn't mean
	 * the file is fully read: it's fully read only
	 * when EOF marker has been read, see i.eof_read
	 */
	while (xlog_cursor_next(&i, &row) == 0) {
		try {
			recovery_apply_row(r, &row);
		} catch (ClientError *e) {
			if (l->dir->panic_if_error)
				throw;
			say_error("can't apply row: ");
			e->log();
		}
	}
	/**
	 * We should never try to read snapshots with no EOF
	 * marker - such snapshots are very likely unfinished
	 * or corrupted, and should not be trusted.
	 */
	if (l->dir->type == SNAP && l->is_inprogress == false &&
	    i.eof_read == false) {
		panic("snapshot `%s' has no EOF marker", l->filename);
	}

}

void
recovery_bootstrap(struct recovery *r)
{
	/* Add a surrogate server id for snapshot rows */
	vclock_add_server(&r->vclock, 0);

	/* Recover from bootstrap.snap */
	say_info("initializing an empty data directory");
	const char *filename = "bootstrap.snap";
	FILE *f = fmemopen((void *) &bootstrap_bin,
			   sizeof(bootstrap_bin), "r");
	struct xlog *snap = xlog_open_stream(&r->snap_dir, 0, f, filename);
	auto guard = make_scoped_guard([=]{
		xlog_close(snap);
	});
	/** The snapshot must have a EOF marker. */
	recover_xlog(r, snap);
}

/**
 * Find out if there are new .xlog files since the current
 * LSN, and read them all up.
 *
 * This function will not close r->current_wal if
 * recovery was successful.
 */
static void
recover_remaining_wals(struct recovery *r)
{
	xdir_scan(&r->wal_dir);

	struct vclock *last = vclockset_last(&r->wal_dir.index);
	if (last == NULL) {
		if (r->current_wal != NULL) {
			say_error("file `%s' was deleted under our feet",
				  r->current_wal->filename);
			recovery_close_log(r);
		}
		/** Nothing to do. */
		return;
	}
	assert(vclockset_next(&r->wal_dir.index, last) == NULL);

	/* If the caller already opened WAL for us, recover from it first */
	struct vclock *clock;
	if (r->current_wal != NULL) {
		clock = vclockset_match(&r->wal_dir.index,
					&r->current_wal->vclock);
		if (clock != NULL &&
		    vclock_compare(clock, &r->current_wal->vclock) == 0)
			goto recover_current_wal;
		/*
		 * The current WAL has disappeared under our feet -
		 * assume anything can happen in production and go
		 * on.
		 */
		say_error("file `%s' was deleted under our feet",
			  r->current_wal->filename);
	}

	for (clock = vclockset_match(&r->wal_dir.index, &r->vclock);
	     clock != NULL;
	     clock = vclockset_next(&r->wal_dir.index, clock)) {

		if (vclock_compare(clock, &r->vclock) > 0) {
			/**
			 * The best clock we could find is
			 * greater or is incomparable with the
			 * current state of recovery.
			 */
			XlogGapError *e =
				tnt_error(XlogGapError, &r->vclock, clock);

			if (r->wal_dir.panic_if_error)
				throw e;
			e->log();
			/* Ignore missing WALs */
			say_warn("ignoring a gap in LSN");
		}
		recovery_close_log(r);

		r->current_wal = xlog_open(&r->wal_dir, vclock_sum(clock));

		say_info("recover from `%s'", r->current_wal->filename);

recover_current_wal:
		if (r->current_wal->eof_read == false)
			recover_xlog(r, r->current_wal);
		/**
		 * Keep the last log open to remember recovery
		 * position. This speeds up recovery in local hot
		 * standby mode, since we don't have to re-open
		 * and re-scan the last log in recovery_finalize().
		 */
	}
	region_free(&fiber()->gc);
}

void
recovery_finalize(struct recovery *r, enum wal_mode wal_mode,
		  int rows_per_wal)
{
	recovery_stop_local(r);

	recover_remaining_wals(r);

	recovery_close_log(r);

	if (vclockset_last(&r->wal_dir.index) != NULL &&
	    vclock_sum(&r->vclock) ==
	    vclock_sum(vclockset_last(&r->wal_dir.index))) {
		/**
		 * The last log file had zero rows -> bump
		 * LSN so that we don't stumble over this
		 * file when trying to open a new xlog
		 * for writing.
		 */
		vclock_inc(&r->vclock, r->server_id);
	}
	r->wal_mode = wal_mode;
	if (r->wal_mode == WAL_FSYNC)
		(void) strcat(r->wal_dir.open_wflags, "s");

	wal_writer_start(r, rows_per_wal);
}


/* }}} */

/* {{{ Local recovery: support of hot standby and replication relay */

/*
 * Implements a subscription to WAL updates.
 * Attempts to register a WAL watcher; if it fails, falls back to fs events.
 * In the latter mode either a change to the WAL dir itself or a change
 * in the XLOG file triggers a wakeup. The WAL dir path is set in
 * constructor. XLOG file path is set via .set_log_path().
 */
class WalSubscription {
public:
	struct fiber *f;
	bool signaled;
	struct ev_stat dir_stat;
	struct ev_stat file_stat;
	struct ev_async async;
	struct wal_watcher watcher;
	char dir_path[PATH_MAX];
	char file_path[PATH_MAX];

	static void stat_cb(struct ev_loop *, struct ev_stat *stat, int)
	{
		((WalSubscription *)stat->data)->wakeup();
	}

	static void async_cb(struct ev_loop *, ev_async *async, int)
	{
		((WalSubscription *)async->data)->wakeup();
	}

	void wakeup()
	{
		signaled = true;
		if (f->flags & FIBER_IS_CANCELLABLE)
			fiber_wakeup(f);
	}

	WalSubscription(const char *wal_dir)
	{
		f = fiber();
		signaled = false;
		if ((size_t)snprintf(dir_path, sizeof(dir_path), "%s", wal_dir) >=
				sizeof(dir_path)) {

			panic("path too long: %s", wal_dir);
		}

		ev_stat_init(&dir_stat, stat_cb, "", 0.0);
		ev_stat_init(&file_stat, stat_cb, "", 0.0);
		ev_async_init(&async, async_cb);
		dir_stat.data = this;
		file_stat.data = this;
		async.data = this;

		ev_async_start(loop(), &async);
		if (wal_register_watcher(recovery, &watcher, &async) == -1) {
			/* Fallback to fs events. */
			ev_async_stop(loop(), &async);
			ev_stat_set(&dir_stat, dir_path, 0.0);
			ev_stat_start(loop(), &dir_stat);
		}
	}

	~WalSubscription()
	{
		ev_stat_stop(loop(), &file_stat);
		ev_stat_stop(loop(), &dir_stat);
		wal_unregister_watcher(recovery, &watcher);
		ev_async_stop(loop(), &async);
	}

	void set_log_path(const char *path)
	{
		if (ev_is_active(&async)) {
			/*
			 * Notifications delivered via watcher, fs events
			 * irrelevant.
			 */
			return;
		}

		/*
		 * Avoid toggling ev_stat if the path didn't change.
		 * Note: .file_path valid iff file_stat is active.
		 */
		if (path && ev_is_active(&file_stat) &&
				strcmp(file_path, path) == 0) {

			return;
		}

		ev_stat_stop(loop(), &file_stat);

		if (path == NULL)
			return;

		if ((size_t)snprintf(file_path, sizeof(file_path), "%s", path) >=
				sizeof(file_path)) {

			panic("path too long: %s", path);
		}
		ev_stat_set(&file_stat, file_path, 0.0);
		ev_stat_start(loop(), &file_stat);
	}
};

static void
recovery_follow_f(va_list ap)
{
	struct recovery *r = va_arg(ap, struct recovery *);
	ev_tstamp wal_dir_rescan_delay = va_arg(ap, ev_tstamp);
	fiber_set_user(fiber(), &admin_credentials);

	WalSubscription subscription(r->wal_dir.dirname);

	while (! fiber_is_cancelled()) {

		recover_remaining_wals(r);

		subscription.set_log_path(
			r->current_wal != NULL ? r->current_wal->filename : NULL);

		if (subscription.signaled == false) {
			/**
			 * Allow an immediate wakeup/break loop
			 * from recovery_stop_local().
			 */
			fiber_set_cancellable(true);
			fiber_yield_timeout(wal_dir_rescan_delay);
			fiber_set_cancellable(false);
		}

		subscription.signaled = false;
	}
}

void
recovery_follow_local(struct recovery *r, const char *name,
		      ev_tstamp wal_dir_rescan_delay)
{
	assert(r->writer == NULL);

	/*
	 * Scan wal_dir and recover all existing at the moment xlogs.
	 * Blocks until finished.
	 */
	recover_remaining_wals(r);
	recovery_close_log(r);

	/*
	 * Start 'hot_standby' background fiber to follow xlog changes.
	 */
	assert(r->watcher == NULL);
	r->watcher = fiber_new_xc(name, recovery_follow_f);
	fiber_set_joinable(r->watcher, true);
	fiber_start(r->watcher, r, wal_dir_rescan_delay);
}

void
recovery_stop_local(struct recovery *r)
{
	if (r->watcher) {
		struct fiber *f = r->watcher;
		r->watcher = NULL;
		fiber_cancel(f);
		fiber_join(f);
		diag_raise();
	}
}

/* }}} */

