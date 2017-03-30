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
#include "vy_log.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <msgpuck/msgpuck.h>
#include <small/region.h>
#include <small/rlist.h>

#include "assoc.h"
#include "cfg.h"
#include "coeio.h"
#include "coeio_file.h"
#include "diag.h"
#include "errcode.h"
#include "errinj.h"
#include "fiber.h"
#include "iproto_constants.h" /* IPROTO_INSERT */
#include "latch.h"
#include "replication.h" /* INSTANCE_UUID */
#include "say.h"
#include "trivia/util.h"
#include "wal.h"
#include "vclock.h"
#include "xlog.h"
#include "xrow.h"

/**
 * Integer key of a field in the vy_log_record structure.
 * Used for packing a record in MsgPack.
 */
enum vy_log_key {
	VY_LOG_KEY_INDEX_ID		= 0,
	VY_LOG_KEY_RANGE_ID		= 1,
	VY_LOG_KEY_RUN_ID		= 2,
	VY_LOG_KEY_RANGE_BEGIN		= 3,
	VY_LOG_KEY_RANGE_END		= 4,
	VY_LOG_KEY_IID			= 5,
	VY_LOG_KEY_SPACE_ID		= 6,
	VY_LOG_KEY_PATH			= 7,
};

/**
 * Bit mask of keys that must be present in a record
 * of a particular type.
 */
static const unsigned long vy_log_key_mask[] = {
	[VY_LOG_CREATE_INDEX]		= (1 << VY_LOG_KEY_INDEX_ID) |
					  (1 << VY_LOG_KEY_IID) |
					  (1 << VY_LOG_KEY_SPACE_ID) |
					  (1 << VY_LOG_KEY_PATH),
	[VY_LOG_DROP_INDEX]		= (1 << VY_LOG_KEY_INDEX_ID),
	[VY_LOG_INSERT_RANGE]		= (1 << VY_LOG_KEY_INDEX_ID) |
					  (1 << VY_LOG_KEY_RANGE_ID) |
					  (1 << VY_LOG_KEY_RANGE_BEGIN) |
					  (1 << VY_LOG_KEY_RANGE_END),
	[VY_LOG_DELETE_RANGE]		= (1 << VY_LOG_KEY_RANGE_ID),
	[VY_LOG_PREPARE_RUN]		= (1 << VY_LOG_KEY_INDEX_ID) |
					  (1 << VY_LOG_KEY_RUN_ID),
	[VY_LOG_INSERT_RUN]		= (1 << VY_LOG_KEY_RANGE_ID) |
					  (1 << VY_LOG_KEY_RUN_ID),
	[VY_LOG_DELETE_RUN]		= (1 << VY_LOG_KEY_RUN_ID),
	[VY_LOG_FORGET_RUN]		= (1 << VY_LOG_KEY_RUN_ID),
};

/** vy_log_key -> human readable name. */
static const char *vy_log_key_name[] = {
	[VY_LOG_KEY_INDEX_ID]		= "index_id",
	[VY_LOG_KEY_RANGE_ID]		= "range_id",
	[VY_LOG_KEY_RUN_ID]		= "run_id",
	[VY_LOG_KEY_RANGE_BEGIN]	= "range_begin",
	[VY_LOG_KEY_RANGE_END]		= "range_end",
	[VY_LOG_KEY_IID]		= "iid",
	[VY_LOG_KEY_SPACE_ID]		= "space_id",
	[VY_LOG_KEY_PATH]		= "path",
};

/** vy_log_type -> human readable name. */
static const char *vy_log_type_name[] = {
	[VY_LOG_CREATE_INDEX]		= "create_index",
	[VY_LOG_DROP_INDEX]		= "drop_index",
	[VY_LOG_INSERT_RANGE]		= "insert_range",
	[VY_LOG_DELETE_RANGE]		= "delete_range",
	[VY_LOG_PREPARE_RUN]		= "prepare_run",
	[VY_LOG_INSERT_RUN]		= "insert_run",
	[VY_LOG_DELETE_RUN]		= "delete_run",
	[VY_LOG_FORGET_RUN]		= "forget_run",
};

struct vy_recovery;

/**
 * Max number of records in the log buffer.
 * This limits the size of a transaction.
 */
enum { VY_LOG_TX_BUF_SIZE = 64 };

/** Metadata log object. */
struct vy_log {
	/** The directory where log files are stored. */
	struct xdir dir;
	/** The vinyl directory. */
	char vinyl_dir[PATH_MAX];
	/** Last checkpoint vclock. */
	struct vclock last_checkpoint;
	/** Next to last checkpoint vclock. */
	struct vclock prev_checkpoint;
	/** Recovery context. */
	struct vy_recovery *recovery;
	/** Latch protecting the log buffer. */
	struct latch latch;
	/**
	 * Next ID to use for a vinyl range.
	 * Used by vy_log_next_range_id().
	 */
	int64_t next_range_id;
	/**
	 * Next ID to use for a vinyl run.
	 * Used by vy_log_next_run_id().
	 */
	int64_t next_run_id;
	/**
	 * Index of the first record of the current
	 * transaction in tx_buf.
	 */
	int tx_begin;
	/**
	 * Index of the record following the last one
	 * of the current transaction in tx_buf.
	 */
	int tx_end;
	/** Records awaiting to be written to disk. */
	struct vy_log_record tx_buf[VY_LOG_TX_BUF_SIZE];
};
static struct vy_log vy_log;

/** Recovery context. */
struct vy_recovery {
	/** ID -> vy_index_recovery_info. */
	struct mh_i64ptr_t *index_hash;
	/** ID -> vy_range_recovery_info. */
	struct mh_i64ptr_t *range_hash;
	/** ID -> vy_run_recovery_info. */
	struct mh_i64ptr_t *run_hash;
	/**
	 * Maximal vinyl range ID, according to the metadata log,
	 * or -1 in case no ranges were recovered.
	 */
	int64_t range_id_max;
	/**
	 * Maximal vinyl run ID, according to the metadata log,
	 * or -1 in case no runs were recovered.
	 */
	int64_t run_id_max;
};

/** Vinyl index info stored in a recovery context. */
struct vy_index_recovery_info {
	/** ID of the index. */
	int64_t id;
	/** Ordinal index number in the space. */
	uint32_t iid;
	/** Space ID. */
	uint32_t space_id;
	/** Path to the index. Empty string if default. */
	char *path;
	/** True if the index was dropped. */
	bool is_dropped;
	/**
	 * Log signature from the time when the index was created
	 * or dropped.
	 */
	int64_t signature;
	/**
	 * List of all ranges in the index, linked by
	 * vy_range_recovery_info::in_index.
	 */
	struct rlist ranges;
	/**
	 * List of runs that were prepared, but never
	 * inserted into a range or deleted, linked by
	 * vy_run_recovery_info::in_incomplete.
	 */
	struct rlist incomplete_runs;
};

/** Vinyl range info stored in a recovery context. */
struct vy_range_recovery_info {
	/** Link in vy_index_recovery_info::ranges. */
	struct rlist in_index;
	/** ID of the range. */
	int64_t id;
	/** Start of the range, stored in MsgPack array. */
	char *begin;
	/** End of the range, stored in MsgPack array. */
	char *end;
	/** True if the range was deleted. */
	bool is_deleted;
	/**
	 * Log signature from the time when the range was created
	 * or deleted.
	 */
	int64_t signature;
	/**
	 * List of all runs in the range, linked by
	 * vy_run_recovery_info::in_range.
	 *
	 * Newer runs are closer to the head.
	 */
	struct rlist runs;
};

/** Run info stored in a recovery context. */
struct vy_run_recovery_info {
	/** Link in vy_range_recovery_info::runs. */
	struct rlist in_range;
	/** Link in vy_index_recovery_info::incomplete_runs. */
	struct rlist in_incomplete;
	/** ID of the run. */
	int64_t id;
	/** True if the run was deleted. */
	bool is_deleted;
	/**
	 * Log signature from the time when the run was last modified
	 * (created, inserted into a range, or deleted).
	 */
	int64_t signature;
};

static struct vy_recovery *
vy_recovery_new(int64_t recovery_signature);
static void
vy_recovery_delete(struct vy_recovery *recovery);
static struct vy_index_recovery_info *
vy_recovery_lookup_index(struct vy_recovery *recovery, int64_t index_id);
static int
vy_recovery_iterate_index(struct vy_index_recovery_info *index,
		bool include_deleted, vy_recovery_cb cb, void *cb_arg);
static int
vy_recovery_iterate(struct vy_recovery *recovery, bool include_deleted,
		    vy_recovery_cb cb, void *cb_arg);

/** An snprint-style function to print a log record. */
static int
vy_log_record_snprint(char *buf, int size, const struct vy_log_record *record)
{
	int total = 0;
	assert(record->type < vy_log_record_type_MAX);
	unsigned long key_mask = vy_log_key_mask[record->type];
	SNPRINT(total, snprintf, buf, size, "%s{",
		vy_log_type_name[record->type]);
	SNPRINT(total, snprintf, buf, size,
		"signature=%"PRIi64", ", record->signature);
	if (key_mask & (1 << VY_LOG_KEY_INDEX_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_INDEX_ID],
			record->index_id);
	if (key_mask & (1 << VY_LOG_KEY_RANGE_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_RANGE_ID],
			record->range_id);
	if (key_mask & (1 << VY_LOG_KEY_RUN_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_RUN_ID],
			record->run_id);
	if (key_mask & (1 << VY_LOG_KEY_RANGE_BEGIN)) {
		SNPRINT(total, snprintf, buf, size, "%s=",
			vy_log_key_name[VY_LOG_KEY_RANGE_BEGIN]);
		if (record->range_begin != NULL)
			SNPRINT(total, mp_snprint, buf, size,
				record->range_begin);
		else
			SNPRINT(total, snprintf, buf, size, "[]");
		SNPRINT(total, snprintf, buf, size, ", ");
	}
	if (key_mask & (1 << VY_LOG_KEY_RANGE_END)) {
		SNPRINT(total, snprintf, buf, size, "%s=",
			vy_log_key_name[VY_LOG_KEY_RANGE_END]);
		if (record->range_end != NULL)
			SNPRINT(total, mp_snprint, buf, size,
				record->range_end);
		else
			SNPRINT(total, snprintf, buf, size, "[]");
		SNPRINT(total, snprintf, buf, size, ", ");
	}
	if (key_mask & (1 << VY_LOG_KEY_IID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIu32", ",
			vy_log_key_name[VY_LOG_KEY_IID], record->iid);
	if (key_mask & (1 << VY_LOG_KEY_SPACE_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIu32", ",
			vy_log_key_name[VY_LOG_KEY_SPACE_ID], record->space_id);
	if (key_mask & (1 << VY_LOG_KEY_PATH))
		SNPRINT(total, snprintf, buf, size, "%s=%.*s, ",
			vy_log_key_name[VY_LOG_KEY_PATH],
			record->path_len, record->path);
	SNPRINT(total, snprintf, buf, size, "}");
	return total;
}

/**
 * Return a string containing a human readable representation
 * of a log record.
 */
static const char *
vy_log_record_str(const struct vy_log_record *record)
{
	char *buf = tt_static_buf();
	if (vy_log_record_snprint(buf, TT_STATIC_BUF_LEN, record) < 0)
		return "<failed to format vy_log log record>";
	return buf;
}

/**
 * Encode a log record into an xrow to be further written to an xlog.
 * Return 0 on success, -1 on failure.
 *
 * When stored in xlog, a vinyl metadata log has the following MsgPack
 * representation:
 *
 * [ type, { key: value, ... } ]
 *
 * 'type': see vy_log_record_type enum
 * 'key': see vy_log_key enum
 * 'value': depends on 'key'
 */
static int
vy_log_record_encode(const struct vy_log_record *record,
		     struct xrow_header *row)
{
	assert(record->type < vy_log_record_type_MAX);
	unsigned long key_mask = vy_log_key_mask[record->type];

	/*
	 * Calculate record size.
	 */
	size_t size = 0;
	size += mp_sizeof_array(2);
	size += mp_sizeof_uint(record->type);
	size_t n_keys = 0;
	if (key_mask & (1 << VY_LOG_KEY_INDEX_ID)) {
		assert(record->index_id >= 0);
		size += mp_sizeof_uint(VY_LOG_KEY_INDEX_ID);
		size += mp_sizeof_uint(record->index_id);
		n_keys++;
	}
	if (key_mask & (1 << VY_LOG_KEY_RANGE_ID)) {
		assert(record->range_id >= 0);
		size += mp_sizeof_uint(VY_LOG_KEY_RANGE_ID);
		size += mp_sizeof_uint(record->range_id);
		n_keys++;
	}
	if (key_mask & (1 << VY_LOG_KEY_RUN_ID)) {
		assert(record->run_id >= 0);
		size += mp_sizeof_uint(VY_LOG_KEY_RUN_ID);
		size += mp_sizeof_uint(record->run_id);
		n_keys++;
	}
	if (key_mask & (1 << VY_LOG_KEY_RANGE_BEGIN)) {
		size += mp_sizeof_uint(VY_LOG_KEY_RANGE_BEGIN);
		if (record->range_begin != NULL) {
			const char *p = record->range_begin;
			assert(mp_typeof(*p) == MP_ARRAY);
			mp_next(&p);
			size += p - record->range_begin;
		} else
			size += mp_sizeof_array(0);
		n_keys++;
	}
	if (key_mask & (1 << VY_LOG_KEY_RANGE_END)) {
		size += mp_sizeof_uint(VY_LOG_KEY_RANGE_END);
		if (record->range_end != NULL) {
			const char *p = record->range_end;
			assert(mp_typeof(*p) == MP_ARRAY);
			mp_next(&p);
			size += p - record->range_end;
		} else
			size += mp_sizeof_array(0);
		n_keys++;
	}
	if (key_mask & (1 << VY_LOG_KEY_IID)) {
		size += mp_sizeof_uint(VY_LOG_KEY_IID);
		size += mp_sizeof_uint(record->iid);
		n_keys++;
	}
	if (key_mask & (1 << VY_LOG_KEY_SPACE_ID)) {
		size += mp_sizeof_uint(VY_LOG_KEY_SPACE_ID);
		size += mp_sizeof_uint(record->space_id);
		n_keys++;
	}
	if (key_mask & (1 << VY_LOG_KEY_PATH)) {
		size += mp_sizeof_uint(VY_LOG_KEY_PATH);
		size += mp_sizeof_str(record->path_len);
		n_keys++;
	}
	size += mp_sizeof_map(n_keys);

	/*
	 * Encode record.
	 */
	char *tuple = region_alloc(&fiber()->gc, size);
	if (tuple == NULL) {
		diag_set(OutOfMemory, size, "region", "vy_log record");
		return -1;
	}
	char *pos = tuple;
	pos = mp_encode_array(pos, 2);
	pos = mp_encode_uint(pos, record->type);
	pos = mp_encode_map(pos, n_keys);
	if (key_mask & (1 << VY_LOG_KEY_INDEX_ID)) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_INDEX_ID);
		pos = mp_encode_uint(pos, record->index_id);
	}
	if (key_mask & (1 << VY_LOG_KEY_RANGE_ID)) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_RANGE_ID);
		pos = mp_encode_uint(pos, record->range_id);
	}
	if (key_mask & (1 << VY_LOG_KEY_RUN_ID)) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_RUN_ID);
		pos = mp_encode_uint(pos, record->run_id);
	}
	if (key_mask & (1 << VY_LOG_KEY_RANGE_BEGIN)) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_RANGE_BEGIN);
		if (record->range_begin != NULL) {
			const char *p = record->range_begin;
			mp_next(&p);
			memcpy(pos, record->range_begin,
			       p - record->range_begin);
			pos += p - record->range_begin;
		} else
			pos = mp_encode_array(pos, 0);
	}
	if (key_mask & (1 << VY_LOG_KEY_RANGE_END)) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_RANGE_END);
		if (record->range_end != NULL) {
			const char *p = record->range_end;
			mp_next(&p);
			memcpy(pos, record->range_end,
			       p - record->range_end);
			pos += p - record->range_end;
		} else
			pos = mp_encode_array(pos, 0);
	}
	if (key_mask & (1 << VY_LOG_KEY_IID)) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_IID);
		pos = mp_encode_uint(pos, record->iid);
	}
	if (key_mask & (1 << VY_LOG_KEY_SPACE_ID)) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_SPACE_ID);
		pos = mp_encode_uint(pos, record->space_id);
	}
	if (key_mask & (1 << VY_LOG_KEY_PATH)) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_PATH);
		pos = mp_encode_str(pos, record->path, record->path_len);
	}
	assert(pos == tuple + size);

	/*
	 * Store record in xrow.
	 */
	struct request req;
	request_create(&req, IPROTO_INSERT);
	req.tuple = tuple;
	req.tuple_end = pos;
	memset(row, 0, sizeof(*row));
	row->lsn = record->signature;
	row->bodycnt = request_encode(&req, row->body);
	return 0;
}

/**
 * Decode a log record from an xrow.
 * Return 0 on success, -1 on failure.
 */
static int
vy_log_record_decode(struct vy_log_record *record,
		     const struct xrow_header *row)
{
	char *buf;

	memset(record, 0, sizeof(*record));
	record->signature = row->lsn;

	struct request req;
	request_create(&req, row->type);
	if (request_decode(&req, row->body->iov_base,
			   row->body->iov_len) < 0)
		return -1;

	const char *pos = req.tuple;

	if (mp_decode_array(&pos) != 2)
		goto fail;

	record->type = mp_decode_uint(&pos);
	if (record->type >= vy_log_record_type_MAX)
		goto fail;

	unsigned long key_mask = 0;
	uint32_t n_keys = mp_decode_map(&pos);
	for (uint32_t i = 0; i < n_keys; i++) {
		uint32_t key = mp_decode_uint(&pos);
		switch (key) {
		case VY_LOG_KEY_INDEX_ID:
			record->index_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_RANGE_ID:
			record->range_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_RUN_ID:
			record->run_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_RANGE_BEGIN:
			record->range_begin = pos;
			mp_next(&pos);
			break;
		case VY_LOG_KEY_RANGE_END:
			record->range_end = pos;
			mp_next(&pos);
			break;
		case VY_LOG_KEY_IID:
			record->iid = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_SPACE_ID:
			record->space_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_PATH:
			record->path = mp_decode_str(&pos, &record->path_len);
			break;
		default:
			goto fail;
		}
		key_mask |= 1 << key;
	}
	if ((key_mask & vy_log_key_mask[record->type]) !=
			vy_log_key_mask[record->type])
		goto fail;
	return 0;
fail:
	buf = tt_static_buf();
	mp_snprint(buf, TT_STATIC_BUF_LEN, req.tuple);
	say_error("invalid record in metadata log: %s", buf);
	diag_set(ClientError, ER_VINYL, "invalid vy_log record");
	return -1;
}

void
vy_log_init(void)
{
	xdir_create(&vy_log.dir, cfg_gets("vinyl_dir"), VYLOG, &INSTANCE_UUID);
	snprintf(vy_log.vinyl_dir, sizeof(vy_log.vinyl_dir), "%s",
		 cfg_gets("vinyl_dir"));
	latch_create(&vy_log.latch);
	wal_init_vy_log();
}

/**
 * Try to flush the log buffer to disk.
 *
 * We always flush the entire vy_log buffer as a single xlog
 * transaction, since we do not track boundaries of @no_discard
 * buffered transactions, and want to avoid a partial write.
 */
static int
vy_log_flush(void)
{
	if (vy_log.tx_end == 0)
		return 0; /* nothing to do */

	struct journal_entry *entry = journal_entry_new(vy_log.tx_end);
	if (entry == NULL)
		return -1;

	struct xrow_header *rows;
	rows = region_aligned_alloc(&fiber()->gc,
				    vy_log.tx_end * sizeof(struct xrow_header),
				    alignof(struct xrow_header));
	if (rows == NULL)
		return -1;

	/*
	 * Encode buffered records.
	 */
	for (int i = 0; i < vy_log.tx_end; i++) {
		struct xrow_header *row = &rows[i];
		if (vy_log_record_encode(&vy_log.tx_buf[i], row) < 0)
			return -1;
		entry->rows[i] = row;
	}
	/*
	 * Do actual disk writes on behalf of the WAL
	 * so as not to block the tx thread.
	 */
	if (wal_write_vy_log(entry) != 0)
		return -1;

	/* Success. Reset the buffer. */
	vy_log.tx_end = 0;
	return 0;
}

void
vy_log_free(void)
{
	xdir_destroy(&vy_log.dir);
	if (vy_log.recovery != NULL)
		vy_recovery_delete(vy_log.recovery);
	latch_destroy(&vy_log.latch);
}

int
vy_log_open(struct xlog *xlog)
{
	char *path = xdir_format_filename(&vy_log.dir,
			vclock_sum(&vy_log.last_checkpoint), NONE);
	return xlog_open(xlog, path);
}

int64_t
vy_log_next_run_id(void)
{
	return vy_log.next_run_id++;
}

int64_t
vy_log_next_range_id(void)
{
	return vy_log.next_range_id++;
}

/*
 * TODO: File name formatting does not belong here.
 * We should move it to a shared header and use it
 * both in vinyl.c and here.
 */
enum vy_file_type {
	VY_FILE_INDEX,
	VY_FILE_RUN,
	vy_file_MAX,
};

static const char *vy_file_suffix[] = {
	"index",	/* VY_FILE_INDEX */
	"run",		/* VY_FILE_RUN */
};

static int
vy_log_snprint_run_path(char *buf, int size, const char *index_path,
			uint32_t space_id, uint32_t iid, int64_t run_id,
			enum vy_file_type type)
{
	int total = 0;
	if (index_path[0] != '\0')
		SNPRINT(total, snprintf, buf, size, "%s/", index_path);
	else
		SNPRINT(total, snprintf, buf, size, "%s/%u/%u/",
			vy_log.vinyl_dir, (unsigned)space_id, (unsigned)iid);
	SNPRINT(total, snprintf, buf, size, "%020lld.%s",
		(long long)run_id, vy_file_suffix[type]);
	return total;
}

/**
 * Try to delete files of a vinyl run.
 * Return 0 on success, -1 on failure.
 */
static int
vy_log_delete_run_files(uint32_t space_id, uint32_t iid,
			const char *index_path, int64_t run_id)
{
	ERROR_INJECT(ERRINJ_VY_GC,
		     {say_error("error injection: run %lld not deleted",
				(long long)run_id); return -1;});
	int rc = 0;
	char path[PATH_MAX];
	for (int type = 0; type < vy_file_MAX; type++) {
		vy_log_snprint_run_path(path, sizeof(path), index_path,
					 space_id, iid, run_id, type);
		if (coeio_unlink(path) < 0 && errno != ENOENT) {
			say_syserror("failed to delete file '%s'", path);
			rc = -1;
		}
	}
	return rc;
}

/**
 * Given a record encoding information about a vinyl run, try to
 * delete the corresponding files. On success, write a "forget" record
 * to the log so that all information about the run is deleted on the
 * next log rotation.
 */
static void
vy_log_run_gc(const struct vy_log_record *record)
{
	if (vy_log_delete_run_files(record->space_id, record->iid,
				     record->path, record->run_id) == 0) {
		struct vy_log_record gc_record = {
			.type = VY_LOG_FORGET_RUN,
			.signature = record->signature,
			.run_id = record->run_id,
		};
		vy_log_tx_begin();
		vy_log_write(&gc_record);
		if (vy_log_tx_commit() < 0) {
			say_warn("failed to log vinyl run %lld cleanup: %s",
				 (long long)record->run_id,
				 diag_last_error(diag_get())->errmsg);
		}
	}
}

int
vy_log_bootstrap(void)
{
	/* Add initial vclock to the xdir. */
	struct vclock *vclock = malloc(sizeof(*vclock));
	if (vclock == NULL) {
		diag_set(OutOfMemory, sizeof(*vclock),
			 "malloc", "struct vclock");
		return -1;
	}
	vclock_create(vclock);
	xdir_add_vclock(&vy_log.dir, vclock);

	/* Create initial vy_log file. */
	struct xlog xlog;
	if (xdir_create_xlog(&vy_log.dir, &xlog, vclock) < 0)
		return -1;

	int rc = xlog_rename(&xlog);
	xlog_close(&xlog, false);
	return rc;
}

int
vy_log_begin_recovery(const struct vclock *vclock)
{
	assert(vy_log.recovery == NULL);

	if (xdir_scan(&vy_log.dir) < 0)
		return -1;

	struct vclock vy_log_vclock;
	if (xdir_last_vclock(&vy_log.dir, &vy_log_vclock) >= 0 &&
	    vclock_compare(&vy_log_vclock, vclock) > 0) {
		/*
		 * Last vy_log log is newer than the last snapshot.
		 * This can't normally happen, as vy_log is rotated
		 * after snapshot is created. Looks like somebody
		 * deleted snap file, but forgot to delete vy_log.
		 */
		diag_set(ClientError, ER_MISSING_SNAPSHOT);
		return -1;
	}

	struct vy_recovery *recovery;
	recovery = vy_recovery_new(INT64_MAX);
	if (recovery == NULL)
		return -1;

	vy_log.next_range_id = recovery->range_id_max + 1;
	vy_log.next_run_id = recovery->run_id_max + 1;

	vy_log.recovery = recovery;
	vclock_copy(&vy_log.last_checkpoint, vclock);
	return 0;
}

/**
 * Callback passed to vy_recovery_iterate() to remove files
 * left from incomplete vinyl runs.
 */
static int
vy_log_incomplete_run_gc(const struct vy_log_record *record, void *cb_arg)
{
	(void)cb_arg;
	if (record->type == VY_LOG_PREPARE_RUN)
		vy_log_run_gc(record);
	return 0;
}

static int
vy_log_create(const struct vclock *vclock, struct vy_recovery *recovery);

int
vy_log_end_recovery(void)
{
	assert(vy_log.recovery != NULL);

	/* Flush all pending records. */
	if (vy_log_flush() < 0)
		return -1;
	/*
	 * Reset vy_log.recovery before getting to garbage collection
	 * so that vy_log_commit() called by vy_log_run_gc() writes
	 * "forget" records to disk instead of accumulating them in
	 * the log buffer.
	 */
	struct vy_recovery *recovery = vy_log.recovery;
	vy_log.recovery = NULL;

	/*
	 * On backup we copy files corresponding to the most recent
	 * checkpoint. Since vy_log does not create snapshots of its log
	 * files, but instead appends records written after checkpoint
	 * to the most recent log file, the signature of the vy_log file
	 * corresponding to the last checkpoint equals the signature
	 * of the previous checkpoint. So upon successful recovery
	 * from a backup we need to rotate the log to keep checkpoint
	 * and vy_log signatures in sync.
	 */
	struct vclock *vclock = vclockset_last(&vy_log.dir.index);
	if (vclock == NULL ||
	    vclock_compare(vclock, &vy_log.last_checkpoint) != 0) {
		vclock = malloc(sizeof(*vclock));
		if (vclock == NULL) {
			diag_set(OutOfMemory, sizeof(*vclock),
				 "malloc", "struct vclock");
			return -1;
		}
		vclock_copy(vclock, &vy_log.last_checkpoint);
		xdir_add_vclock(&vy_log.dir, vclock);
		if (vy_log_create(vclock, recovery) < 0)
			return -1;
	}

	vclock = vclockset_prev(&vy_log.dir.index, vclock);
	if (vclock != NULL)
		vclock_copy(&vy_log.prev_checkpoint, vclock);

	/*
	 * If the instance is shut down while a dump/compaction task
	 * is in progress, we'll get an unfinished run file on disk,
	 * i.e. a run file which was either not written to the end
	 * or not inserted into a range. We need to delete such runs
	 * on recovery.
	 */
	vy_recovery_iterate(recovery, true,
			      vy_log_incomplete_run_gc, NULL);
	vy_recovery_delete(recovery);
	return 0;
}

int
vy_log_recover_index(int64_t index_id,
		     vy_recovery_cb cb, void *cb_arg)
{
	assert(vy_log.recovery != NULL);
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index(vy_log.recovery, index_id);
	if (index == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown vinyl index id");
		return -1;
	}
	return vy_recovery_iterate_index(index, false, cb, cb_arg);
}

/** Callback passed to vy_recovery_iterate() for log rotation. */
static int
vy_log_rotate_cb_func(const struct vy_log_record *record, void *cb_arg)
{
	struct xlog *xlog = cb_arg;
	struct xrow_header row;

	if (vy_log_record_encode(record, &row) < 0 ||
	    xlog_write_row(xlog, &row) < 0)
		return -1;
	return 0;
}

/**
 * Callback passed to vy_recovery_iterate() to remove files
 * left from deleted runs.
 */
static int
vy_log_deleted_run_gc(const struct vy_log_record *record, void *cb_arg)
{
	(void)cb_arg;
	if (record->type == VY_LOG_DELETE_RUN)
		vy_log_run_gc(record);
	return 0;
}

/**
 * Create an vy_log file from a recovery context.
 */
static int
vy_log_create(const struct vclock *vclock, struct vy_recovery *recovery)
{
	struct xlog xlog;
	if (xdir_create_xlog(&vy_log.dir, &xlog, vclock) < 0)
		goto err_create_xlog;

	if (vy_recovery_iterate(recovery, true,
				  vy_log_rotate_cb_func, &xlog) < 0)
		goto err_write_xlog;

	/* Finalize the new xlog. */
	if (xlog_flush(&xlog) < 0 ||
	    xlog_sync(&xlog) < 0 ||
	    xlog_rename(&xlog) < 0)
		goto err_write_xlog;

	xlog_close(&xlog, false);
	return 0;

err_write_xlog:
	/* Delete the unfinished xlog. */
	if (unlink(xlog.filename) < 0)
		say_syserror("failed to delete file '%s'", xlog.filename);
	xlog_close(&xlog, false);
err_create_xlog:
	return -1;
}

static ssize_t
vy_log_rotate_f(va_list ap)
{
	struct vy_recovery *recovery = va_arg(ap, struct vy_recovery *);
	const struct vclock *vclock = va_arg(ap, const struct vclock *);
	return vy_log_create(vclock, recovery);
}

int
vy_log_rotate(const struct vclock *vclock)
{
	assert(vy_log.recovery == NULL);

	/*
	 * This function is called right after bootstrap (by snapshot),
	 * in which case old and new signatures coincide and there's
	 * nothing we need to do.
	 */
	assert(vclock_compare(vclock, &vy_log.last_checkpoint) >= 0);
	if (vclock_compare(vclock, &vy_log.last_checkpoint) == 0)
		return 0;

	struct vclock *new_vclock = malloc(sizeof(*new_vclock));
	if (new_vclock == NULL) {
		diag_set(OutOfMemory, sizeof(*new_vclock),
			 "malloc", "struct vclock");
		return -1;
	}
	vclock_copy(new_vclock, vclock);

	say_debug("%s: signature %lld", __func__,
		  (long long)vclock_sum(vclock));

	struct vy_recovery *recovery = vy_recovery_new(INT64_MAX);
	if (recovery == NULL)
		goto fail;

	/*
	 * Lock out all concurrent log writers while we are rotating it.
	 * This effectively stalls the vinyl scheduler for a while, but
	 * this is acceptable, because (1) the log file is small and
	 * hence can be rotated fairly quickly so the stall isn't going
	 * to take too long and (2) dumps/compactions, which are scheduled
	 * by the scheduler, are rare events so there shouldn't be too
	 * many of them piling up due to log rotation.
	 */
	latch_lock(&vy_log.latch);

	/* Do actual work from coeio so as not to stall tx thread. */
	int rc = coio_call(vy_log_rotate_f, recovery, vclock);
	vy_recovery_delete(recovery);
	if (rc < 0) {
		latch_unlock(&vy_log.latch);
		goto fail;
	}

	/*
	 * Success. Close the old log. The new one will be opened
	 * automatically on the first write (see wal_write_vy_log()).
	 */
	wal_rotate_vy_log();
	vclock_copy(&vy_log.prev_checkpoint, &vy_log.last_checkpoint);
	vclock_copy(&vy_log.last_checkpoint, vclock);

	/* Add the new vclock to the xdir so that we can track it. */
	xdir_add_vclock(&vy_log.dir, new_vclock);

	latch_unlock(&vy_log.latch);
	say_debug("%s: complete", __func__);
	return 0;

fail:
	say_debug("%s: failed", __func__);
	say_error("failed to rotate metadata log: %s",
		  diag_last_error(diag_get())->errmsg);
	free(new_vclock);
	return -1;
}

static ssize_t
vy_log_collect_garbage_f(va_list ap)
{
	int64_t signature = va_arg(ap, int64_t);
	xdir_collect_garbage(&vy_log.dir, signature);
	return 0;
}

void
vy_log_collect_garbage(int64_t signature)
{
	say_debug("%s: signature %lld", __func__, (long long)signature);

	struct vy_recovery *recovery = vy_recovery_new(signature);
	if (recovery != NULL) {
		/* Cleanup unused runs. */
		vy_recovery_iterate(recovery, true,
				      vy_log_deleted_run_gc, NULL);
		vy_recovery_delete(recovery);
	} else {
		say_warn("garbage collection failed: %s",
			 diag_last_error(diag_get())->errmsg);
	}

	/*
	 * Delete old log files.
	 * Always keep the previous file, because
	 * it is still needed for backups.
	 */
	signature = MIN(signature, vclock_sum(&vy_log.prev_checkpoint));
	coio_call(vy_log_collect_garbage_f, signature);

	say_debug("%s: done", __func__);
}

/** Argument passed to vy_log_relay_f(). */
struct vy_log_relay_arg {
	/** The recovery context to relay. */
	struct vy_recovery *recovery;
	/** The relay callback. */
	vy_recovery_cb cb;
	/** The relay callback argument. */
	void *cb_arg;
};

/** Relay cord function. */
static int
vy_log_relay_f(va_list ap)
{
	struct vy_log_relay_arg *arg = va_arg(ap, struct vy_log_relay_arg *);
	return vy_recovery_iterate(arg->recovery, false,
				     arg->cb, arg->cb_arg);
}

int
vy_log_relay(vy_recovery_cb cb, void *cb_arg)
{
	/*
	 * First, load the latest snapshot of the metadata log.
	 */
	struct vy_recovery *recovery;
	recovery = vy_recovery_new(vclock_sum(&vy_log.last_checkpoint));
	if (recovery == NULL)
		return -1;

	/*
	 * Second, relay the state stored in the log via
	 * the provided callback.
	 */
	struct vy_log_relay_arg arg = {
		.recovery = recovery,
		.cb = cb,
		.cb_arg = cb_arg,
	};
	struct cord cord;
	if (cord_costart(&cord, "initial_join", vy_log_relay_f, &arg) != 0) {
		vy_recovery_delete(recovery);
		return -1;
	}
	if (cord_cojoin(&cord) != 0)
		return -1;

	return 0;
}

/** Argument passed to vy_log_backup_func(). */
struct vy_log_backup_arg {
	/** Callback passed to vy_log_backup(). */
	vy_log_backup_cb cb;
	/** Extra argument to cb(). */
	void *cb_arg;
	/** Path buffer. */
	char *path;
};

/**
 * Callback passed to vy_recovery_iterate() to return
 * the list of files needed for backup.
 */
static int
vy_log_backup_func(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_log_backup_arg *arg = cb_arg;

	if (record->type == VY_LOG_INSERT_RUN) {
		for (int type = 0; type < vy_file_MAX; type++) {
			vy_log_snprint_run_path(arg->path, PATH_MAX,
					record->path,
					(unsigned)record->space_id,
					(unsigned)record->iid,
					(long long)record->run_id, type);
			if (arg->cb(arg->path, arg->cb_arg) != 0)
				return -1;
		}
	}
	fiber_reschedule();
	return 0;
}

int
vy_log_backup(vy_log_backup_cb cb, void *cb_arg)
{
	int rc = -1;
	char *path = malloc(PATH_MAX);
	if (path == NULL) {
		diag_set(OutOfMemory, PATH_MAX, "malloc", "path");
		goto out;
	}

	/* Load recovery context. */
	struct vy_recovery *recovery;
	recovery = vy_recovery_new(vclock_sum(&vy_log.last_checkpoint));
	if (recovery == NULL)
		goto out_free_path;

	/*
	 * Backup vy_log log.
	 * Use the previous log file, because the current one
	 * contains records written after the last checkpoint.
	 */
	rc = cb(xdir_format_filename(&vy_log.dir,
			vclock_sum(&vy_log.prev_checkpoint), NONE), cb_arg);
	if (rc != 0)
		goto out_free_recovery;

	/* Backup vinyl runs. */
	struct vy_log_backup_arg arg = {
		.cb = cb,
		.cb_arg = cb_arg,
		.path = path,
	};
	rc = vy_recovery_iterate(recovery, false, vy_log_backup_func, &arg);

out_free_recovery:
	vy_recovery_delete(recovery);
out_free_path:
	free(path);
out:
	return rc;
}

void
vy_log_tx_begin(void)
{
	latch_lock(&vy_log.latch);
	vy_log.tx_begin = vy_log.tx_end;
	say_debug("%s", __func__);
}

/**
 * Commit a transaction started with vy_log_tx_begin().
 *
 * If @no_discard is set, pending records won't be expunged from the
 * buffer on failure, so that the next transaction will retry to write
 * them to disk.
 */
static int
vy_log_tx_do_commit(bool no_discard)
{
	int rc = 0;

	assert(latch_owner(&vy_log.latch) == fiber());
	/*
	 * During recovery, we may replay records we failed to commit
	 * before restart (e.g. drop index). Since the log isn't open
	 * yet, simply leave them in the tx buffer to be flushed upon
	 * recovery completion.
	 */
	if (vy_log.recovery != NULL)
		goto out;

	rc = vy_log_flush();
	/*
	 * Rollback the transaction on failure unless
	 * we were explicitly told not to.
	 */
	if (rc != 0 && !no_discard)
		vy_log.tx_end = vy_log.tx_begin;
out:
	say_debug("%s(no_discard=%d): %s", __func__, no_discard,
		  rc == 0 ? "success" : "fail");
	latch_unlock(&vy_log.latch);
	return rc;
}

int
vy_log_tx_commit(void)
{
	return vy_log_tx_do_commit(false);
}

int
vy_log_tx_try_commit(void)
{
	return vy_log_tx_do_commit(true);
}

void
vy_log_write(const struct vy_log_record *record)
{
	assert(latch_owner(&vy_log.latch) == fiber());

	say_debug("%s: %s", __func__, vy_log_record_str(record));
	if (vy_log.tx_end >= VY_LOG_TX_BUF_SIZE) {
		latch_unlock(&vy_log.latch);
		panic("metadata log buffer overflow");
	}

	struct vy_log_record *tx_record = &vy_log.tx_buf[vy_log.tx_end++];
	*tx_record = *record;
	if (tx_record->signature < 0)
		tx_record->signature = vclock_sum(&vy_log.last_checkpoint);
}

/** Mark a vinyl run as deleted. */
static void
vy_run_mark_deleted(struct vy_run_recovery_info *run, int64_t signature)
{
	assert(!run->is_deleted);
	run->is_deleted = true;
	run->signature = signature;
}

/** Mark a vinyl range and all its runs as deleted. */
static void
vy_range_mark_deleted(struct vy_range_recovery_info *range, int64_t signature)
{
	assert(!range->is_deleted);
	range->is_deleted = true;
	range->signature = signature;
	struct vy_run_recovery_info *run;
	rlist_foreach_entry(run, &range->runs, in_range) {
		if (!run->is_deleted)
			vy_run_mark_deleted(run, signature);
	}
}

/** Mark a vinyl index, all its ranges, and all its runs as deleted. */
static void
vy_index_mark_deleted(struct vy_index_recovery_info *index, int64_t signature)
{
	assert(!index->is_dropped);
	index->is_dropped = true;
	index->signature = signature;
	struct vy_range_recovery_info *range;
	rlist_foreach_entry(range, &index->ranges, in_index) {
		if (!range->is_deleted)
			vy_range_mark_deleted(range, signature);
	}
	struct vy_run_recovery_info *run;
	rlist_foreach_entry(run, &index->incomplete_runs, in_incomplete) {
		if (!run->is_deleted)
			vy_run_mark_deleted(run, signature);
	}
}

/** Lookup a vinyl index in vy_recovery::index_hash map. */
static struct vy_index_recovery_info *
vy_recovery_lookup_index(struct vy_recovery *recovery, int64_t index_id)
{
	struct mh_i64ptr_t *h = recovery->index_hash;
	mh_int_t k = mh_i64ptr_find(h, index_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup a vinyl range in vy_recovery::range_hash map. */
static struct vy_range_recovery_info *
vy_recovery_lookup_range(struct vy_recovery *recovery, int64_t range_id)
{
	struct mh_i64ptr_t *h = recovery->range_hash;
	mh_int_t k = mh_i64ptr_find(h, range_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup a vinyl run in vy_recovery::run_hash map. */
static struct vy_run_recovery_info *
vy_recovery_lookup_run(struct vy_recovery *recovery, int64_t run_id)
{
	struct mh_i64ptr_t *h = recovery->run_hash;
	mh_int_t k = mh_i64ptr_find(h, run_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/**
 * Handle a VY_LOG_CREATE_INDEX log record.
 * This function allocates a new vinyl index with ID @index_id
 * and inserts it to the hash.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
vy_recovery_create_index(struct vy_recovery *recovery, int64_t signature,
			 int64_t index_id, uint32_t iid, uint32_t space_id,
			 const char *path, uint32_t path_len)
{
	if (vy_recovery_lookup_index(recovery, index_id) != NULL) {
		diag_set(ClientError, ER_VINYL, "duplicate vinyl index id");
		return -1;
	}
	struct vy_index_recovery_info *index = malloc(sizeof(*index) +
						      path_len + 1);
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(*index) + path_len + 1,
			 "malloc", "struct vy_index_recovery_info");
		return -1;
	}
	struct mh_i64ptr_t *h = recovery->index_hash;
	struct mh_i64ptr_node_t node = { index_id, index };
	if (mh_i64ptr_put(h, &node, NULL, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put", "mh_i64ptr_node_t");
		free(index);
		return -1;
	}
	index->id = index_id;
	index->iid = iid;
	index->space_id = space_id;
	index->path = (void *)index + sizeof(*index);
	memcpy(index->path, path, path_len);
	index->path[path_len] = '\0';
	index->is_dropped = false;
	index->signature = signature;
	rlist_create(&index->ranges);
	rlist_create(&index->incomplete_runs);
	return 0;
}

/**
 * Handle a VY_LOG_DROP_INDEX log record.
 * This function marks the vinyl index with ID @index_id as dropped.
 * If the index has no ranges and runs, it is freed.
 * Returns 0 on success, -1 if ID not found or index is already marked.
 */
static int
vy_recovery_drop_index(struct vy_recovery *recovery, int64_t signature,
		       int64_t index_id)
{
	struct mh_i64ptr_t *h = recovery->index_hash;
	mh_int_t k = mh_i64ptr_find(h, index_id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_VINYL, "unknown index id");
		return -1;
	}
	struct vy_index_recovery_info *index = mh_i64ptr_node(h, k)->val;
	if (index->is_dropped) {
		diag_set(ClientError, ER_VINYL, "index is already dropped");
		return -1;
	}
	vy_index_mark_deleted(index, signature);
	if (rlist_empty(&index->ranges) &&
	    rlist_empty(&index->incomplete_runs)) {
		mh_i64ptr_del(h, k, NULL);
		free(index);
	}
	return 0;
}

/**
 * Allocate a vinyl run with ID @run_id and insert it to the hash.
 * Return the new run on success, NULL on OOM.
 */
static struct vy_run_recovery_info *
vy_recovery_create_run(struct vy_recovery *recovery, int64_t run_id)
{
	struct vy_run_recovery_info *run = malloc(sizeof(*run));
	if (run == NULL) {
		diag_set(OutOfMemory, sizeof(*run),
			 "malloc", "struct vy_run_recovery_info");
		return NULL;
	}
	struct mh_i64ptr_t *h = recovery->run_hash;
	struct mh_i64ptr_node_t node = { run_id, run };
	struct mh_i64ptr_node_t *old_node = NULL;
	if (mh_i64ptr_put(h, &node, &old_node, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put", "mh_i64ptr_node_t");
		free(run);
		return NULL;
	}
	assert(old_node == NULL);
	run->id = run_id;
	run->is_deleted = false;
	run->signature = -1;
	rlist_create(&run->in_range);
	rlist_create(&run->in_incomplete);
	if (recovery->run_id_max < run_id)
		recovery->run_id_max = run_id;
	return run;
}

/**
 * Handle a VY_LOG_PREPARE_RUN log record.
 * This function creates a new vinyl run with ID @run_id and adds it
 * to the list of incomplete runs of the index with ID @index_id.
 * Return 0 on success, -1 if run already exists, index not found,
 * or OOM.
 */
static int
vy_recovery_prepare_run(struct vy_recovery *recovery, int64_t signature,
			int64_t index_id, int64_t run_id)
{
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index(recovery, index_id);
	if (index == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown vinyl index id");
		return -1;
	}
	if (vy_recovery_lookup_run(recovery, run_id) != NULL) {
		diag_set(ClientError, ER_VINYL, "duplicate vinyl run id");
		return -1;
	}
	struct vy_run_recovery_info *run;
	run = vy_recovery_create_run(recovery, run_id);
	if (run == NULL)
		return -1;
	run->signature = signature;
	rlist_add_entry(&index->incomplete_runs, run, in_incomplete);
	return 0;
}

/**
 * Handle a VY_LOG_INSERT_RUN log record.
 * This function inserts the vinyl run with ID @run_id to the range
 * with ID @range_id. If the run does not exist, it will be created.
 * Return 0 on success, -1 if range not found, run or range is
 * deleted, or OOM.
 */
static int
vy_recovery_insert_run(struct vy_recovery *recovery, int64_t signature,
		       int64_t range_id, int64_t run_id)
{
	struct vy_range_recovery_info *range;
	range = vy_recovery_lookup_range(recovery, range_id);
	if (range == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown vinyl range id");
		return -1;
	}
	if (range->is_deleted) {
		diag_set(ClientError, ER_VINYL, "vinyl range is deleted");
		return -1;
	}
	struct vy_run_recovery_info *run;
	run = vy_recovery_lookup_run(recovery, run_id);
	if (run != NULL && run->is_deleted) {
		diag_set(ClientError, ER_VINYL, "vinyl run is deleted");
		return -1;
	}
	if (run == NULL) {
		run = vy_recovery_create_run(recovery, run_id);
		if (run == NULL)
			return -1;
	}
	run->signature = signature;
	rlist_del_entry(run, in_incomplete);
	rlist_move_entry(&range->runs, run, in_range);
	return 0;
}

/**
 * Handle a VY_LOG_DELETE_RUN log record.
 * This function marks the vinyl run with ID @run_id as deleted.
 * Note, the run is not removed from the recovery context until it is
 * "forgotten", because it is still needed for garbage collection.
 * Return 0 on success, -1 if run not found or already deleted.
 */
static int
vy_recovery_delete_run(struct vy_recovery *recovery, int64_t signature,
		       int64_t run_id)
{
	struct vy_run_recovery_info *run;
	run = vy_recovery_lookup_run(recovery, run_id);
	if (run == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown vinyl run id");
		return -1;
	}
	if (run->is_deleted) {
		diag_set(ClientError, ER_VINYL, "vinyl run is already deleted");
		return -1;
	}
	vy_run_mark_deleted(run, signature);
	return 0;
}

/**
 * Handle a VY_LOG_FORGET_RUN log record.
 * This function frees the vinyl run with ID @run_id.
 * Return 0 on success, -1 if run not found.
 */
static int
vy_recovery_forget_run(struct vy_recovery *recovery, int64_t run_id)
{
	struct mh_i64ptr_t *h = recovery->run_hash;
	mh_int_t k = mh_i64ptr_find(h, run_id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_VINYL, "unknown vinyl run id");
		return -1;
	}
	struct vy_run_recovery_info *run = mh_i64ptr_node(h, k)->val;
	mh_i64ptr_del(h, k, NULL);
	rlist_del_entry(run, in_range);
	rlist_del_entry(run, in_incomplete);
	free(run);
	return 0;
}

/**
 * Handle a VY_LOG_INSERT_RANGE log record.
 * This function allocates a new vinyl range with ID @range_id,
 * inserts it to the hash, and adds it to the list of ranges of the
 * index with ID @index_id.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
vy_recovery_insert_range(struct vy_recovery *recovery, int64_t signature,
			 int64_t index_id, int64_t range_id,
			 const char *begin, const char *end)
{
	if (vy_recovery_lookup_range(recovery, range_id) != NULL) {
		diag_set(ClientError, ER_VINYL, "duplicate vinyl range id");
		return -1;
	}
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index(recovery, index_id);
	if (index == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown vinyl index id");
		return -1;
	}

	size_t size = sizeof(struct vy_range_recovery_info);
	const char *data;
	data = begin;
	mp_next(&data);
	size_t begin_size = data - begin;
	size += begin_size;
	data = end;
	mp_next(&data);
	size_t end_size = data - end;
	size += end_size;

	struct vy_range_recovery_info *range = malloc(size);
	if (range == NULL) {
		diag_set(OutOfMemory, size,
			 "malloc", "struct vy_range_recovery_info");
		return -1;
	}
	struct mh_i64ptr_t *h = recovery->range_hash;
	struct mh_i64ptr_node_t node = { range_id, range };
	if (mh_i64ptr_put(h, &node, NULL, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put", "mh_i64ptr_node_t");
		free(range);
		return -1;
	}
	range->id = range_id;
	range->begin = (void *)range + sizeof(*range);
	memcpy(range->begin, begin, begin_size);
	range->end = (void *)range + sizeof(*range) + begin_size;
	memcpy(range->end, end, end_size);
	range->is_deleted = false;
	range->signature = signature;
	rlist_create(&range->runs);
	rlist_add_entry(&index->ranges, range, in_index);
	if (recovery->range_id_max < range_id)
		recovery->range_id_max = range_id;
	return 0;
}

/**
 * Handle a VY_LOG_DELETE_RANGE log record.
 * This function marks the range with ID @range_id as deleted.
 * If the range has no runs, it is freed.
 * Return 0 on success, -1 if range not found or already deleted.
 */
static int
vy_recovery_delete_range(struct vy_recovery *recovery, int64_t signature,
			 int64_t range_id)
{
	struct mh_i64ptr_t *h = recovery->range_hash;
	mh_int_t k = mh_i64ptr_find(h, range_id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_VINYL, "unknown vinyl range id");
		return -1;
	}
	struct vy_range_recovery_info *range = mh_i64ptr_node(h, k)->val;
	if (range->is_deleted) {
		diag_set(ClientError, ER_VINYL,
			 "vinyl range is already deleted");
		return -1;
	}
	vy_range_mark_deleted(range, signature);
	if (rlist_empty(&range->runs)) {
		mh_i64ptr_del(h, k, NULL);
		rlist_del_entry(range, in_index);
		free(range);
	}
	return 0;
}

/**
 * Update a recovery context with a new log record.
 * Return 0 on success, -1 on failure.
 *
 * The purpose of this function is to restore the latest consistent
 * view of the system by replaying the metadata log.
 */
static int
vy_recovery_process_record(struct vy_recovery *recovery,
			   const struct vy_log_record *record)
{
	say_debug("%s: %s", __func__, vy_log_record_str(record));

	int rc;
	switch (record->type) {
	case VY_LOG_CREATE_INDEX:
		rc = vy_recovery_create_index(recovery,
				record->signature, record->index_id,
				record->iid, record->space_id,
				record->path, record->path_len);
		break;
	case VY_LOG_DROP_INDEX:
		rc = vy_recovery_drop_index(recovery, record->signature,
					    record->index_id);
		break;
	case VY_LOG_INSERT_RANGE:
		rc = vy_recovery_insert_range(recovery, record->signature,
				record->index_id, record->range_id,
				record->range_begin, record->range_end);
		break;
	case VY_LOG_DELETE_RANGE:
		rc = vy_recovery_delete_range(recovery, record->signature,
					      record->range_id);
		break;
	case VY_LOG_PREPARE_RUN:
		rc = vy_recovery_prepare_run(recovery, record->signature,
				record->index_id, record->run_id);
		break;
	case VY_LOG_INSERT_RUN:
		rc = vy_recovery_insert_run(recovery, record->signature,
				record->range_id, record->run_id);
		break;
	case VY_LOG_DELETE_RUN:
		rc = vy_recovery_delete_run(recovery, record->signature,
					    record->run_id);
		break;
	case VY_LOG_FORGET_RUN:
		rc = vy_recovery_forget_run(recovery, record->run_id);
		break;
	default:
		unreachable();
	}
	return rc;
}

static ssize_t
vy_recovery_new_f(va_list ap)
{
	int64_t recovery_signature = va_arg(ap, int64_t);
	struct vy_recovery **p_recovery = va_arg(ap, struct vy_recovery **);

	struct vy_recovery *recovery = malloc(sizeof(*recovery));
	if (recovery == NULL) {
		diag_set(OutOfMemory, sizeof(*recovery),
			 "malloc", "struct vy_recovery");
		goto fail;
	}

	recovery->index_hash = NULL;
	recovery->range_hash = NULL;
	recovery->run_hash = NULL;
	recovery->range_id_max = -1;
	recovery->run_id_max = -1;

	recovery->index_hash = mh_i64ptr_new();
	recovery->range_hash = mh_i64ptr_new();
	recovery->run_hash = mh_i64ptr_new();
	if (recovery->index_hash == NULL ||
	    recovery->range_hash == NULL ||
	    recovery->run_hash == NULL) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_new", "mh_i64ptr_t");
		goto fail_free;
	}

	struct vclock vclock;
	if (xdir_last_vclock(&vy_log.dir, &vclock) < 0) {
		/* No log file, nothing to do. */
		goto out;
	}

	struct xlog_cursor cursor;
	if (xdir_open_cursor(&vy_log.dir, vclock_sum(&vclock), &cursor) < 0)
		goto fail_free;

	int rc;
	struct xrow_header row;
	while ((rc = xlog_cursor_next(&cursor, &row, true)) == 0) {
		struct vy_log_record record;
		rc = vy_log_record_decode(&record, &row);
		if (rc < 0)
			break;
		if (record.signature >= recovery_signature)
			continue;
		rc = vy_recovery_process_record(recovery, &record);
		if (rc < 0)
			break;
	}
	if (rc < 0)
		goto fail_close;

	xlog_cursor_close(&cursor, false);
out:
	*p_recovery = recovery;
	return 0;

fail_close:
	xlog_cursor_close(&cursor, false);
fail_free:
	vy_recovery_delete(recovery);
fail:
	return -1;
}

/**
 * Load records having signatures < @recovery_signature from
 * the metadata log and return the recovery context.
 *
 * Returns NULL on failure.
 */
static struct vy_recovery *
vy_recovery_new(int64_t recovery_signature)
{
	int rc;
	struct vy_recovery *recovery;

	/* Lock out concurrent writers while we are loading the log. */
	latch_lock(&vy_log.latch);
	/*
	 * Before proceeding to log recovery, make sure that all
	 * pending records have been flushed out.
	 */
	rc = vy_log_flush();
	if (rc != 0)
		goto out;

	/* Load the log from coeio so as not to stall tx thread. */
	rc = coio_call(vy_recovery_new_f, recovery_signature, &recovery);
out:
	latch_unlock(&vy_log.latch);
	return rc == 0 ? recovery : NULL;
}

/** Helper to delete mh_i64ptr_t along with all its records. */
static void
vy_recovery_delete_hash(struct mh_i64ptr_t *h)
{
	mh_int_t i;
	mh_foreach(h, i)
		free(mh_i64ptr_node(h, i)->val);
	mh_i64ptr_delete(h);
}

/** Free recovery context created by vy_recovery_new(). */
static void
vy_recovery_delete(struct vy_recovery *recovery)
{
	if (recovery->index_hash != NULL)
		vy_recovery_delete_hash(recovery->index_hash);
	if (recovery->range_hash != NULL)
		vy_recovery_delete_hash(recovery->range_hash);
	if (recovery->run_hash != NULL)
		vy_recovery_delete_hash(recovery->run_hash);
	TRASH(recovery);
	free(recovery);
}

/** Helper to call a recovery callback and log the event if debugging. */
static int
vy_recovery_cb_call(vy_recovery_cb cb, void *cb_arg,
		    const struct vy_log_record *record)
{
	say_debug("%s: %s", __func__, vy_log_record_str(record));
	return cb(record, cb_arg);
}

/**
 * Call function @cb for each range and run of the given index until
 * it returns != 0 or all objects are iterated. Runs of a particular
 * range are iterated right after the range, in the chronological
 * order. If @include_deleted is set, this function will also iterate
 * over deleted objects, issuing the corresponding "delete" record for
 * each of them.
 */
static int
vy_recovery_iterate_index(struct vy_index_recovery_info *index,
		bool include_deleted, vy_recovery_cb cb, void *cb_arg)
{
	struct vy_range_recovery_info *range;
	struct vy_run_recovery_info *run;
	struct vy_log_record record;
	const char *tmp;

	record.type = VY_LOG_CREATE_INDEX;
	record.signature = index->signature;
	record.index_id = index->id;
	record.iid = index->iid;
	record.space_id = index->space_id;
	record.path = index->path;
	record.path_len = strlen(index->path);

	if (vy_recovery_cb_call(cb, cb_arg, &record) != 0)
		return -1;

	if (!include_deleted && index->is_dropped) {
		/*
		 * Do not load the index as it is going to be
		 * dropped on WAL recovery anyway. Just create
		 * an initial range to make vy_get() happy.
		 */
		record.type = VY_LOG_INSERT_RANGE;
		record.range_id = INT64_MAX; /* fake id */
		record.range_begin = record.range_end = NULL;
		if (vy_recovery_cb_call(cb, cb_arg, &record) != 0)
			return -1;
		record.type = VY_LOG_DROP_INDEX;
		if (vy_recovery_cb_call(cb, cb_arg, &record) != 0)
			return -1;
		return 0;
	}

	rlist_foreach_entry(range, &index->ranges, in_index) {
		if (!include_deleted && range->is_deleted)
			continue;
		record.type = VY_LOG_INSERT_RANGE;
		record.signature = range->signature;
		record.range_id = range->id;
		record.range_begin = tmp = range->begin;
		if (mp_decode_array(&tmp) == 0)
			record.range_begin = NULL;
		record.range_end = tmp = range->end;
		if (mp_decode_array(&tmp) == 0)
			record.range_end = NULL;
		if (vy_recovery_cb_call(cb, cb_arg, &record) != 0)
			return -1;
		/*
		 * Newer runs are stored closer to the head of the list,
		 * while we are supposed to return runs in chronological
		 * order, so use reverse iterator.
		 */
		rlist_foreach_entry_reverse(run, &range->runs, in_range) {
			if (!include_deleted && run->is_deleted)
				continue;
			record.type = VY_LOG_INSERT_RUN;
			record.signature = run->signature;
			record.range_id = range->id;
			record.run_id = run->id;
			if (vy_recovery_cb_call(cb, cb_arg, &record) != 0)
				return -1;
			if (run->is_deleted) {
				record.type = VY_LOG_DELETE_RUN;
				if (vy_recovery_cb_call(cb, cb_arg,
							  &record) != 0)
					return -1;
			}
		}
		if (range->is_deleted) {
			record.type = VY_LOG_DELETE_RANGE;
			record.signature = range->signature;
			if (vy_recovery_cb_call(cb, cb_arg, &record) != 0)
				return -1;
		}
	}

	if (include_deleted) {
		rlist_foreach_entry(run, &index->incomplete_runs,
				    in_incomplete) {
			record.type = VY_LOG_PREPARE_RUN;
			record.signature = run->signature;
			record.run_id = run->id;
			if (vy_recovery_cb_call(cb, cb_arg, &record) != 0)
				return -1;
			if (index->is_dropped || run->is_deleted) {
				record.type = VY_LOG_DELETE_RUN;
				if (vy_recovery_cb_call(cb, cb_arg,
							  &record) != 0)
					return -1;
			}
		}
	}

	if (index->is_dropped) {
		record.type = VY_LOG_DROP_INDEX;
		record.signature = index->signature;
		if (vy_recovery_cb_call(cb, cb_arg, &record) != 0)
			return -1;
	}
	return 0;
}

/**
 * Given a recovery context, iterate over all indexes stored in it.
 * See vy_recovery_iterate_index() for more details.
 */
static int
vy_recovery_iterate(struct vy_recovery *recovery, bool include_deleted,
		      vy_recovery_cb cb, void *cb_arg)
{
	mh_int_t i;
	mh_foreach(recovery->index_hash, i) {
		struct vy_index_recovery_info *index;
		index = mh_i64ptr_node(recovery->index_hash, i)->val;
		if (vy_recovery_iterate_index(index, include_deleted,
						   cb, cb_arg) < 0)
			return -1;
	}
	return 0;
}
