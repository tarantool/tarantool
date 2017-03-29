#ifndef INCLUDES_TARANTOOL_BOX_VY_LOG_H
#define INCLUDES_TARANTOOL_BOX_VY_LOG_H
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

struct xlog;
struct vclock;

/** Type of a metadata log record. */
enum vy_log_record_type {
	/**
	 * Create a new vinyl index.
	 * Requires vy_log_record::index_id, iid, space_id,
	 * path, path_len.
	 */
	VY_LOG_CREATE_INDEX		= 0,
	/**
	 * Drop an index.
	 * Requires vy_log_record::index_id.
	 */
	VY_LOG_DROP_INDEX		= 1,
	/**
	 * Insert a new range into a vinyl index.
	 * Requires vy_log_record::index_id, range_id,
	 * range_begin, range_end.
	 */
	VY_LOG_INSERT_RANGE		= 2,
	/**
	 * Delete a vinyl range and all its runs.
	 * Requires vy_log_record::range_id.
	 */
	VY_LOG_DELETE_RANGE		= 3,
	/**
	 * Prepare a vinyl run file.
	 * Requires vy_log_record::index_id, run_id.
	 *
	 * Record of this type is written before creating a run file.
	 * It is needed to keep track of unfinished due to errors run
	 * files so that we could remove them after recovery.
	 */
	VY_LOG_PREPARE_RUN		= 4,
	/**
	 * Insert a run into a vinyl range.
	 * Requires vy_log_record::range_id, run_id.
	 */
	VY_LOG_INSERT_RUN		= 5,
	/**
	 * Delete a vinyl run.
	 * Requires vy_log_record::run_id.
	 *
	 * A record of this type indicates that the run is not in use
	 * any more and its files can be safely removed. When the log
	 * is recovered from, this only marks the run as deleted,
	 * because we still need it for garbage collection. A run is
	 * actually freed by VY_LOG_FORGET_RUN. Runs that were
	 * deleted, but not "forgotten" are not expunged from the log
	 * on rotation.
	 */
	VY_LOG_DELETE_RUN		= 6,
	/**
	 * Forget a vinyl run.
	 * Requires vy_log_record::run_id.
	 *
	 * A record of this type is written after all files left from
	 * an unused run have been successfully removed. On recovery,
	 * this results in freeing all structures associated with the
	 * run. Information about "forgotten" runs is not included in
	 * the new log on rotation.
	 */
	VY_LOG_FORGET_RUN		= 7,

	vy_log_record_type_MAX
};

/** Record in the metadata log. */
struct vy_log_record {
	/** Type of the record. */
	enum vy_log_record_type type;
	/**
	 * The log signature from the time when the record was
	 * written. Set by vy_log_write().
	 */
	int64_t signature;
	/**
	 * Unique ID of the vinyl index.
	 *
	 * The ID must be unique for different incarnations of
	 * the same index, so we use LSN from the time of index
	 * creation for it.
	 */
	int64_t index_id;
	/** Unique ID of the vinyl range. */
	int64_t range_id;
	/** Unique ID of the vinyl run. */
	int64_t run_id;
	/** Msgpack key for start of the vinyl range. */
	const char *range_begin;
	/** Msgpack key for end of the vinyl range. */
	const char *range_end;
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
vy_log_init(void);

/**
 * Destroy the metadata log.
 */
void
vy_log_free(void);

/**
 * Open current vy_log file.
 */
int
vy_log_open(struct xlog *xlog);

/**
 * Rotate the metadata log. This function creates a new
 * xlog file in the log directory having vclock @vclock
 * and writes records required to recover active indexes.
 * The goal of log rotation is to compact the log file by
 * discarding records cancelling each other and records left
 * from dropped indexes.
 *
 * Returns 0 on success, -1 on failure.
 */
int
vy_log_rotate(const struct vclock *vclock);

/**
 * Remove files left from objects deleted before the log
 * received signature @signature.
 */
void
vy_log_collect_garbage(int64_t signature);

/** Allocate a unique ID for a vinyl run. */
int64_t
vy_log_next_run_id(void);

/** Allocate a unique ID for a vinyl range. */
int64_t
vy_log_next_range_id(void);

/**
 * Begin a transaction in the metadata log.
 *
 * To commit the transaction, call vy_log_tx_commit() or
 * vy_log_tx_try_commit().
 */
void
vy_log_tx_begin(void);

/**
 * Commit a transaction started with vy_log_tx_begin().
 *
 * This function flushes all buffered records to disk. If it fails,
 * all records of the current transaction are discarded.
 *
 * See also vy_log_tx_try_commit().
 *
 * Returns 0 on success, -1 on failure.
 */
int
vy_log_tx_commit(void);

/**
 * Try to commit a transaction started with vy_log_tx_begin().
 *
 * Similarly to vy_log_tx_commit(), this function tries to write all
 * buffered records to disk, but in case of failure pending records
 * are not expunged from the buffer, so that the next transaction
 * will retry to flush them.
 *
 * Returns 0 on success, -1 on failure.
 */
int
vy_log_tx_try_commit(void);

/**
 * Write a record to the metadata log.
 *
 * This function simply appends the record to the internal buffer.
 * It must be called inside a vy_log_tx_begin/commit block, and it
 * is up to vy_log_tx_commit() to actually write the record to disk.
 *
 * Returns 0 on success, -1 on failure.
 */
void
vy_log_write(const struct vy_log_record *record);

/**
 * Bootstrap vy_log.
 */
int
vy_log_bootstrap(void);

/**
 * Prepare the metadata log for recovery from the file having
 * vclock @vclock.
 *
 * After this function is called, vinyl indexes may be recovered from
 * the log using vy_log_recover_index(). When recovery is complete,
 * one must call vy_log_end_recovery().
 *
 * Returns 0 on success, -1 on failure.
 */
int
vy_log_begin_recovery(const struct vclock *vclock);

/**
 * Finish recovery from the metadata log.
 *
 * This function destroys the recovery context that was created by
 * vy_log_begin_recovery(), opens the log file for appending, and
 * flushes all records written to the log buffer during recovery.
 *
 * Return 0 on success, -1 on failure.
 */
int
vy_log_end_recovery(void);

typedef int
(*vy_recovery_cb)(const struct vy_log_record *record, void *arg);

/**
 * Recover a vinyl index having ID @index_id from the metadata log.
 * The log must be in recovery mode, see vy_log_begin_recovery().
 *
 * For each range and run of the index, this function calls @cb passing
 * a log record and an optional @cb_arg to it. A log record type is
 * either VY_LOG_CREATE_INDEX, VY_LOG_INSERT_RANGE, or VY_LOG_INSERT_RUN
 * unless the index was dropped. In the latter case, a VY_LOG_DROP_INDEX
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
vy_log_recover_index(int64_t index_id,
		     vy_recovery_cb cb, void *cb_arg);

/**
 * Call @cb for each active object stored in the most recent
 * snapshot of the metadata log. Vinyl objects are iterated in
 * the same order as the one used by vy_log_recover_index().
 *
 * Returns 0 on success, -1 on failure.
 */
int
vy_log_relay(vy_recovery_cb cb, void *cb_arg);

typedef int
(*vy_log_backup_cb)(const char *filename, void *arg);

/**
 * Call @cb for each file that needs to be backed up in
 * order to recover from the latest checkpoint.
 */
int
vy_log_backup(vy_log_backup_cb cb, void *cb_arg);

/** Helper to log a vinyl index creation. */
static inline void
vy_log_create_index(int64_t index_id, uint32_t iid,
		    uint32_t space_id, const char *path)
{
	struct vy_log_record record;
	record.type = VY_LOG_CREATE_INDEX;
	record.signature = -1;
	record.index_id = index_id;
	record.iid = iid;
	record.space_id = space_id;
	record.path = path;
	record.path_len = strlen(path);
	vy_log_write(&record);
}

/** Helper to log a vinyl index drop. */
static inline void
vy_log_drop_index(int64_t index_id)
{
	struct vy_log_record record;
	record.type = VY_LOG_DROP_INDEX;
	record.signature = -1;
	record.index_id = index_id;
	vy_log_write(&record);
}

/** Helper to log a vinyl range insertion. */
static inline void
vy_log_insert_range(int64_t index_id, int64_t range_id,
		    const char *range_begin, const char *range_end)
{
	struct vy_log_record record;
	record.type = VY_LOG_INSERT_RANGE;
	record.signature = -1;
	record.index_id = index_id;
	record.range_id = range_id;
	record.range_begin = range_begin;
	record.range_end = range_end;
	vy_log_write(&record);
}

/** Helper to log a vinyl range deletion. */
static inline void
vy_log_delete_range(int64_t range_id)
{
	struct vy_log_record record;
	record.type = VY_LOG_DELETE_RANGE;
	record.signature = -1;
	record.range_id = range_id;
	vy_log_write(&record);
}

/** Helper to log a vinyl run file creation. */
static inline void
vy_log_prepare_run(int64_t index_id, int64_t run_id)
{
	struct vy_log_record record;
	record.type = VY_LOG_PREPARE_RUN;
	record.signature = -1;
	record.index_id = index_id;
	record.run_id = run_id;
	vy_log_write(&record);
}

/** Helper to log a vinyl run insertion. */
static inline void
vy_log_insert_run(int64_t range_id, int64_t run_id)
{
	struct vy_log_record record;
	record.type = VY_LOG_INSERT_RUN;
	record.signature = -1;
	record.range_id = range_id;
	record.run_id = run_id;
	vy_log_write(&record);
}

/** Helper to log a run deletion. */
static inline void
vy_log_delete_run(int64_t run_id)
{
	struct vy_log_record record;
	record.type = VY_LOG_DELETE_RUN;
	record.signature = -1;
	record.run_id = run_id;
	vy_log_write(&record);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_LOG_H */
