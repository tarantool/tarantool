#ifndef TARANTOOL_LOG_IO_H_INCLUDED
#define TARANTOOL_LOG_IO_H_INCLUDED
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
#include <stdio.h>
#include <limits.h>
#include <stdbool.h>
#include "tarantool/util.h"
#include "tarantool_ev.h"

extern const uint32_t default_version;

enum log_format { XLOG = 65534, SNAP = 65535 };

enum log_mode {
	LOG_READ,
	LOG_WRITE
};

enum log_suffix { NONE, INPROGRESS };

struct log_dir {
	bool panic_if_error;
	/**
	 * true if the file can by fsync()ed at close
	 * in a separate thread.
	 */
	bool sync_is_async;

	/* Additional flags to apply at open(2) to write. */
	int  open_wflags;
	const char *filetype;
	const char *filename_ext;
	char *dirname;
};

extern struct log_dir snap_dir;
extern struct log_dir wal_dir;

int64_t
greatest_lsn(struct log_dir *dir);
char *
format_filename(struct log_dir *dir, int64_t lsn, enum log_suffix suffix);
int64_t
find_including_file(struct log_dir *dir, int64_t target_lsn);

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
log_io_open_for_read(struct log_dir *dir, int64_t lsn, enum log_suffix suffix);
struct log_io *
log_io_open_for_write(struct log_dir *dir, int64_t lsn, enum log_suffix suffix);
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

const char *
log_io_cursor_next(struct log_io_cursor *i, uint32_t *rowlen);

typedef uint32_t log_magic_t;

struct header_v11 {
	uint32_t header_crc32c;
	int64_t lsn;
	double tm;
	uint32_t len;
	uint32_t data_crc32c;
} __attribute__((packed));

static inline struct header_v11 *header_v11(const char *t)
{
	return (struct header_v11 *)t;
}

static inline void
header_v11_fill(struct header_v11 *header, int64_t lsn, size_t data_len)
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
	uint16_t tag;
	uint64_t cookie;
	uint8_t data[];
} __attribute__((packed));

void
row_v11_fill(struct row_v11 *row, int64_t lsn, uint16_t tag,
	     uint64_t cookie, const char *metadata, size_t metadata_len,
	     const char *data, size_t data_len);

static inline size_t
row_v11_size(struct row_v11 *row)
{
	return sizeof(row->marker) + sizeof(struct header_v11) + row->header.len;
}

int
inprogress_log_unlink(char *filename);
int
inprogress_log_rename(struct log_io *l);

#endif /* TARANTOOL_LOG_IO_H_INCLUDED */
