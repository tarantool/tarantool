#ifndef TARANTOOL_RECOVERY_H_INCLUDED
#define TARANTOOL_RECOVERY_H_INCLUDED
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
#include "trivia/util.h"
#include "third_party/tarantool_ev.h"
#include "xlog.h"
#include "vclock.h"
#include "tt_uuid.h"
#include "wal.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct recovery;
extern struct recovery *recovery;

typedef void (apply_row_f)(struct recovery *, void *,
			   struct xrow_header *packet);

/** A "condition variable" that allows fibers to wait when a given
 * LSN makes it to disk.
 */

struct wal_watcher;
struct wal_writer;

struct recovery {
	struct vclock vclock;
	/** The WAL we're currently reading/writing from/to. */
	struct xlog *current_wal;
	struct xdir snap_dir;
	struct xdir wal_dir;
	struct wal_writer *writer;
	/**
	 * This is used in local hot standby or replication
	 * relay mode: look for changes in the wal_dir and apply them
	 * locally or send to the replica.
	 */
	struct fiber *watcher;
	/**
	 * apply_row is a module callback invoked during initial
	 * recovery and when reading rows from the master.
	 */
	apply_row_f *apply_row;
	void *apply_row_param;
	uint64_t snap_io_rate_limit;
	enum wal_mode wal_mode;
	struct tt_uuid server_uuid;
	uint32_t server_id;
};

struct recovery *
recovery_new(const char *snap_dirname, const char *wal_dirname,
	     apply_row_f apply_row, void *apply_row_param);

void
recovery_delete(struct recovery *r);

/* to be called at exit */
void
recovery_exit(struct recovery *r);

void
recovery_update_mode(struct recovery *r, enum wal_mode mode);

void
recovery_update_io_rate_limit(struct recovery *r, double new_limit);

void
recovery_setup_panic(struct recovery *r, bool on_snap_error, bool on_wal_error);

static inline bool
recovery_has_data(struct recovery *r)
{
	return vclockset_first(&r->snap_dir.index) != NULL ||
	       vclockset_first(&r->wal_dir.index) != NULL;
}

void
recovery_bootstrap(struct recovery *r);

void
recover_xlog(struct recovery *r, struct xlog *l);

void
recovery_follow_local(struct recovery *r, const char *name,
		      ev_tstamp wal_dir_rescan_delay);

void
recovery_stop_local(struct recovery *r);

void
recovery_finalize(struct recovery *r, enum wal_mode mode,
		  int rows_per_wal);

void
recovery_fill_lsn(struct recovery *r, struct xrow_header *row);

void
recovery_apply_row(struct recovery *r, struct xrow_header *packet);

/**
 * Return LSN of the most recent snapshot or -1 if there is
 * no snapshot.
 */
int64_t
recovery_last_checkpoint(struct recovery *r);

/**
 * Ensure we don't corrupt the current WAL file in the child.
 */
void
recovery_atfork(struct recovery *r);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_RECOVERY_H_INCLUDED */
