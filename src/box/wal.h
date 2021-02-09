#ifndef TARANTOOL_WAL_WRITER_H_INCLUDED
#define TARANTOOL_WAL_WRITER_H_INCLUDED
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
#include <stdint.h>
#include <sys/types.h>
#include "small/rlist.h"
#include "cbus.h"
#include "journal.h"
#include "vclock/vclock.h"

struct fiber;
struct wal_writer;
struct tt_uuid;

enum wal_mode { WAL_NONE = 0, WAL_WRITE, WAL_FSYNC, WAL_MODE_MAX };

enum {
	/**
	 * Recovery yields once per that number of rows read and
	 * applied from WAL. It allows not to block the event
	 * loop for the whole recovery stage.
	 */
	WAL_ROWS_PER_YIELD = 1 << 15,
};

/** String constants for the supported modes. */
extern const char *wal_mode_STRS[];

extern int wal_dir_lock;

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Callback invoked in the TX thread when the WAL thread runs out
 * of disk space and has to delete some old WAL files to continue.
 * It is supposed to shoot off WAL consumers that need the deleted
 * files. The vclock of the oldest WAL row still available on the
 * instance is passed in @vclock.
 */
typedef void (*wal_on_garbage_collection_f)(const struct vclock *vclock);

/**
 * Callback invoked in the TX thread when the total size of WAL
 * files written since the last checkpoint exceeds the configured
 * threshold.
 */
typedef void (*wal_on_checkpoint_threshold_f)(void);

/**
 * Start WAL thread and initialize WAL writer.
 */
int
wal_init(enum wal_mode wal_mode, const char *wal_dirname,
	 int64_t wal_max_size, const struct tt_uuid *instance_uuid,
	 wal_on_garbage_collection_f on_garbage_collection,
	 wal_on_checkpoint_threshold_f on_checkpoint_threshold);

/**
 * Setup WAL writer as journaling subsystem.
 */
int
wal_enable(void);

/**
 * Stop WAL thread and free WAL writer resources.
 */
void
wal_free(void);

/**
 * Get WAL directory path. The value never changes after box is
 * configured first time. Safe to use from multiple threads.
 */
const char *
wal_dir(void);

struct wal_watcher_msg {
	struct cmsg cmsg;
	struct wal_watcher *watcher;
	unsigned events;
};

enum wal_event {
	/** A row is written to the current WAL. */
	WAL_EVENT_WRITE		= (1 << 0),
	/** A new WAL is created. */
	WAL_EVENT_ROTATE	= (1 << 1),
};

struct wal_watcher {
	/** Link in wal_writer::watchers. */
	struct rlist next;
	/** The watcher callback function. */
	void (*cb)(struct wal_watcher *, unsigned events);
	/** Pipe from the watcher to WAL. */
	struct cpipe wal_pipe;
	/** Pipe from WAL to the watcher. */
	struct cpipe watcher_pipe;
	/** Cbus route used for notifying the watcher. */
	struct cmsg_hop route[2];
	/** Message sent to notify the watcher. */
	struct wal_watcher_msg msg;
	/**
	 * Bit mask of WAL events that happened while
	 * the notification message was en route.
	 * It indicates that the message must be resend
	 * right upon returning to WAL.
	 */
	unsigned pending_events;
};

/**
 * Subscribe to WAL events.
 *
 * The caller will receive a notification after a WAL write with
 * unspecified but reasonable latency. The first notification is
 * sent right after registering the watcher so that the caller
 * can process WALs written before the function was called.
 *
 * Note WAL notifications are delivered via cbus hence the caller
 * must have set up the cbus endpoint and started the event loop.
 * Alternatively, one can pass a callback invoking cbus_process()
 * to this function.
 *
 * @param watcher     WAL watcher to register.
 * @param name        Name of the cbus endpoint at the caller's cord.
 * @param watcher_cb  Callback to invoke from the caller's cord
 *                    upon receiving a WAL event. Apart from the
 *                    watcher itself, it takes a bit mask of events.
 *                    Events are described in wal_event enum.
 * @param process_cb  Function called to process cbus messages
 *                    while the watcher is being attached or NULL
 *                    if the cbus loop is running elsewhere.
 */
void
wal_set_watcher(struct wal_watcher *watcher, const char *name,
		void (*watcher_cb)(struct wal_watcher *, unsigned events),
		void (*process_cb)(struct cbus_endpoint *));

/**
 * Unsubscribe from WAL events.
 *
 * @param watcher     WAL watcher to unregister.
 * @param process_cb  Function invoked to process cbus messages
 *                    while the watcher is being detached or NULL
 *                    if the cbus loop is running elsewhere.
 */
void
wal_clear_watcher(struct wal_watcher *watcher,
		  void (*process_cb)(struct cbus_endpoint *));

void
wal_atfork(void);

enum wal_mode
wal_mode(void);

/**
 * Wait until all submitted writes are successfully flushed
 * to disk. Returns 0 on success, -1 if write failed.
 * Corresponding vclock is returned in @a vclock unless it is
 * NULL.
 */
int
wal_sync(struct vclock *vclock);

struct wal_checkpoint {
	struct cbus_call_msg base;
	/**
	 * VClock of the last record written to the rotated WAL.
	 * This is the vclock that is supposed to be used to
	 * identify the new checkpoint.
	 */
	struct vclock vclock;
	/**
	 * Size of WAL files written since the last checkpoint.
	 * Used to reset the corresponding WAL counter upon
	 * successful checkpoint creation.
	 */
	int64_t wal_size;
};

/**
 * Prepare WAL for checkpointing.
 *
 * This function flushes all pending changes and rotates the
 * current WAL. Checkpoint info is returned in @checkpoint.
 * It is supposed to be passed to wal_commit_checkpoint()
 * upon successful checkpoint creation.
 */
int
wal_begin_checkpoint(struct wal_checkpoint *checkpoint);

/**
 * This function is called upon successful checkpoint creation.
 * It updates the WAL thread's version of the last checkpoint
 * vclock.
 */
void
wal_commit_checkpoint(struct wal_checkpoint *checkpoint);

/**
 * Set the WAL size threshold exceeding which will trigger
 * checkpointing in TX.
 */
void
wal_set_checkpoint_threshold(int64_t threshold);

/**
 * Set the pending write limit in bytes. Once the limit is reached, new
 * writes are blocked until some previous writes succeed.
 */
void
wal_set_queue_max_size(int64_t size);

/**
 * Remove WAL files that are not needed by consumers reading
 * rows at @vclock or newer.
 */
void
wal_collect_garbage(const struct vclock *vclock);

void
wal_init_vy_log(void);

/**
 * Write xrows to the vinyl metadata log.
 */
int
wal_write_vy_log(struct journal_entry *req);

/**
 * Rotate the vinyl metadata log.
 */
void
wal_rotate_vy_log(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_WAL_WRITER_H_INCLUDED */
