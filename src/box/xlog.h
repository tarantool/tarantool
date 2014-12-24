#ifndef TARANTOOL_XLOG_H_INCLUDED
#define TARANTOOL_XLOG_H_INCLUDED
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
#include <stdbool.h>
#include "tt_uuid.h"
#include "vclock.h"


enum xdir_type { SNAP, XLOG };

struct xdir {
	bool panic_if_error;
	/**
	 * true if the file can by fsync()ed at close
	 * in a separate thread.
	 */
	bool sync_is_async;

	/* Additional flags to apply at fopen(2) to write. */
	char open_wflags[6];
	const char *filetype;
	const char *filename_ext;
	char *dirname;
	/** File create mode in this directory. */
	mode_t mode;

	vclockset_t index; /* vclock set for this directory */
};

void
xdir_create(struct xdir *dir, const char *dirname,
	       enum xdir_type type);
void
xdir_destroy(struct xdir *dir);

int
xdir_scan(struct xdir *dir);

enum log_mode { LOG_READ, LOG_WRITE };

enum log_suffix { NONE, INPROGRESS };


struct xlog {
	struct xdir *dir;
	FILE *f;

	enum log_mode mode;
	size_t rows;
	int retry;
	char filename[PATH_MAX + 1];

	bool is_inprogress;

	/* Meta information */
	tt_uuid server_uuid;
	struct vclock vclock;
};

struct xlog *
xlog_open(struct xdir *dir, int64_t signature,
		     const tt_uuid *server_uuid, enum log_suffix suffix);

struct xlog *
xlog_open_stream(struct xdir *dir, const char *filename,
			    const tt_uuid *server_uuid, enum log_suffix suffix,
			    FILE *file);

struct xlog *
xlog_create(struct xdir *dir, const tt_uuid *server_uuid,
		      const struct vclock *vclock);
int
xlog_sync(struct xlog *l);

int
xlog_rename(struct xlog *l);
int
xlog_close(struct xlog **lptr);
void
xlog_atfork(struct xlog **lptr);

struct xlog_cursor
{
	struct xlog *log;
	int row_count;
	off_t good_offset;
	bool eof_read;
};

void
xlog_cursor_open(struct xlog_cursor *i, struct xlog *l);
void
xlog_cursor_close(struct xlog_cursor *i);

int
xlog_cursor_next(struct xlog_cursor *i, struct xrow_header *packet);
int
xlog_encode_row(const struct xrow_header *packet, struct iovec *iov);

typedef uint32_t log_magic_t;

int
inprogress_log_unlink(char *filename);

char *
format_filename(struct xdir *dir, int64_t signature, enum log_suffix suffix);


#endif /* TARANTOOL_XLOG_H_INCLUDED */
