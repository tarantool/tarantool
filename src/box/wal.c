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

#include "fiber.h"
#include "fio.h"
#include "errinj.h"
#include "error.h"
#include "exception.h"

#include "xlog.h"
#include "xrow.h"
#include "vy_log.h"
#include "cbus.h"
#include "coio_task.h"
#include "replication.h"

enum {
	/**
	 * Size of disk space to preallocate with xlog_fallocate().
	 * Obviously, we want to call this function as infrequent as
	 * possible to avoid the overhead associated with a system
	 * call, however at the same time we do not want to call it
	 * to allocate too big chunks, because this may increase tx
	 * latency. 1 MB seems to be a well balanced choice.
	 */
	WAL_FALLOCATE_LEN = 1024 * 1024,
};

const char *wal_mode_STRS[] = { "none", "write", "fsync", NULL };

int wal_dir_lock = -1;

static int
wal_write_async(struct journal *, struct journal_entry *);

static int
wal_write(struct journal *, struct journal_entry *);

static int
wal_write_none_async(struct journal *, struct journal_entry *);

static int
wal_write_none(struct journal *, struct journal_entry *);

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
	wal_on_garbage_collection_f on_garbage_collection;
	wal_on_checkpoint_threshold_f on_checkpoint_threshold;
	/**
	 * The rollback queue. An accumulator for all requests
	 * that need to be rolled back. Also acts as a valve
	 * in wal_write() so that new requests never enter
	 * the wal-tx bus and are rolled back "on arrival".
	 */
	struct stailq rollback;
	/** A pipe from 'tx' thread to 'wal' */
	struct cpipe wal_pipe;
	/** A memory pool for messages. */
	struct mempool msg_pool;
	/**
	 * A last journal entry submitted to write. This is a
	 * 'rollback border'. When rollback starts, all
	 * transactions keep being rolled back until this one is
	 * rolled back too.
	 */
	struct journal_entry *last_entry;
	/* ----------------- wal ------------------- */
	/** A setting from instance configuration - wal_max_size */
	int64_t wal_max_size;
	/** Another one - wal_mode */
	enum wal_mode wal_mode;
	/** wal_dir, from the configuration file. */
	struct xdir wal_dir;
	/** 'wal' thread doing the writes. */
	struct cord cord;
	/**
	 * Return pipe from 'wal' to tx'. This is a
	 * priority pipe and DOES NOT support yield.
	 */
	struct cpipe tx_prio_pipe;
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
	/**
	 * VClock of the most recent successfully created checkpoint.
	 * The WAL writer must not delete WAL files that are needed to
	 * recover from it even if it is running out of disk space.
	 */
	struct vclock checkpoint_vclock;
	/** Total size of WAL files written since the last checkpoint. */
	int64_t checkpoint_wal_size;
	/**
	 * Checkpoint threshold: when the total size of WAL files
	 * written since the last checkpoint exceeds the value of
	 * this variable, the WAL thread will notify TX that it's
	 * time to trigger checkpointing.
	 */
	int64_t checkpoint_threshold;
	/**
	 * This flag is set if the WAL thread has notified TX that
	 * the checkpoint threshold has been exceeded. It is cleared
	 * on checkpoint completion. Needed in order not to invoke
	 * the TX callback over and over again while checkpointing
	 * is in progress.
	 */
	bool checkpoint_triggered;
	/** The current WAL file. */
	struct xlog current_wal;
	/**
	 * Used if there was a WAL I/O error and we need to
	 * keep adding all incoming requests to the rollback
	 * queue, until the tx thread has recovered.
	 */
	bool is_in_rollback;
	/**
	 * WAL watchers, i.e. threads that should be alerted
	 * whenever there are new records appended to the journal.
	 * Used for replication relays.
	 */
	struct rlist watchers;
};

struct wal_msg {
	struct cmsg base;
	/** Approximate size of this request when encoded. */
	size_t approx_len;
	/** Input queue, on output contains all committed requests. */
	struct stailq commit;
	/**
	 * In case of rollback, contains the requests which must
	 * be rolled back.
	 */
	struct stailq rollback;
	/** vclock after the batch processed. */
	struct vclock vclock;
};

/**
 * Vinyl metadata log writer.
 */
struct vy_log_writer {
	/** The metadata log file. */
	struct xlog xlog;
};

static struct vy_log_writer vy_log_writer;
static struct wal_writer wal_writer_singleton;

enum wal_mode
wal_mode(void)
{
	return wal_writer_singleton.wal_mode;
}

const char *
wal_dir(void)
{
	return wal_writer_singleton.wal_dir.dirname;
}

static void
wal_write_to_disk(struct cmsg *msg);

static void
tx_complete_batch(struct cmsg *msg);

static struct cmsg_hop wal_request_route[] = {
	{wal_write_to_disk, &wal_writer_singleton.tx_prio_pipe},
	{tx_complete_batch, NULL},
};

static void
wal_msg_create(struct wal_msg *batch)
{
	cmsg_init(&batch->base, wal_request_route);
	batch->approx_len = 0;
	stailq_create(&batch->commit);
	stailq_create(&batch->rollback);
	vclock_create(&batch->vclock);
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
		struct errinj *inj = errinj(ERRINJ_WAL_BREAK_LSN, ERRINJ_INT);
		if (inj != NULL && inj->iparam == (*row)->lsn) {
			(*row)->lsn = inj->iparam - 1;
			say_warn("injected broken lsn: %lld",
				 (long long) (*row)->lsn);
		}
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
 * Invoke completion callbacks of journal entries to be
 * completed. Callbacks are invoked in strict fifo order:
 * this ensures that, in case of rollback, requests are
 * rolled back in strict reverse order, producing
 * a consistent database state.
 */
static void
tx_schedule_queue(struct stailq *queue)
{
	struct journal_entry *req, *tmp;
	stailq_foreach_entry_safe(req, tmp, queue, fifo)
		journal_async_complete(req);

	journal_queue_wakeup();
}

/**
 * Rollback happens, when disk write fails. In that case all next
 * transactions, sent to WAL, also should be rolled back. Because
 * they could make their statements based on what they saw in the
 * failed transaction. Also rollback of the failed transaction
 * without rollback of the next ones can actually rewrite what
 * they committed.
 * So when rollback is started, *all* pending transactions should
 * be rolled back. However if they would keep coming, the rollback
 * would be infinite. This means to complete a rollback it is
 * necessary to stop sending new transactions to WAL, then
 * rollback all already sent. In the end allow new transactions
 * again.
 *
 * First step is stop accepting all new transactions. For that WAL
 * thread sets a global flag. No rocket science here. All new
 * transactions, if see the flag set, are added to the rollback
 * queue immediately.
 *
 * Second step - tell TX thread to stop sending new transactions
 * to WAL. So as the rollback queue would stop growing.
 *
 * Third step - rollback all transactions in reverse order.
 *
 * Fourth step - allow transactions again. Unset the global flag
 * in WAL thread.
 */
static inline void
wal_begin_rollback(void)
{
	/* Signal WAL-thread stop accepting new transactions. */
	wal_writer_singleton.is_in_rollback = true;
}

static void
wal_complete_rollback(struct cmsg *base)
{
	(void) base;
	/* WAL-thread can try writing transactions again. */
	wal_writer_singleton.is_in_rollback = false;
}

static void
tx_complete_rollback(void)
{
	struct wal_writer *writer = &wal_writer_singleton;
	/*
	 * Despite records are sent in batches, the last entry to
	 * commit can't be in the middle of a batch. After all
	 * transactions to rollback are collected, the last entry
	 * will be exactly, well, the last entry.
	 */
	if (stailq_last_entry(&writer->rollback, struct journal_entry,
			      fifo) != writer->last_entry)
		return;
	stailq_reverse(&writer->rollback);
	tx_schedule_queue(&writer->rollback);
	/* TX-thread can try sending transactions to WAL again. */
	stailq_create(&writer->rollback);
	static struct cmsg_hop route[] = {
		{wal_complete_rollback, NULL}
	};
	static struct cmsg msg;
	cmsg_init(&msg, route);
	cpipe_push(&writer->wal_pipe, &msg);
}

/**
 * Complete execution of a batch of WAL write requests:
 * schedule all committed requests, and, should there
 * be any requests to be rolled back, append them to
 * the rollback queue. In case this is a rollback and the batch
 * contains the last transaction to rollback, the rollback is
 * performed and normal processing is allowed again.
 */
static void
tx_complete_batch(struct cmsg *msg)
{
	struct wal_writer *writer = &wal_writer_singleton;
	struct wal_msg *batch = (struct wal_msg *) msg;
	/*
	 * Move the rollback list to the writer first, since
	 * wal_msg memory disappears after the first
	 * iteration of tx_schedule_queue loop.
	 */
	if (! stailq_empty(&batch->rollback)) {
		stailq_concat(&writer->rollback, &batch->rollback);
		tx_complete_rollback();
	}
	/* Update the tx vclock to the latest written by wal. */
	vclock_copy(&replicaset.vclock, &batch->vclock);
	tx_schedule_queue(&batch->commit);
	mempool_free(&writer->msg_pool, container_of(msg, struct wal_msg, base));
}

/**
 * This message is sent from WAL to TX when the WAL thread hits
 * ENOSPC and has to delete some backup WAL files to continue.
 * The TX thread uses this message to shoot off WAL consumers
 * that needed deleted WAL files.
 */
struct tx_notify_gc_msg {
	struct cmsg base;
	/** VClock of the oldest WAL row preserved by WAL. */
	struct vclock vclock;
};

static void
tx_notify_gc(struct cmsg *msg)
{
	struct wal_writer *writer = &wal_writer_singleton;
	struct vclock *vclock = &((struct tx_notify_gc_msg *)msg)->vclock;
	writer->on_garbage_collection(vclock);
	free(msg);
}

static void
tx_notify_checkpoint(struct cmsg *msg)
{
	struct wal_writer *writer = &wal_writer_singleton;
	writer->on_checkpoint_threshold();
	free(msg);
}

/**
 * Initialize WAL writer context. Even though it's a singleton,
 * encapsulate the details just in case we may use
 * more writers in the future.
 */
static void
wal_writer_create(struct wal_writer *writer, enum wal_mode wal_mode,
		  const char *wal_dirname, int64_t wal_max_size,
		  const struct tt_uuid *instance_uuid,
		  wal_on_garbage_collection_f on_garbage_collection,
		  wal_on_checkpoint_threshold_f on_checkpoint_threshold)
{
	writer->wal_mode = wal_mode;
	writer->wal_max_size = wal_max_size;

	journal_create(&writer->base,
		       wal_mode == WAL_NONE ?
		       wal_write_none_async : wal_write_async,
		       wal_mode == WAL_NONE ?
		       wal_write_none : wal_write);

	struct xlog_opts opts = xlog_opts_default;
	opts.sync_is_async = true;
	xdir_create(&writer->wal_dir, wal_dirname, XLOG, instance_uuid, &opts);
	xlog_clear(&writer->current_wal);
	if (wal_mode == WAL_FSYNC)
		writer->wal_dir.open_wflags |= O_SYNC;

	stailq_create(&writer->rollback);
	writer->is_in_rollback = false;

	writer->checkpoint_wal_size = 0;
	writer->checkpoint_threshold = INT64_MAX;
	writer->checkpoint_triggered = false;

	vclock_create(&writer->vclock);
	vclock_create(&writer->checkpoint_vclock);
	rlist_create(&writer->watchers);

	writer->on_garbage_collection = on_garbage_collection;
	writer->on_checkpoint_threshold = on_checkpoint_threshold;

	mempool_create(&writer->msg_pool, &cord()->slabc,
		       sizeof(struct wal_msg));
}

/** Destroy a WAL writer structure. */
static void
wal_writer_destroy(struct wal_writer *writer)
{
	xdir_destroy(&writer->wal_dir);
}

/** WAL writer thread routine. */
static int
wal_writer_f(va_list ap);

static int
wal_open_f(struct cbus_call_msg *msg)
{
	(void)msg;
	struct wal_writer *writer = &wal_writer_singleton;
	const char *path = xdir_format_filename(&writer->wal_dir,
				vclock_sum(&writer->vclock), NONE);
	assert(!xlog_is_open(&writer->current_wal));
	return xlog_open(&writer->current_wal, path, &writer->wal_dir.opts);
}

/**
 * Try to open the current WAL file for appending if it exists.
 */
static int
wal_open(struct wal_writer *writer)
{
	const char *path = xdir_format_filename(&writer->wal_dir,
				vclock_sum(&writer->vclock), NONE);
	if (access(path, F_OK) != 0) {
		if (errno == ENOENT) {
			/* No WAL, nothing to do. */
			return 0;
		}
		diag_set(SystemError, "failed to access %s", path);
		return -1;
	}

	/*
	 * The WAL file exists, try to open it.
	 *
	 * Note, an xlog object cannot be opened and used in
	 * different threads (because it uses slab arena), so
	 * we have to call xlog_open() on behalf of the WAL
	 * thread.
	 */
	struct cbus_call_msg msg;
	if (cbus_call(&writer->wal_pipe, &writer->tx_prio_pipe, &msg,
		      wal_open_f, NULL, TIMEOUT_INFINITY) == 0) {
		/*
		 * Success: we can now append to
		 * the existing WAL file.
		 */
		return 0;
	}
	struct error *e = diag_last_error(diag_get());
	if (!type_assignable(&type_XlogError, e->type)) {
		/*
		 * Out of memory or system error.
		 * Nothing we can do.
		 */
		return -1;
	}
	diag_log();

	/*
	 * Looks like the WAL file is corrupted.
	 * Rename it so that we can proceed.
	 */
	say_warn("renaming corrupted %s", path);
	char new_path[PATH_MAX];
	snprintf(new_path, sizeof(new_path), "%s.corrupted", path);
	if (rename(path, new_path) != 0) {
		diag_set(SystemError, "failed to rename %s", path);
		return -1;
	}
	return 0;
}

int
wal_init(enum wal_mode wal_mode, const char *wal_dirname,
	 int64_t wal_max_size, const struct tt_uuid *instance_uuid,
	 wal_on_garbage_collection_f on_garbage_collection,
	 wal_on_checkpoint_threshold_f on_checkpoint_threshold)
{
	/* Initialize the state. */
	struct wal_writer *writer = &wal_writer_singleton;
	wal_writer_create(writer, wal_mode, wal_dirname, wal_max_size,
			  instance_uuid, on_garbage_collection,
			  on_checkpoint_threshold);

	/* Start WAL thread. */
	if (cord_costart(&writer->cord, "wal", wal_writer_f, NULL) != 0)
		return -1;

	/* Create a pipe to WAL thread. */
	cpipe_create(&writer->wal_pipe, "wal");
	cpipe_set_max_input(&writer->wal_pipe, IOV_MAX);
	return 0;
}

int
wal_enable(void)
{
	struct wal_writer *writer = &wal_writer_singleton;

	/* Initialize the writer vclock from the recovery state. */
	vclock_copy(&writer->vclock, &replicaset.vclock);

	/*
	 * Scan the WAL directory to build an index of all
	 * existing WAL files. Required for garbage collection,
	 * see wal_collect_garbage().
	 */
	if (xdir_scan(&writer->wal_dir, true))
		return -1;

	/* Open the most recent WAL file. */
	if (wal_open(writer) != 0)
		return -1;

	/* Enable journalling. */
	journal_set(&writer->base);
	return 0;
}

void
wal_free(void)
{
	struct wal_writer *writer = &wal_writer_singleton;

	cbus_stop_loop(&writer->wal_pipe);

	if (cord_join(&writer->cord)) {
		/* We can't recover from this in any reasonable way. */
		panic_syserror("WAL writer: thread join failed");
	}

	wal_writer_destroy(writer);
}

struct wal_vclock_msg {
    struct cbus_call_msg base;
    struct vclock vclock;
};

static int
wal_sync_f(struct cbus_call_msg *data)
{
	struct wal_vclock_msg *msg = (struct wal_vclock_msg *) data;
	struct wal_writer *writer = &wal_writer_singleton;
	if (writer->is_in_rollback) {
		/* We're rolling back a failed write. */
		diag_set(ClientError, ER_CASCADE_ROLLBACK);
		return -1;
	}
	vclock_copy(&msg->vclock, &writer->vclock);
	return 0;
}

int
wal_sync(struct vclock *vclock)
{
	ERROR_INJECT(ERRINJ_WAL_SYNC, {
		diag_set(ClientError, ER_INJECTION, "wal sync");
		return -1;
	});

	struct wal_writer *writer = &wal_writer_singleton;
	if (writer->wal_mode == WAL_NONE) {
		if (vclock != NULL)
			vclock_copy(vclock, &writer->vclock);
		return 0;
	}
	if (!stailq_empty(&writer->rollback)) {
		/* We're rolling back a failed write. */
		diag_set(ClientError, ER_CASCADE_ROLLBACK);
		return -1;
	}
	bool cancellable = fiber_set_cancellable(false);
	struct wal_vclock_msg msg;
	int rc = cbus_call(&writer->wal_pipe, &writer->tx_prio_pipe,
			   &msg.base, wal_sync_f, NULL, TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);
	if (vclock != NULL)
		vclock_copy(vclock, &msg.vclock);
	return rc;
}

static int
wal_begin_checkpoint_f(struct cbus_call_msg *data)
{
	struct wal_checkpoint *msg = (struct wal_checkpoint *) data;
	struct wal_writer *writer = &wal_writer_singleton;
	if (writer->is_in_rollback) {
		/*
		 * We're rolling back a failed write and so
		 * can't make a checkpoint - see the comment
		 * in wal_begin_checkpoint() for the explanation.
		 */
		diag_set(ClientError, ER_CASCADE_ROLLBACK);
		return -1;
	}
	/*
	 * Avoid closing the current WAL if it has no rows (empty).
	 */
	if (xlog_is_open(&writer->current_wal) &&
	    vclock_sum(&writer->current_wal.meta.vclock) !=
	    vclock_sum(&writer->vclock)) {

		xlog_close(&writer->current_wal, false);
		/*
		 * The next WAL will be created on the first write.
		 */
	}
	vclock_copy(&msg->vclock, &writer->vclock);
	msg->wal_size = writer->checkpoint_wal_size;
	ERROR_INJECT_SLEEP(ERRINJ_WAL_DELAY);
	return 0;
}

int
wal_begin_checkpoint(struct wal_checkpoint *checkpoint)
{
	struct wal_writer *writer = &wal_writer_singleton;
	if (writer->wal_mode == WAL_NONE) {
		vclock_copy(&checkpoint->vclock, &writer->vclock);
		checkpoint->wal_size = 0;
		return 0;
	}
	if (!stailq_empty(&writer->rollback)) {
		/*
		 * If cascading rollback is in progress, in-memory
		 * indexes can contain changes scheduled for rollback.
		 * If we made a checkpoint, we could write them to
		 * the snapshot. So we abort checkpointing in this
		 * case.
		 */
		diag_set(ClientError, ER_CASCADE_ROLLBACK);
		return -1;
	}
	bool cancellable = fiber_set_cancellable(false);
	int rc = cbus_call(&writer->wal_pipe, &writer->tx_prio_pipe,
			   &checkpoint->base, wal_begin_checkpoint_f, NULL,
			   TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);
	if (rc != 0)
		return -1;
	return 0;
}

static int
wal_commit_checkpoint_f(struct cbus_call_msg *data)
{
	struct wal_checkpoint *msg = (struct wal_checkpoint *) data;
	struct wal_writer *writer = &wal_writer_singleton;
	/*
	 * Now, once checkpoint has been created, we can update
	 * the WAL's version of the last checkpoint vclock and
	 * reset the size of WAL files written since the last
	 * checkpoint. Note, since new WAL records may have been
	 * written while the checkpoint was created, we subtract
	 * the value of checkpoint_wal_size observed at the time
	 * when checkpointing started from the current value
	 * rather than just setting it to 0.
	 */
	vclock_copy(&writer->checkpoint_vclock, &msg->vclock);
	assert(writer->checkpoint_wal_size >= msg->wal_size);
	writer->checkpoint_wal_size -= msg->wal_size;
	writer->checkpoint_triggered = false;
	return 0;
}

void
wal_commit_checkpoint(struct wal_checkpoint *checkpoint)
{
	struct wal_writer *writer = &wal_writer_singleton;
	if (writer->wal_mode == WAL_NONE) {
		vclock_copy(&writer->checkpoint_vclock, &checkpoint->vclock);
		return;
	}
	bool cancellable = fiber_set_cancellable(false);
	cbus_call(&writer->wal_pipe, &writer->tx_prio_pipe,
		  &checkpoint->base, wal_commit_checkpoint_f, NULL,
		  TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);
}

struct wal_set_checkpoint_threshold_msg {
	struct cbus_call_msg base;
	int64_t checkpoint_threshold;
};

static int
wal_set_checkpoint_threshold_f(struct cbus_call_msg *data)
{
	struct wal_writer *writer = &wal_writer_singleton;
	struct wal_set_checkpoint_threshold_msg *msg;
	msg = (struct wal_set_checkpoint_threshold_msg *)data;
	writer->checkpoint_threshold = msg->checkpoint_threshold;
	return 0;
}

void
wal_set_checkpoint_threshold(int64_t threshold)
{
	struct wal_writer *writer = &wal_writer_singleton;
	if (writer->wal_mode == WAL_NONE)
		return;
	struct wal_set_checkpoint_threshold_msg msg;
	msg.checkpoint_threshold = threshold;
	bool cancellable = fiber_set_cancellable(false);
	cbus_call(&writer->wal_pipe, &writer->tx_prio_pipe,
		  &msg.base, wal_set_checkpoint_threshold_f, NULL,
		  TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);
}

void
wal_set_queue_max_size(int64_t size)
{
	journal_queue_set_max_size(size);
}

struct wal_gc_msg
{
	struct cbus_call_msg base;
	const struct vclock *vclock;
};

static int
wal_collect_garbage_f(struct cbus_call_msg *data)
{
	struct wal_writer *writer = &wal_writer_singleton;
	const struct vclock *vclock = ((struct wal_gc_msg *)data)->vclock;

	if (!xlog_is_open(&writer->current_wal) &&
	    vclock_sum(vclock) >= vclock_sum(&writer->vclock)) {
		/*
		 * The last available WAL file has been sealed and
		 * all registered consumers have done reading it.
		 * We can delete it now.
		 */
	} else {
		/*
		 * Find the most recent WAL file that contains rows
		 * required by registered consumers and delete all
		 * older WAL files.
		 */
		vclock = vclockset_psearch(&writer->wal_dir.index, vclock);
	}
	if (vclock != NULL)
		xdir_collect_garbage(&writer->wal_dir, vclock_sum(vclock),
				     XDIR_GC_ASYNC);

	return 0;
}

void
wal_collect_garbage(const struct vclock *vclock)
{
	struct wal_writer *writer = &wal_writer_singleton;
	if (writer->wal_mode == WAL_NONE)
		return;
	struct wal_gc_msg msg;
	msg.vclock = vclock;
	bool cancellable = fiber_set_cancellable(false);
	cbus_call(&writer->wal_pipe, &writer->tx_prio_pipe, &msg.base,
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
	    writer->current_wal.offset >= writer->wal_max_size) {
		/*
		 * We can not handle xlog_close()
		 * failure in any reasonable way.
		 * A warning is written to the error log.
		 */
		xlog_close(&writer->current_wal, false);
	}

	if (xlog_is_open(&writer->current_wal))
		return 0;

	if (xdir_create_xlog(&writer->wal_dir, &writer->current_wal,
			     &writer->vclock) != 0)
		return -1;
	/*
	 * Keep track of the new WAL vclock. Required for garbage
	 * collection, see wal_collect_garbage().
	 */
	xdir_add_vclock(&writer->wal_dir, &writer->vclock);

	wal_notify_watchers(writer, WAL_EVENT_ROTATE);
	return 0;
}

/**
 * Make sure there's enough disk space to append @len bytes
 * of data to the current WAL.
 *
 * If fallocate() fails with ENOSPC, delete old WAL files
 * that are not needed for recovery and retry.
 */
static int
wal_fallocate(struct wal_writer *writer, size_t len)
{
	bool warn_no_space = true, notify_gc = false;
	struct xlog *l = &writer->current_wal;
	struct errinj *errinj = errinj(ERRINJ_WAL_FALLOCATE, ERRINJ_INT);
	int rc = 0;

	/*
	 * Max LSN that can be collected in case of ENOSPC -
	 * we must not delete WALs necessary for recovery.
	 */
	int64_t gc_lsn = vclock_sum(&writer->checkpoint_vclock);

	/*
	 * The actual write size can be greater than the sum size
	 * of encoded rows (compression, fixheaders). Double the
	 * given length to get a rough upper bound estimate.
	 */
	len *= 2;

retry:
	if (errinj == NULL || errinj->iparam == 0) {
		if (l->allocated >= len)
			goto out;
		if (xlog_fallocate(l, MAX(len, WAL_FALLOCATE_LEN)) == 0)
			goto out;
	} else {
		errinj->iparam--;
		diag_set(ClientError, ER_INJECTION, "xlog fallocate");
		errno = ENOSPC;
	}
	if (errno != ENOSPC)
		goto error;
	if (!xdir_has_garbage(&writer->wal_dir, gc_lsn))
		goto error;

	if (warn_no_space) {
		say_crit("ran out of disk space, try to delete old WAL files");
		warn_no_space = false;
	}

	xdir_collect_garbage(&writer->wal_dir, gc_lsn, XDIR_GC_REMOVE_ONE);
	notify_gc = true;
	goto retry;
error:
	diag_log();
	rc = -1;
out:
	/*
	 * Notify the TX thread if the WAL thread had to delete
	 * some WAL files to proceed so that TX can shoot off WAL
	 * consumers that still need those files.
	 *
	 * We allocate the message with malloc() and we ignore
	 * allocation failures, because this is a pretty rare
	 * event and a failure to send this message isn't really
	 * critical.
	 */
	if (notify_gc) {
		static struct cmsg_hop route[] = {
			{ tx_notify_gc, NULL },
		};
		struct tx_notify_gc_msg *msg = malloc(sizeof(*msg));
		if (msg != NULL) {
			if (xdir_first_vclock(&writer->wal_dir,
					      &msg->vclock) < 0)
				vclock_copy(&msg->vclock, &writer->vclock);
			cmsg_init(&msg->base, route);
			cpipe_push(&writer->tx_prio_pipe, &msg->base);
		} else
			say_warn("failed to allocate gc notification message");
	}
	return rc;
}

/*
 * Assign lsn and replica identifier for local writes and track
 * row into vclock_diff.
 */
static void
wal_assign_lsn(struct vclock *vclock_diff, struct vclock *base,
	       struct journal_entry *entry)
{
	int64_t tsn = 0;
	struct xrow_header **start = entry->rows;
	struct xrow_header **end = entry->rows + entry->n_rows;
	struct xrow_header **first_glob_row = entry->rows;
	/** Assign LSN to all local rows. */
	for (struct xrow_header **row = start; row < end; row++) {
		if ((*row)->replica_id == 0) {
			/*
			 * All rows representing local space data
			 * manipulations are signed with a zero
			 * instance id. This is also true for
			 * anonymous replicas, since they are
			 * only capable of writing to local and
			 * temporary spaces.
			 */
			if ((*row)->group_id != GROUP_LOCAL)
				(*row)->replica_id = instance_id;

			(*row)->lsn = vclock_inc(vclock_diff, (*row)->replica_id) +
				      vclock_get(base, (*row)->replica_id);
			/*
			 * Use lsn of the first global row as
			 * transaction id.
			 */
			if ((*row)->group_id != GROUP_LOCAL && tsn == 0) {
				tsn = (*row)->lsn;
				/*
				 * Remember the tail being processed.
				 */
				first_glob_row = row;
			}
			(*row)->tsn = tsn == 0 ? (*start)->lsn : tsn;
			/* Tx meta is stored in the last tx row. */
			if (row == end - 1) {
				(*row)->flags = entry->flags;
				(*row)->is_commit = true;
			}
		} else {
			int64_t diff = (*row)->lsn - vclock_get(base, (*row)->replica_id);
			if (diff <= vclock_get(vclock_diff,
					       (*row)->replica_id)) {
				say_crit("Attempt to write a broken LSN to WAL:"
					 " replica id: %d, confirmed lsn: %d,"
					 " new lsn %d", (*row)->replica_id,
					 vclock_get(base, (*row)->replica_id) +
					 vclock_get(vclock_diff,
						    (*row)->replica_id),
						    (*row)->lsn);
				assert(false);
			} else {
				vclock_follow(vclock_diff, (*row)->replica_id, diff);
			}
		}
	}

	/*
	 * Fill transaction id for all the local rows preceding
	 * the first global row. tsn was yet unknown when those
	 * rows were processed.
	 */
	for (struct xrow_header **row = start; row < first_glob_row; row++)
		(*row)->tsn = tsn;
}

static void
wal_write_to_disk(struct cmsg *msg)
{
	struct wal_writer *writer = &wal_writer_singleton;
	struct wal_msg *wal_msg = (struct wal_msg *) msg;
	int err_code = JOURNAL_ENTRY_ERR_UNKNOWN;
	struct stailq_entry *last_committed = NULL;
	struct journal_entry *entry;
	struct error *error;
	if (stailq_empty(&wal_msg->commit))
		panic("Attempted to write an empty batch to WAL");

	/*
	 * Track all vclock changes made by this batch into
	 * vclock_diff variable and then apply it into writers'
	 * vclock after each xlog flush.
	 */
	struct vclock vclock_diff;
	vclock_create(&vclock_diff);

	ERROR_INJECT_SLEEP(ERRINJ_WAL_DELAY);

	ERROR_INJECT_COUNTDOWN(ERRINJ_WAL_DELAY_COUNTDOWN, {
		struct errinj *e = errinj(ERRINJ_WAL_DELAY, ERRINJ_BOOL);
		e->bparam = true;
		ERROR_INJECT_SLEEP(ERRINJ_WAL_DELAY);
	});

	if (writer->is_in_rollback) {
		/* We're rolling back a failed write. */
		err_code = JOURNAL_ENTRY_ERR_CASCADE;
		goto done;
	}

	/* Xlog is only rotated between queue processing  */
	if (wal_opt_rotate(writer) != 0) {
		err_code = JOURNAL_ENTRY_ERR_IO;
		goto done;
	}

	/* Ensure there's enough disk space before writing anything. */
	if (wal_fallocate(writer, wal_msg->approx_len) != 0) {
		err_code = JOURNAL_ENTRY_ERR_IO;
		goto done;
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
	int rc;
	stailq_foreach_entry(entry, &wal_msg->commit, fifo) {
		wal_assign_lsn(&vclock_diff, &writer->vclock, entry);
		entry->res = vclock_sum(&vclock_diff) +
			     vclock_sum(&writer->vclock);
		rc = xlog_write_entry(l, entry);
		if (rc < 0) {
			err_code = JOURNAL_ENTRY_ERR_IO;
			goto done;
		}
		if (rc > 0) {
			writer->checkpoint_wal_size += rc;
			last_committed = &entry->fifo;
			vclock_merge(&writer->vclock, &vclock_diff);
		}
		/* rc == 0: the write is buffered in xlog_tx */
	}
	rc = xlog_flush(l);
	if (rc < 0) {
		err_code= JOURNAL_ENTRY_ERR_IO;
		goto done;
	}

	writer->checkpoint_wal_size += rc;
	last_committed = stailq_last(&wal_msg->commit);
	vclock_merge(&writer->vclock, &vclock_diff);

	/*
	 * Notify TX if the checkpoint threshold has been exceeded.
	 * Use malloc() for allocating the notification message and
	 * don't panic on error, because if we fail to send the
	 * message now, we will retry next time we process a request.
	 */
	if (!writer->checkpoint_triggered &&
	    writer->checkpoint_wal_size > writer->checkpoint_threshold) {
		static struct cmsg_hop route[] = {
			{ tx_notify_checkpoint, NULL },
		};
		struct cmsg *msg = malloc(sizeof(*msg));
		if (msg != NULL) {
			cmsg_init(msg, route);
			cpipe_push(&writer->tx_prio_pipe, msg);
			writer->checkpoint_triggered = true;
		} else {
			say_warn("failed to allocate checkpoint "
				 "notification message");
		}
	}

done:
	error = diag_last_error(diag_get());
	if (error) {
		/* Until we can pass the error to tx, log it and clear. */
		error_log(error);
		diag_clear(diag_get());
	}
	/*
	 * Remember the vclock of the last successfully written row so
	 * that we can update replicaset.vclock once this message gets
	 * back to tx.
	 */
	vclock_copy(&wal_msg->vclock, &writer->vclock);
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
		assert(err_code != JOURNAL_ENTRY_ERR_UNKNOWN);
		/* Update status of the successfully committed requests. */
		stailq_foreach_entry(entry, &rollback, fifo)
			entry->res = err_code;
		/* Rollback unprocessed requests */
		stailq_concat(&wal_msg->rollback, &rollback);
		wal_begin_rollback();
	} else {
		assert(err_code == JOURNAL_ENTRY_ERR_UNKNOWN);
	}
	fiber_gc();
	wal_notify_watchers(writer, WAL_EVENT_WRITE);
	ERROR_INJECT_SLEEP(ERRINJ_RELAY_FASTER_THAN_TX);
}

/** WAL writer main loop.  */
static int
wal_writer_f(va_list ap)
{
	(void) ap;
	struct wal_writer *writer = &wal_writer_singleton;

	/** Initialize eio in this thread */
	coio_enable();

	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, "wal", fiber_schedule_cb, fiber());
	/*
	 * Create a pipe to TX thread. Use a high priority
	 * endpoint, to ensure that WAL messages are delivered
	 * even when tx fiber pool is used up by net messages.
	 */
	cpipe_create(&writer->tx_prio_pipe, "tx_prio");

	cbus_loop(&endpoint);

	/*
	 * Create a new empty WAL on shutdown so that we don't
	 * have to rescan the last WAL to find the instance vclock.
	 * Don't create a WAL if the last one is empty.
	 */
	if (writer->wal_mode != WAL_NONE &&
	    (!xlog_is_open(&writer->current_wal) ||
	     vclock_compare(&writer->vclock,
			    &writer->current_wal.meta.vclock) > 0)) {
		struct xlog l;
		if (xdir_create_xlog(&writer->wal_dir, &l,
				     &writer->vclock) == 0)
			xlog_close(&l, false);
		else
			diag_log();
	}

	if (xlog_is_open(&writer->current_wal))
		xlog_close(&writer->current_wal, false);

	if (xlog_is_open(&vy_log_writer.xlog))
		xlog_close(&vy_log_writer.xlog, false);

	cpipe_destroy(&writer->tx_prio_pipe);
	return 0;
}

/**
 * WAL writer main entry point: queue a single request
 * to be written to disk.
 */
static int
wal_write_async(struct journal *journal, struct journal_entry *entry)
{
	struct wal_writer *writer = (struct wal_writer *) journal;

	ERROR_INJECT(ERRINJ_WAL_IO, {
		diag_set(ClientError, ER_WAL_IO);
		goto fail;
	});

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
		diag_set(ClientError, ER_CASCADE_ROLLBACK);
		goto fail;
	}

	struct wal_msg *batch;
	if (!stailq_empty(&writer->wal_pipe.input) &&
	    (batch = wal_msg(stailq_first_entry(&writer->wal_pipe.input,
						struct cmsg, fifo)))) {

		stailq_add_tail_entry(&batch->commit, entry, fifo);
	} else {
		batch = (struct wal_msg *)mempool_alloc(&writer->msg_pool);
		if (batch == NULL) {
			diag_set(OutOfMemory, sizeof(struct wal_msg),
				 "region", "struct wal_msg");
			goto fail;
		}
		wal_msg_create(batch);
		/*
		 * Sic: first add a request, then push the batch,
		 * since cpipe_push() may pass the batch to WAL
		 * thread right away.
		 */
		stailq_add_tail_entry(&batch->commit, entry, fifo);
		cpipe_push(&writer->wal_pipe, &batch->base);
	}
	/*
	 * Remember last entry sent to WAL. In case of rollback
	 * WAL will use this entry as an anchor to rollback all
	 * transactions until and including this one.
	 */
	writer->last_entry = entry;
	batch->approx_len += entry->approx_len;
	writer->wal_pipe.n_input += entry->n_rows * XROW_IOVMAX;
#ifndef NDEBUG
	++errinj(ERRINJ_WAL_WRITE_COUNT, ERRINJ_INT)->iparam;
#endif
	cpipe_flush_input(&writer->wal_pipe);
	return 0;

fail:
	assert(entry->res == JOURNAL_ENTRY_ERR_UNKNOWN);
	return -1;
}

static int
wal_write(struct journal *journal, struct journal_entry *entry)
{
	/*
	 * We can reuse async WAL engine transparently
	 * to the caller.
	 */
	if (wal_write_async(journal, entry) != 0)
		return -1;

	bool cancellable = fiber_set_cancellable(false);
	fiber_yield();
	fiber_set_cancellable(cancellable);

	return 0;
}

static int
wal_write_none_async(struct journal *journal,
		     struct journal_entry *entry)
{
	struct wal_writer *writer = (struct wal_writer *) journal;
	struct vclock vclock_diff;

	vclock_create(&vclock_diff);
	wal_assign_lsn(&vclock_diff, &writer->vclock, entry);
	vclock_merge(&writer->vclock, &vclock_diff);
	vclock_copy(&replicaset.vclock, &writer->vclock);
	entry->res = vclock_sum(&writer->vclock);
	journal_async_complete(entry);
	return 0;
}

static int
wal_write_none(struct journal *journal, struct journal_entry *entry)
{
	return wal_write_none_async(journal, entry);
}

void
wal_init_vy_log(void)
{
	xlog_clear(&vy_log_writer.xlog);
}

struct wal_write_vy_log_msg
{
	struct cbus_call_msg base;
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
	struct wal_writer *writer = &wal_writer_singleton;
	struct wal_write_vy_log_msg msg;
	msg.entry= entry;
	bool cancellable = fiber_set_cancellable(false);
	int rc = cbus_call(&writer->wal_pipe, &writer->tx_prio_pipe,
			   &msg.base, wal_write_vy_log_f, NULL,
			   TIMEOUT_INFINITY);
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
wal_rotate_vy_log(void)
{
	struct wal_writer *writer = &wal_writer_singleton;
	struct cbus_call_msg msg;
	bool cancellable = fiber_set_cancellable(false);
	cbus_call(&writer->wal_pipe, &writer->tx_prio_pipe, &msg,
		  wal_rotate_vy_log_f, NULL, TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);
}

static void
wal_watcher_notify(struct wal_watcher *watcher, unsigned events)
{
	assert(!rlist_empty(&watcher->next));

	struct wal_watcher_msg *msg = &watcher->msg;
	if (msg->cmsg.route != NULL) {
		/*
		 * If the notification message is still en route,
		 * mark the watcher to resend it as soon as it
		 * returns to WAL so as not to lose any events.
		 */
		watcher->pending_events |= events;
		return;
	}

	msg->events = events;
	cmsg_init(&msg->cmsg, watcher->route);
	cpipe_push(&watcher->watcher_pipe, &msg->cmsg);
	ERROR_INJECT(ERRINJ_RELAY_FASTER_THAN_TX,
		     cpipe_deliver_now(&watcher->watcher_pipe));
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

	if (watcher->pending_events != 0) {
		/*
		 * Resend the message if we got notified while
		 * it was en route, see wal_watcher_notify().
		 */
		wal_watcher_notify(watcher, watcher->pending_events);
		watcher->pending_events = 0;
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
	watcher->pending_events = 0;

	assert(lengthof(watcher->route) == 2);
	watcher->route[0] = (struct cmsg_hop)
		{ wal_watcher_notify_perform, &watcher->wal_pipe };
	watcher->route[1] = (struct cmsg_hop)
		{ wal_watcher_notify_complete, NULL };
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
wal_atfork(void)
{
	if (xlog_is_open(&wal_writer_singleton.current_wal))
		xlog_atfork(&wal_writer_singleton.current_wal);
	if (xlog_is_open(&vy_log_writer.xlog))
		xlog_atfork(&vy_log_writer.xlog);
}
