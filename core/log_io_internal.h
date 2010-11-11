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

#ifndef TARANTOOL_LOG_IO_INTERNAL_H
#define TARANTOOL_LOG_IO_INTERNAL_H

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
};

struct recovery_state {
	i64 lsn, confirmed_lsn;

	struct log_io *current_wal;	/* the WAL we'r currently reading/writing from/to */
	struct log_io_class **snap_class, **wal_class, *snap_prefered_class, *wal_prefered_class;
	struct child *wal_writer;

	/* handlers will be presented by most new format of data
	   log_io_class->reader is responsible of converting data from old format */
	row_handler *wal_row_handler, *snap_row_handler;
	ev_timer wal_timer;
	ev_tstamp recovery_lag;

	int snap_io_rate_limit;

	/* pointer to user supplied custom data */
	void *data;
};

struct wal_write_request {
	i64 lsn;
	u32 len;
	u8 data[];
} __packed__;

bool wal_write(struct recovery_state *r, i64 lsn, struct tbuf *data);

#endif
