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

#include "log_io.h"
#include "fiber.h"
#include "tt_pthread.h"
#include "fio.h"
#include "sio.h"
#include "errinj.h"
#include "bootstrap.h"

#include "replica.h"
#include "fiber.h"
#include "msgpuck/msgpuck.h"
#include "iproto_constants.h"
#include "crc32.h"
#include "scoped_guard.h"
#include "box/cluster.h"
#include "vclock.h"

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

const char *wal_mode_STRS[] = { "none", "write", "fsync", NULL };

/* {{{ LSN API */

static void
fill_lsn(struct recovery_state *r, struct iproto_header *row)
{
	if (row == NULL || row->server_id == 0) {
		/* Local request */
		int64_t lsn = vclock_inc(&r->vclock, r->server_id);
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
				  (unsigned) row->server_id);
		}
		vclock_follow(&r->vclock,  row->server_id, row->lsn);
	}
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
	      row_handler row_handler, void *row_handler_param,
	      snapshot_handler snapshot_handler, join_handler join_handler,
	      int rows_per_wal)
{
	assert(recovery_state == NULL);
	recovery_state = (struct recovery_state *) calloc(1, sizeof(struct recovery_state));
	struct recovery_state *r = recovery_state;
	recovery_update_mode(r, WAL_NONE);

	assert(rows_per_wal > 1);

	r->row_handler = row_handler;
	r->row_handler_param = row_handler_param;
	r->signature = -1;

	r->snapshot_handler = snapshot_handler;
	r->join_handler = join_handler;

	log_dir_create(&r->snap_dir, snap_dirname, SNAP);

	log_dir_create(&r->wal_dir, wal_dirname, XLOG);

	if (r->wal_mode == WAL_FSYNC)
		(void) strcat(r->wal_dir.open_wflags, "s");

	r->rows_per_wal = rows_per_wal;

	vclock_create(&r->vclock);

	if (log_dir_scan(&r->snap_dir) != 0)
		panic("can't scan snapshot directory");
	if (log_dir_scan(&r->wal_dir) != 0)
		panic("can't scan WAL directory");
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
recovery_free()
{
	struct recovery_state *r = recovery_state;
	if (r == NULL)
		return;

	if (r->watcher)
		recovery_stop_local(r);

	if (r->writer)
		wal_writer_stop(r);

	log_dir_destroy(&r->snap_dir);
	log_dir_destroy(&r->wal_dir);
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
	r->wal_dir.panic_if_error = on_wal_error;
	r->snap_dir.panic_if_error = on_snap_error;
}

void
recovery_process(struct recovery_state *r, struct iproto_header *row)
{
	/* Check lsn */
	int64_t current_signt = vclock_get(&r->vclock, row->server_id);
	if (current_signt >=0 && row->lsn <= current_signt) {
		say_debug("skipping too young row");
		return;
	}

	return r->row_handler(r->row_handler_param, row);
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
	struct log_io *snap = log_io_open_stream_for_read(&r->snap_dir,
		filename, NULL, NONE, f);
	if (snap == NULL)
		panic("failed to open %s", filename);
	int rc = recover_wal(r, snap);
	log_io_close(&snap);

	if (rc != 0)
		panic("failed to recover from %s", filename);
}

/**
 * Read a snapshot and call row_handler for every snapshot row.
 * Panic in case of error.
 */
void
recover_snap(struct recovery_state *r)
{
	/* There's no current_wal during initial recover. */
	assert(r->current_wal == NULL);
	say_info("recovery start");

	struct log_io *snap;
	int64_t sign;

	if (log_dir_scan(&r->snap_dir) != 0)
		panic("can't scan snapshot directory");
	struct vclock *res = vclockset_last(&r->snap_dir.index);
	if (res == NULL)
		panic("can't find snapshot");
	sign = vclock_signature(res);
	snap = log_io_open_for_read(&r->snap_dir, sign, NULL, NONE);
	if (snap == NULL)
		panic("can't open snapshot");

	/* Save server uuid */
	memcpy(&r->server_uuid, &snap->server_uuid, sizeof(r->server_uuid));

	/* Add a surrogate server id for snapshot rows */
	vclock_add_server(&r->vclock, 0);

	say_info("recovering from `%s'", snap->filename);
	if (recover_wal(r, snap) != 0)
		panic("can't process snapshot");

	/* Replace server vclock using data from snapshot */
	vclock_copy(&r->vclock, &snap->vclock);
}

#define LOG_EOF 0

/**
 * @retval -1 error
 * @retval 0 EOF
 * @retval 1 ok, maybe read something
 */
int
recover_wal(struct recovery_state *r, struct log_io *l)
{
	int res = -1;
	struct log_io_cursor i;

	log_io_cursor_open(&i, l);

	struct iproto_header row;
	while (log_io_cursor_next(&i, &row) == 0) {
		try {
			recovery_process(r, &row);
		} catch (SocketError *e) {
			say_error("can't apply row: %s", e->errmsg());
			goto end;
		} catch (Exception *e) {
			say_error("can't apply row: %s", e->errmsg());
			if (l->dir->panic_if_error)
				goto end;
		}
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
	int64_t current_signt, last_signt;
	struct vclock *current_vclock;
	size_t rows_before;
	enum log_suffix suffix;

	if (log_dir_scan(&r->wal_dir) != 0)
		return -1;

	current_vclock = vclockset_last(&r->wal_dir.index);
	last_signt = current_vclock != NULL ?
		vclock_signature(current_vclock) : -1;
	/* if the caller already opened WAL for us, recover from it first */
	if (r->current_wal != NULL)
		goto recover_current_wal;

	while (1) {
		current_vclock = vclockset_isearch(&r->wal_dir.index, &r->vclock);
		if (current_vclock == NULL)
			break; /* No more WALs */

		current_signt = vclock_signature(current_vclock);
		if (current_signt == r->signature) {
			if (current_signt != last_signt) {
				say_error("missing xlog between %020lld and %020lld",
					  (long long) current_signt,
					  (long long) last_signt);
			}
			break;
		}

		/*
		 * For the last WAL, first try to open .inprogress
		 * file: if it doesn't exist, we can safely try an
		 * .xlog, with no risk of a concurrent
		 * inprogress_log_rename().
		 */
		suffix = current_signt == last_signt ? INPROGRESS : NONE;
		next_wal = log_io_open_for_read(&r->wal_dir, current_signt,
			&r->server_uuid, suffix);
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
				if (inprogress_log_unlink(format_filename(
				    &r->wal_dir, current_signt, INPROGRESS)) != 0)
					panic("can't unlink 'inprogres' WAL");
			}
			result = 0;
			break;
		}
		assert(r->current_wal == NULL);
		r->signature = current_signt;
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
			say_info("done `%s'", r->current_wal->filename);
			log_io_close(&r->current_wal);
			/* goto find_next_wal; */
		} else if (r->signature == last_signt) {
			/* last file is not finished */
			break;
		} else if (r->finalize && r->current_wal->is_inprogress) {
			say_warn("fail to find eof on inprogress");
			/* Let recovery_finalize deal with last file */
			break;
		} else if (r->current_wal->retry++ < 3) {
			/*
			 * If a newer WAL appeared in the directory before
			 * current_wal was fully read, try re-reading
			 * one last time. */
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

	/*
	 * It's not a fatal error when last WAL is empty, but if
	 * we lose some logs it is a fatal error.
	 */
	if (last_signt > r->signature) {
		say_error("not all WALs have been successfully read");
		result = -1;
	}

#if 0
	region_free(&fiber()->gc);
#endif
	return result;
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
		} else if (r->current_wal->rows <= 1 /* one row */) {
			/* Rename inprogress wal with one row */
			say_warn("rename unfinished %s WAL", r->current_wal->filename);
			if (inprogress_log_rename(r->current_wal) != 0)
				panic("can't rename 'inprogress' WAL");
		} else
			panic("too many rows in inprogress WAL `%s'", r->current_wal->filename);

		log_io_close(&r->current_wal);
	}

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

static void recovery_rescan_file(ev_loop *, ev_stat *w, int /* revents */);

static void
recovery_watch_file(ev_loop *loop, struct wal_watcher *watcher,
		    struct log_io *wal)
{
	strncpy(watcher->filename, wal->filename, PATH_MAX);
	ev_stat_init(&watcher->stat, recovery_rescan_file,
		     watcher->filename, 0.);
	ev_stat_start(loop, &watcher->stat);
}

static void
recovery_stop_file(struct wal_watcher *watcher)
{
	ev_stat_stop(loop(), &watcher->stat);
}

static void
recovery_rescan_dir(ev_loop * loop, ev_timer *w, int /* revents */)
{
	struct recovery_state *r = (struct recovery_state *) w->data;
	struct wal_watcher *watcher = r->watcher;
	struct log_io *save_current_wal = r->current_wal;

	int result = recover_remaining_wals(r);
	if (result < 0)
		panic("recover failed: %i", result);
	if (save_current_wal != r->current_wal) {
		if (save_current_wal != NULL)
			recovery_stop_file(watcher);
		if (r->current_wal != NULL)
			recovery_watch_file(loop, watcher, r->current_wal);
	}
}

static void
recovery_rescan_file(ev_loop * loop, ev_stat *w, int /* revents */)
{
	struct recovery_state *r = (struct recovery_state *) w->data;
	struct wal_watcher *watcher = r->watcher;
	int result = recover_wal(r, r->current_wal);
	if (result < 0)
		panic("recover failed");
	if (result == LOG_EOF) {
		say_info("done `%s'", r->current_wal->filename);
		log_io_close(&r->current_wal);
		recovery_stop_file(watcher);
		/* Don't wait for wal_dir_rescan_delay. */
		recovery_rescan_dir(loop, &watcher->dir_timer, 0);
	}
}

void
recovery_follow_local(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay)
{
	assert(r->watcher == NULL);
	assert(r->writer == NULL);
	ev_loop *loop = loop();

	struct wal_watcher  *watcher = r->watcher= &wal_watcher;

	ev_timer_init(&watcher->dir_timer, recovery_rescan_dir,
		      wal_dir_rescan_delay, wal_dir_rescan_delay);
	watcher->dir_timer.data = watcher->stat.data = r;
	ev_timer_start(loop, &watcher->dir_timer);
	/*
	 * recover() leaves the current wal open if it has no
	 * EOF marker.
	 */
	if (r->current_wal != NULL)
		recovery_watch_file(loop, watcher, r->current_wal);
}

static void
recovery_stop_local(struct recovery_state *r)
{
	struct wal_watcher *watcher = r->watcher;
	assert(ev_is_active(&watcher->dir_timer));
	ev_timer_stop(loop(), &watcher->dir_timer);
	if (ev_is_active(&watcher->stat))
		ev_stat_stop(loop(), &watcher->stat);

	r->watcher = NULL;
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
	struct iproto_header *row;
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
	struct fio_batch *batch;
	bool is_shutdown;
	bool is_rollback;
	ev_loop *txn_loop;
	struct vclock vclock;
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
wal_writer_init(struct wal_writer *writer, struct vclock *vclock)
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

	(void) tt_pthread_once(&wal_writer_once, wal_writer_init_once);

	writer->batch = fio_batch_alloc(sysconf(_SC_IOV_MAX));

	if (writer->batch == NULL)
		panic_syserror("fio_batch_alloc");

	/* Create and fill writer->cluster hash */
	vclock_create(&writer->vclock);
	vclock_copy(&writer->vclock, vclock);
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
	assert(! wal_writer.is_shutdown);
	assert(STAILQ_EMPTY(&wal_writer.input));
	assert(STAILQ_EMPTY(&wal_writer.commit));

	/* I. Initialize the state. */
	wal_writer_init(&wal_writer, &r->vclock);
	r->writer = &wal_writer;

	ev_async_start(wal_writer.txn_loop, &wal_writer.write_event);

	/* II. Start the thread. */

	if (cord_start(&wal_writer.cord, "wal", wal_writer_thread, r)) {
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
	if (cord_join(&writer->cord)) {
		/* We can't recover from this in any reasonable way. */
		panic_syserror("WAL writer: thread join failed");
	}

	ev_async_stop(writer->txn_loop, &writer->write_event);
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
wal_opt_rotate(struct log_io **wal, struct recovery_state *r,
	       struct vclock *vclock)
{
	struct log_io *l = *wal, *wal_to_close = NULL;

	ERROR_INJECT_RETURN(ERRINJ_WAL_ROTATE);

	if (l != NULL && l->rows >= r->rows_per_wal) {
		/*
		 * if l->rows == 1, log_io_close() does
		 * inprogress_log_rename() for us.
		 */
		wal_to_close = l;
		l = NULL;
	}
	if (l == NULL) {
		/* Open WAL with '.inprogress' suffix. */
		l = log_io_open_for_write(&r->wal_dir, &r->server_uuid, vclock);
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
	} else if (l->rows == 1) {
		/*
		 * Rename WAL after the first successful write
		 * to a name  without .inprogress suffix.
		 */
		if (inprogress_log_rename(l))
			log_io_close(&l);       /* error. */
	}
	assert(wal_to_close == NULL);
	*wal = l;
	return l ? 0 : -1;
}

static struct wal_write_request *
wal_fill_batch(struct log_io *wal, struct fio_batch *batch, int rows_per_wal,
	       struct wal_write_request *req)
{
	int max_rows = wal->is_inprogress ? 1 : rows_per_wal - wal->rows;
	/* Post-condition of successful wal_opt_rotate(). */
	assert(max_rows > 0);
	fio_batch_start(batch, max_rows);

	struct iovec iov[XLOG_ROW_IOVMAX];
	while (req != NULL && !fio_batch_has_space(batch, nelem(iov))) {
		int iovcnt = xlog_encode_row(req->row, iov);
		fio_batch_add(batch, iov, iovcnt);
		req = STAILQ_NEXT(req, wal_fifo_entry);
	}
	return req;
}

static struct wal_write_request *
wal_write_batch(struct log_io *wal, struct fio_batch *batch,
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
	struct log_io **wal = &r->current_wal;
	struct fio_batch *batch = writer->batch;

	struct wal_write_request *req = STAILQ_FIRST(input);
	struct wal_write_request *write_end = req;

	while (req) {
		if (wal_opt_rotate(wal, r, &writer->vclock) != 0)
			break;
		struct wal_write_request *batch_end;
		batch_end = wal_fill_batch(*wal, batch, r->rows_per_wal,
					   req);
		write_end = wal_write_batch(*wal, batch, req, batch_end,
					    &writer->vclock);
		if (batch_end != write_end)
			break;
		req = write_end;
	}
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
		log_io_close(&r->current_wal);
	return NULL;
}

/**
 * WAL writer main entry point: queue a single request
 * to be written to disk and wait until this task is completed.
 */
int
wal_write(struct recovery_state *r, struct iproto_header *row)
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

	fiber_yield(); /* Request was inserted. */

	/* req->res is -1 on error */
	if (req->res < 0)
		return -1; /* error */

	return 0; /* success */
}

/* }}} */

/* {{{ box.snapshot() */

void
snapshot_write_row(struct log_io *l, struct iproto_header *row)
{
	static uint64_t bytes;
	ev_tstamp elapsed;
	static ev_tstamp last = 0;
	ev_loop *loop = loop();

	row->tm = last;
	row->server_id = 0;
	/**
	 * Rows in snapshot are numbered from 1 to %rows.
	 * This makes streaming such rows to a replica or
	 * to recovery look similar to streaming a normal
	 * WAL. @sa the place which skips old rows in
	 * recovery_process().
	 */
	if (iproto_request_is_dml(row->type))
		row->lsn = ++l->rows;
	row->sync = 0; /* don't write sync to wal */

	struct iovec iov[XLOG_ROW_IOVMAX];
	int iovcnt = xlog_encode_row(row, iov);

	/* TODO: use writev here */
	for (int i = 0; i < iovcnt; i++) {
		if (fwrite(iov[i].iov_base, iov[i].iov_len, 1, l->f) != 1) {
			say_error("Can't write row (%zu bytes)",
				  iov[i].iov_len);
			panic_syserror("snapshot_write_row");
		}
		bytes += iov[i].iov_len;
	}

	if (l->rows % 100000 == 0)
		say_crit("%.1fM rows written", l->rows / 1000000.);

	region_free_after(&fiber()->gc, 128 * 1024);

	if (recovery_state->snap_io_rate_limit != UINT64_MAX) {
		if (last == 0) {
			/*
			 * Remember the time of first
			 * write to disk.
			 */
			ev_now_update(loop);
			last = ev_now(loop);
		}
		/**
		 * If io rate limit is set, flush the
		 * filesystem cache, otherwise the limit is
		 * not really enforced.
		 */
		if (bytes > recovery_state->snap_io_rate_limit)
			fdatasync(fileno(l->f));
	}
	while (bytes > recovery_state->snap_io_rate_limit) {
		ev_now_update(loop);
		/*
		 * How much time have passed since
		 * last write?
		 */
		elapsed = ev_now(loop) - last;
		/*
		 * If last write was in less than
		 * a second, sleep until the
		 * second is reached.
		 */
		if (elapsed < 1)
			usleep(((1 - elapsed) * 1000000));

		ev_now_update(loop);
		last = ev_now(loop);
		bytes -= recovery_state->snap_io_rate_limit;
	}
}

void
snapshot_save(struct recovery_state *r)
{
	assert(r->snapshot_handler != NULL);
	struct log_io *snap = log_io_open_for_write(&r->snap_dir,
		&r->server_uuid, &r->vclock);
	if (snap == NULL)
		panic_status(errno, "Failed to save snapshot: failed to open file in write mode.");
	/*
	 * While saving a snapshot, snapshot name is set to
	 * <lsn>.snap.inprogress. When done, the snapshot is
	 * renamed to <lsn>.snap.
	 */
	say_info("saving snapshot `%s'", snap->filename);

	r->snapshot_handler(snap);

	log_io_close(&snap);

	say_info("done");
}

/* }}} */

