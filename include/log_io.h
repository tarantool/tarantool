#ifndef TARANTOOL_LOG_IO_H_INCLUDED
#define TARANTOOL_LOG_IO_H_INCLUDED
/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdbool.h>
#include <stdio.h>
#include <limits.h>

#include <tarantool_ev.h>
#include <tbuf.h>
#include <util.h>
#include <palloc.h>
#include <netinet/in.h> /* struct sockaddr_in */
#include <third_party/queue.h>

#define RECOVER_READONLY 1

extern const u32 default_version;

struct recovery_state;
typedef int (row_handler)(struct tbuf *);

enum log_mode {
	LOG_READ,
	LOG_WRITE
};

enum log_format { XLOG = 65534, SNAP = 65535 };

/** A "condition variable" that allows fibers to wait when a given
 * LSN makes it to disk.
 */

struct wait_lsn {
	struct fiber *waiter;
	i64 lsn;
};

void
wait_lsn_set(struct wait_lsn *wait_lsn, i64 lsn);

inline static void
wait_lsn_clear(struct wait_lsn *wait_lsn)
{
	wait_lsn->waiter = NULL;
	wait_lsn->lsn = 0LL;
}

struct log_io {
	struct log_dir *dir;
	FILE *f;

	enum log_mode mode;
	size_t rows;
	int retry;
	char filename[PATH_MAX + 1];

	bool is_inprogress;
};

struct wal_writer;
struct wal_watcher;

struct recovery_state {
	i64 lsn, confirmed_lsn;
	/* The WAL we're currently reading/writing from/to. */
	struct log_io *current_wal;
	/*
	 * When opening the next WAL, we want to first open
	 * a new file before closing the previous one. Thus
	 * we save the old WAL here.
	 */
	struct log_io *previous_wal;
	struct log_dir *snap_dir;
	struct log_dir *wal_dir;
	struct wal_writer *writer;
	struct wal_watcher *watcher;
	struct fiber *remote_recovery;

	/**
	 * Row_handler is invoked during initial recovery.
	 * It will be presented with the most recent format of
	 * data. Row_reader is responsible for converting data
	 * from old formats.
	 */
	row_handler *row_handler;
	struct sockaddr_in remote_addr;

	ev_tstamp recovery_lag, recovery_last_update_tstamp;

	int snap_io_rate_limit;
	int rows_per_wal;
	int flags;
	double wal_fsync_delay;
	u64 cookie;
	struct wait_lsn wait_lsn;

	bool finalize;
};

struct recovery_state *recovery_state;

struct header_v11 {
	u32 header_crc32c;
	i64 lsn;
	double tm;
	u32 len;
	u32 data_crc32c;
} __attribute__((packed));

static inline struct header_v11 *header_v11(const struct tbuf *t)
{
	return (struct header_v11 *)t->data;
}

void recovery_init(const char *snap_dirname, const char *xlog_dirname,
		   row_handler row_handler,
		   int rows_per_wal, const char *wal_mode,
		   double wal_fsync_delay,
		   int flags);
void recovery_update_mode(const char *wal_mode, double fsync_delay);
void recovery_update_io_rate_limit(double new_limit);
void recovery_free();
void recover(struct recovery_state *, i64 lsn);
void recovery_follow_local(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay);
void recovery_finalize(struct recovery_state *r);
int wal_write(struct recovery_state *r, i64 lsn, u64 cookie,
	      u16 op, struct tbuf *data);

void recovery_setup_panic(struct recovery_state *r, bool on_snap_error, bool on_wal_error);

int confirm_lsn(struct recovery_state *r, i64 lsn);
int64_t next_lsn(struct recovery_state *r, i64 new_lsn);
void recovery_wait_lsn(struct recovery_state *r, i64 lsn);

int read_log(const char *filename,
	     row_handler xlog_handler, row_handler snap_handler);

void recovery_follow_remote(struct recovery_state *r, const char *remote);
void recovery_stop_remote(struct recovery_state *r);

void snapshot_write_row(struct log_io *i,
			const void *metadata, size_t metadata_size,
			const void *data, size_t data_size);
void snapshot_save(struct recovery_state *r, void (*loop) (struct log_io *));

#endif /* TARANTOOL_LOG_IO_H_INCLUDED */
