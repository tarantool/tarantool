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
#define MH_SOURCE 1
#include "recovery.h"

#include <fcntl.h>

#include "xlog.h"
#include "fiber.h"
#include "tt_pthread.h"
#include "fio.h"
#include "sio.h"
#include "errinj.h"
#include "bootstrap.h"

#include "replica.h"
#include "fiber.h"
#include "msgpuck/msgpuck.h"
#include "xrow.h"
#include "crc32.h"
#include "scoped_guard.h"
#include "box/cluster.h"
#include "vclock.h"
#include "session.h"
#include "coio.h"

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

const char *wal_mode_STRS[] = { "none", "write", "fsync", NULL };

/* {{{ LSN API */

static void
fill_lsn(struct recovery_state *r, struct xrow_header *row)
{
	if (row == NULL || row->server_id == 0) {
		/* Local request */
		int64_t lsn = vclock_inc(&r->vclock, r->server_id);
		/* row is NULL if wal_mode = NONE */
		if (row != NULL) {
			row->server_id = r->server_id;
			row->lsn = lsn;
		}
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

/* }}} */

/* {{{ Initial recovery */

static int
wal_writer_start(struct recovery_state *state, int rows_per_wal);
void
wal_writer_stop(struct recovery_state *r);

/**
 * Throws an exception in  case of error.
 */
struct recovery_state *
recovery_new(const char *snap_dirname, const char *wal_dirname,
	     apply_row_f apply_row, void *apply_row_param)
{
	struct recovery_state *r = (struct recovery_state *)
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
	r->signature = -1;
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
	recovery_init_remote(r);

	guard.is_active = false;
	return r;
}

void
recovery_update_mode(struct recovery_state *r, enum wal_mode mode)
{
	assert(mode < WAL_MODE_MAX);
	r->wal_mode = mode;
}

void
recovery_update_io_rate_limit(struct recovery_state *r, double new_limit)
{
	r->snap_io_rate_limit = new_limit * 1024 * 1024;
	if (r->snap_io_rate_limit == 0)
		r->snap_io_rate_limit = UINT64_MAX;
}

void
recovery_setup_panic(struct recovery_state *r, bool on_snap_error,
		     bool on_wal_error)
{
	r->wal_dir.panic_if_error = on_wal_error;
	r->snap_dir.panic_if_error = on_snap_error;
}

static inline void
recovery_close_log(struct recovery_state *r)
{
	xlog_close(r->current_wal);
	r->current_wal = NULL;
}

void
recovery_delete(struct recovery_state *r)
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
recovery_exit(struct recovery_state *r)
{
	/* Avoid fibers, there is no event loop */
	r->watcher = NULL;
	recovery_delete(r);
}

void
recovery_apply_row(struct recovery_state *r, struct xrow_header *row)
{
	/* Check lsn */
	int64_t current_lsn = vclock_get(&r->vclock, row->server_id);
	assert(current_lsn >= 0);
	if (row->lsn > current_lsn)
		r->apply_row(r, r->apply_row_param, row);
}

#define LOG_EOF 0

/**
 * @retval 0 OK, read full xlog.
 * @retval 1 OK, read some but not all rows, or no EOF marker
 */
int
recover_xlog(struct recovery_state *r, struct xlog *l)
{
	struct xlog_cursor i;

	xlog_cursor_open(&i, l);

	auto guard = make_scoped_guard([&]{
		xlog_cursor_close(&i);
	});

	struct xrow_header row;
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

	/*
	 * xlog_cursor_next() returns 1 when
	 * it can not read more rows. This doesn't mean
	 * the file is fully read: it's fully read only
	 * when EOF marker has been read. This is
	 * why eof_read is used here to indicate the
	 * end of log.
	 */
	return !i.eof_read;
}

void
recovery_bootstrap(struct recovery_state *r)
{
	/* Add a surrogate server id for snapshot rows */
	vclock_add_server(&r->vclock, 0);

	/* Recover from bootstrap.snap */
	say_info("initializing an empty data directory");
	const char *filename = "bootstrap.snap";
	FILE *f = fmemopen((void *) &bootstrap_bin,
			   sizeof(bootstrap_bin), "r");
	struct xlog *snap = xlog_open_stream(&r->snap_dir, 0, NONE, f,
					     filename);
	auto guard = make_scoped_guard([=]{
		xlog_close(snap);
	});
	/** The snapshot must have a EOF marker. */
	recover_xlog(r, snap);
}

/** Find out if there are new .xlog files since the current
 * LSN, and read them all up.
 *
 * This function will not close r->current_wal if
 * recovery was successful.
 */
static void
recover_remaining_wals(struct recovery_state *r)
{
	struct xlog *next_wal;
	int64_t current_signature, last_signature;
	struct vclock *current_vclock;
	enum log_suffix suffix;

	xdir_scan(&r->wal_dir);

	current_vclock = vclockset_last(&r->wal_dir.index);
	last_signature = current_vclock != NULL ?
		vclock_signature(current_vclock) : -1;
	/* If the caller already opened WAL for us, recover from it first */
	if (r->current_wal != NULL) {
		if (r->signature == -1) {
			r->signature
				= vclock_signature(&r->current_wal->vclock);
		}
		goto recover_current_wal;
	}

	while (1) {
		current_vclock = vclockset_match(&r->wal_dir.index,
						 &r->vclock,
						 r->wal_dir.panic_if_error);
		if (current_vclock == NULL)
			break; /* No more WALs */

		current_signature = vclock_signature(current_vclock);
		if (current_signature <= r->signature) {
			if (r->signature == last_signature)
				break;
			say_error("missing xlog between %020lld and %020lld",
				  (long long) current_signature,
				  (long long) last_signature);
			if (r->wal_dir.panic_if_error)
				break;

			/* Ignore missing WALs */
			say_warn("ignoring missing WALs");
			current_vclock = vclockset_next(&r->wal_dir.index,
							current_vclock);
			/* current_signature != last_signature */
			assert(current_vclock != NULL);
			current_signature = vclock_signature(current_vclock);
		}

		/*
		 * For the last WAL, first try to open .inprogress
		 * file: if it doesn't exist, we can safely try an
		 * .xlog, with no risk of a concurrent
		 * xlog_rename().
		 */
		suffix = current_signature == last_signature ? INPROGRESS : NONE;
		try {
			next_wal = xlog_open(&r->wal_dir, current_signature, suffix);
		} catch (XlogError *e) {
			e->log();
			break;
		}
		assert(r->current_wal == NULL);
		r->signature = current_signature;
		r->current_wal = next_wal;
		say_info("recover from `%s'", r->current_wal->filename);

recover_current_wal:
		int result = recover_xlog(r, r->current_wal);

		/* rows == 0 could indicate an empty WAL */
		if (r->current_wal->rows == 0) {
			say_error("read zero records from `%s`",
				  r->current_wal->filename);
			break;
		}
		if (result == LOG_EOF) {
			say_info("done `%s'", r->current_wal->filename);
			recovery_close_log(r);
			/* goto find_next_wal; */
		} else if (r->signature == last_signature) {
			/* last file is not finished */
			break;
		} else if (r->finalize && r->current_wal->is_inprogress) {
			say_warn("failed to find EOF in `%s`",
				 r->current_wal->filename);
			/* Let recovery_finalize deal with last file */
			break;
		} else {
			say_warn("WAL `%s` wasn't correctly closed",
				 r->current_wal->filename);
			recovery_close_log(r);
		}
	}

	/*
	 * It's not a fatal error when last WAL is empty, but if
	 * we lose some logs it is a fatal error.
	 */
	if (last_signature > r->signature) {
		tnt_raise(XlogError,
			  "not all WALs have been successfully read");
	}

	region_free(&fiber()->gc);
}

void
recovery_finalize(struct recovery_state *r, enum wal_mode wal_mode,
		  int rows_per_wal)
{
	recovery_stop_local(r);

	r->finalize = true;

	recover_remaining_wals(r);

	if (r->current_wal != NULL) {
		say_warn("WAL `%s' wasn't correctly closed", r->current_wal->filename);

		if (!r->current_wal->is_inprogress) {
			if (r->current_wal->rows == 0)
				/* Regular WAL (not inprogress) must contain at least one row */
				panic("zero rows was successfully read from last WAL '%s'",
				      r->current_wal->filename);
		} else if (r->current_wal->rows == 0) {
			/* Unlink empty inprogress WAL */
			say_warn("deleting broken WAL '%s'", r->current_wal->filename);
			if (inprogress_log_unlink(r->current_wal->filename) != 0)
				panic("can't unlink 'inprogress' WAL");
		} else if (r->current_wal->rows <= 1 /* one row */) {
			/* Rename inprogress wal with one row */
			say_warn("renaming unfinished WAL '%s'",
				 r->current_wal->filename);
			if (xlog_rename(r->current_wal) != 0)
				panic("can't rename 'inprogress' WAL '%s'",
				      r->current_wal->filename);
		} else {
			panic("too many rows in 'inprogress' WAL '%s'",
			      r->current_wal->filename);
		}

		recovery_close_log(r);
	}

	r->wal_mode = wal_mode;
	if (r->wal_mode == WAL_FSYNC)
		(void) strcat(r->wal_dir.open_wflags, "s");

	wal_writer_start(r, rows_per_wal);
}


/* }}} */

/* {{{ Local recovery: support of hot standby and replication relay */

static void
recovery_follow_f(va_list ap)
{
	struct recovery_state *r = va_arg(ap, struct recovery_state *);
	ev_tstamp wal_dir_rescan_delay = va_arg(ap, ev_tstamp);
	fiber_set_user(fiber(), &admin_credentials);

	while (! fiber_is_cancelled()) {
		recover_remaining_wals(r);
		/**
		 * Allow an immediate wakeup/break loop
		 * from recovery_stop_local().
		 */
		fiber_set_cancellable(true);
		if (r->current_wal != NULL) {
			ev_stat stat;
			coio_stat_init(&stat, r->current_wal->filename);
			coio_stat_stat_timeout(&stat, wal_dir_rescan_delay);
		} else {
			fiber_yield_timeout(wal_dir_rescan_delay);
		}
		fiber_set_cancellable(false);
	}
}

void
recovery_follow_local(struct recovery_state *r, const char *name,
		      ev_tstamp wal_dir_rescan_delay)
{
	assert(r->writer == NULL);
	assert(r->watcher == NULL);
	r->watcher = fiber_new(name, recovery_follow_f);
	fiber_set_joinable(r->watcher, true);
	fiber_start(r->watcher, r, wal_dir_rescan_delay);
}

void
recovery_stop_local(struct recovery_state *r)
{
	if (r->watcher) {
		struct fiber *f = r->watcher;
		r->watcher = NULL;
		fiber_cancel(f);
		fiber_join(f);
	}
}

/* }}} */

/* {{{ WAL writer - maintain a Write Ahead Log for every change
 * in the data state.
 */

struct wal_write_request {
	STAILQ_ENTRY(wal_write_request) wal_fifo_entry;
	/* Auxiliary. */
	int64_t res;
	struct fiber *fiber;
	struct xrow_header *row;
};

/* Context of the WAL writer thread. */
STAILQ_HEAD(wal_fifo, wal_write_request);

struct wal_writer
{
	struct wal_fifo input;
	struct wal_fifo commit;
	struct cord cord;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	ev_async write_event;
	int rows_per_wal;
	struct fio_batch *batch;
	bool is_shutdown;
	bool is_rollback;
	ev_loop *txn_loop;
	struct vclock vclock;
	bool is_started;
};

static struct wal_writer wal_writer;

/**
 * A pthread_atfork() callback for a child process. Today we only
 * fork the master process to save a snapshot, and in the child
 * the WAL writer thread is not necessary and not present.
 */
void
recovery_atfork(struct recovery_state *r)
{
	xlog_atfork(&r->current_wal);
	if (r->writer == NULL)
		return;
	if (r->writer->batch) {
		free(r->writer->batch);
		r->writer->batch = NULL;
	}
	/*
	 * Make sure that atexit() handlers in the child do
	 * not try to stop the non-existent thread.
	 * The writer is not used in the child.
	 */
	r->writer = NULL;
}

/**
 * A commit watcher callback is invoked whenever there
 * are requests in wal_writer->commit. This callback is
 * associated with an internal WAL writer watcher and is
 * invoked in the front-end main event loop.
 *
 * A rollback watcher callback is invoked only when there is
 * a rollback request and commit is empty.
 * We roll back the entire input queue.
 *
 * ev_async, under the hood, is a simple pipe. The WAL
 * writer thread writes to that pipe whenever it's done
 * handling a pack of requests (look for ev_async_send()
 * call in the writer thread loop).
 */
static void
wal_schedule_queue(struct wal_fifo *queue)
{
	/*
	 * Can't use STAILQ_FOREACH since fiber_call()
	 * destroys the list entry.
	 */
	struct wal_write_request *req, *tmp;
	STAILQ_FOREACH_SAFE(req, queue, wal_fifo_entry, tmp)
		fiber_call(req->fiber);
}

static void
wal_schedule(ev_loop * /* loop */, ev_async *watcher, int /* event */)
{
	struct wal_writer *writer = (struct wal_writer *) watcher->data;
	struct wal_fifo commit = STAILQ_HEAD_INITIALIZER(commit);
	struct wal_fifo rollback = STAILQ_HEAD_INITIALIZER(rollback);

	(void) tt_pthread_mutex_lock(&writer->mutex);
	STAILQ_CONCAT(&commit, &writer->commit);
	if (writer->is_rollback) {
		STAILQ_CONCAT(&rollback, &writer->input);
		writer->is_rollback = false;
	}
	(void) tt_pthread_mutex_unlock(&writer->mutex);

	wal_schedule_queue(&commit);
	/*
	 * Perform a cascading abort of all transactions which
	 * depend on the transaction which failed to get written
	 * to the write ahead log. Abort transactions
	 * in reverse order, performing a playback of the
	 * in-memory database state.
	 */
	STAILQ_REVERSE(&rollback, wal_write_request, wal_fifo_entry);
	wal_schedule_queue(&rollback);
}

/**
 * Initialize WAL writer context. Even though it's a singleton,
 * encapsulate the details just in case we may use
 * more writers in the future.
 */
static void
wal_writer_init(struct wal_writer *writer, struct vclock *vclock,
		int rows_per_wal)
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
	STAILQ_INIT(&writer->commit);

	ev_async_init(&writer->write_event, wal_schedule);
	writer->write_event.data = writer;
	writer->txn_loop = loop();
	writer->rows_per_wal = rows_per_wal;

	writer->batch = fio_batch_alloc(sysconf(_SC_IOV_MAX));

	if (writer->batch == NULL)
		panic_syserror("fio_batch_alloc");

	/* Create and fill writer->cluster hash */
	vclock_create(&writer->vclock);
	vclock_copy(&writer->vclock, vclock);
	writer->is_started = false;
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
wal_writer_start(struct recovery_state *r, int rows_per_wal)
{
	assert(r->writer == NULL);
	assert(r->current_wal == NULL);
	assert(rows_per_wal > 1);
	assert(! wal_writer.is_shutdown);
	assert(STAILQ_EMPTY(&wal_writer.input));
	assert(STAILQ_EMPTY(&wal_writer.commit));

	assert(wal_writer.is_started == false);
	/* I. Initialize the state. */
	wal_writer_init(&wal_writer, &r->vclock, rows_per_wal);
	r->writer = &wal_writer;

	ev_async_start(wal_writer.txn_loop, &wal_writer.write_event);

	/* II. Start the thread. */

	if (cord_start(&wal_writer.cord, "wal", wal_writer_thread, r)) {
		wal_writer_destroy(&wal_writer);
		r->writer = NULL;
		return -1;
	}
	wal_writer.is_started = true;
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
	if (cord_join(&writer->cord)) {
		/* We can't recover from this in any reasonable way. */
		panic_syserror("WAL writer: thread join failed");
	}

	ev_async_stop(writer->txn_loop, &writer->write_event);
	wal_writer.is_started = false;
	wal_writer_destroy(writer);

	r->writer = NULL;
}

/**
 * Pop a bulk of requests to write to disk to process.
 * Block on the condition only if we have no other work to
 * do. Loop in case of a spurious wakeup.
 */
void
wal_writer_pop(struct wal_writer *writer, struct wal_fifo *input)
{
	while (! writer->is_shutdown)
	{
		if (! writer->is_rollback && ! STAILQ_EMPTY(&writer->input)) {
			STAILQ_CONCAT(input, &writer->input);
			break;
		}
		(void) tt_pthread_cond_wait(&writer->cond, &writer->mutex);
	}
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
wal_opt_rotate(struct xlog **wal, struct recovery_state *r,
	       struct vclock *vclock)
{
	struct xlog *l = *wal, *wal_to_close = NULL;

	ERROR_INJECT_RETURN(ERRINJ_WAL_ROTATE);

	if (l != NULL && l->rows >= r->writer->rows_per_wal) {
		/*
		 * if l->rows == 1, xlog_close() does
		 * xlog_rename() for us.
		 */
		wal_to_close = l;
		l = NULL;
	}
	if (l == NULL) {
		/*
		 * Close the file *before* we create the new WAL, to
		 * make sure local hot standby/replication can see
		 * EOF in the old WAL before switching to the new
		 * one.
		 */
		if (wal_to_close) {
			/*
			 * We can not handle xlog_close()
			 * failure in any reasonable way.
			 * A warning is written to the server
			 * log file.
			 */
			xlog_close(wal_to_close);
			wal_to_close = NULL;
		}
		/* Open WAL with '.inprogress' suffix. */
		l = xlog_create(&r->wal_dir, vclock);
	} else if (l->rows == 1) {
		/*
		 * Rename WAL after the first successful write
		 * to a name  without .inprogress suffix.
		 */
		if (xlog_rename(l)) {
			xlog_close(l);       /* error. */
			l = NULL;
		}
	}
	assert(wal_to_close == NULL);
	*wal = l;
	return l ? 0 : -1;
}

static struct wal_write_request *
wal_fill_batch(struct xlog *wal, struct fio_batch *batch, int rows_per_wal,
	       struct wal_write_request *req)
{
	int max_rows = wal->is_inprogress ? 1 : rows_per_wal - wal->rows;
	/* Post-condition of successful wal_opt_rotate(). */
	assert(max_rows > 0);
	fio_batch_start(batch, max_rows);

	struct iovec iov[XROW_IOVMAX];
	while (req != NULL && !fio_batch_has_space(batch, nelem(iov))) {
		int iovcnt = xlog_encode_row(req->row, iov);
		fio_batch_add(batch, iov, iovcnt);
		req = STAILQ_NEXT(req, wal_fifo_entry);
	}
	return req;
}

static struct wal_write_request *
wal_write_batch(struct xlog *wal, struct fio_batch *batch,
		struct wal_write_request *req, struct wal_write_request *end,
		struct vclock *vclock)
{
	int rows_written = fio_batch_write(batch, fileno(wal->f));
	wal->rows += rows_written;
	while (req != end && rows_written-- != 0)  {
		vclock_follow(vclock, req->row->server_id, req->row->lsn);
		req->res = 0;
		req = STAILQ_NEXT(req, wal_fifo_entry);
	}
	return req;
}

static void
wal_write_to_disk(struct recovery_state *r, struct wal_writer *writer,
		  struct wal_fifo *input, struct wal_fifo *commit,
		  struct wal_fifo *rollback)
{
	struct xlog **wal = &r->current_wal;
	struct fio_batch *batch = writer->batch;

	struct wal_write_request *req = STAILQ_FIRST(input);
	struct wal_write_request *write_end = req;

	while (req) {
		if (wal_opt_rotate(wal, r, &writer->vclock) != 0)
			break;
		struct wal_write_request *batch_end;
		batch_end = wal_fill_batch(*wal, batch, writer->rows_per_wal,
					   req);
		write_end = wal_write_batch(*wal, batch, req, batch_end,
					    &writer->vclock);
		if (batch_end != write_end)
			break;
		req = write_end;
	}
	fiber_gc();
	STAILQ_SPLICE(input, write_end, wal_fifo_entry, rollback);
	STAILQ_CONCAT(commit, input);
}

/** WAL writer thread main loop.  */
static void *
wal_writer_thread(void *worker_args)
{
	struct recovery_state *r = (struct recovery_state *) worker_args;
	struct wal_writer *writer = r->writer;
	struct wal_fifo input = STAILQ_HEAD_INITIALIZER(input);
	struct wal_fifo commit = STAILQ_HEAD_INITIALIZER(commit);
	struct wal_fifo rollback = STAILQ_HEAD_INITIALIZER(rollback);

	(void) tt_pthread_mutex_lock(&writer->mutex);
	while (! writer->is_shutdown) {
		wal_writer_pop(writer, &input);
		(void) tt_pthread_mutex_unlock(&writer->mutex);

		wal_write_to_disk(r, writer, &input, &commit, &rollback);

		(void) tt_pthread_mutex_lock(&writer->mutex);
		STAILQ_CONCAT(&writer->commit, &commit);
		if (! STAILQ_EMPTY(&rollback)) {
			/*
			 * Begin rollback: create a rollback queue
			 * from all requests which were not
			 * written to disk and all requests in the
			 * input queue.
			 */
			writer->is_rollback = true;
			STAILQ_CONCAT(&rollback, &writer->input);
			STAILQ_CONCAT(&writer->input, &rollback);
		}
		ev_async_send(writer->txn_loop, &writer->write_event);
	}
	(void) tt_pthread_mutex_unlock(&writer->mutex);
	if (r->current_wal != NULL)
		recovery_close_log(r);
	return NULL;
}

/**
 * WAL writer main entry point: queue a single request
 * to be written to disk and wait until this task is completed.
 */
int64_t
wal_write(struct recovery_state *r, struct xrow_header *row)
{
	/*
	 * Bump current LSN even if wal_mode = NONE, so that
	 * snapshots still works with WAL turned off.
	 */
	fill_lsn(r, row);
	if (r->wal_mode == WAL_NONE)
		return 0;

	ERROR_INJECT_RETURN(ERRINJ_WAL_IO);

	struct wal_writer *writer = r->writer;

	struct wal_write_request *req = (struct wal_write_request *)
		region_alloc(&fiber()->gc, sizeof(struct wal_write_request));

	req->fiber = fiber();
	req->res = -1;
	req->row = row;
	row->tm = ev_now(loop());
	row->sync = 0;

	(void) tt_pthread_mutex_lock(&writer->mutex);

	bool input_was_empty = STAILQ_EMPTY(&writer->input);
	STAILQ_INSERT_TAIL(&writer->input, req, wal_fifo_entry);

	if (input_was_empty)
		(void) tt_pthread_cond_signal(&writer->cond);

	(void) tt_pthread_mutex_unlock(&writer->mutex);

	/**
	 * It's not safe to spuriously wakeup this fiber
	 * since in that case it will ignore a possible
	 * error from WAL writer and not roll back the
	 * transaction.
	 */
	bool cancellable = fiber_set_cancellable(false);
	fiber_yield(); /* Request was inserted. */
	fiber_set_cancellable(cancellable);
	return req->res;
}

/* }}} */

/* {{{ box.snapshot() */

int64_t
recovery_last_checkpoint(struct recovery_state *r)
{
	/* recover last snapshot lsn */
	struct vclock *vclock = vclockset_last(&r->snap_dir.index);
	return vclock ? vclock_signature(vclock) : -1;
}

/* }}} */

