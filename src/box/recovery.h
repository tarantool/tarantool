#ifndef TARANTOOL_RECOVERY_H_INCLUDED
#define TARANTOOL_RECOVERY_H_INCLUDED
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
#include "small/rlist.h"
#include "trivia/util.h"
#include <tarantool_ev.h>
#include "xlog.h"
#include "vclock/vclock.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct xrow_header;
struct xstream;

struct recovery {
	struct vclock vclock;
	/** The WAL cursor we're currently reading/writing from/to. */
	struct xlog_cursor cursor;
	struct xdir wal_dir;
	/**
	 * This fiber is used in local hot standby mode.
	 * It looks for changes in the wal_dir and applies
	 * them locally.
	 */
	struct fiber *watcher;
	/** List of triggers invoked when the current WAL is closed. */
	struct rlist on_close_log;
};

struct recovery *
recovery_new(const char *wal_dirname, bool force_recovery,
	     const struct vclock *vclock);

void
recovery_delete(struct recovery *r);

/**
 * Scan the WAL directory, build an index of all found
 * WAL files, then scan the most recent WAL file to find
 * the vclock of the last record (returned in @end_vclock).
 * @gc_vclock is set to the oldest vclock available in the
 * WAL directory.
 */
void
recovery_scan(struct recovery *r,  struct vclock *end_vclock,
	      struct vclock *gc_vclock, struct xstream *stream);

void
recovery_follow_local(struct recovery *r, struct xstream *stream,
		      const char *name, ev_tstamp wal_dir_rescan_delay);

void
recovery_stop_local(struct recovery *r);

void
recovery_finalize(struct recovery *r);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

/**
 * Find out if there are new .xlog files since the current
 * vclock, and read them all up.
 *
 * Reading will be stopped on reaching stop_vclock.
 * Use NULL for boundless recover
 *
 * This function will not close r->current_wal if
 * recovery was successful.
 */
void
recover_remaining_wals(struct recovery *r, struct xstream *stream,
		       const struct vclock *stop_vclock, bool scan_dir);

#endif /* TARANTOOL_RECOVERY_H_INCLUDED */
