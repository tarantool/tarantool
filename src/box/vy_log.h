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
#include <small/rlist.h>

#include "salad/stailq.h"

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
struct key_def;
struct key_part_def;
struct mh_i64ptr_t;

/** Type of a metadata log record. */
enum vy_log_record_type {
	/**
	 * Create a new LSM tree.
	 * Requires vy_log_record::lsm_id, index_id, space_id,
	 * key_def, create_lsn.
	 */
	VY_LOG_CREATE_LSM		= 0,
	/**
	 * Drop an LSM tree.
	 * Requires vy_log_record::lsm_id.
	 */
	VY_LOG_DROP_LSM			= 1,
	/**
	 * Insert a new range into an LSM tree.
	 * Requires vy_log_record::lsm_id, range_id, begin, end.
	 */
	VY_LOG_INSERT_RANGE		= 2,
	/**
	 * Delete a vinyl range and all its runs.
	 * Requires vy_log_record::range_id.
	 */
	VY_LOG_DELETE_RANGE		= 3,
	/**
	 * Prepare a vinyl run file.
	 * Requires vy_log_record::lsm_id, run_id.
	 *
	 * Record of this type is written before creating a run file.
	 * It is needed to keep track of unfinished due to errors run
	 * files so that we could remove them after recovery.
	 */
	VY_LOG_PREPARE_RUN		= 4,
	/**
	 * Commit a vinyl run file creation.
	 * Requires vy_log_record::lsm_id, run_id, dump_lsn.
	 *
	 * Written after a run file was successfully created.
	 */
	VY_LOG_CREATE_RUN		= 5,
	/**
	 * Drop a vinyl run.
	 * Requires vy_log_record::run_id, gc_lsn.
	 *
	 * A record of this type indicates that the run is not in use
	 * any more and its files can be safely removed. When the log
	 * is recovered from, this only marks the run as deleted,
	 * because we still need it for garbage collection. A run is
	 * actually freed by VY_LOG_FORGET_RUN. Runs that were
	 * deleted, but not "forgotten" are not expunged from the log
	 * on rotation.
	 */
	VY_LOG_DROP_RUN			= 6,
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
	/**
	 * Insert a run slice into a range.
	 * Requires vy_log_record::range_id, run_id, slice_id, begin, end.
	 */
	VY_LOG_INSERT_SLICE		= 8,
	/**
	 * Delete a run slice.
	 * Requires vy_log_record::slice_id.
	 */
	VY_LOG_DELETE_SLICE		= 9,
	/**
	 * Update LSN of the last LSM tree dump.
	 * Requires vy_log_record::lsm_id, dump_lsn.
	 */
	VY_LOG_DUMP_LSM			= 10,
	/**
	 * We don't split vylog into snapshot and log - all records
	 * are written to the same file. Since we need to load a
	 * consistent view from a given checkpoint (for replication
	 * and backup), we write a marker after the last record
	 * corresponding to the snapshot. The following record
	 * represents the marker.
	 *
	 * See also: @only_checkpoint argument of vy_recovery_new().
	 */
	VY_LOG_SNAPSHOT			= 11,
	/**
	 * When we used LSN for identifying LSM trees in vylog, we
	 * couldn't simply recreate an LSM tree on space truncation,
	 * because in case the space had more than one LSM tree, we
	 * wouldn't be able to distinguish them after truncation.
	 * So we wrote special 'truncate' record.
	 *
	 * Now, we assign a unique id to each LSM tree and so we don't
	 * need a special record type for space truncation. If we
	 * are recovering from an old vylog, we simply ignore all
	 * 'truncate' records - this will result in replay of all
	 * WAL records written after truncation.
	 */
	VY_LOG_TRUNCATE_LSM		= 12,
	/**
	 * Modify key definition of an LSM tree.
	 * Requires vy_log_record::lsm_id, key_def, modify_lsn.
	 */
	VY_LOG_MODIFY_LSM		= 13,

	vy_log_record_type_MAX
};

/** Record in the metadata log. */
struct vy_log_record {
	/** Type of the record. */
	enum vy_log_record_type type;
	/** Unique ID of the vinyl LSM tree. */
	int64_t lsm_id;
	/** Unique ID of the vinyl range. */
	int64_t range_id;
	/** Unique ID of the vinyl run. */
	int64_t run_id;
	/** Unique ID of the run slice. */
	int64_t slice_id;
	/**
	 * Msgpack key for start of the range/slice.
	 * NULL if the range/slice starts from -inf.
	 */
	const char *begin;
	/**
	 * Msgpack key for end of the range/slice.
	 * NULL if the range/slice ends with +inf.
	 */
	const char *end;
	/** Ordinal index number in the space. */
	uint32_t index_id;
	/** Space ID. */
	uint32_t space_id;
	/** Index key definition, as defined by the user. */
	const struct key_def *key_def;
	/** Array of key part definitions. */
	struct key_part_def *key_parts;
	/** Number of key parts. */
	uint32_t key_part_count;
	/** LSN of the WAL row that created the LSM tree. */
	int64_t create_lsn;
	/** LSN of the WAL row that last modified the LSM tree. */
	int64_t modify_lsn;
	/** Max LSN stored on disk. */
	int64_t dump_lsn;
	/**
	 * For deleted runs: LSN of the last checkpoint
	 * that uses this run.
	 */
	int64_t gc_lsn;
	/** Link in vy_log::tx. */
	struct stailq_entry in_tx;
};

/** Recovery context. */
struct vy_recovery {
	/**
	 * List of all LSM trees stored in the recovery context,
	 * linked by vy_lsm_recovery_info::in_recovery.
	 */
	struct rlist lsms;
	/** space_id, index_id -> vy_lsm_recovery_info. */
	struct mh_i64ptr_t *index_id_hash;
	/** ID -> vy_lsm_recovery_info. */
	struct mh_i64ptr_t *lsm_hash;
	/** ID -> vy_range_recovery_info. */
	struct mh_i64ptr_t *range_hash;
	/** ID -> vy_run_recovery_info. */
	struct mh_i64ptr_t *run_hash;
	/** ID -> vy_slice_recovery_info. */
	struct mh_i64ptr_t *slice_hash;
	/**
	 * Maximal vinyl object ID, according to the metadata log,
	 * or -1 in case no vinyl objects were recovered.
	 */
	int64_t max_id;
};

/** LSM tree info stored in a recovery context. */
struct vy_lsm_recovery_info {
	/** Link in vy_recovery::lsms. */
	struct rlist in_recovery;
	/** ID of the LSM tree. */
	int64_t id;
	/** Ordinal index number in the space. */
	uint32_t index_id;
	/** Space ID. */
	uint32_t space_id;
	/** Array of key part definitions. */
	struct key_part_def *key_parts;
	/** Number of key parts. */
	uint32_t key_part_count;
	/** True if the LSM tree was dropped. */
	bool is_dropped;
	/** LSN of the WAL row that created the LSM tree. */
	int64_t create_lsn;
	/** LSN of the WAL row that last modified the LSM tree. */
	int64_t modify_lsn;
	/** LSN of the last LSM tree dump. */
	int64_t dump_lsn;
	/**
	 * List of all ranges in the LSM tree, linked by
	 * vy_range_recovery_info::in_lsm.
	 */
	struct rlist ranges;
	/**
	 * List of all runs created for the LSM tree
	 * (both committed and not), linked by
	 * vy_run_recovery_info::in_lsm.
	 */
	struct rlist runs;
};

/** Vinyl range info stored in a recovery context. */
struct vy_range_recovery_info {
	/** Link in vy_lsm_recovery_info::ranges. */
	struct rlist in_lsm;
	/** ID of the range. */
	int64_t id;
	/** Start of the range, stored in MsgPack array. */
	char *begin;
	/** End of the range, stored in MsgPack array. */
	char *end;
	/**
	 * List of all slices in the range, linked by
	 * vy_slice_recovery_info::in_range.
	 *
	 * Newer slices are closer to the head.
	 */
	struct rlist slices;
};

/** Run info stored in a recovery context. */
struct vy_run_recovery_info {
	/** Link in vy_lsm_recovery_info::runs. */
	struct rlist in_lsm;
	/** ID of the run. */
	int64_t id;
	/** Max LSN stored on disk. */
	int64_t dump_lsn;
	/**
	 * For deleted runs: LSN of the last checkpoint
	 * that uses this run.
	 */
	int64_t gc_lsn;
	/**
	 * True if the run was not committed (there's
	 * VY_LOG_PREPARE_RUN, but no VY_LOG_CREATE_RUN).
	 */
	bool is_incomplete;
	/** True if the run was dropped (VY_LOG_DROP_RUN). */
	bool is_dropped;
	/*
	 * The following field is initialized to NULL and
	 * ignored by vy_log subsystem. It may be used by
	 * the caller to store some extra information.
	 *
	 * During recovery, we store a pointer to vy_run
	 * corresponding to this object.
	 */
	void *data;
};

/** Slice info stored in a recovery context. */
struct vy_slice_recovery_info {
	/** Link in vy_range_recovery_info::slices. */
	struct rlist in_range;
	/** ID of the slice. */
	int64_t id;
	/** Run this slice was created for. */
	struct vy_run_recovery_info *run;
	/** Start of the slice, stored in MsgPack array. */
	char *begin;
	/** End of the slice, stored in MsgPack array. */
	char *end;
};

/**
 * Initialize the metadata log.
 * @dir is the directory where log files are stored.
 */
void
vy_log_init(const char *dir);

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
 * and writes records required to recover active LSM trees.
 * The goal of log rotation is to compact the log file by
 * discarding records cancelling each other and records left
 * from dropped LSM trees.
 *
 * Returns 0 on success, -1 on failure.
 */
int
vy_log_rotate(const struct vclock *vclock);

/**
 * Remove metadata log files that are not needed to recover
 * from the snapshot with the given signature or newer.
 */
void
vy_log_collect_garbage(int64_t signature);

/**
 * Return the path to the log file that needs to be backed up
 * in order to recover to checkpoint @vclock.
 */
const char *
vy_log_backup_path(struct vclock *vclock);

/** Allocate a unique ID for a vinyl object. */
int64_t
vy_log_next_id(void);

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
 */
void
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
 * vclock @vclock and return the recovery context.
 *
 * After this function is called, LSM trees may be recovered from
 * the log using vy_recovery methods. When recovery is complete,
 * one must call vy_log_end_recovery(). After that the recovery context
 * may be deleted with vy_recovery_delete().
 *
 * Returns NULL on failure.
 */
struct vy_recovery *
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

/**
 * Create a recovery context from the metadata log created
 * by checkpoint with the given signature.
 *
 * If @only_checkpoint is set, do not load records appended to
 * the log after checkpoint (i.e. get a consistent view of
 * Vinyl at the time of the checkpoint).
 *
 * Returns NULL on failure.
 */
struct vy_recovery *
vy_recovery_new(int64_t signature, bool only_checkpoint);

/**
 * Free a recovery context created by vy_recovery_new().
 */
void
vy_recovery_delete(struct vy_recovery *recovery);

/**
 * Look up the last incarnation of an LSM tree stored in a recovery context.
 *
 * Returns NULL if the LSM tree was not found.
 */
struct vy_lsm_recovery_info *
vy_recovery_lsm_by_index_id(struct vy_recovery *recovery,
			    uint32_t space_id, uint32_t index_id);

/**
 * Initialize a log record with default values.
 * Note, a key is only written to the log file
 * if its value is different from default.
 */
static inline void
vy_log_record_init(struct vy_log_record *record)
{
	memset(record, 0, sizeof(*record));
}

/** Helper to log a vinyl LSM tree creation. */
static inline void
vy_log_create_lsm(int64_t id, uint32_t space_id, uint32_t index_id,
		  const struct key_def *key_def, int64_t create_lsn)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_CREATE_LSM;
	record.lsm_id = id;
	record.space_id = space_id;
	record.index_id = index_id;
	record.key_def = key_def;
	record.create_lsn = create_lsn;
	vy_log_write(&record);
}

/** Helper to log a vinyl LSM tree modification. */
static inline void
vy_log_modify_lsm(int64_t id, const struct key_def *key_def, int64_t modify_lsn)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_MODIFY_LSM;
	record.lsm_id = id;
	record.key_def = key_def;
	record.modify_lsn = modify_lsn;
	vy_log_write(&record);
}

/** Helper to log a vinyl LSM tree drop. */
static inline void
vy_log_drop_lsm(int64_t id)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_DROP_LSM;
	record.lsm_id = id;
	vy_log_write(&record);
}

/** Helper to log a vinyl range insertion. */
static inline void
vy_log_insert_range(int64_t lsm_id, int64_t range_id,
		    const char *begin, const char *end)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_INSERT_RANGE;
	record.lsm_id = lsm_id;
	record.range_id = range_id;
	record.begin = begin;
	record.end = end;
	vy_log_write(&record);
}

/** Helper to log a vinyl range deletion. */
static inline void
vy_log_delete_range(int64_t range_id)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_DELETE_RANGE;
	record.range_id = range_id;
	vy_log_write(&record);
}

/** Helper to log a vinyl run file creation. */
static inline void
vy_log_prepare_run(int64_t lsm_id, int64_t run_id)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_PREPARE_RUN;
	record.lsm_id = lsm_id;
	record.run_id = run_id;
	vy_log_write(&record);
}

/** Helper to log a vinyl run creation. */
static inline void
vy_log_create_run(int64_t lsm_id, int64_t run_id, int64_t dump_lsn)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_CREATE_RUN;
	record.lsm_id = lsm_id;
	record.run_id = run_id;
	record.dump_lsn = dump_lsn;
	vy_log_write(&record);
}

/** Helper to log a run deletion. */
static inline void
vy_log_drop_run(int64_t run_id, int64_t gc_lsn)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_DROP_RUN;
	record.run_id = run_id;
	record.gc_lsn = gc_lsn;
	vy_log_write(&record);
}

/** Helper to log a run cleanup. */
static inline void
vy_log_forget_run(int64_t run_id)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_FORGET_RUN;
	record.run_id = run_id;
	vy_log_write(&record);
}

/** Helper to log creation of a run slice. */
static inline void
vy_log_insert_slice(int64_t range_id, int64_t run_id, int64_t slice_id,
		    const char *begin, const char *end)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_INSERT_SLICE;
	record.range_id = range_id;
	record.run_id = run_id;
	record.slice_id = slice_id;
	record.begin = begin;
	record.end = end;
	vy_log_write(&record);
}

/** Helper to log deletion of a run slice. */
static inline void
vy_log_delete_slice(int64_t slice_id)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_DELETE_SLICE;
	record.slice_id = slice_id;
	vy_log_write(&record);
}

/** Helper to log LSM tree dump. */
static inline void
vy_log_dump_lsm(int64_t id, int64_t dump_lsn)
{
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_DUMP_LSM;
	record.lsm_id = id;
	record.dump_lsn = dump_lsn;
	vy_log_write(&record);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_LOG_H */
