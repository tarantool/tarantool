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
#include "recovery.h"

#include "scoped_guard.h"
#include "fiber.h"
#include "xlog.h"
#include "xrow.h"
#include "xstream.h"
#include "wal.h" /* wal_watcher */
#include "replication.h"
#include "session.h"
#include "coeio_file.h"

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

/* {{{ Initial recovery */

/**
 * Throws an exception in  case of error.
 */
struct recovery *
recovery_new(const char *wal_dirname, bool force_recovery,
	     struct vclock *vclock)
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

	xdir_create(&r->wal_dir, wal_dirname, XLOG, &INSTANCE_UUID);
	r->wal_dir.force_recovery = force_recovery;

	vclock_copy(&r->vclock, vclock);

	/**
	 * Avoid scanning WAL dir before we recovered
	 * the snapshot and know instance UUID - this will
	 * make sure the scan skips files with wrong
	 * UUID, see replication/cluster.test for
	 * details.
	 */
	xdir_check_xc(&r->wal_dir);

	r->watcher = NULL;

	guard.is_active = false;
	return r;
}

static inline void
recovery_close_log(struct recovery *r)
{
	if (r->cursor.state == XLOG_CURSOR_CLOSED)
		return;
	if (r->cursor.state == XLOG_CURSOR_EOF) {
		say_info("done `%s'", r->cursor.name);
	} else {
		say_warn("file `%s` wasn't correctly closed",
			 r->cursor.name);
	}
	xlog_cursor_close(&r->cursor, false);
}

void
recovery_delete(struct recovery *r)
{
	recovery_stop_local(r);

	xdir_destroy(&r->wal_dir);
	if (r->cursor.state != XLOG_CURSOR_CLOSED) {
		/*
		 * Possible if shutting down a replication
		 * relay or if error during startup.
		 */
		xlog_cursor_close(&r->cursor, false);
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

/**
 * Read all rows in a file starting from the last position.
 * Advance the position. If end of file is reached,
 * set l.eof_read.
 * The reading will be stopped on reaching stop_vclock.
 * Use NULL for boundless recover
 */
static void
recover_xlog(struct recovery *r, struct xstream *stream,
	     struct vclock *stop_vclock)
{
	struct xrow_header row;
	uint64_t row_count = 0;
	while (xlog_cursor_next_xc(&r->cursor, &row,
				   r->wal_dir.force_recovery) == 0) {
		/*
		 * Read the next row from xlog file.
		 *
		 * xlog_cursor_next_xc() returns 1 when
		 * it can not read more rows. This doesn't mean
		 * the file is fully read: it's fully read only
		 * when EOF marker has been read, see i.eof_read
		 */
		if (stop_vclock != NULL &&
		    r->vclock.signature >= stop_vclock->signature)
			return;
		int64_t current_lsn = vclock_get(&r->vclock, row.replica_id);
		if (row.lsn <= current_lsn)
			continue; /* already applied, skip */

		try {
			/*
			 * All rows in xlog files have an assigned
			 * replica id.
			 */
			assert(row.replica_id != 0);
			/*
			 * We can promote the vclock either before
			 * or after xstream_write(): it only makes
			 * any impact in case of forced recovery,
			 * when we skip the failed row anyway.
			 */
			vclock_follow(&r->vclock,  row.replica_id, row.lsn);
			xstream_write_xc(stream, &row);
			++row_count;
			if (row_count % 100000 == 0)
				say_info("%.1fM rows processed",
					 row_count / 1000000.);
		} catch (ClientError *e) {
			say_error("can't apply row: ");
			e->log();
			if (!r->wal_dir.force_recovery)
				throw;
		}
	}
}

/**
 * Find out if there are new .xlog files since the current
 * LSN, and read them all up.
 *
 * Reading will be stopped on reaching recovery
 * vclock signature > to_checkpoint (after playing to_checkpoint record)
 * use NULL for boundless recover
 *
 * This function will not close r->current_wal if
 * recovery was successful.
 */
void
recover_remaining_wals(struct recovery *r, struct xstream *stream,
		       struct vclock *stop_vclock)
{
	/*
	 * Sic: it could be tempting to put xdir_scan() inside
	 * this function. This would slow down relay quite a bit,
	 * since xdir_scan() would be invoked on every relay
	 * row.
	 */
	struct vclock *last = vclockset_last(&r->wal_dir.index);
	if (last == NULL) {
		if (r->cursor.state != XLOG_CURSOR_CLOSED) {
			say_error("file `%s' was deleted under our feet",
				  r->cursor.name);
			recovery_close_log(r);
		}
		/** Nothing to do. */
		return;
	}
	assert(vclockset_next(&r->wal_dir.index, last) == NULL);

	/* If the caller already opened WAL for us, recover from it first */
	struct vclock *clock;
	if (r->cursor.state != XLOG_CURSOR_CLOSED) {
		clock = vclockset_match(&r->wal_dir.index,
					&r->cursor.meta.vclock);
		if (clock != NULL &&
		    vclock_compare(clock, &r->cursor.meta.vclock) == 0)
			goto recover_current_wal;
		/*
		 * The current WAL has disappeared under our feet -
		 * assume anything can happen in production and go
		 * on.
		 */
		say_error("file `%s' was deleted under our feet",
			  r->cursor.name);
	}

	for (clock = vclockset_match(&r->wal_dir.index, &r->vclock);
	     clock != NULL;
	     clock = vclockset_next(&r->wal_dir.index, clock)) {
		if (stop_vclock != NULL &&
		    clock->signature >= stop_vclock->signature) {
			break;
		}

		if (vclock_compare(clock, &r->vclock) > 0) {
			/**
			 * The best clock we could find is
			 * greater or is incomparable with the
			 * current state of recovery.
			 */
			XlogGapError *e =
				tnt_error(XlogGapError, &r->vclock, clock);

			if (!r->wal_dir.force_recovery)
				throw e;
			e->log();
			/* Ignore missing WALs */
			say_warn("ignoring a gap in LSN");
		}
		recovery_close_log(r);

		xdir_open_cursor_xc(&r->wal_dir, vclock_sum(clock), &r->cursor);

		say_info("recover from `%s'", r->cursor.name);

recover_current_wal:
		if (r->cursor.state != XLOG_CURSOR_EOF)
			recover_xlog(r, stream, stop_vclock);
		/**
		 * Keep the last log open to remember recovery
		 * position. This speeds up recovery in local hot
		 * standby mode, since we don't have to re-open
		 * and re-scan the last log in recovery_finalize().
		 */
	}

	if (stop_vclock != NULL && vclock_compare(&r->vclock, stop_vclock) != 0)
		tnt_raise(XlogGapError, &r->vclock, stop_vclock);

	region_free(&fiber()->gc);
}

void
recovery_finalize(struct recovery *r, struct xstream *stream)
{
	recovery_stop_local(r);

	xdir_scan_xc(&r->wal_dir);
	recover_remaining_wals(r, stream, NULL);

	recovery_close_log(r);

	/*
	 * Check that the last xlog file has rows.
	 */
	if (vclockset_last(&r->wal_dir.index) != NULL &&
	    vclock_sum(&r->vclock) ==
	    vclock_sum(vclockset_last(&r->wal_dir.index))) {
		/*
		 * Delete the last empty xlog file.
		 */
		char *name = xdir_format_filename(&r->wal_dir,
						  vclock_sum(&r->vclock),
						  NONE);
		if (unlink(name) != 0) {
			tnt_raise(SystemError, "%s: failed to unlink file",
				  name);
		}
	}
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
		if (wal_set_watcher(&watcher, &async) == -1) {
			/* Fallback to fs events. */
			ev_async_stop(loop(), &async);
			ev_stat_set(&dir_stat, dir_path, 0.0);
			ev_stat_start(loop(), &dir_stat);
			watcher.loop = NULL;
			watcher.async = NULL;
		}
	}

	~WalSubscription()
	{
		ev_stat_stop(loop(), &file_stat);
		ev_stat_stop(loop(), &dir_stat);
		wal_clear_watcher(&watcher);
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

static int
recovery_follow_f(va_list ap)
{
	struct recovery *r = va_arg(ap, struct recovery *);
	struct xstream *stream = va_arg(ap, struct xstream *);
	ev_tstamp wal_dir_rescan_delay = va_arg(ap, ev_tstamp);
	fiber_set_user(fiber(), &admin_credentials);

	WalSubscription subscription(r->wal_dir.dirname);

	while (! fiber_is_cancelled()) {

		/*
		 * Recover until there is no new stuff which appeared in
		 * the log dir while recovery was running.
		 *
		 * Use vclock signature to represent the current wal
		 * since the xlog object itself may be freed in
		 * recover_remaining_rows().
		 */
		int64_t start, end;
		do {
			start = r->cursor.state != XLOG_CURSOR_CLOSED ?
				vclock_sum(&r->cursor.meta.vclock) : 0;
			/*
			 * If there is no current WAL, or we reached
			 * an end  of one, look for new WALs.
			 */
			if (r->cursor.state == XLOG_CURSOR_CLOSED
			    || r->cursor.state == XLOG_CURSOR_EOF)
				xdir_scan_xc(&r->wal_dir);

			recover_remaining_wals(r, stream, NULL);

			end = r->cursor.state != XLOG_CURSOR_CLOSED ?
			      vclock_sum(&r->cursor.meta.vclock) : 0;
			/*
			 * Continue, given there's been progress *and* there is a
			 * chance new WALs have appeared since.
			 * Sic: end * is < start (is 0) if someone deleted all logs
			 * on the filesystem.
			 */
		} while (end > start &&
			 (r->cursor.state == XLOG_CURSOR_CLOSED ||
			  r->cursor.state == XLOG_CURSOR_EOF));

		subscription.set_log_path(r->cursor.state != XLOG_CURSOR_CLOSED ?
					  r->cursor.name: NULL);

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
	return 0;
}

void
recovery_follow_local(struct recovery *r, struct xstream *stream,
		      const char *name, ev_tstamp wal_dir_rescan_delay)
{
	/*
	 * Scan wal_dir and recover all existing at the moment xlogs.
	 * Blocks until finished.
	 */
	xdir_scan_xc(&r->wal_dir);
	recover_remaining_wals(r, stream, NULL);
	/*
	 * Start 'hot_standby' background fiber to follow xlog changes.
	 * It will pick up from the position of the currently open
	 * xlog.
	 */
	assert(r->watcher == NULL);
	r->watcher = fiber_new_xc(name, recovery_follow_f);
	fiber_set_joinable(r->watcher, true);
	fiber_start(r->watcher, r, stream, wal_dir_rescan_delay);
}

void
recovery_stop_local(struct recovery *r)
{
	if (r->watcher) {
		struct fiber *f = r->watcher;
		r->watcher = NULL;
		fiber_cancel(f);
		if (fiber_join(f) != 0)
			diag_raise();
	}
}

/* }}} */

