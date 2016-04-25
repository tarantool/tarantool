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
#include "wal.h"

#include "vclock.h"
#include "fiber.h"
#include "fio.h"
#include "errinj.h"

#include "xlog.h"
#include "xrow.h"
#include "cbus.h"
#include "coeio.h"

const char *wal_mode_STRS[] = { "none", "write", "fsync", NULL };

/*
 * WAL writer - maintain a Write Ahead Log for every change
 * in the data state.
 *
 * @sic the members are arranged to ensure proper cache alignment,
 * members used mainly in tx thread go first, wal thread members
 * following.
 */
struct wal_writer
{
	/* ----------------- tx ------------------- */
	/* tx-wal message bus */
	struct cbus tx_wal_bus;
	/**
	 * The rollback queue. An accumulator for all requests
	 * that need to be rolled back. Also acts as a valve
	 * in wal_write() so that new requests never enter
	 * the wal-tx bus and are rolled back "on arrival".
	 */
	struct stailq rollback;
	/** A pipe from 'tx' thread to 'wal' */
	struct cpipe wal_pipe;
	/* ----------------- wal ------------------- */
	/** A setting from server configuration - rows_per_wal */
	int64_t rows_per_wal;
	/** Another one - wal_mode */
	enum wal_mode wal_mode;
	/** wal_dir, from the configuration file. */
	struct xdir wal_dir;
	/** 'wal' thread doing the writes. */
	struct cord cord;
	/** WAL writer thread's main fiber, saved to be able to stop it. */
	struct fiber *main_f;
	/**
	 * The vector clock of the WAL writer. It's a bit behind
	 * the vector clock of the transaction thread, since it
	 * "follows" the tx vector clock.
	 * By "following" we mean this: whenever a transaction
	 * is started in 'tx' thread, it's assigned a tentative
	 * LSN. If the transaction is rolled back, this LSN
	 * is abandoned. Otherwise, after the transaction is written
	 * to the log with this LSN, WAL writer vclock is advanced
	 * with this LSN and LSN becomes "real".
	 */
	struct vclock vclock;
	/** Return pipe from 'wal' to tx' */
	struct cpipe tx_pipe;
	/** Cached write buffer */
	struct fio_batch *batch;
	/** The current WAL file. */
	struct xlog *current_wal;
	/**
	 * Used if there was a WAL I/O error and we need to
	 * keep adding all incoming requests to the rollback
	 * queue, until the tx thread has recovered.
	 */
	struct cmsg in_rollback;
	/**
	 * WAL watchers, i.e. threads that should be alerted
	 * whenever there are new records appended to the journal.
	 * Used for replication relays.
	 */
	struct rlist watchers;
	/** The lock protecting the watchers list. */
	pthread_mutex_t watchers_mutex;
};

struct wal_msg: public cmsg {
	/** Input queue, on output contains all committed requests. */
	struct stailq commit;
	/**
	 * In case of rollback, contains the requests which must
	 * be rolled back.
	 */
	struct stailq rollback;
};

static struct wal_writer wal_writer_singleton;

struct wal_writer *wal = NULL;
struct rmean *rmean_tx_wal_bus;

static void
wal_write_to_disk(struct cmsg *msg);

static void
tx_schedule_commit(struct cmsg *msg);

static struct cmsg_hop wal_request_route[] = {
	{wal_write_to_disk, &wal_writer_singleton.tx_pipe},
	{tx_schedule_commit, NULL},
};

static void
wal_msg_create(struct wal_msg *batch)
{
	cmsg_init(batch, wal_request_route);
	stailq_create(&batch->commit);
	stailq_create(&batch->rollback);
}

static struct wal_msg *
wal_msg(struct cmsg *msg)
{
	return msg->route == wal_request_route ? (struct wal_msg *) msg : NULL;
}

/**
 * Invoke fibers waiting for their wal_request's to be
 * completed. The fibers are invoked in strict fifo order:
 * this ensures that, in case of rollback, requests are
 * rolled back in strict reverse order, producing
 * a consistent database state.
 */
static void
tx_schedule_queue(struct stailq *queue)
{
	/*
	 * fiber_wakeup() is faster than fiber_call() when there
	 * are many ready fibers.
	 */
	struct wal_request *req;
	stailq_foreach_entry(req, queue, fifo)
		fiber_wakeup(req->fiber);
}

/**
 * Complete execution of a batch of WAL write requests:
 * schedule all committed requests, and, should there
 * be any requests to be rolled back, append them to
 * the rollback queue.
 */
static void
tx_schedule_commit(struct cmsg *msg)
{
	struct wal_msg *batch = (struct wal_msg *) msg;
	/*
	 * Move the rollback list to the writer first, since
	 * wal_msg memory disappears after the first
	 * iteration of tx_schedule_queue loop.
	 */
	if (! stailq_empty(&batch->rollback)) {
		struct wal_writer *writer = wal;
		/* Closes the input valve. */
		stailq_concat(&writer->rollback, &batch->rollback);
	}
	tx_schedule_queue(&batch->commit);
}

static void
tx_schedule_rollback(struct cmsg *msg)
{
	(void) msg;
	struct wal_writer *writer = (struct wal_writer *) wal;
	/*
	 * Perform a cascading abort of all transactions which
	 * depend on the transaction which failed to get written
	 * to the write ahead log. Abort transactions
	 * in reverse order, performing a playback of the
	 * in-memory database state.
	 */
	stailq_reverse(&writer->rollback);
	/* Must not yield. */
	tx_schedule_queue(&writer->rollback);
	stailq_create(&writer->rollback);
}

/**
 * Initialize WAL writer context. Even though it's a singleton,
 * encapsulate the details just in case we may use
 * more writers in the future.
 */
static void
wal_writer_create(struct wal_writer *writer, enum wal_mode wal_mode,
		  const char *wal_dirname, const struct tt_uuid *server_uuid,
		  struct vclock *vclock, int64_t rows_per_wal)
{
	writer->wal_mode = wal_mode;
	writer->rows_per_wal = rows_per_wal;

	xdir_create(&writer->wal_dir, wal_dirname, XLOG, server_uuid);
	writer->current_wal = NULL;
	if (wal_mode == WAL_FSYNC)
		(void) strcat(writer->wal_dir.open_wflags, "s");
	cbus_create(&writer->tx_wal_bus);

	cpipe_create(&writer->tx_pipe);
	cpipe_create(&writer->wal_pipe);
	cpipe_set_max_input(&writer->wal_pipe, IOV_MAX);

	writer->batch = fio_batch_new();
	if (writer->batch == NULL)
		panic_syserror("fio_batch_alloc");

	stailq_create(&writer->rollback);
	cmsg_init(&writer->in_rollback, NULL);

	/* Create and fill writer->vclock. */
	vclock_create(&writer->vclock);
	vclock_copy(&writer->vclock, vclock);

	tt_pthread_mutex_init(&writer->watchers_mutex, NULL);
	rlist_create(&writer->watchers);
}

/** Destroy a WAL writer structure. */
static void
wal_writer_destroy(struct wal_writer *writer)
{
	xdir_destroy(&writer->wal_dir);
	cbus_destroy(&writer->tx_wal_bus);
	fio_batch_delete(writer->batch);
	tt_pthread_mutex_destroy(&writer->watchers_mutex);
}

/** WAL writer thread routine. */
static int
wal_writer_f(va_list ap);

/* Rotate the WAL. */
static int
wal_opt_rotate(struct wal_writer *writer);

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
void
wal_writer_start(enum wal_mode wal_mode, const char *wal_dirname,
		 const struct tt_uuid *server_uuid, struct vclock *vclock,
		 int64_t rows_per_wal)
{
	assert(rows_per_wal > 1);

	struct wal_writer *writer = &wal_writer_singleton;

	/* I. Initialize the state. */
	wal_writer_create(writer, wal_mode, wal_dirname, server_uuid,
			vclock, rows_per_wal);

	rmean_tx_wal_bus = writer->tx_wal_bus.stats;

	/* II. Start the thread. */

	if (cord_costart(&writer->cord, "wal", wal_writer_f, writer)) {
		wal_writer_destroy(writer);
		wal = NULL;
		panic("failed to start WAL thread");
	}
	cbus_join(&writer->tx_wal_bus, &writer->tx_pipe);
	wal = writer;
}

static void
wal_writer_stop_f(struct cmsg *msg)
{
	(void) msg;
	fiber_wakeup(wal->main_f);
}

/** Stop and destroy the writer thread (at shutdown). */
void
wal_writer_stop()
{
	struct wal_writer *writer = wal;

	/* Stop the worker thread. */
	struct cmsg wakeup;
	struct cmsg_hop route[1] = {
		{wal_writer_stop_f, NULL}
	};
	cmsg_init(&wakeup, route);

	cpipe_push(&writer->wal_pipe, &wakeup);
	ev_invoke(writer->wal_pipe.producer,
		  &writer->wal_pipe.flush_input, EV_CUSTOM);

	if (cord_join(&writer->cord)) {
		/* We can't recover from this in any reasonable way. */
		panic_syserror("WAL writer: thread join failed");
	}

	wal_writer_destroy(writer);

	rmean_tx_wal_bus = NULL;
	wal = NULL;
}

struct wal_checkpoint: public cmsg
{
	struct vclock *vclock;
	struct fiber *fiber;
	bool rotate;
};

void
wal_checkpoint_f(struct cmsg *data)
{
	struct wal_checkpoint *msg = (struct wal_checkpoint *) data;
	struct wal_writer *writer = wal;
	/*
	 * Avoid closing the current WAL if it has no rows (empty).
	 */
	if (msg->rotate && writer->current_wal != NULL &&
	    vclock_sum(&writer->current_wal->vclock) !=
	    vclock_sum(&writer->vclock)) {

		xlog_close(writer->current_wal);
		writer->current_wal = NULL;
		/*
		 * Avoid creating an empty xlog if this is the
		 * last snapshot before shutdown.
		 */
	}
	vclock_copy(msg->vclock, &writer->vclock);
}

void
wal_checkpoint_done_f(struct cmsg *data)
{
	struct wal_checkpoint *msg = (struct wal_checkpoint *) data;
	fiber_wakeup(msg->fiber);
}

void
wal_checkpoint(struct wal_writer *writer, struct vclock *vclock, bool rotate)
{
	static struct cmsg_hop wal_checkpoint_route[] = {
		{wal_checkpoint_f, &wal_writer_singleton.tx_pipe},
		{wal_checkpoint_done_f, NULL},
	};
	vclock_create(vclock);
	struct wal_checkpoint msg;
	cmsg_init(&msg, wal_checkpoint_route);
	msg.vclock = vclock;
	msg.fiber = fiber();
	msg.rotate = rotate;
	cpipe_push(&writer->wal_pipe, &msg);
	fiber_set_cancellable(false);
	fiber_yield();
	fiber_set_cancellable(true);
}

/**
 * If there is no current WAL, try to open it, and close the
 * previous WAL. We close the previous WAL only after opening
 * a new one to smoothly move local hot standby and replication
 * over to the next WAL.
 * In case of error, we try to close any open WALs.
 *
 * @post r->current_wal is in a good shape for writes or is NULL.
 * @return 0 in case of success, -1 on error.
 */
static int
wal_opt_rotate(struct wal_writer *writer)
{
	struct xlog *l = writer->current_wal, *wal_to_close = NULL;

	ERROR_INJECT_RETURN(ERRINJ_WAL_ROTATE);

	if (l != NULL && l->rows >= writer->rows_per_wal) {
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
		l = xlog_create(&writer->wal_dir, &writer->vclock);
	}
	assert(wal_to_close == NULL);
	writer->current_wal = l;
	return l ? 0 : -1;
}

/**
 * fio_batch_write() version with recovery specific
 * error injection.
 */
static inline ssize_t
wal_fio_batch_write(struct fio_batch *batch, int fd)
{
	ERROR_INJECT(ERRINJ_WAL_WRITE, return 0);
	return fio_batch_write(batch, fd);
}

static void
wal_writer_clear_bus(struct cmsg *msg)
{
	(void) msg;
}

static void
wal_writer_end_rollback(struct cmsg *msg)
{
	(void) msg;
	struct wal_writer *writer = wal;
	cmsg_init(&writer->in_rollback, NULL);
}

static void
wal_writer_begin_rollback(struct wal_writer *writer)
{
	static struct cmsg_hop rollback_route[4] = {
		/*
		 * Step 1: clear the bus, so that it contains
		 * no WAL write requests. This is achieved as a
		 * side effect of an empty message travelling
		 * through both bus pipes, while writer input
		 * valve is closed by non-empty writer->rollback
		 * list.
		 */
		{ wal_writer_clear_bus, &wal_writer_singleton.wal_pipe },
		{ wal_writer_clear_bus, &wal_writer_singleton.tx_pipe },
		/*
		 * Step 2: writer->rollback queue contains all
		 * messages which need to be rolled back,
		 * perform the rollback.
		 */
		{ tx_schedule_rollback, &wal_writer_singleton.wal_pipe },
		/*
		 * Step 3: re-open the WAL for writing.
		 */
		{ wal_writer_end_rollback, NULL }
	};

	/*
	 * Make sure the WAL writer rolls back
	 * all input until rollback mode is off.
	 */
	cmsg_init(&writer->in_rollback, rollback_route);
	cpipe_push(&writer->tx_pipe, &writer->in_rollback);
}

static void
wal_notify_watchers(struct wal_writer *writer);

static void
wal_write_to_disk(struct cmsg *msg)
{
	struct wal_writer *writer = wal;
	struct wal_msg *wal_msg = (struct wal_msg *) msg;

	if (writer->in_rollback.route != NULL) {
		/* We're rolling back a failed write. */
		stailq_concat(&wal_msg->rollback, &wal_msg->commit);
		return;
	}

	/* Xlog is only rotated between queue processing  */
	if (wal_opt_rotate(writer) != 0) {
		stailq_concat(&wal_msg->rollback, &wal_msg->commit);
		return wal_writer_begin_rollback(writer);
	}

	/*
	 * This code tries to write queued requests (=transactions) using as
	 * few I/O syscalls and memory copies as possible. For this reason
	 * writev(2) and `struct iovec[]` are used (see `struct fio_batch`).
	 *
	 * For each request (=transaction) each request row (=statement) is
	 * added to iov `batch`. A row can contain up to XLOG_IOVMAX iovecs.
	 * A request can have an **unlimited** number of rows. Since OS has
	 * a hard coded limit up to `sysconf(_SC_IOV_MAX)` iovecs (usually
	 * 1024), a huge transaction may not fit into a single batch.
	 * Therefore, it is not possible to "atomically" write an entire
	 * transaction using a single writev(2) call.
	 *
	 * Request boundaries and batch boundaries are not connected at all
	 * in this code. Batches flushed to disk as soon as they are full.
	 * In order to guarantee that a transaction is either fully written
	 * to file or isn't written at all, ftruncate(2) is used to shrink
	 * the file to the last fully written request. The absolute position
	 * of request in xlog file is stored inside `struct wal_request`.
	 */

	struct xlog *l = writer->current_wal;
	/* The size of batched data */
	off_t batched_bytes = 0;
	/* The size of written data */
	off_t written_bytes = 0;
	/* Start new iov batch */
	struct fio_batch *batch = writer->batch;
	fio_batch_reset(batch);

	/*
	 * Iterate over requests (transactions)
	 */
	struct wal_request *req;
	stailq_foreach_entry(req, &wal_msg->commit, fifo) {
		/* Save relative offset of request start */
		req->start_offset = batched_bytes;
		req->end_offset = -1;

		/*
		 * Iterate over request rows (tx statements)
		 */
		struct xrow_header **row = req->rows;
		for (; row < req->rows + req->n_rows; row++) {
			/* Check batch has enough space to fit statement */
			if (unlikely(fio_batch_unused(batch) < XROW_IOVMAX)) {
				/*
				 * No space in the batch for this statement,
				 * flush added statements and rotate batch.
				 */
				assert(fio_batch_size(batch) > 0);
				ssize_t nwr = wal_fio_batch_write(batch,
					fileno(l->f));
				if (nwr < 0)
					goto done; /* to break outer loop */

				/* Update cached file offset */
				written_bytes += nwr;
			}

			/* Add the statement to iov batch */
			struct iovec *iov = fio_batch_book(batch, XROW_IOVMAX);
			assert(iov != NULL); /* checked above */
			int iovcnt = xlog_encode_row(*row, iov);
			batched_bytes += fio_batch_add(batch, iovcnt);
		}

		/* Save relative offset of request end */
		req->end_offset = batched_bytes;
	}
	/* Flush remaining data in batch (if any) */
	while (fio_batch_size(batch) > 0) {
		ssize_t nwr = wal_fio_batch_write(batch, fileno(l->f));
		if (nwr > 0) {
			/* Update cached file offset */
			written_bytes += nwr;
		} else {
			break;
		}
	}

done:
	/*
	 * Iterate over `input` queue and add all processed requests to
	 * `commit` queue and all other to `rollback` queue.
	 */
	struct wal_request *reqend = req;
	for (req = stailq_first_entry(&wal_msg->commit, struct wal_request, fifo);
	     req != reqend;
	     req = stailq_next_entry(req, fifo)) {
		/*
		 * Check if request has been fully written to xlog.
		 */
		if (unlikely(req->end_offset == -1 ||
			     req->end_offset > written_bytes)) {
			/*
			 * This and all subsequent requests have failed
			 * to write. Truncate xlog to the end of last
			 * successfully written request.
			 */

			/* Calculate relative position of the good request */
			off_t garbage_bytes = written_bytes - req->start_offset;
			assert(garbage_bytes >= 0);

			/* Get absolute position */
			off_t good_offset = fio_lseek(fileno(l->f),
				-garbage_bytes, SEEK_CUR);
			if (good_offset < 0)
				panic_syserror("failed to get xlog position");

			/* Truncate xlog */
			if (ftruncate(fileno(l->f), good_offset) != 0)
				panic_syserror("failed to rollback xlog");
			written_bytes = req->start_offset;

			/* Move tail to `rollback` queue. */
			stailq_splice(&wal_msg->commit, &req->fifo, &wal_msg->rollback);
			wal_writer_begin_rollback(writer);
			break;
		}

		/* Update internal vclock */
		vclock_follow(&writer->vclock,
			      req->rows[req->n_rows - 1]->server_id,
			      req->rows[req->n_rows - 1]->lsn);
		/* Update row counter for wal_opt_rotate() */
		l->rows += req->n_rows;
		/* Mark request as successful for tx thread */
		req->res = vclock_sum(&writer->vclock);
	}

	fiber_gc();
	wal_notify_watchers(writer);
}

/** WAL writer thread main loop.  */
static int
wal_writer_f(va_list ap)
{
	struct wal_writer *writer = va_arg(ap, struct wal_writer *);
	/** Initialize eio in this thread */
	coeio_init();

	writer->main_f = fiber();
	cbus_join(&writer->tx_wal_bus, &writer->wal_pipe);

	fiber_yield();

	if (writer->current_wal != NULL) {
		xlog_close(writer->current_wal);
		writer->current_wal = NULL;
	}
	return 0;
}

/**
 * WAL writer main entry point: queue a single request
 * to be written to disk and wait until this task is completed.
 */
int64_t
wal_write(struct wal_writer *writer, struct wal_request *req)
{
	ERROR_INJECT_RETURN(ERRINJ_WAL_IO);

	if (! stailq_empty(&writer->rollback)) {
		/*
		 * The writer rollback queue is not empty,
		 * roll back this transaction immediately.
		 * This is to ensure we do not accidentally
		 * commit a transaction which has seen changes
		 * that will be rolled back.
		 */
		return -1;
	}

	req->fiber = fiber();
	req->res = -1;

	struct wal_msg *batch;
	if (!stailq_empty(&writer->wal_pipe.input) &&
	    (batch = wal_msg(stailq_first_entry(&writer->wal_pipe.input,
						struct cmsg, fifo)))) {

		stailq_add_tail_entry(&batch->commit, req, fifo);
	} else {
		batch = (struct wal_msg *)
			region_alloc_xc(&fiber()->gc,
					sizeof(struct wal_msg));
		wal_msg_create(batch);
		/*
		 * Sic: first add a request, then push the batch,
		 * since cpipe_push() may pass the batch to WAL
		 * thread right away.
		 */
		stailq_add_tail_entry(&batch->commit, req, fifo);
		cpipe_push(&writer->wal_pipe, batch);
	}
	writer->wal_pipe.n_input += req->n_rows * XROW_IOVMAX;
	cpipe_flush_input(&writer->wal_pipe);
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

int
wal_set_watcher(struct wal_writer *writer, struct wal_watcher *watcher,
		struct ev_async *async)
{

	if (writer == NULL)
		return -1;

	watcher->loop = loop();
	watcher->async = async;
	tt_pthread_mutex_lock(&writer->watchers_mutex);
	rlist_add_tail_entry(&writer->watchers, watcher, next);
	tt_pthread_mutex_unlock(&writer->watchers_mutex);
	return 0;
}

void
wal_clear_watcher(struct wal_writer *writer, struct wal_watcher *watcher)
{
	if (writer == NULL)
		return;

	tt_pthread_mutex_lock(&writer->watchers_mutex);
	rlist_del_entry(watcher, next);
	tt_pthread_mutex_unlock(&writer->watchers_mutex);
}

static void
wal_notify_watchers(struct wal_writer *writer)
{
	struct wal_watcher *watcher;
	/* notify watchers */
	tt_pthread_mutex_lock(&writer->watchers_mutex);
	rlist_foreach_entry(watcher, &writer->watchers, next) {
		ev_async_send(watcher->loop, watcher->async);
	}
	tt_pthread_mutex_unlock(&writer->watchers_mutex);
}


/**
 * After fork, the WAL writer thread disappears.
 * Make sure that atexit() handlers in the child do
 * not try to stop a non-existent thread or write
 * a second EOF marker to an open file.
 */
void
wal_atfork()
{
	if (wal) { /* NULL when forking for box.cfg{background = true} */
		xlog_atfork(&wal->current_wal);
		wal = NULL;
	}
}
