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
#include <netinet/in.h>

#include "trivia/util.h"
#include "tarantool_ev.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct fiber;
struct tbuf;

typedef int (row_handler)(void *, const struct log_row *row);

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

enum { REMOTE_SOURCE_MAXLEN = 32 };

/** Master connection */
struct remote {
	struct sockaddr_in addr;
	struct fiber *reader;
	uint64_t cookie;
	ev_tstamp recovery_lag, recovery_last_update_tstamp;
	char source[REMOTE_SOURCE_MAXLEN];
};

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
		   int rows_per_wal);
void recovery_update_mode(struct recovery_state *r,
			  const char *wal_mode, double fsync_delay);
void recovery_update_io_rate_limit(struct recovery_state *r,
				   double new_limit);
void recovery_free();
void recover_snap(struct recovery_state *, const char *replication_source);
void recover_existing_wals(struct recovery_state *);
void recovery_follow_local(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay);
void recovery_finalize(struct recovery_state *r);
int wal_write(struct recovery_state *r, int64_t lsn, uint64_t cookie,
	      uint16_t op, const char *data, uint32_t len);

void recovery_setup_panic(struct recovery_state *r, bool on_snap_error, bool on_wal_error);

void confirm_lsn(struct recovery_state *r, int64_t lsn, bool is_commit);
int64_t next_lsn(struct recovery_state *r);
void set_lsn(struct recovery_state *r, int64_t lsn);

void recovery_wait_lsn(struct recovery_state *r, int64_t lsn);

void recovery_follow_remote(struct recovery_state *r, const char *addr);
void recovery_stop_remote(struct recovery_state *r);

void recovery_follow_remote_1_5(struct recovery_state *r, const char *addr);
void recovery_stop_remote_1_5(struct recovery_state *r);

/**
 * The replication protocol is request/response. The
 * replica saends a request, and the master responds with
 * appropriate data.
 */
enum rpl_request_type {
	RPL_GET_WAL = 0,
	RPL_GET_SNAPSHOT
};

struct fio_batch;

void snapshot_write_row(struct log_io *i, struct fio_batch *batch,
			const char *metadata, size_t metadata_size,
			const char *data, size_t data_size);
void snapshot_save(struct recovery_state *r,
		   void (*loop) (struct log_io *, struct fio_batch *));

void
init_storage(struct log_dir *dir, const char *replication_source);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_RECOVERY_H_INCLUDED */
