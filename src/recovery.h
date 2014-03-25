#ifndef TARANTOOL_RECOVERY_H_INCLUDED
#define TARANTOOL_RECOVERY_H_INCLUDED
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
#include <stdbool.h>

#include "trivia/util.h"
#include "tarantool_ev.h"
#include "log_io.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct fiber;
struct tbuf;

typedef int (row_handler)(void *, struct iproto_packet *packet);
typedef void (snapshot_handler)(struct log_io *);

/** A "condition variable" that allows fibers to wait when a given
 * LSN makes it to disk.
 */

struct wait_lsn {
	struct fiber *waiter;
	int64_t lsn;
};

void
wait_lsn_set(struct wait_lsn *wait_lsn, int64_t lsn);

inline static void
wait_lsn_clear(struct wait_lsn *wait_lsn)
{
	wait_lsn->waiter = NULL;
	wait_lsn->lsn = 0LL;
}

struct wal_writer;
struct wal_watcher;
struct remote;

enum wal_mode { WAL_NONE = 0, WAL_WRITE, WAL_FSYNC, WAL_FSYNC_DELAY, WAL_MODE_MAX };

/** String constants for the supported modes. */
extern const char *wal_mode_STRS[];

struct recovery_state {
	int64_t lsn, confirmed_lsn;
	/* The WAL we're currently reading/writing from/to. */
	struct log_io *current_wal;
	struct log_dir *snap_dir;
	struct log_dir *wal_dir;
	struct wal_writer *writer;
	struct wal_watcher *watcher;
	struct remote *remote;
	/**
	 * row_handler is a module callback invoked during initial
	 * recovery and when reading rows from the master.  It is
	 * presented with the most recent format of data.
	 * row_reader is responsible for converting data from old
	 * formats.
	 */
	row_handler *row_handler;
	void *row_handler_param;
	snapshot_handler *snapshot_handler;
	uint64_t snap_io_rate_limit;
	int rows_per_wal;
	double wal_fsync_delay;
	struct wait_lsn wait_lsn;
	enum wal_mode wal_mode;

	bool finalize;
};

extern struct recovery_state *recovery_state;

void recovery_init(const char *snap_dirname, const char *xlog_dirname,
		   row_handler row_handler, void *row_handler_param,
		   snapshot_handler snapshot_handler, int rows_per_wal);
void recovery_update_mode(struct recovery_state *r,
			  const char *wal_mode, double fsync_delay);
void recovery_update_io_rate_limit(struct recovery_state *r,
				   double new_limit);
void recovery_free();
void recover_snap(struct recovery_state *r, const char *replication_source);
void recover_existing_wals(struct recovery_state *);
void recovery_follow_local(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay);
void recovery_finalize(struct recovery_state *r);

int
recover_wal(struct recovery_state *r, struct log_io *l); /* for replication */
int wal_write(struct recovery_state *r, struct iproto_packet *packet);

void recovery_setup_panic(struct recovery_state *r, bool on_snap_error, bool on_wal_error);

void confirm_lsn(struct recovery_state *r, int64_t lsn, bool is_commit);
int64_t next_lsn(struct recovery_state *r);
void set_lsn(struct recovery_state *r, int64_t lsn);

void recovery_wait_lsn(struct recovery_state *r, int64_t lsn);

struct fio_batch;

void
snapshot_write_row(struct log_io *l, struct iproto_packet *packet);
void snapshot_save(struct recovery_state *r);

void
init_storage_on_master(struct log_dir *dir);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_RECOVERY_H_INCLUDED */
