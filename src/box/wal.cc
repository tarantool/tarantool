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
struct wal_writer
{
	struct cord cord;
	struct cpipe tx_pipe;
	struct cpipe wal_pipe;
	struct cbus tx_wal_bus;
	int rows_per_wal;
	struct fio_batch *batch;
	bool is_shutdown;
	bool is_rollback;
	ev_loop *txn_loop;
	struct vclock vclock;
	pthread_mutex_t watchers_mutex;
	struct rlist watchers;
};

static void
wal_flush_input(ev_loop * /* loop */, ev_async *watcher, int /* event */)
{
	struct cpipe *pipe = (struct cpipe *) watcher->data;

	cbus_lock(pipe->bus);
	bool input_was_empty = stailq_empty(&pipe->pipe);
	stailq_concat(&pipe->pipe, &pipe->input);
	cbus_unlock(pipe->bus);

	if (input_was_empty)
		cbus_signal(pipe->bus);
	pipe->n_input = 0;
}
/**
 * A commit watcher callback is invoked whenever there
 * are requests in wal_writer->tx.pipe. This callback is
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
tx_schedule_queue(struct stailq *queue)
{
	/*
	 * Can't use stailq_foreach since fiber_call()
	 * destroys the list entry.
	 */
	struct wal_request *req, *tmp;
	stailq_foreach_entry_safe(req, tmp, queue, fifo)
		fiber_call(req->fiber);
}

static void
tx_fetch_output(ev_loop * /* loop */, ev_async *watcher, int /* event */)
{
	struct wal_writer *writer = (struct wal_writer *) watcher->data;
	struct stailq commit;
	struct stailq rollback;
	stailq_create(&commit);
	stailq_create(&rollback);

	bool is_rollback;
	cbus_lock(&writer->tx_wal_bus);
	stailq_concat(&commit, &writer->tx_pipe.pipe);
	is_rollback = writer->is_rollback;
	if (is_rollback)
		stailq_concat(&rollback, &writer->wal_pipe.pipe);
	writer->is_rollback = false;
	cbus_unlock(&writer->tx_wal_bus);
	if (is_rollback)
		stailq_concat(&rollback, &writer->wal_pipe.input);

	tx_schedule_queue(&commit);
	/*
	 * Perform a cascading abort of all transactions which
	 * depend on the transaction which failed to get written
	 * to the write ahead log. Abort transactions
	 * in reverse order, performing a playback of the
	 * in-memory database state.
	 */
	stailq_reverse(&rollback);
	tx_schedule_queue(&rollback);
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

	cpipe_create(&writer->tx_pipe);
	cpipe_set_fetch_cb(&writer->tx_pipe, tx_fetch_output, writer);

	writer->rows_per_wal = rows_per_wal;

	writer->batch = fio_batch_new();

	if (writer->batch == NULL)
		panic_syserror("fio_batch_alloc");

	/* Create and fill writer->cluster hash */
	vclock_create(&writer->vclock);
	vclock_copy(&writer->vclock, vclock);

	tt_pthread_mutex_init(&writer->watchers_mutex, NULL);
	rlist_create(&writer->watchers);
}

/** Destroy a WAL writer structure. */
static void
wal_writer_destroy(struct wal_writer *writer)
{
	cpipe_destroy(&writer->tx_pipe);
	cbus_destroy(&writer->tx_wal_bus);
	fio_batch_delete(writer->batch);
	tt_pthread_mutex_destroy(&writer->watchers_mutex);
}

/** WAL writer thread routine. */
static void wal_writer_f(va_list ap);

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
wal_writer_start(struct recovery *r, int rows_per_wal)
{
	assert(r->writer == NULL);
	assert(r->current_wal == NULL);
	assert(rows_per_wal > 1);

	static struct wal_writer wal_writer;

	struct wal_writer *writer = &wal_writer;
	r->writer = writer;

	/* I. Initialize the state. */
	wal_writer_init(writer, &r->vclock, rows_per_wal);

	/* II. Start the thread. */

	if (cord_costart(&writer->cord, "wal", wal_writer_f, r)) {
		wal_writer_destroy(writer);
		r->writer = NULL;
		return -1;
	}
	cbus_join(&writer->tx_wal_bus, &writer->tx_pipe);
	cpipe_set_flush_cb(&writer->wal_pipe, wal_flush_input,
			   &writer->wal_pipe);
	return 0;
}

/** Stop and destroy the writer thread (at shutdown). */
void
wal_writer_stop(struct recovery *r)
{
	struct wal_writer *writer = r->writer;

	/* Stop the worker thread. */

	cbus_lock(&writer->tx_wal_bus);
	writer->is_shutdown= true;
	cbus_unlock(&writer->tx_wal_bus);
	cbus_signal(&writer->tx_wal_bus);
	if (cord_join(&writer->cord)) {
		/* We can't recover from this in any reasonable way. */
		panic_syserror("WAL writer: thread join failed");
	}

	wal_writer_destroy(writer);

	r->writer = NULL;
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
wal_opt_rotate(struct xlog **wal, struct recovery *r,
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

/**
 * Pop a bulk of requests to write to disk to process.
 * Block on the condition only if we have no other work to
 * do. Loop in case of a spurious wakeup.
 */
void
wal_writer_pop(struct wal_writer *writer)
{
	while (! writer->is_shutdown)
	{
		if (! writer->is_rollback &&
		    ! stailq_empty(&writer->wal_pipe.pipe)) {
			stailq_concat(&writer->wal_pipe.output,
				      &writer->wal_pipe.pipe);
			break;
		}
		cbus_wait_signal(&writer->tx_wal_bus);
	}
}

static void
wal_write_to_disk(struct recovery *r, struct wal_writer *writer,
		  struct stailq *input, struct stailq *commit,
		  struct stailq *rollback)
{
	/*
	 * Input queue can only be empty on wal writer shutdown.
	 * In this case wal_opt_rotate can create an extra empty xlog.
	 */
	if (unlikely(stailq_empty(input)))
		return;

	/* Xlog is only rotated between queue processing  */
	if (wal_opt_rotate(&r->current_wal, r, &writer->vclock) != 0) {
		stailq_concat(rollback, input);
		return;
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

	struct xlog *wal = r->current_wal;
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
	stailq_foreach_entry(req, input, fifo) {
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
					fileno(wal->f));
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
	if (fio_batch_size(batch) > 0) {
		ssize_t nwr = wal_fio_batch_write(batch, fileno(wal->f));
		if (nwr > 0) {
			/* Update cached file offset */
			written_bytes += nwr;
		}
	}

done:
	/*
	 * Iterate over `input` queue and add all processed requests to
	 * `commit` queue and all other to `rollback` queue.
	 */
	struct wal_request *reqend = req;
	for (req = stailq_first_entry(input, struct wal_request, fifo);
	     req != reqend;
	     req = stailq_next_entry(req, fifo)) {
		/*
		 * Check if request has been fully written to xlog.
		 */
		if (unlikely(req->end_offset == -1 ||
			     req->end_offset > written_bytes)) {
			/*
			 * This and all subsequent requests have been failed
			 * to write. Truncate xlog to the end of last
			 * successfully written request.
			 */

			/* Calculate relative position of the good request */
			off_t garbage_bytes = written_bytes - req->start_offset;
			assert(garbage_bytes >= 0);

			/* Get absolute position */
			off_t good_offset = fio_lseek(fileno(wal->f),
				-garbage_bytes, SEEK_CUR);
			if (good_offset < 0)
				panic_syserror("failed to get xlog position");

			/* Truncate xlog */
			if (ftruncate(fileno(wal->f), good_offset) != 0)
				panic_syserror("failed to rollback xlog");
			written_bytes = req->start_offset;

			/* Move tail to `rollback` queue. */
			stailq_splice(input, &req->fifo, rollback);
			break;
		}

		/* Update internal vclock */
		vclock_follow(&writer->vclock,
			      req->rows[req->n_rows - 1]->server_id,
			      req->rows[req->n_rows - 1]->lsn);
		/* Update row counter for wal_opt_rotate() */
		wal->rows += req->n_rows;
		/* Mark request as successed for tx thread */
		req->res = vclock_sum(&writer->vclock);
	}

	fiber_gc();
	/* Move all processed requests to `commit` queue */
	stailq_concat(commit, input);
	return;
}

/** WAL writer thread main loop.  */
static void
wal_writer_f(va_list ap)
{
	struct recovery *r = va_arg(ap, struct recovery *);
	struct wal_writer *writer = r->writer;
	struct wal_watcher *watcher;

	cpipe_create(&writer->wal_pipe);
	cbus_join(&writer->tx_wal_bus, &writer->wal_pipe);

	struct stailq commit;
	struct stailq rollback;
	stailq_create(&commit);
	stailq_create(&rollback);

	cbus_lock(&writer->tx_wal_bus);
	while (! writer->is_shutdown) {
		wal_writer_pop(writer);
		cbus_unlock(&writer->tx_wal_bus);

		wal_write_to_disk(r, writer, &writer->wal_pipe.output,
				  &commit, &rollback);

		/* notify watchers */
		tt_pthread_mutex_lock(&writer->watchers_mutex);
		rlist_foreach_entry(watcher, &writer->watchers, next) {
			ev_async_send(watcher->loop, watcher->async);
		}
		tt_pthread_mutex_unlock(&writer->watchers_mutex);

		cbus_lock(&writer->tx_wal_bus);
		stailq_concat(&writer->tx_pipe.pipe, &commit);
		if (! stailq_empty(&rollback)) {
			/*
			 * Begin rollback: create a rollback queue
			 * from all requests which were not
			 * written to disk and all requests in the
			 * input queue.
			 */
			writer->is_rollback = true;
			stailq_concat(&rollback, &writer->wal_pipe.pipe);
			stailq_concat(&writer->wal_pipe.pipe, &rollback);
		}
		ev_async_send(writer->tx_pipe.consumer,
			      &writer->tx_pipe.fetch_output);
	}
	cbus_unlock(&writer->tx_wal_bus);
	if (r->current_wal != NULL) {
		xlog_close(r->current_wal);
		r->current_wal = NULL;
	}
	cpipe_destroy(&writer->wal_pipe);
}

/**
 * WAL writer main entry point: queue a single request
 * to be written to disk and wait until this task is completed.
 */
int64_t
wal_write(struct recovery *r, struct wal_request *req)
{
	if (r->wal_mode == WAL_NONE)
		return vclock_sum(&r->vclock);

	ERROR_INJECT_RETURN(ERRINJ_WAL_IO);

	struct wal_writer *writer = r->writer;

	req->fiber = fiber();
	req->res = -1;

	cpipe_push(&writer->wal_pipe, req);
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
	return req->res;
}

int
wal_register_watcher(
	struct recovery *recovery,
	struct wal_watcher *watcher, struct ev_async *async)
{
	struct wal_writer *writer;

	if (recovery == NULL || recovery->writer == NULL)
		return -1;

	writer = recovery->writer;

	watcher->loop = loop();
	watcher->async = async;
	tt_pthread_mutex_lock(&writer->watchers_mutex);
	rlist_add_tail_entry(&writer->watchers, watcher, next);
	tt_pthread_mutex_unlock(&writer->watchers_mutex);
	return 0;
}

void
wal_unregister_watcher(
	struct recovery *recovery,
	struct wal_watcher *watcher)
{
	struct wal_writer *writer;

	if (recovery == NULL || recovery->writer == NULL)
		return;

	writer = recovery->writer;

	tt_pthread_mutex_lock(&writer->watchers_mutex);
	rlist_del_entry(watcher, next);
	tt_pthread_mutex_unlock(&writer->watchers_mutex);
}
