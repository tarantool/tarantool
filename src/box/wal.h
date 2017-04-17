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
#include "journal.h"

struct fiber;
struct vclock;
struct wal_writer;

enum wal_mode { WAL_NONE = 0, WAL_WRITE, WAL_FSYNC, WAL_MODE_MAX };

/** String constants for the supported modes. */
extern const char *wal_mode_STRS[];

extern int wal_dir_lock;

#if defined(__cplusplus)

void
wal_thread_start();

void
wal_init(enum wal_mode wal_mode, const char *wal_dirname,
	 const struct tt_uuid *instance_uuid, struct vclock *vclock,
	 int64_t wal_max_rows, int64_t wal_max_size);

enum wal_mode
wal_mode();

void
wal_thread_stop();

struct wal_watcher
{
	struct rlist next;
	struct ev_loop *loop;
	struct ev_async *async;
};

/**
 * Receive a notification the next time a wal_write is completed
 * (+unspecified but reasonable latency).
 * Fails (-1) if recovery is NULL or lacking a WAL writer.
 */
int
wal_set_watcher(struct wal_watcher *, struct ev_async *);

void
wal_clear_watcher(struct wal_watcher *);

void
wal_atfork();

extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Wait till all pending changes to the WAL are flushed.
 * Rotates the WAL.
 *
 * @param[out] vclock WAL vclock
 *
 */
int
wal_checkpoint(struct vclock *vclock, bool rotate);

/**
 * Remove WAL files that are not needed to recover
 * from snapshot with @lsn or newer.
 */
void
wal_collect_garbage(int64_t lsn);

void
wal_init_vy_log();

/**
 * Write xrows to the vinyl metadata log.
 */
int
wal_write_vy_log(struct journal_entry *req);

/**
 * Rotate the vinyl metadata log.
 */
void
wal_rotate_vy_log();

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_WAL_WRITER_H_INCLUDED */
