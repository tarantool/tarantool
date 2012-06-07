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
#include <stdio.h>
#include <limits.h>
#include <stdbool.h>
#include "util.h"
#include "tbuf.h"
#include "tarantool_ev.h"

extern const u32 default_version;

enum log_format { XLOG = 65534, SNAP = 65535 };

enum log_mode {
	LOG_READ,
	LOG_WRITE
};

enum log_suffix { NONE, INPROGRESS };

struct log_dir {
	bool panic_if_error;

	/* Additional flags to apply at open(2) to write. */
	int  open_wflags;
	const char *filetype;
	const char *filename_ext;
	char *dirname;
};

extern struct log_dir snap_dir;
extern struct log_dir wal_dir;

i64
greatest_lsn(struct log_dir *dir);
char *
format_filename(struct log_dir *dir, i64 lsn, enum log_suffix suffix);
i64
find_including_file(struct log_dir *dir, i64 target_lsn);

struct log_io {
	struct log_dir *dir;
	FILE *f;

	enum log_mode mode;
	size_t rows;
	int retry;
	char filename[PATH_MAX + 1];

	bool is_inprogress;
};

struct log_io *
log_io_open_for_read(struct log_dir *dir, i64 lsn, enum log_suffix suffix);
struct log_io *
log_io_open_for_write(struct log_dir *dir, i64 lsn, enum log_suffix suffix);
struct log_io *
log_io_open(struct log_dir *dir, enum log_mode mode,
	    const char *filename, enum log_suffix suffix, FILE *file);
int
log_io_sync(struct log_io *l);
int
log_io_close(struct log_io **lptr);
void
log_io_atfork(struct log_io **lptr);

struct log_io_cursor
{
	struct log_io *log;
	int row_count;
	off_t good_offset;
	bool eof_read;
};

void
log_io_cursor_open(struct log_io_cursor *i, struct log_io *l);
void
log_io_cursor_close(struct log_io_cursor *i);
struct tbuf *
log_io_cursor_next(struct log_io_cursor *i);

typedef u32 log_magic_t;

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

static inline void
header_v11_fill(struct header_v11 *header, u64 lsn, size_t data_len)
{
	header->lsn = lsn;
	header->tm = ev_now();
	header->len = data_len;
}

void
header_v11_sign(struct header_v11 *header);

struct row_v11 {
	log_magic_t marker;
	struct header_v11 header;
	u16 tag;
	u64 cookie;
	u8 data[];
} __attribute__((packed));

void
row_v11_fill(struct row_v11 *row, u64 lsn, u16 tag, u64 cookie,
	     const void *metadata, size_t metadata_len, const void
	     *data, size_t data_len);

static inline size_t
row_v11_size(struct row_v11 *row)
{
	return sizeof(row->marker) + sizeof(struct header_v11) + row->header.len;
}

int
inprogress_log_unlink(char *filename);
int
inprogress_log_rename(char *filename);

#endif /* TARANTOOL_LOG_IO_H_INCLUDED */
