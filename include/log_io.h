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

struct tbuf;

#define RECOVER_READONLY 1

extern const u16 wal_tag, snap_tag;
extern const u64 default_cookie;
extern const u32 default_version;

struct recovery_state;
typedef int (row_handler) (struct recovery_state *, struct tbuf *);
typedef struct tbuf *(row_reader) (FILE *f, struct palloc_pool *pool);

enum log_mode {
	LOG_READ,
	LOG_WRITE
};

struct log_io_class {
	row_reader *reader;
	u64 marker, eof_marker;
	size_t marker_size, eof_marker_size;
	bool panic_if_error;

	/* Additional flags to apply at open(2) to write. */
	int  open_wflags;
	const char *filetype;
	const char *version;
	const char *suffix;
	char *dirname;
};


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
	struct log_io_class *class;
	FILE *f;

	ev_stat stat;
	enum log_mode mode;
	size_t rows;
	size_t retry;
	char filename[PATH_MAX + 1];

	bool is_inprogress;
};

struct wal_writer;

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
	struct log_io_class *snap_class;
	struct log_io_class *wal_class;
	struct wal_writer *writer;

	/* row_handler will be presented by most recent format of data
	   log_io_class->reader is responsible of converting data from old format */
	row_handler *row_handler;
	struct sockaddr_in remote_addr;
	struct fiber *remote_recovery;

	ev_timer wal_timer;
	ev_tstamp recovery_lag, recovery_last_update_tstamp;

	int snap_io_rate_limit;
	int rows_per_wal;
	int flags;
	double wal_fsync_delay;
	u64 cookie;
	struct wait_lsn wait_lsn;

	bool finalize;

	/* Points to module-specific state */
	void *data;
};

struct recovery_state *recovery_state;

struct wal_write_request {
	STAILQ_ENTRY(wal_write_request) wal_fifo_entry;
	/* Auxiliary. */
	u64 out_lsn;
	struct fiber *fiber;
	/** Header. */
	u32 marker;
	u32 header_crc32c;
	i64 lsn;
	double tm;
	u32 len;
	u32 data_crc32c;
	/* Data. */
	u16 tag;
	u64 cookie;
	u16 op;
	u8 data[];
} __attribute__((packed));

/* @todo: merge with wal_write_request. */
struct row_v11 {
	u32 header_crc32c;
	i64 lsn;
	double tm;
	u32 len;
	u32 data_crc32c;
	u8 data[];
} __attribute__((packed));


static inline struct row_v11 *row_v11(const struct tbuf *t)
{
	return (struct row_v11 *)t->data;
}

struct tbuf *convert_to_v11(struct tbuf *orig, u16 tag, u64 cookie, i64 lsn);

void recovery_init(const char *snap_dirname, const char *xlog_dirname,
		   row_handler row_handler,
		   int rows_per_wal, const char *wal_mode,
		   double wal_fsync_delay,
		   int flags, void *data);
void recovery_update_mode(const char *wal_mode, double fsync_delay);
void recovery_update_io_rate_limit(double new_limit);
void recovery_free();
int recover(struct recovery_state *, i64 lsn);
void recover_follow(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay);
void recover_finalize(struct recovery_state *r);
int wal_write(struct recovery_state *r, u16 tag, u16 op,
	      u64 cookie, i64 lsn, struct tbuf *data);

void recovery_setup_panic(struct recovery_state *r, bool on_snap_error, bool on_wal_error);

int confirm_lsn(struct recovery_state *r, i64 lsn);
int64_t next_lsn(struct recovery_state *r, i64 new_lsn);
void recovery_wait_lsn(struct recovery_state *r, i64 lsn);

int read_log(const char *filename,
	     row_handler xlog_handler, row_handler snap_handler, void *state);

void recovery_follow_remote(struct recovery_state *r, const char *remote);
void recovery_stop_remote(struct recovery_state *r);

struct log_io_iter;
void snapshot_write_row(struct log_io_iter *i, u16 tag, u64 cookie, struct tbuf *row);
void snapshot_save(struct recovery_state *r, void (*loop) (struct log_io_iter *));

#endif /* TARANTOOL_LOG_IO_H_INCLUDED */
