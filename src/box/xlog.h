#ifndef TARANTOOL_XLOG_H_INCLUDED
#define TARANTOOL_XLOG_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
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
#include <sys/stat.h>
#include "tt_uuid.h"
#include "vclock/vclock.h"

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

#include "small/ibuf.h"
#include "small/obuf.h"

struct iovec;
struct xrow_header;

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * This structure combines all xlog write options set on xlog
 * creation.
 */
struct xlog_opts {
	/** Write rate limit, in bytes per second. */
	uint64_t rate_limit;
	/** Sync interval, in bytes. */
	uint64_t sync_interval;
	/**
	 * If this flag is set and sync interval is greater than 0,
	 * page cache will be freed after each sync.
	 *
	 * This option is useful for memtx snapshots, which won't
	 * be reread soon and hence shouldn't stay cached in memory.
	 */
	bool free_cache;
	/**
	 * If this flag is set, xlog file will be synced in a coio
	 * thread on close.
	 *
	 * This option is useful for WAL files as it allows not to
	 * block writers when an xlog is rotated.
	 */
	bool sync_is_async;
	/**
	 * If this flag is set, the xlog writer won't use zstd
	 * compression.
	 *
	 * This option is useful for xlog files that are intended
	 * to be read frequently, e.g. L1 run files in Vinyl.
	 */
	bool no_compression;
};

extern const struct xlog_opts xlog_opts_default;

/* {{{ log dir */

/**
 * Type of log directory. A single filesystem directory can be
 * used for write ahead logs, memtx snapshots or vinyl run files,
 * but an xlog object sees only those files which match its type.
 */
enum xdir_type {
	SNAP,		/* memtx snapshot */
	XLOG,		/* write ahead log */
	VYLOG,		/* vinyl metadata log */
};

/**
 * Newly created snapshot files get .inprogress filename suffix.
 * The suffix is removed  when the file is finished
 * and closed.
 */
enum log_suffix { NONE, INPROGRESS };

/**
 * Suffix added to path of inprogress files.
 */
#define inprogress_suffix ".inprogress"

/**
 * A handle for a data directory with write ahead logs, snapshots,
 * vylogs.
 * Can be used to find the last log in the directory, scan
 * through all logs, create a new log.
 */
struct xdir {
	/** Xlog write options. */
	struct xlog_opts opts;
	/**
	 * Allow partial recovery from a damaged/incorrect
	 * data directory. Suppresses exceptions when scanning
	 * the directory, parsing file headers, or reading
	 * partial or corrupt rows. Incorrect objects
	 * are skipped.
	 */
	bool force_recovery;
	/* Default filename suffix for a new file. */
	enum log_suffix suffix;
	/**
	 * Additional flags to apply at open(2) to write.
	 */
	int open_wflags;
	/**
	 * A pointer to this instance uuid. If not assigned
	 * (tt_uuid_is_nil returns true), instance id check
	 * for logs in this directory is not performed.
	 * Otherwise, any log in this directory must have
	 * the matching instance id.
	 */
	const struct tt_uuid *instance_uuid;
	/**
	 * Text of a marker written to the text file header:
	 * XLOG (meaning it's a write ahead log) SNAP (a
	 * snapshot) or VYLOG.
	 */
	const char *filetype;
	/**
	 * File name extension (.xlog or .snap).
	 */
	const char *filename_ext;
	/** File create mode in this directory. */
	mode_t mode;
	/*
	 * Index of files present in the directory. Initially
	 * empty, must be initialized with xdir_scan().
	 */
	vclockset_t index;
	/**
	 * Directory path.
	 */
	char dirname[PATH_MAX];
	/** Snapshots or xlogs */
	enum xdir_type type;
};

/**
 * Initialize a log dir.
 */
void
xdir_create(struct xdir *dir, const char *dirname, enum xdir_type type,
	    const struct tt_uuid *instance_uuid, const struct xlog_opts *opts);

/**
 * Destroy a log dir object.
 */
void
xdir_destroy(struct xdir *dir);

/**
 * Scan or re-scan a directory and update directory
 * index with all log files (or snapshots) in the directory.
 * Must be used if it is necessary to find the last log/
 * snapshot or scan through all logs.
 * Function arguments described in xlog.c source file.
 */
int
xdir_scan(struct xdir *dir, bool is_dir_required);

/**
 * Check that a directory exists and is writable.
 */
int
xdir_check(struct xdir *dir);

/**
 * Return a file name based on directory type, vector clock
 * sum, and a suffix (.inprogress or not).
 */
const char *
xdir_format_filename(struct xdir *dir, int64_t signature,
		     enum log_suffix suffix);

/**
 * Return true if the given directory index has files whose
 * signature is less than specified.
 *
 * Supposed to be used to check if xdir_collect_garbage() can
 * actually delete some files.
 */
static inline bool
xdir_has_garbage(struct xdir *dir, int64_t signature)
{
	struct vclock *vclock = vclockset_first(&dir->index);
	return vclock != NULL && vclock_sum(vclock) < signature;
}

/**
 * Flags passed to xdir_collect_garbage().
 */
enum {
	/**
	 * Delete files in coio threads so as not to block
	 * the caller thread.
	 */
	XDIR_GC_ASYNC = 1 << 0,
	/**
	 * Return after removing a file.
	 */
	XDIR_GC_REMOVE_ONE = 1 << 1,
};

/**
 * Remove files whose signature is less than specified.
 * For possible values of @flags see XDIR_GC_*.
 */
void
xdir_collect_garbage(struct xdir *dir, int64_t signature, unsigned flags);

/**
 * Unlink single file with given vclock. If there's no file corresponding to
 * this vclock then log an error and return -1.
 */
int
xdir_remove_file_by_vclock(struct xdir *dir, struct vclock *vclock);

/**
 * Remove inprogress files in the specified directory.
 */
void
xdir_collect_inprogress(struct xdir *xdir);

/**
 * Return LSN and vclock (unless @vclock is NULL) of the oldest
 * file in a directory or -1 if the directory is empty.
 */
static inline int64_t
xdir_first_vclock(struct xdir *xdir, struct vclock *vclock)
{
	struct vclock *first = vclockset_first(&xdir->index);
	if (first == NULL)
		return -1;
	if (vclock != NULL)
		vclock_copy(vclock, first);
	return vclock_sum(first);
}

/**
 * Return LSN and vclock (unless @vclock is NULL) of the newest
 * file in a directory or -1 if the directory is empty.
 */
static inline int64_t
xdir_last_vclock(struct xdir *xdir, struct vclock *vclock)
{
	struct vclock *last = vclockset_last(&xdir->index);
	if (last == NULL)
		return -1;
	if (vclock != NULL)
		vclock_copy(vclock, last);
	return vclock_sum(last);
}

/**
 * Insert a vclock into the file index of a directory.
 */
void
xdir_add_vclock(struct xdir *xdir, const struct vclock *vclock);

/* }}} */

/* {{{ xlog meta */

/**
 * A xlog meta info
 */
struct xlog_meta {
	/** Text file header: filetype */
	char filetype[10];
	/**
	 * Text file header: instance uuid. We read
	 * only logs with our own uuid, to avoid situations
	 * when a DBA has manually moved a few logs around
	 * and messed the data directory up.
	 */
	struct tt_uuid instance_uuid;
	/**
	 * Text file header: vector clock taken at the time
	 * this file was created. For WALs, this is vector
	 * clock *at start of WAL*, for snapshots, this
	 * is vector clock *at the time the snapshot is taken*.
	 */
	struct vclock vclock;
	/**
	 * Text file header: vector clock of the previous
	 * file at the directory. Used for checking the
	 * directory for missing WALs.
	 */
	struct vclock prev_vclock;
};

/**
 * Initialize xlog meta struct.
 *
 * @vclock and @prev_vclock are optional: if the value is NULL,
 * the key won't be written to the xlog header.
 */
void
xlog_meta_create(struct xlog_meta *meta, const char *filetype,
		 const struct tt_uuid *instance_uuid,
		 const struct vclock *vclock,
		 const struct vclock *prev_vclock);

/* }}} */

/**
 * A single log file - a snapshot, a vylog or a write ahead log.
 */
struct xlog {
	/** Xlog write options. */
	struct xlog_opts opts;
	/** xlog meta header */
	struct xlog_meta meta;
	/** File handle. */
	int fd;
	/**
	 * How many xlog rows are in the file last time it
	 * was read or written. Updated in xlog_cursor_close()
	 * and is used to check whether or not we have discovered
	 * a new row in the file since it was last read. This is
	 * used in local hot standby to "follow up" on new rows
	 * appended to the file.
	 */
	int64_t rows; /* should have the same type as lsn */
	/**
	 * The number of rows in the current tx, part of
	 * tx state used only in write mode.
	 */
	int64_t tx_rows;
	/** Log file name. */
	char filename[PATH_MAX];
	/** Whether this file has .inprogress suffix. */
	bool is_inprogress;
	/*
	 * If true, we can flush the data in this buffer whenever
	 * we like, and it's usually when the buffer gets
	 * sufficiently big to get compressed.
	 *
	 * Otherwise, we must observe transactional boundaries
	 * to avoid writing a partial transaction to WAL: a
	 * single transaction always goes to WAL in a single
	 * "chunk" with 1 fixed header and common checksum
	 * for all transactional rows. This prevents miscarriage
	 * or partial delivery of transactional rows to a slave
	 * during replication.
	 */
	bool is_autocommit;
	/** The current offset in the log file, for writing. */
	off_t offset;
	/**
	 * Size of disk space preallocated at @offset with
	 * xlog_fallocate().
	 */
	size_t allocated;
	/**
	 * Output buffer, works as row accumulator for
	 * compression.
	 */
	struct obuf obuf;
	/** The context of zstd compression */
	ZSTD_CCtx *zctx;
	/**
	 * Compressed output buffer
	 */
	struct obuf zbuf;
	/**
	 * Synced file size
	 */
	uint64_t synced_size;
	/** Time when xlog wast synced last time */
	double sync_time;
};

/**
 * Touch xdir snapshot file.
 *
 * @param xdir xdir
 * @param vclock        the global state of replication (vector
 *			clock) at the moment the file is created.
 *
 * @retval 0 if OK
 * @retval -1 if error
 */
int
xdir_touch_xlog(struct xdir *dir, const struct vclock *vclock);

/**
 * Create a new file and open it in write (append) mode.
 * Note: an existing file is impossible to open for append,
 * the old files are never appended to.
 *
 * @param xdir xdir
 * @param[out] xlog xlog structure
 * @param instance uuid   the instance which created the file
 * @param vclock        the global state of replication (vector
 *			clock) at the moment the file is created.
 *
 * @retval 0 if OK
 * @retval -1 if error
 */
int
xdir_create_xlog(struct xdir *dir, struct xlog *xlog,
		 const struct vclock *vclock);

/**
 * Create new xlog writer based on fd.
 * @param fd            file descriptor
 * @param name          the assiciated name
 * @param flags		flags to open the file or 0 for defaults
 * @param meta          xlog meta
 * @param opts          write options
 *
 * @retval 0 for success
 * @retvl -1 if error
 */

int
xlog_create(struct xlog *xlog, const char *name, int flags,
	    const struct xlog_meta *meta, const struct xlog_opts *opts);

/**
 * Open an existing xlog file for appending.
 * @param xlog          xlog descriptor
 * @param name          file name
 * @param opts          write options
 *
 * @retval 0 success
 * @retval -1 error
 */
int
xlog_open(struct xlog *xlog, const char *name, const struct xlog_opts *opts);


/**
 * Reset an xlog object without opening it.
 * The object is in limbo state: it doesn't hold
 * any resources and doesn't need close, but
 * xlog_is_open() returns false.
 */
void
xlog_clear(struct xlog *xlog);


/** Returns true if the xlog file is open. */
static inline bool
xlog_is_open(struct xlog *l)
{
	return l->fd != -1;
}

/**
 * Rename xlog
 *
 * @retval 0 for ok
 * @retval -1 for error
 */
int
xlog_rename(struct xlog *l);

/**
 * Allocate @size bytes of disk space at the end of the given
 * xlog file.
 *
 * Returns -1 on fallocate error and sets both diag and errno
 * accordingly. On success returns 0. If the underlying OS
 * does not support fallocate, this function also returns 0.
 */
ssize_t
xlog_fallocate(struct xlog *log, size_t size);

/**
 * Write a row to xlog, 
 *
 * @retval count of writen bytes
 * @retval -1 for error
 */
ssize_t
xlog_write_row(struct xlog *log, const struct xrow_header *packet);

/**
 * Prevent xlog row buffer offloading, should be use
 * at transaction start to write transaction in one xlog tx
 */
void
xlog_tx_begin(struct xlog *log);

/**
 * Enable xlog row buffer offloading
 *
 * @retval count of writen bytes
 * @retval 0 if buffer is not writen
 * @retval -1 if error
 */
ssize_t
xlog_tx_commit(struct xlog *log);

/**
 * Discard xlog row buffer
 */
void
xlog_tx_rollback(struct xlog *log);

/**
 * Flush buffered rows and sync file
 */
ssize_t
xlog_flush(struct xlog *log);


/**
 * Sync a log file. The exact action is defined
 * by xdir flags.
 *
 * @retval 0 success
 * @retval -1 error
 */
int
xlog_sync(struct xlog *l);

/**
 * Close the log file and free xlog object.
 *
 * @retval 0 success
 * @retval -1 error (fclose() failed).
 */
int
xlog_close(struct xlog *l, bool reuse_fd);

/* {{{ xlog_tx_cursor - iterate over rows in xlog transaction */

/**
 * xlog tx iterator
 */
struct xlog_tx_cursor
{
	/** rows buffer */
	struct ibuf rows;
	/** tx size */
	size_t size;
};

/**
 * Create xlog tx iterator from memory data.
 * *data will be adjusted to end of tx
 *
 * @retval 0 for Ok
 * @retval -1 for error
 * @retval >0 how many additional bytes should be read to parse tx
 */
ssize_t
xlog_tx_cursor_create(struct xlog_tx_cursor *cursor,
		      const char **data, const char *data_end,
		      ZSTD_DStream *zdctx);

/**
 * Destroy xlog tx cursor and free all associated memory
 * including parsed xrows
 */
int
xlog_tx_cursor_destroy(struct xlog_tx_cursor *tx_cursor);

/**
 * Fetch next xrow from xlog tx cursor
 *
 * @retval 0 for Ok
 * @retval -1 for error
 */
int
xlog_tx_cursor_next_row(struct xlog_tx_cursor *tx_cursor, struct xrow_header *xrow);

/**
 * Return current tx cursor position
 *
 * @param tx_cursor tx_cursor
 * @retval current tx cursor position
 */
static inline off_t
xlog_tx_cursor_pos(struct xlog_tx_cursor *tx_cursor)
{
	return tx_cursor->size - ibuf_used(&tx_cursor->rows);
}

/**
 * A conventional helper to decode rows from the raw tx buffer.
 * Decodes fixheader, checks crc32 and length, decompresses rows.
 *
 * @param data a buffer with the raw tx data, including fixheader
 * @param data_end the end of @a data buffer
 * @param[out] rows a buffer to store decoded rows
 * @param[out] rows_end the end of @a rows buffer
 * @retval  0 success
 * @retval -1 error, check diag
 */
int
xlog_tx_decode(const char *data, const char *data_end,
	       char *rows, char *rows_end,
	       ZSTD_DStream *zdctx);

/* }}} */

/* {{{ xlog_cursor - read rows from a log file */

enum xlog_cursor_state {
	/* The cursor was never opened. */
	XLOG_CURSOR_NEW = 0,
	/* The cursor is open but no tx is read */
	XLOG_CURSOR_ACTIVE = 1,
	/* The Cursor is open and a tx is read */
	XLOG_CURSOR_TX = 2,
	/* The cursor is open but is at the end of file. */
	XLOG_CURSOR_EOF = 3,
	/* The cursor was closed after reaching EOF. */
	XLOG_CURSOR_EOF_CLOSED = 4,
	/* The cursor was closed before reaching EOF. */
	XLOG_CURSOR_CLOSED = 5,
};

/**
 * Xlog cursor, read rows from xlog
 */
struct xlog_cursor {
	/** cursor current state */
	enum xlog_cursor_state state;
	/** file descriptor or -1 for in memory */
	int fd;
	/** xlog meta info */
	struct xlog_meta meta;
	/** associated file name */
	char name[PATH_MAX];
	/** file read buffer */
	struct ibuf rbuf;
	/** file read position */
	off_t read_offset;
	/** cursor for current tx */
	struct xlog_tx_cursor tx_cursor;
	/** ZSTD context for decompression */
	ZSTD_DStream *zdctx;
};

/**
 * Return true if the cursor was opened and has not
 * been closed yet.
 */
static inline bool
xlog_cursor_is_open(const struct xlog_cursor *cursor)
{
	return (cursor->state != XLOG_CURSOR_NEW &&
		cursor->state != XLOG_CURSOR_CLOSED &&
		cursor->state != XLOG_CURSOR_EOF_CLOSED);
}

/**
 * Return true if the cursor has reached EOF.
 * The cursor may be closed or still open.
 */
static inline bool
xlog_cursor_is_eof(const struct xlog_cursor *cursor)
{
	return (cursor->state == XLOG_CURSOR_EOF ||
		cursor->state == XLOG_CURSOR_EOF_CLOSED);
}

/**
 * Open cursor from file descriptor
 * @param cursor cursor
 * @param fd file descriptor
 * @param name associated file name
 * @retval 0 succes
 * @retval -1 error, check diag
 */
int
xlog_cursor_openfd(struct xlog_cursor *cursor, int fd, const char *name);

/**
 * Open cursor from file
 * @param cursor cursor
 * @param name file name
 * @retval 0 succes
 * @retval -1 error, check diag
 */
int
xlog_cursor_open(struct xlog_cursor *cursor, const char *name);

/**
 * Open cursor from memory
 * @param cursor cursor
 * @param data pointer to memory block
 * @param size memory block size
 * @param name associated file name
 * @retval 0 succes
 * @retval -1 error, check diag
 */
int
xlog_cursor_openmem(struct xlog_cursor *cursor, const char *data, size_t size,
		    const char *name);

/**
 * Close cursor
 * @param cursor cursor
 */
void
xlog_cursor_close(struct xlog_cursor *cursor, bool reuse_fd);

/**
 * Open next tx from xlog
 * @param cursor cursor
 * @retval 0 succes
 * @retval 1 eof
 * retval -1 error, check diag
 */
int
xlog_cursor_next_tx(struct xlog_cursor *cursor);

/**
 * Fetch next xrow from current xlog tx
 *
 * @retval 0 for Ok
 * @retval 1 if current tx is done
 * @retval -1 for error
 */
int
xlog_cursor_next_row(struct xlog_cursor *cursor, struct xrow_header *xrow);

/**
 * Fetch next row from cursor, ignores xlog tx boundary,
 * open a next one tx if current is done.
 *
 * @retval 0 for Ok
 * @retval 1 for EOF
 * @retval -1 for error
 */
int
xlog_cursor_next(struct xlog_cursor *cursor,
		 struct xrow_header *xrow, bool force_recovery);

/**
 * Move to the next xlog tx
 *
 * @retval 0 magic found
 * @retval 1 magic not found and eof reached
 * @retval -1 error
 */
int
xlog_cursor_find_tx_magic(struct xlog_cursor *i);

/**
 * Cursor xlog position
 *
 * @param cursor xlog cursor
 * @retval xlog current position
 */
static inline off_t
xlog_cursor_pos(struct xlog_cursor *cursor)
{
	return cursor->read_offset - ibuf_used(&cursor->rbuf);
}

/**
 * Return tx positon for xlog cursor
 *
 * @param cursor xlog_cursor
 * @retval current tx postion
 */
static inline off_t
xlog_cursor_tx_pos(struct xlog_cursor *cursor)
{
	return xlog_tx_cursor_pos(&cursor->tx_cursor);
}
/* }}} */

/** {{{ miscellaneous log io functions. */

/**
 * Open cursor for xdir entry pointed by signature
 * @param xdir xdir
 * @param signature xlog signature
 * @param cursor cursor
 * @retval 0 succes
 * @retval -1 error, check diag
 */
int
xdir_open_cursor(struct xdir *dir, int64_t signature,
		 struct xlog_cursor *cursor);

/** }}} */

#if defined(__cplusplus)
} /* extern C */

#include "exception.h"

static inline void
xdir_scan_xc(struct xdir *dir, bool is_dir_required)
{
	if (xdir_scan(dir, is_dir_required) == -1)
		diag_raise();
}

static inline void
xdir_check_xc(struct xdir *dir)
{
	if (xdir_check(dir) == -1)
		diag_raise();
}

/**
 * @copydoc xdir_open_cursor
 */
static inline int
xdir_open_cursor_xc(struct xdir *dir, int64_t signature,
		    struct xlog_cursor *cursor)
{
	int rc = xdir_open_cursor(dir, signature, cursor);
	if (rc == -1)
		diag_raise();
	return rc;
}

/**
 * @copydoc xlog_cursor_openfd
 */
static inline int
xlog_cursor_openfd_xc(struct xlog_cursor *cursor, int fd, const char *name)
{
	int rc = xlog_cursor_openfd(cursor, fd, name);
	if (rc == -1)
		diag_raise();
	return rc;
}
/**
 * @copydoc xlog_cursor_open
 */
static inline int
xlog_cursor_open_xc(struct xlog_cursor *cursor, const char *name)
{
	int rc = xlog_cursor_open(cursor, name);
	if (rc == -1)
		diag_raise();
	return rc;
}

/**
 * @copydoc xlog_cursor_next
 */
static inline int
xlog_cursor_next_xc(struct xlog_cursor *cursor,
		    struct xrow_header *xrow, bool force_recovery)
{
	int rc = xlog_cursor_next(cursor, xrow, force_recovery);
	if (rc == -1)
		diag_raise();
	return rc;
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_XLOG_H_INCLUDED */
