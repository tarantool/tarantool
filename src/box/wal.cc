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
#include "wal.h"

#include "vclock.h"
#include "fiber.h"
#include "fio.h"
#include "errinj.h"

#include "xlog.h"
#include "xrow.h"
#include "vy_log.h"
#include "cbus.h"
#include "coio_task.h"
#include "replication.h"


const char *wal_mode_STRS[] = { "none", "write", "fsync", NULL };

int wal_dir_lock = -1;

static int64_t
wal_write(struct journal *, struct journal_entry *);

static int64_t
wal_write_in_wal_mode_none(struct journal *, struct journal_entry *);

/* WAL thread. */
struct wal_thread {
	/** 'wal' thread doing the writes. */
	struct cord cord;
	/** A pipe from 'tx' thread to 'wal' */
	struct cpipe wal_pipe;
	/** Return pipe from 'wal' to tx' */
	struct cpipe tx_pipe;
};

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
	struct journal base;
	/* ----------------- tx ------------------- */
	/**
	 * The rollback queue. An accumulator for all requests
	 * that need to be rolled back. Also acts as a valve
	 * in wal_write() so that new requests never enter
	 * the wal-tx bus and are rolled back "on arrival".
	 */
	struct stailq rollback;
	/* ----------------- wal ------------------- */
	/** A setting from instance configuration - rows_per_wal */
	int64_t wal_max_rows;
	/** A setting from instance configuration - wal_max_size */
	int64_t wal_max_size;
	/** Another one - wal_mode */
	enum wal_mode wal_mode;
	/** wal_dir, from the configuration file. */
	struct xdir wal_dir;
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
	/** The current WAL file. */
	struct xlog current_wal;
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

/**
 * Vinyl metadata log writer.
 */
struct vy_log_writer {
	/** The metadata log file. */
	struct xlog xlog;
};

static struct vy_log_writer vy_log_writer;
static struct wal_thread wal_thread;
static struct wal_writer wal_writer_singleton;

enum wal_mode
wal_mode()
{
	return wal_writer_singleton.wal_mode;
}

static void
wal_write_to_disk(struct cmsg *msg);

static void
tx_schedule_commit(struct cmsg *msg);

static struct cmsg_hop wal_request_route[] = {
	{wal_write_to_disk, &wal_thread.tx_pipe},
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

/** Write a request to a log in a single transaction. */
static ssize_t
xlog_write_entry(struct xlog *l, struct journal_entry *entry)
{
	/*
	 * Iterate over request rows (tx statements)
	 */
	xlog_tx_begin(l);
	struct xrow_header **row = entry->rows;
	for (; row < entry->rows + entry->n_rows; row++) {
		(*row)->tm = ev_now(loop());
		if (xlog_write_row(l, *row) < 0) {
			/*
			 * Rollback all un-written rows
			 */
			xlog_tx_rollback(l);
			return -1;
		}
	}
	return xlog_tx_commit(l);
}

/**
 * Invoke fibers waiting for their journal_entry's to be
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
	struct journal_entry *req;
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
		struct wal_writer *writer = &wal_writer_singleton;
		/* Closes the input valve. */
		stailq_concat(&writer->rollback, &batch->rollback);
	}
	tx_schedule_queue(&batch->commit);
}

static void
tx_schedule_rollback(struct cmsg *msg)
{
	(void) msg;
	struct wal_writer *writer = &wal_writer_singleton;
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
		  const char *wal_dirname, const struct tt_uuid *instance_uuid,
		  struct vclock *vclock, int64_t wal_max_rows,
		  int64_t wal_max_size)
{
	writer->wal_mode = wal_mode;
	writer->wal_max_rows = wal_max_rows;
	writer->wal_max_size = wal_max_size;
	journal_create(&writer->base, wal_mode == WAL_NONE ?
		       wal_write_in_wal_mode_none : wal_write, NULL);

	xdir_create(&writer->wal_dir, wal_dirname, XLOG, instance_uuid);
	xlog_clear(&writer->current_wal);
	if (wal_mode == WAL_FSYNC)
		writer->wal_dir.open_wflags |= O_SYNC;

	stailq_create(&writer->rollback);
	cmsg_init(&writer->in_rollback, NULL);

	/* Create and fill writer->vclock. */
	vclock_create(&writer->vclock);
	vclock_copy(&writer->vclock, vclock);

	rlist_create(&writer->watchers);
}

/** Destroy a WAL writer structure. */
static void
wal_writer_destroy(struct wal_writer *writer)
{
	xdir_destroy(&writer->wal_dir);
}

/** WAL thread routine. */
static int
wal_thread_f(va_list ap);

/** Start WAL thread and setup pipes to and from TX. */
void
wal_thread_start()
{
	if (cord_costart(&wal_thread.cord, "wal", wal_thread_f, NULL) != 0)
		panic("failed to start WAL thread");

	/* Create a pipe to WAL thread. */
	cpipe_create(&wal_thread.wal_pipe, "wal");
	cpipe_set_max_input(&wal_thread.wal_pipe, IOV_MAX);
}

/**
 * Initialize WAL writer.
 *
 * @pre   The instance has completed recovery from a snapshot
 *        and/or existing WALs. All WALs opened in read-only
 *        mode are closed. WAL thread has been started.
 */
void
wal_init(enum wal_mode wal_mode, const char *wal_dirname,
	 const struct tt_uuid *instance_uuid, struct vclock *vclock,
	 int64_t wal_max_rows, int64_t wal_max_size)
{
	assert(wal_max_rows > 1);

	struct wal_writer *writer = &wal_writer_singleton;

	wal_writer_create(writer, wal_mode, wal_dirname, instance_uuid,
			  vclock, wal_max_rows, wal_max_size);

	xdir_scan_xc(&writer->wal_dir);

	journal_set(&writer->base);
}

/**
 * Stop WAL thread, wait until it exits, and destroy WAL writer
 * if it was initialized. Called on shutdown.
 */
void
wal_thread_stop()
{
	cbus_stop_loop(&wal_thread.wal_pipe);

	if (cord_join(&wal_thread.cord)) {
		/* We can't recover from this in any reasonable way. */
		panic_syserror("WAL writer: thread join failed");
	}

	if (journal_is_initialized(&wal_writer_singleton.base))
		wal_writer_destroy(&wal_writer_singleton);
}

struct wal_checkpoint: public cmsg
{
	struct vclock *vclock;
	struct fiber *fiber;
	bool rotate;
	int res;
};

void
wal_checkpoint_f(struct cmsg *data)
{
	struct wal_checkpoint *msg = (struct wal_checkpoint *) data;
	struct wal_writer *writer = &wal_writer_singleton;
	if (writer->in_rollback.route != NULL) {
		/* We're rolling back a failed write. */
		msg->res = -1;
		return;
	}
	/*
	 * Avoid closing the current WAL if it has no rows (empty).
	 */
	if (msg->rotate && xlog_is_open(&writer->current_wal) &&
	    vclock_sum(&writer->current_wal.meta.vclock) !=
	    vclock_sum(&writer->vclock)) {

		xlog_close(&writer->current_wal, false);
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

int
wal_checkpoint(struct vclock *vclock, bool rotate)
{
	struct wal_writer *writer = &wal_writer_singleton;
	if (! stailq_empty(&writer->rollback)) {
		/*
		 * The writer rollback queue is not empty,
		 * roll back this transaction immediately.
		 * This is to ensure we do not accidentally
		 * commit a transaction which has seen changes
		 * that will be rolled back.
		 */
		say_error("Aborting transaction %llu during "
			  "cascading rollback",
			  vclock_sum(&writer->vclock));
		return -1;
	}
	if (writer->wal_mode == WAL_NONE) {
		vclock_copy(vclock, &writer->vclock);
		return 0;
	}
	static struct cmsg_hop wal_checkpoint_route[] = {
		{wal_checkpoint_f, &wal_thread.tx_pipe},
		{wal_checkpoint_done_f, NULL},
	};
	vclock_create(vclock);
	struct wal_checkpoint msg;
	cmsg_init(&msg, wal_checkpoint_route);
	msg.vclock = vclock;
	msg.fiber = fiber();
	msg.rotate = rotate;
	msg.res = 0;
	cpipe_push(&wal_thread.wal_pipe, &msg);
	fiber_set_cancellable(false);
	fiber_yield();
	fiber_set_cancellable(true);
	return msg.res;
}

struct wal_gc_msg: public cbus_call_msg
{
	int64_t lsn;
};

static int
wal_collect_garbage_f(struct cbus_call_msg *data)
{
	int64_t lsn = ((struct wal_gc_msg *)data)->lsn;
	xdir_collect_garbage(&wal_writer_singleton.wal_dir, lsn, false);
	return 0;
}

void
wal_collect_garbage(int64_t lsn)
{
	struct wal_writer *writer = &wal_writer_singleton;
	if (writer->wal_mode == WAL_NONE)
		return;
	struct wal_gc_msg msg;
	msg.lsn = lsn;
	bool cancellable = fiber_set_cancellable(false);
	cbus_call(&wal_thread.wal_pipe, &wal_thread.tx_pipe, &msg,
		  wal_collect_garbage_f, NULL, TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);
}

static void
wal_notify_watchers(struct wal_writer *writer, unsigned events);

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
	ERROR_INJECT_RETURN(ERRINJ_WAL_ROTATE);

	/*
	 * Close the file *before* we create the new WAL, to
	 * make sure local hot standby/replication can see
	 * EOF in the old WAL before switching to the new
	 * one.
	 */
	if (xlog_is_open(&writer->current_wal) &&
	    (writer->current_wal.rows >= writer->wal_max_rows ||
	     writer->current_wal.offset >= writer->wal_max_size)) {
		/*
		 * We can not handle xlog_close()
		 * failure in any reasonable way.
		 * A warning is written to the error log.
		 */
		xlog_close(&writer->current_wal, false);
	}

	if (xlog_is_open(&writer->current_wal))
		return 0;

	struct vclock *vclock = (struct vclock *)malloc(sizeof(*vclock));
	if (vclock == NULL) {
		diag_set(OutOfMemory, sizeof(*vclock),
			 "malloc", "struct vclock");
		diag_log();
		return -1;
	}
	vclock_copy(vclock, &writer->vclock);

	if (xdir_create_xlog(&writer->wal_dir, &writer->current_wal,
			     &writer->vclock) != 0) {
		diag_log();
		free(vclock);
		return -1;
	}
	xdir_add_vclock(&writer->wal_dir, vclock);

	wal_notify_watchers(writer, WAL_EVENT_ROTATE);
	return 0;
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
	struct wal_writer *writer = &wal_writer_singleton;
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
		{ wal_writer_clear_bus, &wal_thread.wal_pipe },
		{ wal_writer_clear_bus, &wal_thread.tx_pipe },
		/*
		 * Step 2: writer->rollback queue contains all
		 * messages which need to be rolled back,
		 * perform the rollback.
		 */
		{ tx_schedule_rollback, &wal_thread.wal_pipe },
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
	cpipe_push(&wal_thread.tx_pipe, &writer->in_rollback);
}

static void
wal_assign_lsn(struct wal_writer *writer, struct xrow_header **row,
	       struct xrow_header **end)
{
	/** Assign LSN to all local rows. */
	for ( ; row < end; row++) {
		if ((*row)->replica_id == 0) {
			(*row)->lsn = vclock_inc(&writer->vclock, instance_id);
			(*row)->replica_id = instance_id;
		} else {
			vclock_follow(&writer->vclock, (*row)->replica_id,
				      (*row)->lsn);
		}
	}
}

static void
wal_write_to_disk(struct cmsg *msg)
{
	struct wal_writer *writer = &wal_writer_singleton;
	struct wal_msg *wal_msg = (struct wal_msg *) msg;

	struct errinj *inj = errinj(ERRINJ_WAL_DELAY, ERRINJ_BOOL);
	while (inj != NULL && inj->bparam)
		usleep(10);

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
	 * of request in xlog file is stored inside `struct journal_entry`.
	 */

	struct xlog *l = &writer->current_wal;

	/*
	 * Iterate over requests (transactions)
	 */
	struct journal_entry *entry;
	struct stailq_entry *last_committed = NULL;
	stailq_foreach_entry(entry, &wal_msg->commit, fifo) {
		wal_assign_lsn(writer, entry->rows, entry->rows + entry->n_rows);
		entry->res = vclock_sum(&writer->vclock);
		int rc = xlog_write_entry(l, entry);
		if (rc < 0)
			goto done;
		if (rc > 0)
			last_committed = &entry->fifo;
		/* rc == 0: the write is buffered in xlog_tx */
	}
	if (xlog_flush(l) < 0)
		goto done;

	last_committed = stailq_last(&wal_msg->commit);

done:
	struct error *error = diag_last_error(diag_get());
	if (error) {
		/* Until we can pass the error to tx, log it and clear. */
		error_log(error);
		diag_clear(diag_get());
	}
	/*
	 * We need to start rollback from the first request
	 * following the last committed request. If
	 * last_commit_req is NULL, it means we have committed
	 * nothing, and need to start rollback from the first
	 * request. Otherwise we rollback from the first request.
	 */
	struct stailq rollback;
	stailq_cut_tail(&wal_msg->commit, last_committed, &rollback);

	if (!stailq_empty(&rollback)) {
		/* Update status of the successfully committed requests. */
		stailq_foreach_entry(entry, &rollback, fifo)
			entry->res = -1;
		/* Rollback unprocessed requests */
		stailq_concat(&wal_msg->rollback, &rollback);
		wal_writer_begin_rollback(writer);
	}
	fiber_gc();
	wal_notify_watchers(writer, WAL_EVENT_WRITE);
}

/** WAL thread main loop.  */
static int
wal_thread_f(va_list ap)
{
	(void) ap;

	/** Initialize eio in this thread */
	coio_enable();

	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, "wal", fiber_schedule_cb, fiber());
	/*
	 * Create a pipe to TX thread. Use a high priority
	 * endpoint, to ensure that WAL messages are delivered
	 * even when tx fiber pool is used up by net messages.
	 */
	cpipe_create(&wal_thread.tx_pipe, "tx_prio");

	cbus_loop(&endpoint);

	struct wal_writer *writer = &wal_writer_singleton;

	if (xlog_is_open(&writer->current_wal))
		xlog_close(&writer->current_wal, false);

	if (xlog_is_open(&vy_log_writer.xlog))
		xlog_close(&vy_log_writer.xlog, false);

	cpipe_destroy(&wal_thread.tx_pipe);
	return 0;
}

/**
 * WAL writer main entry point: queue a single request
 * to be written to disk and wait until this task is completed.
 */
int64_t
wal_write(struct journal *journal, struct journal_entry *entry)
{
	struct wal_writer *writer = (struct wal_writer *) journal;

	ERROR_INJECT_RETURN(ERRINJ_WAL_IO);

	if (! stailq_empty(&writer->rollback)) {
		/*
		 * The writer rollback queue is not empty,
		 * roll back this transaction immediately.
		 * This is to ensure we do not accidentally
		 * commit a transaction which has seen changes
		 * that will be rolled back.
		 */
		say_error("Aborting transaction %llu during "
			  "cascading rollback",
			  vclock_sum(&writer->vclock));
		return -1;
	}

	struct wal_msg *batch;
	if (!stailq_empty(&wal_thread.wal_pipe.input) &&
	    (batch = wal_msg(stailq_first_entry(&wal_thread.wal_pipe.input,
						struct cmsg, fifo)))) {

		stailq_add_tail_entry(&batch->commit, entry, fifo);
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
		stailq_add_tail_entry(&batch->commit, entry, fifo);
		cpipe_push(&wal_thread.wal_pipe, batch);
	}
	wal_thread.wal_pipe.n_input += entry->n_rows * XROW_IOVMAX;
	cpipe_flush_input(&wal_thread.wal_pipe);
	/**
	 * It's not safe to spuriously wakeup this fiber
	 * since in that case it will ignore a possible
	 * error from WAL writer and not roll back the
	 * transaction.
	 */
	bool cancellable = fiber_set_cancellable(false);
	fiber_yield(); /* Request was inserted. */
	fiber_set_cancellable(cancellable);
	if (entry->res > 0) {
		struct xrow_header **last = entry->rows + entry->n_rows - 1;
		while (last >= entry->rows) {
			/*
			 * Find last row from local instance id
			 * and promote vclock.
			 */
			if ((*last)->replica_id == instance_id) {
				/*
				 * In master-master configuration, during sudden
				 * power-loss, if the data have not been written
				 * to WAL but have already been sent to others,
				 * they will send the data back. In this case
				 * vclock has already been promoted by applier.
				 */
				if (vclock_get(&replicaset.vclock,
					       instance_id) < (*last)->lsn) {
					vclock_follow(&replicaset.vclock,
						      instance_id,
						      (*last)->lsn);
				}
				break;
			}
			--last;
		}
	}
	return entry->res;
}

int64_t
wal_write_in_wal_mode_none(struct journal *journal,
			   struct journal_entry *entry)
{
	struct wal_writer *writer = (struct wal_writer *) journal;
	wal_assign_lsn(writer, entry->rows, entry->rows + entry->n_rows);
	int64_t old_lsn = vclock_get(&replicaset.vclock, instance_id);
	int64_t new_lsn = vclock_get(&writer->vclock, instance_id);
	if (new_lsn > old_lsn) {
		/* There were local writes, promote vclock. */
		vclock_follow(&replicaset.vclock, instance_id, new_lsn);
	}
	return vclock_sum(&writer->vclock);
}

void
wal_init_vy_log()
{
	xlog_clear(&vy_log_writer.xlog);
}

struct wal_write_vy_log_msg: public cbus_call_msg
{
	struct journal_entry *entry;
};

static int
wal_write_vy_log_f(struct cbus_call_msg *msg)
{
	struct journal_entry *entry =
		((struct wal_write_vy_log_msg *)msg)->entry;

	if (! xlog_is_open(&vy_log_writer.xlog)) {
		if (vy_log_open(&vy_log_writer.xlog) < 0)
			return -1;
	}

	if (xlog_write_entry(&vy_log_writer.xlog, entry) < 0)
		return -1;

	if (xlog_flush(&vy_log_writer.xlog) < 0)
		return -1;

	return 0;
}

int
wal_write_vy_log(struct journal_entry *entry)
{
	struct wal_write_vy_log_msg msg;
	msg.entry= entry;
	bool cancellable = fiber_set_cancellable(false);
	int rc = cbus_call(&wal_thread.wal_pipe, &wal_thread.tx_pipe, &msg,
			   wal_write_vy_log_f, NULL, TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);
	return rc;
}

static int
wal_rotate_vy_log_f(struct cbus_call_msg *msg)
{
	(void) msg;
	if (xlog_is_open(&vy_log_writer.xlog))
		xlog_close(&vy_log_writer.xlog, false);
	return 0;
}

void
wal_rotate_vy_log()
{
	struct cbus_call_msg msg;
	bool cancellable = fiber_set_cancellable(false);
	cbus_call(&wal_thread.wal_pipe, &wal_thread.tx_pipe, &msg,
		  wal_rotate_vy_log_f, NULL, TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);
}

static void
wal_watcher_notify(struct wal_watcher *watcher, unsigned events)
{
	assert(!rlist_empty(&watcher->next));

	if (watcher->msg.cmsg.route != NULL) {
		/*
		 * If the notification message is still en route,
		 * mark the watcher to resend it as soon as it
		 * returns to WAL so as not to lose any events.
		 */
		watcher->events |= events;
		return;
	}

	watcher->msg.events = events;
	cmsg_init(&watcher->msg.cmsg, watcher->route);
	cpipe_push(&watcher->watcher_pipe, &watcher->msg.cmsg);
}

static void
wal_watcher_notify_perform(struct cmsg *cmsg)
{
	struct wal_watcher_msg *msg = (struct wal_watcher_msg *) cmsg;
	struct wal_watcher *watcher = msg->watcher;
	unsigned events = msg->events;

	watcher->cb(watcher, events);
}

static void
wal_watcher_notify_complete(struct cmsg *cmsg)
{
	struct wal_watcher_msg *msg = (struct wal_watcher_msg *) cmsg;
	struct wal_watcher *watcher = msg->watcher;

	cmsg->route = NULL;

	if (rlist_empty(&watcher->next)) {
		/* The watcher is about to be destroyed. */
		return;
	}

	if (watcher->events != 0) {
		/*
		 * Resend the message if we got notified while
		 * it was en route, see wal_watcher_notify().
		 */
		wal_watcher_notify(watcher, watcher->events);
		watcher->events = 0;
	}
}

static void
wal_watcher_attach(void *arg)
{
	struct wal_watcher *watcher = (struct wal_watcher *) arg;
	struct wal_writer *writer = &wal_writer_singleton;

	assert(rlist_empty(&watcher->next));
	rlist_add_tail_entry(&writer->watchers, watcher, next);

	/*
	 * Notify the watcher right after registering it
	 * so that it can process existing WALs.
	 */
	wal_watcher_notify(watcher, WAL_EVENT_ROTATE);
}

static void
wal_watcher_detach(void *arg)
{
	struct wal_watcher *watcher = (struct wal_watcher *) arg;

	assert(!rlist_empty(&watcher->next));
	rlist_del_entry(watcher, next);
}

void
wal_set_watcher(struct wal_watcher *watcher, const char *name,
		void (*watcher_cb)(struct wal_watcher *, unsigned events),
		void (*process_cb)(struct cbus_endpoint *))
{
	assert(journal_is_initialized(&wal_writer_singleton.base));

	rlist_create(&watcher->next);
	watcher->cb = watcher_cb;
	watcher->msg.watcher = watcher;
	watcher->msg.events = 0;
	watcher->msg.cmsg.route = NULL;
	watcher->events = 0;

	assert(lengthof(watcher->route) == 2);
	watcher->route[0] = {wal_watcher_notify_perform, &watcher->wal_pipe};
	watcher->route[1] = {wal_watcher_notify_complete, NULL};

	cbus_pair("wal", name, &watcher->wal_pipe, &watcher->watcher_pipe,
		  wal_watcher_attach, watcher, process_cb);
}

void
wal_clear_watcher(struct wal_watcher *watcher,
		  void (*process_cb)(struct cbus_endpoint *))
{
	assert(journal_is_initialized(&wal_writer_singleton.base));

	cbus_unpair(&watcher->wal_pipe, &watcher->watcher_pipe,
		    wal_watcher_detach, watcher, process_cb);
}

static void
wal_notify_watchers(struct wal_writer *writer, unsigned events)
{
	struct wal_watcher *watcher;
	rlist_foreach_entry(watcher, &writer->watchers, next)
		wal_watcher_notify(watcher, events);
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
	if (xlog_is_open(&wal_writer_singleton.current_wal))
		xlog_atfork(&wal_writer_singleton.current_wal);
	if (xlog_is_open(&vy_log_writer.xlog))
		xlog_atfork(&vy_log_writer.xlog);
}
