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

#include <stdint.h>

/*
 * Data stored in vinyl is organized in ranges and runs.
 * Runs correspond to data files written to disk, while
 * ranges are used to group runs together. Sometimes, we
 * need to manipulate several ranges or runs atomically,
 * e.g. on compaction several runs are replaced with a
 * single one. To reflect events like this on disk and be
 * able to recover to a consistent state after restart, we
 * need to log all metadata changes. This module implements
 * the infrastructure necesary for such logging as well
 * as recovery.
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct mh_i64ptr_t;

/** Type of a vinyl metadata log record. */
enum vy_log_type {
	/**
	 * Create a new index.
	 * Requires vy_log_record::index_id.
	 */
	VY_LOG_NEW_INDEX		= 0,
	/**
	 * Insert a new range into an index.
	 * Requires vy_log_record::index_id, range_id,
	 * range_begin, range_end.
	 */
	VY_LOG_INSERT_RANGE		= 1,
	/**
	 * Delete a range and all its runs.
	 * Requires vy_log_record::range_id.
	 */
	VY_LOG_DELETE_RANGE		= 2,
	/**
	 * Insert a new run into an index.
	 * Requires vy_log_record::range_id, run_id.
	 */
	VY_LOG_INSERT_RUN		= 3,
	/**
	 * Delete a run.
	 * Requires vy_log_record::run_id.
	 */
	VY_LOG_DELETE_RUN		= 4,

	vy_log_MAX
};

/** Record in vinyl metadata log. */
struct vy_log_record {
	/** Type of the record. */
	enum vy_log_type type;
	/**
	 * Unique ID of index.
	 *
	 * The ID must be unique for different incarnations of
	 * the same index, so we use LSN from the time of index
	 * creation for it.
	 */
	int64_t index_id;
	/** Unique ID of range. */
	int64_t range_id;
	/** Unique ID of run. */
	int64_t run_id;
	/** Start of range. */
	const char *range_begin;
	/** End of range. */
	const char *range_end;
};

/* Opaque to reduce dependencies. */
struct vy_log;

/** Recovery context. */
struct vy_recovery {
	/** ID -> vy_index_recovery_info. */
	struct mh_i64ptr_t *index_hash;
	/** ID -> vy_range_recovery_info. */
	struct mh_i64ptr_t *range_hash;
	/** ID -> vy_run_recovery_info. */
	struct mh_i64ptr_t *run_hash;
	/** Maximal range ID. */
	int64_t range_id_max;
	/** Maximal run ID. */
	int64_t run_id_max;
};

/**
 * Open the vinyl metadata log file stored in a given directory
 * for appending, or create a new one if it doesn't exist.
 *
 * Returns NULL on failure.
 */
struct vy_log *
vy_log_new(const char *dir);

/**
 * Close a metadata log and free associated structures.
 */
void
vy_log_delete(struct vy_log *log);

/**
 * Begin a transaction in a metadata log.
 *
 * To commit the transaction, call vy_log_tx_commit(),
 * for rollback call vy_log_tx_rollback().
 */
void
vy_log_tx_begin(struct vy_log *log);

/**
 * Commit a transaction started with vy_log_tx_begin().
 *
 * Returns 0 on success, -1 on failure.
 */
int
vy_log_tx_commit(struct vy_log *log);

/**
 * Rollback a transaction started with vy_log_tx_begin().
 */
void
vy_log_tx_rollback(struct vy_log *log);

/**
 * Write a record to a metadata log.
 *
 * This function may be called outside a vy_log_tx_begin/end block,
 * in which case the write occurs in its own transaction.
 *
 * Returns 0 on success, -1 on failure.
 */
int
vy_log_write(struct vy_log *log, const struct vy_log_record *record);

/**
 * Load the vinyl metadata log from a given directory and return
 * the recovery context that can be further used for recovering
 * vinyl indexes with vy_recovery_load_index().
 *
 * Returns NULL on failure.
 */
struct vy_recovery *
vy_recovery_new(const char *dir);

/**
 * Free the recovery context created by a call to vy_recovery_new().
 */
void
vy_recovery_delete(struct vy_recovery *recovery);

typedef int
(*vy_recovery_cb)(const struct vy_log_record *record, void *arg);

/**
 * Given a context and index ID, recover the corresponding vinyl index.
 *
 * For each range and run of the index, this function calls @cb passing
 * a log record and an optional @cb_arg to it. A log record type is
 * either VY_LOG_INSERT_RANGE or VY_LOG_INSERT_RUN. The callback is
 * supposed to rebuild the index structure and open run files. If the
 * callback returns a non-zero value, the function fails. To ease the
 * work done by the callback, records corresponding to runs of a range
 * always go right after the range in the chronological order.
 *
 * Returns 0 on success, -1 on failure.
 */
int
vy_recovery_load_index(struct vy_recovery *recovery, int64_t index_id,
		       vy_recovery_cb cb, void *cb_arg);

/** Helper to log an index creation. */
static inline int
vy_log_new_index(struct vy_log *log, int64_t index_id)
{
	struct vy_log_record record = {
		.type = VY_LOG_NEW_INDEX,
		.index_id = index_id,
	};
	return vy_log_write(log, &record);
}

/** Helper to log a range insertion. */
static inline int
vy_log_insert_range(struct vy_log *log, int64_t index_id, int64_t range_id,
		    const char *range_begin, const char *range_end)
{
	struct vy_log_record record = {
		.type = VY_LOG_INSERT_RANGE,
		.index_id = index_id,
		.range_id = range_id,
		.range_begin = range_begin,
		.range_end = range_end,
	};
	return vy_log_write(log, &record);
}

/** Helper to log a range deletion. */
static inline int
vy_log_delete_range(struct vy_log *log, int64_t range_id)
{
	struct vy_log_record record = {
		.type = VY_LOG_DELETE_RANGE,
		.range_id = range_id,
	};
	return vy_log_write(log, &record);
}

/** Helper to log a run insertion. */
static inline int
vy_log_insert_run(struct vy_log *log, int64_t range_id, int64_t run_id)
{
	struct vy_log_record record = {
		.type = VY_LOG_INSERT_RUN,
		.range_id = range_id,
		.run_id = run_id,
	};
	return vy_log_write(log, &record);
}

/** Helper to log a run deletion. */
static inline int
vy_log_delete_run(struct vy_log *log, int64_t run_id)
{
	struct vy_log_record record = {
		.type = VY_LOG_DELETE_RUN,
		.run_id = run_id,
	};
	return vy_log_write(log, &record);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_LOG_H */
