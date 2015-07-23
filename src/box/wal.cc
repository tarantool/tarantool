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
#include "wal.h"

#include "recovery.h"

#include "fiber.h"
#include "fio.h"
#include "errinj.h"

#include "xrow.h"

const char *wal_mode_STRS[] = { "none", "write", "fsync", NULL };

/*
 * WAL writer - maintain a Write Ahead Log for every change
 * in the data state.
 */

/* Context of the WAL writer thread. */
STAILQ_HEAD(wal_fifo, wal_request);

struct wal_writer
{
	struct wal_fifo input;
	struct wal_fifo commit;
	struct cord cord;
	struct cbus tx_wal_bus;
	ev_async write_event;
	int rows_per_wal;
	struct fio_batch *batch;
	bool is_shutdown;
	bool is_rollback;
	ev_loop *txn_loop;
	struct vclock vclock;
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
	struct wal_request *req, *tmp;
	STAILQ_FOREACH_SAFE(req, queue, wal_fifo_entry, tmp)
		fiber_call(req->fiber);
}

static void
wal_schedule(ev_loop * /* loop */, ev_async *watcher, int /* event */)
{
	struct wal_writer *writer = (struct wal_writer *) watcher->data;
	struct wal_fifo commit = STAILQ_HEAD_INITIALIZER(commit);
	struct wal_fifo rollback = STAILQ_HEAD_INITIALIZER(rollback);

	cbus_lock(&writer->tx_wal_bus);
	STAILQ_CONCAT(&commit, &writer->commit);
	if (writer->is_rollback) {
		STAILQ_CONCAT(&rollback, &writer->input);
		writer->is_rollback = false;
	}
	cbus_unlock(&writer->tx_wal_bus);

	wal_schedule_queue(&commit);
	/*
	 * Perform a cascading abort of all transactions which
	 * depend on the transaction which failed to get written
	 * to the write ahead log. Abort transactions
	 * in reverse order, performing a playback of the
	 * in-memory database state.
	 */
	STAILQ_REVERSE(&rollback, wal_request, wal_fifo_entry);
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
	cbus_create(&writer->tx_wal_bus);

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
}

/** Destroy a WAL writer structure. */
static void
wal_writer_destroy(struct wal_writer *writer)
{
	cbus_destroy(&writer->tx_wal_bus);
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
int
wal_writer_start(struct recovery_state *r, int rows_per_wal)
{
	assert(r->writer == NULL);
	assert(r->current_wal == NULL);
	assert(rows_per_wal > 1);
	assert(! wal_writer.is_shutdown);
	assert(STAILQ_EMPTY(&wal_writer.input));
	assert(STAILQ_EMPTY(&wal_writer.commit));

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
	return 0;
}

/** Stop and destroy the writer thread (at shutdown). */
void
wal_writer_stop(struct recovery_state *r)
{
	struct wal_writer *writer = r->writer;

	/* Stop the worker thread. */

	cbus_lock(&writer->tx_wal_bus);
	writer->is_shutdown= true;
	cbus_signal(&writer->tx_wal_bus);
	cbus_unlock(&writer->tx_wal_bus);
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
		cbus_wait_signal(&writer->tx_wal_bus);
	}
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
wal_opt_rotate(struct xlog **wal, struct recovery_state *r,
	       struct vclock *vclock)
{
	struct xlog *l = *wal, *wal_to_close = NULL;

	ERROR_INJECT_RETURN(ERRINJ_WAL_ROTATE);

	if (l != NULL && l->rows >= r->writer->rows_per_wal) {
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
	}
	assert(wal_to_close == NULL);
	*wal = l;
	return l ? 0 : -1;
}

static struct wal_request *
wal_fill_batch(struct xlog *wal, struct fio_batch *batch, int rows_per_wal,
	       struct wal_request *req)
{
	int max_rows = rows_per_wal - wal->rows;
	/* Post-condition of successful wal_opt_rotate(). */
	assert(max_rows > 0);
	fio_batch_start(batch, max_rows);

	while (req != NULL && batch->rows < batch->max_rows) {
		int iovcnt = 0;
		struct iovec *iov;
		struct xrow_header **row = req->rows;
		for (; row < req->rows + req->n_rows; row++) {
			iov = fio_batch_book(batch, iovcnt, XROW_IOVMAX);
			if (iov == NULL) {
				/*
				 * No space in the batch for
				 * this transaction, open a new
				 * batch for it and hope that it
				 * is sufficient to hold it.
				 */
				return req;
			}
			iovcnt += xlog_encode_row(*row, iov);
		}
		fio_batch_add(batch, iovcnt);
		req = STAILQ_NEXT(req, wal_fifo_entry);
	}
	return req;
}

/**
 * fio_batch_write() version with recovery specific
 * error injection.
 */
static inline int
wal_fio_batch_write(struct fio_batch *batch, int fd)
{
	ERROR_INJECT(ERRINJ_WAL_WRITE, return 0);
	return fio_batch_write(batch, fd);
}

static struct wal_request *
wal_write_batch(struct xlog *wal, struct fio_batch *batch,
		struct wal_request *req, struct wal_request *end,
		struct vclock *vclock)
{
	int rows_written = wal_fio_batch_write(batch, fileno(wal->f));
	wal->rows += rows_written;
	while (req != end && rows_written-- != 0)  {
		vclock_follow(vclock,
			      req->rows[req->n_rows - 1]->server_id,
			      req->rows[req->n_rows - 1]->lsn);
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

	struct wal_request *req = STAILQ_FIRST(input);
	struct wal_request *write_end = req;

	while (req) {
		if (wal_opt_rotate(wal, r, &writer->vclock) != 0)
			break;
		struct wal_request *batch_end;
		batch_end = wal_fill_batch(*wal, batch, writer->rows_per_wal,
					   req);
		if (batch_end == req)
			break;
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

	cbus_lock(&writer->tx_wal_bus);
	while (! writer->is_shutdown) {
		wal_writer_pop(writer, &input);
		cbus_unlock(&writer->tx_wal_bus);

		wal_write_to_disk(r, writer, &input, &commit, &rollback);

		cbus_lock(&writer->tx_wal_bus);
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
	cbus_unlock(&writer->tx_wal_bus);
	if (r->current_wal != NULL) {
		xlog_close(r->current_wal);
		r->current_wal = NULL;
	}
	return NULL;
}

/**
 * WAL writer main entry point: queue a single request
 * to be written to disk and wait until this task is completed.
 */
int64_t
wal_write(struct recovery_state *r, struct wal_request *req)
{
	if (r->wal_mode == WAL_NONE)
		return vclock_sum(&r->vclock);

	ERROR_INJECT_RETURN(ERRINJ_WAL_IO);

	struct wal_writer *writer = r->writer;

	req->fiber = fiber();
	req->res = -1;

	cbus_lock(&writer->tx_wal_bus);

	bool input_was_empty = STAILQ_EMPTY(&writer->input);
	STAILQ_INSERT_TAIL(&writer->input, req, wal_fifo_entry);

	if (input_was_empty)
		cbus_signal(&writer->tx_wal_bus);

	cbus_unlock(&writer->tx_wal_bus);

	/**
	 * It's not safe to spuriously wakeup this fiber
	 * since in that case it will ignore a possible
	 * error from WAL writer and not roll back the
	 * transaction.
	 */
	bool cancellable = fiber_set_cancellable(false);
	fiber_yield(); /* Request was inserted. */
	fiber_set_cancellable(cancellable);
	if (req->res == -1)
		return -1;
	return vclock_sum(&r->vclock);
}

