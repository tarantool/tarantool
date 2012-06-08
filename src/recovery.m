/*
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

#include <fcntl.h>

#include "log_io.h"
#include "fiber.h"
#include "tarantool_pthread.h"
#include "nio.h"
#include "errinj.h"

/*
 * Recovery subsystem
 * ------------------
 *
 * A facade of the recovery subsystem is struct recovery_state,
 * which is a singleton.
 *
 * Depending on the configuration, start-up parameters, the
 * actual task being performed, the recovery can be
 * in a different state.
 *
 * The main factors influencing recovery state are:
 * - temporal: whether or not the instance is just booting
 *   from a snapshot, is in 'local hot standby mode', or
 *   is already accepting requests
 * - topological: whether or not it is a master instance
 *   or a replica
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
 * S - snapshot mode, writing entire in-memory state to a compact
 * snapshot file.
 *
 * The following state transitions are possible/supported:
 *
 * recovery_init() -> IR | IRR # recover()
 * IR -> HS         # recovery_follow_local()
 * IRR -> RR        # recovery_follow_local()
 * HS -> M          # recovery_finalize()
 * M -> R           # recovery_follow_remote()
 * R -> M           # recovery_stop_remote()
 * M -> S           # snapshot()
 * R -> S           # snapshot()
 */

struct recovery_state *recovery_state;

static const u64 snapshot_cookie = 0;

/* {{{ LSN API */

void
wait_lsn_set(struct wait_lsn *wait_lsn, i64 lsn)
{
	assert(wait_lsn->waiter == NULL);
	wait_lsn->waiter = fiber;
	wait_lsn->lsn = lsn;
}

int
confirm_lsn(struct recovery_state *r, i64 lsn)
{
	assert(r->confirmed_lsn <= r->lsn);

	if (r->confirmed_lsn < lsn) {
		if (r->confirmed_lsn + 1 != lsn)
			say_warn("non consecutive lsn, last confirmed:%" PRIi64
				 " new:%" PRIi64 " diff: %" PRIi64,
				 r->confirmed_lsn, lsn, lsn - r->confirmed_lsn);
		r->confirmed_lsn = lsn;
		/* Alert the waiter, if any. There can be holes in
		 * confirmed_lsn, in case of disk write failure,
		 * but wal_writer never confirms LSNs out order.
		 */
		if (r->wait_lsn.waiter && r->confirmed_lsn >= r->wait_lsn.lsn) {
			fiber_call(r->wait_lsn.waiter);
		}

		return 0;
	} else {
		say_warn("lsn double confirmed:%" PRIi64, lsn);
	}

	return -1;
}

/** Wait until the given LSN makes its way to disk. */
void
recovery_wait_lsn(struct recovery_state *r, i64 lsn)
{
	while (lsn < r->confirmed_lsn) {
		wait_lsn_set(&r->wait_lsn, lsn);
		@try {
			fiber_yield();
		} @finally {
			wait_lsn_clear(&r->wait_lsn);
		}
	}
}


i64
next_lsn(struct recovery_state *r, i64 new_lsn)
{
	if (new_lsn == 0)
		r->lsn++;
	else
		r->lsn = new_lsn;

	say_debug("next_lsn(%p, %" PRIi64 ") => %" PRIi64, r, new_lsn, r->lsn);
	return r->lsn;
}

/* }}} */

/* {{{ Initial recovery */

static int
wal_writer_start(struct recovery_state *state);
void
wal_writer_stop(struct recovery_state *r);
static void
recovery_stop_local(struct recovery_state *r);

void
recovery_init(const char *snap_dirname, const char *wal_dirname,
	      row_handler row_handler, int rows_per_wal,
	      const char *wal_mode, double wal_fsync_delay, int flags)
{
	assert(recovery_state == NULL);
	recovery_state = p0alloc(eter_pool, sizeof(struct recovery_state));
	struct recovery_state *r = recovery_state;

	if (rows_per_wal <= 1)
		panic("unacceptable value of 'rows_per_wal'");

	r->row_handler = row_handler;

	r->snap_dir = &snap_dir;
	r->snap_dir->dirname = strdup(snap_dirname);
	r->wal_dir = &wal_dir;
	r->wal_dir->dirname = strdup(wal_dirname);
	r->wal_dir->open_wflags = strcasecmp(wal_mode, "fsync") ? 0 : WAL_SYNC_FLAG;
	r->rows_per_wal = rows_per_wal;
	r->wal_fsync_delay = wal_fsync_delay;
	wait_lsn_clear(&r->wait_lsn);
	r->flags = flags;
}

void
recovery_update_mode(const char *mode, double fsync_delay)
{
	struct recovery_state *r = recovery_state;
	(void) mode;
	/* No mutex lock: let's not bother with whether
	 * or not a WAL writer thread is present, and
	 * if it's present, the delay will be propagated
	 * to it whenever there is a next lock/unlock of
	 * wal_writer->mutex.
	 */
	r->wal_fsync_delay = fsync_delay;
}

void
recovery_update_io_rate_limit(double new_limit)
{
	recovery_state->snap_io_rate_limit = new_limit * 1024 * 1024;
}

void
recovery_free()
{
	struct recovery_state *r = recovery_state;
	if (r == NULL)
		return;

	if (r->watcher)
		recovery_stop_local(r);

	if (r->writer)
		wal_writer_stop(r);

	free(r->snap_dir->dirname);
	free(r->wal_dir->dirname);
	if (r->current_wal) {
		/*
		 * Possible if shutting down a replication
		 * relay or if error during startup.
		 */
		log_io_close(&r->current_wal);
	}

	recovery_state = NULL;
}

void
recovery_setup_panic(struct recovery_state *r, bool on_snap_error, bool on_wal_error)
{
	r->wal_dir->panic_if_error = on_wal_error;
	r->snap_dir->panic_if_error = on_snap_error;
}


/**
 * Read a snapshot and call row_handler for every snapshot row.
 *
 * @retval 0 success
 * @retval -1 failure
 */
static int
recover_snap(struct recovery_state *r)
{
	int res = -1;
	struct log_io *snap;
	i64 lsn;

	lsn = greatest_lsn(r->snap_dir);
	if (lsn <= 0) {
		say_error("can't find snapshot");
		return -1;
	}
	snap = log_io_open_for_read(r->snap_dir, lsn, NONE);
	if (snap == NULL) {
		say_error("can't find/open snapshot");
		return -1;
	}
	say_info("recover from `%s'", snap->filename);
	struct log_io_cursor i;

	log_io_cursor_open(&i, snap);

	struct tbuf *row;
	while ((row = log_io_cursor_next(&i))) {
		if (r->row_handler(row) < 0) {
			say_error("can't apply row");
			goto end;
		}
	}
	r->lsn = r->confirmed_lsn = lsn;
	res = 0;
end:
	log_io_cursor_close(&i);
	log_io_close(&snap);
	return res;
}

#define LOG_EOF 0

/**
 * @retval -1 error
 * @retval 0 EOF
 * @retval 1 ok, maybe read something
 */
static int
recover_wal(struct recovery_state *r, struct log_io *l)
{
	int res = -1;
	struct log_io_cursor i;

	log_io_cursor_open(&i, l);

	struct tbuf *row = NULL;
	while ((row = log_io_cursor_next(&i))) {
		i64 lsn = header_v11(row)->lsn;
		if (lsn <= r->confirmed_lsn) {
			say_debug("skipping too young row");
			continue;
		}
		/*
		 * After handler(row) returned, row may be
		 * modified, do not use it.
		 */
		if (r->row_handler(row) < 0) {
			say_error("can't apply row");
			goto end;
		}
		next_lsn(r, lsn);
		confirm_lsn(r, lsn);
	}
	res = i.eof_read ? LOG_EOF : 1;
end:
	log_io_cursor_close(&i);
	/* Sic: we don't close the log here. */
	return res;
}

/** Find out if there are new .xlog files since the current
 * LSN, and read them all up.
 *
 * This function will not close r->current_wal if
 * recovery was successful.
 */
static int
recover_remaining_wals(struct recovery_state *r)
{
	int result = 0;
	struct log_io *next_wal;
	i64 current_lsn, wal_greatest_lsn;
	size_t rows_before;

	current_lsn = r->confirmed_lsn + 1;
	wal_greatest_lsn = greatest_lsn(r->wal_dir);

	/* if the caller already opened WAL for us, recover from it first */
	if (r->current_wal != NULL)
		goto recover_current_wal;

	while (r->confirmed_lsn < wal_greatest_lsn) {
		/*
		 * If a newer WAL appeared in the directory before
		 * current_wal was fully read, try re-reading
		 * one last time. */
		if (r->current_wal != NULL) {
			if (r->current_wal->retry++ < 3) {
				say_warn("`%s' has no EOF marker, yet a newer WAL file exists:"
					 " trying to re-read (attempt #%d)",
					 r->current_wal->filename, r->current_wal->retry);
				goto recover_current_wal;
			} else {
				say_warn("WAL `%s' wasn't correctly closed",
					 r->current_wal->filename);
				log_io_close(&r->current_wal);
			}
		}

		/* TODO: find a better way of finding the next xlog */
		current_lsn = r->confirmed_lsn + 1;
		/*
		 * For the last WAL, first try to open .inprogress
		 * file: if it doesn't exist, we can safely try an
		 * .xlog, with no risk of a concurrent
		 * inprogress_log_rename().
		 */
		FILE *f = NULL;
		char *filename;
		enum log_suffix suffix = INPROGRESS;
		if (current_lsn == wal_greatest_lsn) {
			/* Last WAL present at the time of rescan. */
			filename = format_filename(r->wal_dir,
						   current_lsn, suffix);
			f = fopen(filename, "r");
		}
		if (f == NULL) {
			suffix = NONE;
			filename = format_filename(r->wal_dir,
						   current_lsn, suffix);
			f = fopen(filename, "r");
		}
		next_wal = log_io_open(r->wal_dir, LOG_READ, filename, suffix, f);
		/*
		 * When doing final recovery, and dealing with the
		 * last file, try opening .<ext>.inprogress.
		 */
		if (next_wal == NULL) {
			if (r->finalize && suffix == INPROGRESS) {
				/*
				 * There is an .inprogress file, but
				 * we failed to open it. Try to
				 * delete it.
				 */
				say_warn("unlink broken %s WAL", filename);
				if (inprogress_log_unlink(filename) != 0)
					panic("can't unlink 'inprogres' WAL");
			}
			result = 0;
			break;
		}
		assert(r->current_wal == NULL);
		r->current_wal = next_wal;
		say_info("recover from `%s'", r->current_wal->filename);

recover_current_wal:
		rows_before = r->current_wal->rows;
		result = recover_wal(r, r->current_wal);
		if (result < 0) {
			say_error("failure reading from %s",
				  r->current_wal->filename);
			break;
		}

		if (r->current_wal->rows > 0 &&
		    r->current_wal->rows != rows_before) {
			r->current_wal->retry = 0;
		}
		/* rows == 0 could indicate an empty WAL */
		if (r->current_wal->rows == 0) {
			say_error("read zero records from %s",
				  r->current_wal->filename);
			break;
		}

		if (result == LOG_EOF) {
			say_info("done `%s' confirmed_lsn: %" PRIi64,
				 r->current_wal->filename,
				 r->confirmed_lsn);
			log_io_close(&r->current_wal);
		}
	}

	/*
	 * It's not a fatal error when last WAL is empty, but if
	 * we lose some logs it is a fatal error.
	 */
	if (wal_greatest_lsn > r->confirmed_lsn + 1) {
		say_error("not all WALs have been successfully read");
		result = -1;
	}

	prelease(fiber->gc_pool);
	return result;
}

void
recover(struct recovery_state *r, i64 lsn)
{
	/* * current_wal isn't open during initial recover. */
	assert(r->current_wal == NULL);
	/*
	 * If the caller sets confirmed_lsn to a non-zero value,
	 * snapshot recovery is skipped and we proceed directly to
	 * finding the WAL with the respective LSN and continue
	 * recovery from this WAL.  @fixme: this is a gotcha, due
	 * to whihc a replica is unable to read data from a master
	 * if the replica has no snapshot or the master has no WAL
	 * with the requested LSN.
	 */
	say_info("recovery start");
	if (lsn == 0) {
		if (recover_snap(r) != 0) {
			if (greatest_lsn(r->snap_dir) <= 0) {
				say_crit("didn't you forget to initialize storage with --init-storage switch?");
				_exit(1);
			}
			panic("snapshot recovery failed");
		}
		say_info("snapshot recovered, confirmed lsn: %"
			 PRIi64, r->confirmed_lsn);
	} else {
		/*
		 * Note that recovery starts with lsn _NEXT_ to
		 * the confirmed one.
		 */
		r->lsn = r->confirmed_lsn = lsn - 1;
	}
	i64 next_lsn = r->confirmed_lsn + 1;
	i64 wal_lsn = find_including_file(r->wal_dir, next_lsn);
	if (wal_lsn <= 0) {
		if (lsn != 0) {
			/*
			 * Recovery for replication relay, did not
			 * find the requested LSN.
			 */
			say_error("can't find WAL containing record with lsn: %" PRIi64, next_lsn);
		}
		/* No WALs to recover from. */
		goto out;
	}
	r->current_wal = log_io_open_for_read(r->wal_dir, wal_lsn, NONE);
	if (r->current_wal == NULL)
		goto out;
	if (recover_remaining_wals(r) < 0)
		panic("recover failed");
	say_info("WALs recovered, confirmed lsn: %" PRIi64, r->confirmed_lsn);
out:
	prelease(fiber->gc_pool);
}

void
recovery_finalize(struct recovery_state *r)
{
	int result;

	if (r->watcher)
		recovery_stop_local(r);

	r->finalize = true;

	result = recover_remaining_wals(r);
	if (result < 0)
		panic("unable to successfully finalize recovery");

	if (r->current_wal != NULL && result != LOG_EOF) {
		say_warn("WAL `%s' wasn't correctly closed", r->current_wal->filename);

		if (!r->current_wal->is_inprogress) {
			if (r->current_wal->rows == 0)
			        /* Regular WAL (not inprogress) must contain at least one row */
				panic("zero rows was successfully read from last WAL `%s'",
				      r->current_wal->filename);
		} else if (r->current_wal->rows == 0) {
			/* Unlink empty inprogress WAL */
			say_warn("unlink broken %s WAL", r->current_wal->filename);
			if (inprogress_log_unlink(r->current_wal->filename) != 0)
				panic("can't unlink 'inprogress' WAL");
		} else if (r->current_wal->rows == 1) {
			/* Rename inprogress wal with one row */
			say_warn("rename unfinished %s WAL", r->current_wal->filename);
			if (inprogress_log_rename(r->current_wal->filename) != 0)
				panic("can't rename 'inprogress' WAL");
		} else
			panic("too many rows in inprogress WAL `%s'", r->current_wal->filename);

		log_io_close(&r->current_wal);
	}

	if ((r->flags & RECOVER_READONLY) == 0)
		wal_writer_start(r);
}


/* }}} */

/* {{{ Local recovery: support of hot standby and replication relay */

/**
 * This is used in local hot standby or replication
 * relay mode: look for changes in the wal_dir and apply them
 * locally or send to the replica.
 */
struct wal_watcher {
	/**
	 * Rescan the WAL directory in search for new WAL files
	 * every wal_dir_rescan_delay seconds.
	 */
	ev_timer dir_timer;
	/**
	 * When the latest WAL does not contain a EOF marker,
	 * re-read its tail on every change in file metadata.
	 */
	ev_stat stat;
	/** Path to the file being watched with 'stat'. */
	char filename[PATH_MAX+1];
};

static struct wal_watcher wal_watcher;

static void recovery_rescan_file(ev_stat *w, int revents __attribute__((unused)));

static void
recovery_watch_file(struct wal_watcher *watcher, struct log_io *wal)
{
	strncpy(watcher->filename, wal->filename, PATH_MAX);
	ev_stat_init(&watcher->stat, recovery_rescan_file, watcher->filename, 0.);
	ev_stat_start(&watcher->stat);
}

static void
recovery_stop_file(struct wal_watcher *watcher)
{
	ev_stat_stop(&watcher->stat);
}

static void
recovery_rescan_dir(ev_timer *w, int revents __attribute__((unused)))
{
	struct recovery_state *r = w->data;
	struct wal_watcher *watcher = r->watcher;
	struct log_io *save_current_wal = r->current_wal;

	int result = recover_remaining_wals(r);
	if (result < 0)
		panic("recover failed: %i", result);
	if (save_current_wal != r->current_wal) {
		if (save_current_wal != NULL)
			recovery_stop_file(watcher);
		if (r->current_wal != NULL)
			recovery_watch_file(watcher, r->current_wal);
	}
}

static void
recovery_rescan_file(ev_stat *w, int revents __attribute__((unused)))
{
	struct recovery_state *r = w->data;
	struct wal_watcher *watcher = r->watcher;
	int result = recover_wal(r, r->current_wal);
	if (result < 0)
		panic("recover failed");
	if (result == LOG_EOF) {
		say_info("done `%s' confirmed_lsn: %" PRIi64,
			 r->current_wal->filename,
			 r->confirmed_lsn);
		log_io_close(&r->current_wal);
		recovery_stop_file(watcher);
		/* Don't wait for wal_dir_rescan_delay. */
		recovery_rescan_dir(&watcher->dir_timer, 0);
	}
}

void
recovery_follow_local(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay)
{
	assert(r->watcher == NULL);
	assert(r->writer == NULL);

	struct wal_watcher  *watcher = r->watcher= &wal_watcher;

	ev_timer_init(&watcher->dir_timer, recovery_rescan_dir,
		      wal_dir_rescan_delay, wal_dir_rescan_delay);
	watcher->dir_timer.data = watcher->stat.data = r;
	ev_timer_start(&watcher->dir_timer);
	/*
	 * recover() leaves the current wal open if it has no
	 * EOF marker.
	 */
	if (r->current_wal != NULL)
		recovery_watch_file(watcher, r->current_wal);
}

static void
recovery_stop_local(struct recovery_state *r)
{
	struct wal_watcher *watcher = r->watcher;
	assert(ev_is_active(&watcher->dir_timer));
	ev_timer_stop(&watcher->dir_timer);
	if (ev_is_active(&watcher->stat))
		ev_stat_stop(&watcher->stat);

	r->watcher = NULL;
}

/* }}} */

/* {{{ WAL writer - maintain a Write Ahead Log for every change
 * in the data state.
 */

struct wal_write_request {
	STAILQ_ENTRY(wal_write_request) wal_fifo_entry;
	/* Auxiliary. */
	int res;
	struct fiber *fiber;
	struct row_v11 row;
};

/* Context of the WAL writer thread. */

struct wal_writer
{
	STAILQ_HEAD(wal_fifo, wal_write_request) input, output;
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	ev_async async;
	struct nbatch *batch;
	bool is_shutdown;
};

static pthread_once_t wal_writer_once = PTHREAD_ONCE_INIT;

static struct wal_writer wal_writer;

/**
 * A pthread_atfork() callback for a child process. Today we only
 * fork the master process to save a snapshot, and in the child
 * the WAL writer thread is not necessary and not present.
 */
static void
wal_writer_child()
{
	log_io_atfork(&recovery_state->current_wal);
	if (wal_writer.batch) {
		free(wal_writer.batch);
		wal_writer.batch = NULL;
	}
	/*
	 * Make sure that atexit() handlers in the child do
	 * not try to stop the non-existent thread.
	 * The writer is not used in the child.
	 */
	recovery_state->writer = NULL;
}

/**
 * Today a WAL writer is started once at start of the
 * server.  Nevertheless, use pthread_once() to make
 * sure we can start/stop the writer many times.
 */
static void
wal_writer_init_once()
{
	(void) tt_pthread_atfork(NULL, NULL, wal_writer_child);
}

/**
 * A watcher callback which is invoked whenever there
 * are requests in wal_writer->output. This callback is
 * associated with an internal WAL writer watcher and is
 * invoked in the front-end main event loop.
 *
 * ev_async, under the hood, is a simple pipe. The WAL
 * writer thread writes to that pipe whenever it's done
 * handling a pack of requests (look for ev_async_send()
 * call in the writer thread loop).
 */
static void
wal_writer_schedule(ev_watcher *watcher, int event __attribute__((unused)))
{
	struct wal_writer *writer = watcher->data;
	struct wal_fifo output;

	(void) tt_pthread_mutex_lock(&writer->mutex);
	output = writer->output;
	STAILQ_INIT(&writer->output);
	(void) tt_pthread_mutex_unlock(&writer->mutex);

	/*
	 * Can't use STAILQ_FOREACH since fiber_call()
	 * destroys the list entry.
	 */
	struct wal_write_request *req = STAILQ_FIRST(&output);
	while (req) {
		struct fiber *f = req->fiber;
		req = STAILQ_NEXT(req, wal_fifo_entry);
		fiber_call(f);
	}
}

/**
 * Initialize WAL writer context. Even though it's a singleton,
 * encapsulate the details just in case we may use
 * more writers in the future.
 */
static void
wal_writer_init(struct wal_writer *writer)
{
	/* I. Initialize the state. */
	pthread_mutexattr_t errorcheck;

	(void) tt_pthread_mutexattr_init(&errorcheck);

#ifndef NDEBUG
	(void) tt_pthread_mutexattr_settype(&errorcheck, PTHREAD_MUTEX_ERRORCHECK);
#endif
	/* Initialize queue lock mutex. */
	(void) tt_pthread_mutex_init(&writer->mutex, &errorcheck);
	(void) tt_pthread_mutexattr_destroy(&errorcheck);

	(void) tt_pthread_cond_init(&writer->cond, NULL);

	STAILQ_INIT(&writer->input);
	STAILQ_INIT(&writer->output);

	ev_async_init(&writer->async, (void *) wal_writer_schedule);
	writer->async.data = writer;

	(void) tt_pthread_once(&wal_writer_once, wal_writer_init_once);

	writer->batch = nbatch_alloc(sysconf(_SC_IOV_MAX));

	if (writer->batch == NULL)
		panic_syserror("nbatch_alloc");
}

/** Destroy a WAL writer structure. */
static void
wal_writer_destroy(struct wal_writer *writer)
{
	(void) tt_pthread_mutex_destroy(&writer->mutex);
	(void) tt_pthread_cond_destroy(&writer->cond);
	free(writer->batch);
}

/** WAL writer thread routine. */
static void *wal_writer_thread(void *worker_args);

/**
 * Initialize WAL writer, start the thread.
 *
 * @pre   The server has completed recovery from a snapshot
 *        and/or existing WALs. All WALs opened in read-only
 *        mode are closed.
 *
 * @param state			WAL writer meta-data.
 *
 * @return 0 success, -1 on error. On success, recovery->writer
 *         points to a newly created WAL writer.
 */
static int
wal_writer_start(struct recovery_state *r)
{
	assert(r->writer == NULL);
	assert(r->watcher == NULL);
	assert(r->current_wal == NULL);
	assert(wal_writer.is_shutdown == false);
	assert(STAILQ_EMPTY(&wal_writer.input));
	assert(STAILQ_EMPTY(&wal_writer.output));

	/* I. Initialize the state. */
	wal_writer_init(&wal_writer);
	r->writer = &wal_writer;

	ev_async_start(&wal_writer.async);

	/* II. Start the thread. */

	if (tt_pthread_create(&wal_writer.thread, NULL, wal_writer_thread, r)) {
		wal_writer_destroy(&wal_writer);
		r->writer = NULL;
		return -1;
	}
	return 0;
}

/** Stop and destroy the writer thread (at shutdown). */
void
wal_writer_stop(struct recovery_state *r)
{
	struct wal_writer *writer = r->writer;

	/* Stop the worker thread. */

	(void) tt_pthread_mutex_lock(&writer->mutex);
	writer->is_shutdown= true;
	(void) tt_pthread_cond_signal(&writer->cond);
	(void) tt_pthread_mutex_unlock(&writer->mutex);

	if (tt_pthread_join(writer->thread, NULL) != 0) {
		/* We can't recover from this in any reasonable way. */
		panic_syserror("WAL writer: thread join failed");
	}

	ev_async_stop(&writer->async);
	wal_writer_destroy(writer);

	r->writer = NULL;
}

/**
 * Pop a bulk of requests to write to disk to process.
 * Block on the condition only if we have no other work to
 * do. Loop in case of a spurious wakeup.
 */
static struct wal_fifo
wal_writer_pop(struct wal_writer *writer, bool input_was_empty)
{
	struct wal_fifo input;
	do {
		input = writer->input;
		STAILQ_INIT(&writer->input);
		if (STAILQ_EMPTY(&input) == false || input_was_empty == false)
			break;
		(void) tt_pthread_cond_wait(&writer->cond, &writer->mutex);
	} while (writer->is_shutdown == false);
	return input;
}

/**
 * If there is no current WAL, try to open it, and close the
 * previous WAL. We close the previous WAL only after opening
 * a new one to smoothly move local hot standby and replication
 * over to the next WAL.
 * If the current WAL has only 1 record, it means we need to
 * rename it from '.inprogress' to '.xlog'. We maintain
 * '.inprogress' WALs to ensure that, at any point in time,
 * an .xlog file contains at least 1 valid record.
 * In case of error, we try to close any open WALs.
 *
 * @post r->current_wal is in a good shape for writes or is NULL.
 * @return 0 in case of success, -1 on error.
 */
static int
wal_opt_rotate(struct log_io **wal, int rows_per_wal, struct log_dir *dir, u64 lsn)
{
	struct log_io *l = *wal, *wal_to_close = NULL;
	if (l == NULL || l->rows >= rows_per_wal || lsn % rows_per_wal == 0) {
		wal_to_close = l;
		l = NULL;
	}
	if (l) {
		/*
		 * Rename WAL after the first successful write
		 * to a name  without .inprogress suffix.
		 */
		if (l->rows == 1) {
			if (inprogress_log_rename(l->filename))
				log_io_close(&l);
		}
	} else {
		/* Open WAL with '.inprogress' suffix. */
		l = log_io_open_for_write(dir, lsn, INPROGRESS);
		/*
		 * Close the file *after* we create the new WAL, since
		 * this is when replication relays get an inotify alarm
		 * (when we close the file), and try to reopen the next
		 * WAL. In other words, make sure that replication relays
		 * try to open the next WAL only when it exists.
		 */
		if (wal_to_close) {
			/*
			 * We can not handle log_io_close()
			 * failure in any reasonable way.
			 * A warning is written to the server
			 * log file.
			 */
			log_io_close(&wal_to_close);
		}
	}
	assert(wal_to_close == NULL);
	*wal = l;
	return l ? 0 : -1;
}

static void
wal_opt_sync(struct log_io *wal, double sync_delay)
{
	static ev_tstamp last_sync = 0;

	if (sync_delay > 0 && ev_now() - last_sync >= sync_delay) {
		/*
		 * XXX: in case of error, we don't really know how
		 * many records were not written to disk: probably
		 * way more than the last one.
		 */
		(void) log_io_sync(wal);
		last_sync = ev_now();
	}
}

static struct wal_write_request *
wal_fill_batch(struct log_io *wal, struct nbatch *batch, int rows_per_wal,
	       struct wal_write_request *req)
{
	int max_rows = wal->is_inprogress ? 1 : rows_per_wal - wal->rows;
	/* Post-condition of successful by wal_opt_rotate(). */
	assert(max_rows > 0);
	nbatch_start(batch, max_rows);
	while (req) {
		struct row_v11 *row = &req->row;
		header_v11_sign(&row->header);
		nbatch_add(batch, row, row_v11_size(row));
		req = STAILQ_NEXT(req, wal_fifo_entry);
		if (nbatch_is_full(batch))
			break;
	}
	return req;
}

static int
wal_write_batch(struct log_io *wal, struct nbatch *batch,
		struct wal_write_request *req, struct wal_write_request *end)
{
	int rows_written = nbatch_write(batch, fileno(wal->f));
	wal->rows += rows_written;
	while (req != end) {
		req->res = rows_written-- > 0 ? 0 : -1;
		req = STAILQ_NEXT(req, wal_fifo_entry);
	}
	return rows_written >= 0 ? 0 : -1;
}

/** WAL writer thread main loop.  */
static void *
wal_writer_thread(void *worker_args)
{
	struct recovery_state *r = worker_args;
	struct wal_writer *writer = r->writer;
	bool input_was_empty = true;
	struct log_io **wal = &r->current_wal;
	struct nbatch *batch = writer->batch;

	(void) tt_pthread_mutex_lock(&writer->mutex);
	while (writer->is_shutdown == false) {
		struct wal_fifo input = wal_writer_pop(writer, input_was_empty);
		(void) tt_pthread_mutex_unlock(&writer->mutex);
		/*
		 * Wake up fibers waiting on the old list *here*
		 * since we need a membar for request out_lsn's to
		 * sync up.
		 */
		if (input_was_empty == false)
			ev_async_send(&writer->async);

		struct wal_write_request *req = STAILQ_FIRST(&input);
		input_was_empty = req == NULL;
		while (req) {
			if (wal_opt_rotate(wal, r->rows_per_wal, r->wal_dir,
					   req->row.header.lsn) != 0) {
				req->res = -1;
				req = STAILQ_NEXT(req, wal_fifo_entry);
				continue;
			}
			struct wal_write_request *end;
			end = wal_fill_batch(*wal, batch, r->rows_per_wal, req);
			(void) wal_write_batch(*wal, batch, req, end);
			wal_opt_sync(*wal, r->wal_fsync_delay);
			req = end;
		}
		(void) tt_pthread_mutex_lock(&writer->mutex);
		STAILQ_CONCAT(&writer->output, &input);
	}
	(void) tt_pthread_mutex_unlock(&writer->mutex);
	/*
	 * Handle the case when a shutdown request came before
	 * we were able to awake all fibers waiting on the
	 * previous pack.
	 */
	if (*wal != NULL)
		log_io_close(wal);
	if (input_was_empty == false)
		ev_async_send(&writer->async);
	return NULL;
}

/**
 * WAL writer main entry point: queue a single request
 * to be written to disk and wait until this task is completed.
 */
int
wal_write(struct recovery_state *r, i64 lsn, u64 cookie,
	  u16 op, struct tbuf *row)
{
	say_debug("wal_write lsn=%" PRIi64, lsn);
	ERROR_INJECT_RETURN(ERRINJ_WAL_IO);

	struct wal_writer *writer = r->writer;

	struct wal_write_request *req =
		palloc(fiber->gc_pool, sizeof(struct wal_write_request) +
		       sizeof(op) + row->size);

	req->fiber = fiber;
	req->res = -1;
	row_v11_fill(&req->row, lsn, XLOG, cookie, &op, sizeof(op),
		     row->data, row->size);

	(void) tt_pthread_mutex_lock(&writer->mutex);

	bool was_empty = STAILQ_EMPTY(&writer->input);

	STAILQ_INSERT_TAIL(&writer->input, req, wal_fifo_entry);

	if (was_empty)
		(void) tt_pthread_cond_signal(&writer->cond);

	(void) tt_pthread_mutex_unlock(&writer->mutex);

	fiber_yield();

	return req->res;
}

/* }}} */

/* {{{ SAVE SNAPSHOT and tarantool_box --cat */

void
snapshot_write_row(struct log_io *l, struct nbatch *batch,
		   const void *metadata, size_t metadata_len,
		   const void *data, size_t data_len)
{
	static int rows;
	static int bytes;
	ev_tstamp elapsed;
	static ev_tstamp last = 0;

	struct row_v11 *row = palloc(fiber->gc_pool,
				     sizeof(struct row_v11) +
				     data_len + metadata_len);

	row_v11_fill(row, 0, SNAP, snapshot_cookie,
		     metadata, metadata_len, data, data_len);
	header_v11_sign(&row->header);

	nbatch_add(batch, row, row_v11_size(row));

	if (++rows % 100000 == 0)
		say_crit("%.1fM rows written", rows / 1000000.);

	if (nbatch_is_full(batch)) {
		if (nbatch_write(batch, fileno(l->f)) != batch->rows)
			panic_syserror("nbatch_write");
		nbatch_start(batch, INT_MAX);
		prelease_after(fiber->gc_pool, 128 * 1024);
	}

	if (recovery_state->snap_io_rate_limit > 0) {
		if (last == 0) {
			ev_now_update();
			last = ev_now();
		}
		bytes += row_v11_size(row);
		while (bytes >= recovery_state->snap_io_rate_limit) {

			ev_now_update();
			elapsed = ev_now() - last;
			if (elapsed < 1)
				usleep(((1 - elapsed) * 1000000));

			ev_now_update();
			last = ev_now();
			bytes -= recovery_state->snap_io_rate_limit;
		}
	}
}

void
snapshot_save(struct recovery_state *r,
	      void (*f) (struct log_io *, struct nbatch *))
{
	struct log_io *snap;
	snap = log_io_open_for_write(r->snap_dir, r->confirmed_lsn,
				     INPROGRESS);
	if (snap == NULL)
		panic_status(errno, "Failed to save snapshot: failed to open file in write mode.");
	struct nbatch *batch = nbatch_alloc(sysconf(_SC_IOV_MAX));
	if (batch == NULL)
		panic_syserror("malloc");
	nbatch_start(batch, INT_MAX);
	/*
	 * While saving a snapshot, snapshot name is set to
	 * <lsn>.snap.inprogress. When done, the snapshot is
	 * renamed to <lsn>.snap.
	 */
	const char *final_filename =
		format_filename(r->snap_dir, r->confirmed_lsn, NONE);
	say_info("saving snapshot `%s'", final_filename);
	f(snap, batch);

	if (batch->rows && nbatch_write(batch, fileno(snap->f)) != batch->rows)
		panic_syserror("nbatch_write");

	if (log_io_sync(snap) < 0)
		panic("fsync");

	if (link(snap->filename, final_filename) == -1)
		panic_status(errno, "can't create hard link to snapshot");

	if (unlink(snap->filename) == -1)
		say_syserror("can't unlink 'inprogress' snapshot");

	free(batch);
	log_io_close(&snap);

	say_info("done");
}

/**
 * Read WAL/SNAPSHOT and invoke a callback on every record (used
 * for --cat command line option).
 * @retval 0  success
 * @retval -1 error
 */

int
read_log(const char *filename,
	 row_handler *xlog_handler, row_handler *snap_handler)
{
	struct log_dir *dir;
	row_handler *h;

	if (strstr(filename, wal_dir.filename_ext)) {
		dir = &wal_dir;
		h = xlog_handler;
	} else if (strstr(filename, snap_dir.filename_ext)) {
		dir = &snap_dir;
		h = snap_handler;
	} else {
		say_error("don't know how to read `%s'", filename);
		return -1;
	}

	FILE *f = fopen(filename, "r");
	struct log_io *l = log_io_open(dir, LOG_READ, filename, NONE, f);
	struct log_io_cursor i;

	log_io_cursor_open(&i, l);
	struct tbuf *row;
	while ((row = log_io_cursor_next(&i)))
		h(row);

	log_io_cursor_close(&i);
	log_io_close(&l);
	return 0;
}

/* }}} */

/*
 * vim: foldmethod=marker
 */
