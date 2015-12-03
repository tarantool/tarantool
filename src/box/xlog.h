#ifndef TARANTOOL_XLOG_H_INCLUDED
#define TARANTOOL_XLOG_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "tt_uuid.h"
#include "vclock.h"

#if defined(__cplusplus)
/**
 * XlogError is raised when there is an error with contents
 * of the data directory or a log file. A special subclass
 * of exception is introduced to gracefully skip such errors
 * in panic_if_error = false mode.
 */
struct XlogError: public Exception
{
	XlogError(const char *file, unsigned line,
		  const char *format, ...);
	virtual void raise() { throw this; }
protected:
	XlogError(const struct type *type, const char *file, unsigned line,
		  const char *format, ...);
};

struct XlogGapError: public XlogError
{
	XlogGapError(const char *file, unsigned line,
		  const struct vclock *from,
		  const struct vclock *to);
	virtual void raise() { throw this; }
};

extern "C" {
#endif /* defined(__cplusplus) */

/* {{{ log dir */

/**
 * Type of log directory. A single filesystem directory can be
 * used for logs and snapshots, but an xlog object sees only
 * those files which match its type.
 */
enum xdir_type { SNAP, XLOG };

/**
 * Newly created snapshot files get .inprogress filename suffix.
 * The suffix is removed  when the file is finished
 * and closed.
 */
enum log_suffix { NONE, INPROGRESS };

/**
 * A handle for a data directory with write ahead logs or snapshots.
 * Can be used to find the last log in the directory, scan
 * through all logs, create a new log.
 */
struct xdir {
	/**
	 * Allow partial recovery from a damaged/incorrect
	 * data directory. Suppresses exceptions when scanning
	 * the directory, parsing file headers, or reading
	 * partial or corrupt rows. Incorrect objects
	 * are skipped.
	 */
	bool panic_if_error;

	/**
	 * true if a log file in this directory can by fsync()ed
	 * at close in a separate thread (we use this technique to
	 * speed up sync of write ahead logs, but not snapshots).
	 */
	bool sync_is_async;

	/* Default filename suffix for a new file. */
	enum log_suffix suffix;
	/**
	 * Additional flags to apply at fopen(2) to write.
	 * A write ahead log opened with write mode can use
	 * O_DIRECT flag, for example.
	 */
	char open_wflags[6];
	/**
	 * A pointer to this server uuid. If not assigned
	 * (tt_uuid_is_nil returns true), server id check
	 * for logs in this directory is not performed.
	 * Otherwise, any log in this directory must have
	 * the matching server id.
	 */
	const struct tt_uuid *server_uuid;
	/**
	 * Text of a marker written to the text file header:
	 * XLOG (meaning it's a write ahead log) or SNAP (a
	 * snapshot).
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
	char dirname[PATH_MAX+1];
	/** Snapshots or xlogs */
	enum xdir_type type;
};

#if defined(__cplusplus)
} /* extern "C" */

/**
 * Initialize a log dir.
 */
void
xdir_create(struct xdir *dir, const char *dirname, enum xdir_type type,
	    const struct tt_uuid *server_uuid);

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
 */
void
xdir_scan(struct xdir *dir);

/**
 * Check that a directory exists and is writable.
 */
void
xdir_check(struct xdir *dir);

extern "C" {
#endif /* defined(__cplusplus) */

/* }}} */

/**
 * Basic open mode for a log file: read or write.
 */
enum log_mode { LOG_READ, LOG_WRITE };

/**
 * A single log file - a snapshot or a write ahead log.
 */
struct xlog {
	/** The directory this file is in. */
	struct xdir *dir;
	/** File handle. */
	FILE *f;
	/** Mode in which this file has been opened: read or write. */
	enum log_mode mode;
	/**
	 * How many xlog rows are in the file last time it
	 * was read or written. Updated in xlog_cursor_close()
	 * and is used to check whether or not we have discovered
	 * a new row in the file since it was last read. This is
	 * used in local hot standby to "follow up" on new rows
	 * appended to the file.
	 */
	size_t rows;
	/** Log file name. */
	char filename[PATH_MAX + 1];
	/** Whether this file has .inprogress suffix. */
	bool is_inprogress;
	/** True if eof has been read when reading the log. */
	bool eof_read;
	/**
	 * Text file header: server uuid. We read
	 * only logs with our own uuid, to avoid situations
	 * when a DBA has manually moved a few logs around
	 * and messed the data directory up.
	 */
	struct tt_uuid server_uuid;
	/**
	 * Text file header: vector clock taken at the time
	 * this file was created. For WALs, this is vector
	 * clock *at start of WAL*, for snapshots, this
	 * is vector clock *at the time the snapshot is taken*.
	 */
	struct vclock vclock;
};

#if defined(__cplusplus)
} /* extern "C" */

/**
 * Open an existing log file or snapshot for reading.
 *
 * @param  dir           the log directory to look for a file
 * @param  signature     file name
 *
 * The caller must free the returned structure with
 * xlog_close().
 * Raises an exception in case of error.
 */
struct xlog *
xlog_open(struct xdir *dir, int64_t signature);

/**
 * Open an xlog from a pre-created stdio stream.
 * The log is open for reading.
 * The file is closed on error, even if open fails.
 *
 * The caller must free the created xlog object with
 * xlog_close().
 *
 * Throws an exception in case of error.
 */

struct xlog *
xlog_open_stream(struct xdir *dir, int64_t signature,
		 FILE *file, const char *filename);

/**
 * Create a new file and open it in write (append) mode.
 * Note: an existing file is impossible to open for append,
 * the old files are never appended to.
 *
 * @param server uuid   the server which created the file
 * @param vclock        the global state of replication (vector
 *			clock) at the moment the file is created.
 *
 * @return  xlog object or NULL in case of error.
 */
struct xlog *
xlog_create(struct xdir *dir, const struct vclock *vclock);

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
xlog_close(struct xlog *l);

/**
 * atfork() handler function to close the log pointed
 * at by *lptr in the child.
 */
void
xlog_atfork(struct xlog **lptr);

/* {{{ xlog_cursor - read rows from a log file */

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

/* }}} */

/** {{{ miscellaneous log io functions. */

/**
 * Return a file name based on directory type, vector clock
 * sum, and a suffix (.inprogress or not).
 */
char *
format_filename(struct xdir *dir, int64_t signature, enum log_suffix suffix);

/**
 * Construct a row to write to the log file.
 */
int
xlog_encode_row(const struct xrow_header *packet, struct iovec *iov);

/** }}} */

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_XLOG_H_INCLUDED */
