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

extern const u16 default_tag;

struct log_io;
struct recovery_state;
typedef int (row_handler)(struct recovery_state *, struct tbuf *);
typedef struct tbuf *(row_reader)(FILE *f, struct palloc_pool * pool);
struct row_v04 {
	i64 lsn;		/* this used to be tid */
	u16 type;
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

static inline struct row_v04 *row_v04(const struct tbuf *t)
{
	return (struct row_v04 *)t->data;
}

static inline struct row_v11 *row_v11(const struct tbuf *t)
{
	return (struct row_v11 *)t->data;
}

struct tbuf *convert_to_v11(struct tbuf *orig, i64 lsn);

struct recovery_state *recover_init(const char *snap_dirname, const char *xlog_dirname,
				    row_reader snap_row_reader, row_handler snap_row_handler,
				    row_handler xlog_row_handler, int rows_per_file,
				    double fsync_delay, double snap_io_rate_limit, int inbox_size,
				    int flags, void *data);
int recover(struct recovery_state *, i64 lsn);
void recover_follow(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay);
void recover_finalize(struct recovery_state *r);
bool wal_write(struct recovery_state *r, i64 lsn, struct tbuf *data);

/* recovery accessors */
struct palloc_pool *recovery_pool(struct recovery_state *r);
int confirm_lsn(struct recovery_state *r, i64 lsn);
int64_t confirmed_lsn(struct recovery_state *r);
int64_t next_lsn(struct recovery_state *r, i64 new_lsn);
struct child *wal_writer(struct recovery_state *r);

int read_log(const char *filename, row_reader reader,
	     row_handler xlog_handler, row_handler snap_handler, void *state);

struct fiber *recover_follow_remote(struct recovery_state *r, char *ip_addr, int port);

struct log_io_iter;
void snapshot_write_row(struct log_io_iter *i, struct tbuf *row);
void snapshot_save(struct recovery_state *r, void (*loop) (struct log_io_iter *));
#endif
