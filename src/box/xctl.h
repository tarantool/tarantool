#ifndef INCLUDES_TARANTOOL_BOX_XCTL_H
#define INCLUDES_TARANTOOL_BOX_XCTL_H
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Data stored in vinyl is organized in ranges and runs.
 * Runs correspond to data files written to disk, while
 * ranges are used to group runs together. Sometimes, we
 * need to manipulate several ranges or runs atomically,
 * e.g. on compaction several runs are replaced with a
 * single one. To reflect events like this on disk and be
 * able to recover to a consistent state after restart, we
 * need to log all metadata changes. This module implements
 * the infrastructure necessary for such logging as well
 * as recovery.
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Xlog type of a metadata log file. */
#define XCTL_TYPE			"XCTL"

/** Type of a metadata log record. */
enum xctl_record_type {
	/**
	 * Create a new vinyl index.
	 * Requires xctl_record::vy_index_id, iid, space_id,
	 * path, path_len.
	 */
	XCTL_CREATE_VY_INDEX		= 0,
	/**
	 * Drop an index.
	 * Requires xctl_record::vy_index_id.
	 */
	XCTL_DROP_VY_INDEX		= 1,
	/**
	 * Insert a new range into a vinyl index.
	 * Requires xctl_record::vy_index_id, vy_range_id,
	 * vy_range_begin, vy_range_end.
	 */
	XCTL_INSERT_VY_RANGE		= 2,
	/**
	 * Delete a vinyl range and all its runs.
	 * Requires xctl_record::vy_range_id.
	 */
	XCTL_DELETE_VY_RANGE		= 3,
	/**
	 * Prepare a vinyl run file.
	 * Requires xctl_record::vy_index_id, vy_run_id.
	 *
	 * Record of this type is written before creating a run file.
	 * It is needed to keep track of unfinished due to errors run
	 * files so that we could remove them after recovery.
	 */
	XCTL_PREPARE_VY_RUN		= 4,
	/**
	 * Insert a run into a vinyl range.
	 * Requires xctl_record::vy_range_id, vy_run_id.
	 */
	XCTL_INSERT_VY_RUN		= 5,
	/**
	 * Delete a vinyl run.
	 * Requires xctl_record::vy_run_id.
	 *
	 * A record of this type indicates that the run is not in use
	 * any more and its files can be safely removed. When the log
	 * is recovered from, this only marks the run as deleted,
	 * because we still need it for garbage collection. A run is
	 * actually freed by XCTL_FORGET_VY_RUN. Runs that were
	 * deleted, but not "forgotten" are not expunged from the log
	 * on rotation.
	 */
	XCTL_DELETE_VY_RUN		= 6,
	/**
	 * Forget a vinyl run.
	 * Requires xctl_record::vy_run_id.
	 *
	 * A record of this type is written after all files left from
	 * an unused run have been successfully removed. On recovery,
	 * this results in freeing all structures associated with the
	 * run. Information about "forgotten" runs is not included in
	 * the new log on rotation.
	 */
	XCTL_FORGET_VY_RUN		= 7,

	xctl_record_type_MAX
};

/** Record in the metadata log. */
struct xctl_record {
	/** Type of the record. */
	enum xctl_record_type type;
	/**
	 * The log signature from the time when the record was
	 * written. Set by xctl_write().
	 */
	int64_t signature;
	/**
	 * Unique ID of the vinyl index.
	 *
	 * The ID must be unique for different incarnations of
	 * the same index, so we use LSN from the time of index
	 * creation for it.
	 */
	int64_t vy_index_id;
	/** Unique ID of the vinyl range. */
	int64_t vy_range_id;
	/** Unique ID of the vinyl run. */
	int64_t vy_run_id;
	/** Msgpack key for start of the vinyl range. */
	const char *vy_range_begin;
	/** Msgpack key for end of the vinyl range. */
	const char *vy_range_end;
	/** Ordinal index number in the space. */
	uint32_t iid;
	/** Space ID. */
	uint32_t space_id;
	/**
	 * Path to the index. Empty string if default path is used.
	 * Note, the string is not necessarily nul-termintaed, its
	 * length is stored in path_len.
	 */
	const char *path;
	/** Length of the path string. */
	uint32_t path_len;
};

/**
 * Initialize the metadata log.
 */
void
xctl_init(void);

/**
 * Destroy the metadata log.
 */
void
xctl_free(void);

/**
 * Return the path to the current xctl log file.
 */
const char *
xctl_path(void);

/**
 * Rotate the metadata log. This function creates a new
 * xlog file in the log directory having signature @signature
 * and writes records required to recover active indexes.
 * The goal of log rotation is to compact the log file by
 * discarding records cancelling each other and records left
 * from dropped indexes.
 *
 * Returns 0 on success, -1 on failure.
 */
int
xctl_rotate(int64_t signature);

/**
 * Remove files left from objects deleted before the log
 * received signature @signature.
 */
void
xctl_collect_garbage(int64_t signature);

/** Allocate a unique ID for a vinyl run. */
int64_t
xctl_next_vy_run_id(void);

/** Allocate a unique ID for a vinyl range. */
int64_t
xctl_next_vy_range_id(void);

/**
 * Begin a transaction in the metadata log.
 *
 * To commit the transaction, call xctl_tx_commit() or
 * xctl_tx_try_commit().
 */
void
xctl_tx_begin(void);

/**
 * Commit a transaction started with xctl_tx_begin().
 *
 * This function flushes all buffered records to disk. If it fails,
 * all records of the current transaction are discarded.
 *
 * See also xctl_tx_try_commit().
 *
 * Returns 0 on success, -1 on failure.
 */
int
xctl_tx_commit(void);

/**
 * Try to commit a transaction started with xctl_tx_begin().
 *
 * Similarly to xctl_tx_commit(), this function tries to write all
 * buffered records to disk, but in case of failure pending records
 * are not expunged from the buffer, so that the next transaction
 * will retry to flush them.
 *
 * Returns 0 on success, -1 on failure.
 */
int
xctl_tx_try_commit(void);

/**
 * Write a record to the metadata log.
 *
 * This function simply appends the record to the internal buffer.
 * It must be called inside a xctl_tx_begin/commit block, and it
 * is up to xctl_tx_commit() to actually write the record to disk.
 *
 * Returns 0 on success, -1 on failure.
 */
void
xctl_write(const struct xctl_record *record);

/**
 * Prepare the metadata log for recovery from the file having
 * signature @signature.
 *
 * After this function is called, vinyl indexes may be recovered from
 * the log using xctl_recover_vy_index(). When recovery is complete,
 * one must call xctl_end_recovery().
 *
 * Returns 0 on success, -1 on failure.
 */
int
xctl_begin_recovery(int64_t signature);

/**
 * Finish recovery from the metadata log.
 *
 * This function destroys the recovery context that was created by
 * xctl_begin_recovery(), opens the log file for appending, and
 * flushes all records written to the log buffer during recovery.
 *
 * Return 0 on success, -1 on failure.
 */
int
xctl_end_recovery(void);

typedef int
(*xctl_recovery_cb)(const struct xctl_record *record, void *arg);

/**
 * Recover a vinyl index having ID @index_id from the metadata log.
 * The log must be in recovery mode, see xctl_begin_recovery().
 *
 * For each range and run of the index, this function calls @cb passing
 * a log record and an optional @cb_arg to it. A log record type is
 * either XCTL_CREATE_VY_INDEX, XCTL_INSERT_VY_RANGE, or XCTL_INSERT_VY_RUN
 * unless the index was dropped. In the latter case, a XCTL_DROP_VY_INDEX
 * record is issued in the end.
 * The callback is supposed to rebuild the index structure and open run
 * files. If the callback returns a non-zero value, the function stops
 * iteration over ranges and runs and returns error.
 * To ease the work done by the callback, records corresponding to
 * runs of a range always go right after the range, in the
 * chronological order.
 *
 * Returns 0 on success, -1 on failure.
 */
int
xctl_recover_vy_index(int64_t vy_index_id,
		      xctl_recovery_cb cb, void *cb_arg);

/** Helper to log a vinyl index creation. */
static inline void
xctl_create_vy_index(int64_t vy_index_id, uint32_t iid,
		     uint32_t space_id, const char *path)
{
	struct xctl_record record;
	record.type = XCTL_CREATE_VY_INDEX;
	record.signature = -1;
	record.vy_index_id = vy_index_id;
	record.iid = iid;
	record.space_id = space_id;
	record.path = path;
	record.path_len = strlen(path);
	xctl_write(&record);
}

/** Helper to log a vinyl index drop. */
static inline void
xctl_drop_vy_index(int64_t vy_index_id)
{
	struct xctl_record record;
	record.type = XCTL_DROP_VY_INDEX;
	record.signature = -1;
	record.vy_index_id = vy_index_id;
	xctl_write(&record);
}

/** Helper to log a vinyl range insertion. */
static inline void
xctl_insert_vy_range(int64_t vy_index_id, int64_t vy_range_id,
		     const char *vy_range_begin, const char *vy_range_end)
{
	struct xctl_record record;
	record.type = XCTL_INSERT_VY_RANGE;
	record.signature = -1;
	record.vy_index_id = vy_index_id;
	record.vy_range_id = vy_range_id;
	record.vy_range_begin = vy_range_begin;
	record.vy_range_end = vy_range_end;
	xctl_write(&record);
}

/** Helper to log a vinyl range deletion. */
static inline void
xctl_delete_vy_range(int64_t vy_range_id)
{
	struct xctl_record record;
	record.type = XCTL_DELETE_VY_RANGE;
	record.signature = -1;
	record.vy_range_id = vy_range_id;
	xctl_write(&record);
}

/** Helper to log a vinyl run file creation. */
static inline void
xctl_prepare_vy_run(int64_t vy_index_id, int64_t vy_run_id)
{
	struct xctl_record record;
	record.type = XCTL_PREPARE_VY_RUN;
	record.signature = -1;
	record.vy_index_id = vy_index_id;
	record.vy_run_id = vy_run_id;
	xctl_write(&record);
}

/** Helper to log a vinyl run insertion. */
static inline void
xctl_insert_vy_run(int64_t vy_range_id, int64_t vy_run_id)
{
	struct xctl_record record;
	record.type = XCTL_INSERT_VY_RUN;
	record.signature = -1;
	record.vy_range_id = vy_range_id;
	record.vy_run_id = vy_run_id;
	xctl_write(&record);
}

/** Helper to log a run deletion. */
static inline void
xctl_delete_vy_run(int64_t vy_run_id)
{
	struct xctl_record record;
	record.type = XCTL_DELETE_VY_RUN;
	record.signature = -1;
	record.vy_run_id = vy_run_id;
	xctl_write(&record);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_XCTL_H */
