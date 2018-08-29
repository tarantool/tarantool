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

#include "small/rlist.h"
#include "scoped_guard.h"
#include "trigger.h"
#include "fiber.h"
#include "xlog.h"
#include "xrow.h"
#include "xstream.h"
#include "wal.h" /* wal_watcher */
#include "replication.h"
#include "session.h"
#include "coio_file.h"
#include "error.h"

/*
 * Recovery subsystem
 * ------------------
 *
 * A facade of the recovery subsystem is struct recovery.
 *
 * Depending on the actual task being performed the recovery
 * can be in a different state.
 *
 * Let's enumerate all possible distinct states of recovery:
 *
 * IR - initial recovery, initiated right after server start:
 * reading data from a checkpoint and existing WALs
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
 * The following state transitions are possible/supported:
 *
 * recovery_init() -> IR | IRR # recover()
 * IR -> HS         # recovery_follow_local()
 * IRR -> RR        # recovery_follow_local()
 */

/* {{{ Initial recovery */

/**
 * Throws an exception in  case of error.
 */
struct recovery *
recovery_new(const char *wal_dirname, bool force_recovery,
	     const struct vclock *vclock)
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
	rlist_create(&r->on_close_log);

	guard.is_active = false;
	return r;
}

void
recovery_scan(struct recovery *r, struct vclock *end_vclock)
{
	xdir_scan_xc(&r->wal_dir);

	struct vclock *vclock = vclockset_last(&r->wal_dir.index);
	if (vclock == NULL || vclock_compare(vclock, &r->vclock) < 0) {
		/* No xlogs after last checkpoint. */
		vclock_copy(end_vclock, &r->vclock);
		return;
	}

	/* Scan the last xlog to find end vclock. */
	vclock_copy(end_vclock, vclock);
	struct xlog_cursor cursor;
	if (xdir_open_cursor(&r->wal_dir, vclock_sum(vclock), &cursor) != 0)
		return;
	struct xrow_header row;
	while (xlog_cursor_next(&cursor, &row, true) == 0)
		vclock_follow_xrow(end_vclock, &row);
	xlog_cursor_close(&cursor, false);
}

static inline void
recovery_close_log(struct recovery *r)
{
	if (!xlog_cursor_is_open(&r->cursor))
		return;
	if (xlog_cursor_is_eof(&r->cursor)) {
		say_info("done `%s'", r->cursor.name);
	} else {
		say_warn("file `%s` wasn't correctly closed",
			 r->cursor.name);
	}
	xlog_cursor_close(&r->cursor, false);
	trigger_run_xc(&r->on_close_log, NULL);
}

static void
recovery_open_log(struct recovery *r, const struct vclock *vclock)
{
	XlogGapError *e;
	struct xlog_meta meta = r->cursor.meta;
	enum xlog_cursor_state state = r->cursor.state;

	recovery_close_log(r);

	xdir_open_cursor_xc(&r->wal_dir, vclock_sum(vclock), &r->cursor);

	if (state == XLOG_CURSOR_NEW &&
	    vclock_compare(vclock, &r->vclock) > 0) {
		/*
		 * This is the first WAL we are about to scan
		 * and the best clock we could find is greater
		 * or is incomparable with the initial recovery
		 * position.
		 */
		goto gap_error;
	}

	if (state != XLOG_CURSOR_NEW &&
	    vclock_is_set(&r->cursor.meta.prev_vclock) &&
	    vclock_compare(&r->cursor.meta.prev_vclock, &meta.vclock) != 0) {
		/*
		 * WALs are missing between the last scanned WAL
		 * and the next one.
		 */
		goto gap_error;
	}
out:
	/*
	 * We must promote recovery clock even if we don't recover
	 * anything from the next WAL. Otherwise if the last WAL
	 * in the directory is corrupted or empty and the previous
	 * one has an LSN gap at the end (due to a write error),
	 * we will create the next WAL between two existing ones,
	 * thus breaking the file order.
	 */
	if (vclock_compare(&r->vclock, vclock) < 0)
		vclock_copy(&r->vclock, vclock);
	return;

gap_error:
	e = tnt_error(XlogGapError, &r->vclock, vclock);
	if (!r->wal_dir.force_recovery)
		throw e;
	/* Ignore missing WALs if force_recovery is set. */
	e->log();
	say_warn("ignoring a gap in LSN");
	goto out;
}

void
recovery_delete(struct recovery *r)
{
	recovery_stop_local(r);

	trigger_destroy(&r->on_close_log);
	xdir_destroy(&r->wal_dir);
	if (xlog_cursor_is_open(&r->cursor)) {
		/*
		 * Possible if shutting down a replication
		 * relay or if error during startup.
		 */
		xlog_cursor_close(&r->cursor, false);
	}
	free(r);
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
	     const struct vclock *stop_vclock)
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

		/*
		 * All rows in xlog files have an assigned
		 * replica id.
		 */
		assert(row.replica_id != 0);
		/*
		 * We can promote the vclock either before or
		 * after xstream_write(): it only makes any impact
		 * in case of forced recovery, when we skip the
		 * failed row anyway.
		 */
		vclock_follow_xrow(&r->vclock, &row);
		if (xstream_write(stream, &row) == 0) {
			++row_count;
			if (row_count % 100000 == 0)
				say_info("%.1fM rows processed",
					 row_count / 1000000.);
		} else {
			say_error("can't apply row: ");
			diag_log();
			if (!r->wal_dir.force_recovery)
				diag_raise();
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
		       const struct vclock *stop_vclock, bool scan_dir)
{
	struct vclock *clock;

	if (scan_dir)
		xdir_scan_xc(&r->wal_dir);

	if (xlog_cursor_is_open(&r->cursor)) {
		/* If there's a WAL open, recover from it first. */
		assert(!xlog_cursor_is_eof(&r->cursor));
		clock = vclockset_search(&r->wal_dir.index,
					 &r->cursor.meta.vclock);
		if (clock != NULL)
			goto recover_current_wal;
		/*
		 * The current WAL has disappeared under our feet -
		 * assume anything can happen in production and go on.
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

		if (xlog_cursor_is_eof(&r->cursor) &&
		    vclock_sum(&r->cursor.meta.vclock) >= vclock_sum(clock)) {
			/*
			 * If we reached EOF while reading last xlog,
			 * we don't need to rescan it.
			 */
			continue;
		}

		recovery_open_log(r, clock);

		say_info("recover from `%s'", r->cursor.name);

recover_current_wal:
		recover_xlog(r, stream, stop_vclock);
	}

	if (xlog_cursor_is_eof(&r->cursor))
		recovery_close_log(r);

	if (stop_vclock != NULL && vclock_compare(&r->vclock, stop_vclock) != 0)
		tnt_raise(XlogGapError, &r->vclock, stop_vclock);

	region_free(&fiber()->gc);
}

void
recovery_finalize(struct recovery *r)
{
	recovery_close_log(r);
}


/* }}} */

/* {{{ Local recovery: support of hot standby and replication relay */

/**
 * Implements a subscription to WAL updates via fs events.
 * Any change to the WAL dir itself or a change in the XLOG
 * file triggers a wakeup. The WAL dir path is set in the
 * constructor. XLOG file path is set with set_log_path().
 */
class WalSubscription {
public:
	struct fiber *f;
	unsigned events;
	struct ev_stat dir_stat;
	struct ev_stat file_stat;
	char dir_path[PATH_MAX];
	char file_path[PATH_MAX];

	static void dir_stat_cb(struct ev_loop *, struct ev_stat *stat, int)
	{
		((WalSubscription *)stat->data)->wakeup(WAL_EVENT_ROTATE);
	}

	static void file_stat_cb(struct ev_loop *, struct ev_stat *stat, int)
	{
		((WalSubscription *)stat->data)->wakeup(WAL_EVENT_WRITE);
	}

	void wakeup(unsigned events)
	{
		this->events |= events;
		if (f->flags & FIBER_IS_CANCELLABLE)
			fiber_wakeup(f);
	}

	WalSubscription(const char *wal_dir)
	{
		f = fiber();
		events = 0;
		if ((size_t)snprintf(dir_path, sizeof(dir_path), "%s", wal_dir) >=
				sizeof(dir_path)) {

			panic("path too long: %s", wal_dir);
		}

		ev_stat_init(&dir_stat, dir_stat_cb, "", 0.0);
		ev_stat_init(&file_stat, file_stat_cb, "", 0.0);
		dir_stat.data = this;
		file_stat.data = this;

		ev_stat_set(&dir_stat, dir_path, 0.0);
		ev_stat_start(loop(), &dir_stat);
	}

	~WalSubscription()
	{
		ev_stat_stop(loop(), &file_stat);
		ev_stat_stop(loop(), &dir_stat);
	}

	void set_log_path(const char *path)
	{
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
hot_standby_f(va_list ap)
{
	struct recovery *r = va_arg(ap, struct recovery *);
	struct xstream *stream = va_arg(ap, struct xstream *);
	bool scan_dir = true;

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
			start = vclock_sum(&r->vclock);

			recover_remaining_wals(r, stream, NULL, scan_dir);

			end = vclock_sum(&r->vclock);
			/*
			 * Continue, given there's been progress *and* there is a
			 * chance new WALs have appeared since.
			 * Sic: end * is < start (is 0) if someone deleted all logs
			 * on the filesystem.
			 */
		} while (end > start && !xlog_cursor_is_open(&r->cursor));

		subscription.set_log_path(xlog_cursor_is_open(&r->cursor) ?
					  r->cursor.name : NULL);

		bool timed_out = false;
		if (subscription.events == 0) {
			/**
			 * Allow an immediate wakeup/break loop
			 * from recovery_stop_local().
			 */
			fiber_set_cancellable(true);
			timed_out = fiber_yield_timeout(wal_dir_rescan_delay);
			fiber_set_cancellable(false);
		}

		scan_dir = timed_out ||
			(subscription.events & WAL_EVENT_ROTATE) != 0;

		subscription.events = 0;
	}
	return 0;
}

void
recovery_follow_local(struct recovery *r, struct xstream *stream,
		      const char *name, ev_tstamp wal_dir_rescan_delay)
{
	/*
	 * Start 'hot_standby' background fiber to follow xlog changes.
	 * It will pick up from the position of the currently open
	 * xlog.
	 */
	assert(r->watcher == NULL);
	r->watcher = fiber_new_xc(name, hot_standby_f);
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
