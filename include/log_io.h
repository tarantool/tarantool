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

#ifndef TARANTOOL_LOG_IO_H
#define TARANTOOL_LOG_IO_H

#include <stdio.h>
#include <limits.h>

#include <fiber.h>
#include <tarantool_ev.h>
#include <tbuf.h>
#include <util.h>

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
	size_t rows_per_file;
	double fsync_delay;
	bool panic_if_error;

	const char *filetype;
	const char *version;
	const char *suffix;
	const char *dirname;
};

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

struct recovery_state {
	i64 lsn, confirmed_lsn;

	struct log_io *current_wal;	/* the WAL we'r currently reading/writing from/to */
	struct log_io_class **snap_class, **wal_class, *snap_prefered_class, *wal_prefered_class;
	struct child *wal_writer;

	/* row_handler will be presented by most recent format of data
	   log_io_class->reader is responsible of converting data from old format */
	row_handler *row_handler;

	ev_timer wal_timer;
	ev_tstamp recovery_lag, recovery_last_update_tstamp;

	int snap_io_rate_limit;
	u64 cookie;

	bool finalize;

	/* pointer to user supplied custom data */
	void *data;
};

struct remote_state {
	struct recovery_state *r;
	int (*handler) (struct recovery_state * r, struct tbuf *row);
};

struct wal_write_request {
	i64 lsn;
	u32 len;
	u8 data[];
} __packed__;

struct row_v11 {
	u32 header_crc32c;
	i64 lsn;
	double tm;
	u32 len;
	u32 data_crc32c;
	u8 data[];
} __packed__;

static inline struct row_v11 *row_v11(const struct tbuf *t)
{
	return (struct row_v11 *)t->data;
}

struct tbuf *convert_to_v11(struct tbuf *orig, u16 tag, u64 cookie, i64 lsn);

struct recovery_state *recover_init(const char *snap_dirname, const char *xlog_dirname,
				    row_reader snap_row_reader, row_handler row_handler,
				    int rows_per_file, double fsync_delay, int inbox_size,
				    int flags, void *data);
int recover(struct recovery_state *, i64 lsn);
void recover_follow(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay);
void recover_finalize(struct recovery_state *r);
bool wal_write(struct recovery_state *r, u16 tag, u64 cookie, i64 lsn, struct tbuf *data);

void recovery_setup_panic(struct recovery_state *r, bool on_snap_error, bool on_wal_error);

int confirm_lsn(struct recovery_state *r, i64 lsn);
int64_t next_lsn(struct recovery_state *r, i64 new_lsn);

int read_log(const char *filename, row_reader reader,
	     row_handler xlog_handler, row_handler snap_handler, void *state);

int default_remote_row_handler(struct recovery_state *r, struct tbuf *row);
struct fiber *recover_follow_remote(struct recovery_state *r, char *ip_addr, int port,
				    int (*handler) (struct recovery_state *r, struct tbuf *row));

struct log_io_iter;
void snapshot_write_row(struct log_io_iter *i, u16 tag, u64 cookie, struct tbuf *row);
void snapshot_save(struct recovery_state *r, void (*loop) (struct log_io_iter *));

#endif
