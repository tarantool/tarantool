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
#include <sys/uio.h>
#include "trivia/util.h"
#include "third_party/tarantool_ev.h"
#include "iproto_constants.h"
#include "tt_uuid.h"

extern const uint32_t xlog_format;

enum log_mode {
	LOG_READ,
	LOG_WRITE
};

enum log_suffix { NONE, INPROGRESS };

struct log_meta;
struct log_meta_lsn;

#define RB_COMPACT 1
#include <third_party/rb.h>

/* Used by internal functions */
struct log_meta_lsn {
	rb_node(struct log_meta_lsn) link;
	int32_t node_id;
	int64_t lsn;
	struct log_meta *meta;
};

/* Used by internal functions */
struct log_meta {
	rb_node(struct log_meta) link;
	int64_t lsnsum;
	bool remove_flag; /* used internally */
	uint32_t lsn_count;
	struct log_meta_lsn lsns[0]; /* [0] is better for clang */
};

/*
 * Map: (lsnsum) => (struct log_meta)
 */

typedef rb_tree(struct log_meta) log_dir_map_t;
rb_proto(, log_dir_map_, log_dir_map_t, struct log_meta)

/*
 * Map: (node_id, lsn) => (struct log_meta)
 */

typedef rb_tree(struct log_meta_lsn) log_dir_lsnmap_t;
rb_proto(, log_dir_lsnmap_, log_dir_lsnmap_t, struct log_meta_lsn)

/*
 * Set: (node_id) - defined in .cc
 */
struct mh_nodeids_t;

struct log_dir {
	bool panic_if_error;
	/**
	 * true if the file can by fsync()ed at close
	 * in a separate thread.
	 */
	bool sync_is_async;
	/* don't check that sum(setlsn) == lsnsum in filename (for snaps) */
	bool ignore_initial_setlsn;

	/* Additional flags to apply at fopen(2) to write. */
	char open_wflags[6];
	const char *filetype;
	const char *filename_ext;
	char *dirname;
	/** File create mode in this directory. */
	mode_t mode;

	/* Directory indexes for log_dir_next() */
	log_dir_lsnmap_t lsnmap;
	log_dir_map_t map;
	struct mh_nodeids_t *nodeids;
};

int
log_dir_create(struct log_dir *dir);
void
log_dir_destroy(struct log_dir *dir);

int
log_dir_scan(struct log_dir *dir);

int64_t
log_dir_greatest(struct log_dir *dir);

int64_t
log_dir_next(struct log_dir *dir, struct vclock *vclock);

char *
format_filename(struct log_dir *dir, int64_t lsn, enum log_suffix suffix);

void
log_encode_setlsn(struct iproto_packet *packet, const struct vclock *vclock);

struct log_setlsn_row {
	uint32_t node_id;
	int64_t lsn;
};

struct log_setlsn_row *
log_decode_setlsn(struct iproto_packet *packet, uint32_t *p_size);

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
log_io_open_for_read(struct log_dir *dir, int64_t lsn, tt_uuid *node_uuid,
		     enum log_suffix suffix);
struct log_io *
log_io_open_for_write(struct log_dir *dir, int64_t lsn,
		      tt_uuid *node_uuid, enum log_suffix suffix);
struct log_io *
log_io_open(struct log_dir *dir, enum log_mode mode, const char *filename,
	    tt_uuid *node_uuid, enum log_suffix suffix, FILE *file);
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

int
log_io_cursor_next(struct log_io_cursor *i, struct iproto_packet *packet);
int
xlog_encode_row(const struct iproto_packet *packet, struct iovec *iov,
		char fixheader[XLOG_FIXHEADER_SIZE]);
enum { XLOG_ROW_IOVMAX = IPROTO_PACKET_IOVMAX + 1 };

typedef uint32_t log_magic_t;

int
inprogress_log_unlink(char *filename);
int
inprogress_log_rename(struct log_io *l);

#endif /* TARANTOOL_LOG_IO_H_INCLUDED */
