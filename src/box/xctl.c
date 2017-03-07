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
#include "xctl.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
#include "say.h"
#include "trivia/util.h"
#include "wal.h"
#include "xlog.h"
#include "xrow.h"

/** File extension of a metadata log file. */
#define XCTL_SUFFIX			"xctl"

/**
 * Integer key of a field in the xctl_record structure.
 * Used for packing a record in MsgPack.
 */
enum xctl_key {
	XCTL_KEY_VY_INDEX_ID		= 0,
	XCTL_KEY_VY_RANGE_ID		= 1,
	XCTL_KEY_VY_RUN_ID		= 2,
	XCTL_KEY_VY_RANGE_BEGIN		= 3,
	XCTL_KEY_VY_RANGE_END		= 4,
	XCTL_KEY_IID			= 5,
	XCTL_KEY_SPACE_ID		= 6,
	XCTL_KEY_PATH			= 7,
};

/**
 * Bit mask of keys that must be present in a record
 * of a particular type.
 */
static const unsigned long xctl_key_mask[] = {
	[XCTL_CREATE_VY_INDEX]		= (1 << XCTL_KEY_VY_INDEX_ID) |
					  (1 << XCTL_KEY_IID) |
					  (1 << XCTL_KEY_SPACE_ID) |
					  (1 << XCTL_KEY_PATH),
	[XCTL_DROP_VY_INDEX]		= (1 << XCTL_KEY_VY_INDEX_ID),
	[XCTL_INSERT_VY_RANGE]		= (1 << XCTL_KEY_VY_INDEX_ID) |
					  (1 << XCTL_KEY_VY_RANGE_ID) |
					  (1 << XCTL_KEY_VY_RANGE_BEGIN) |
					  (1 << XCTL_KEY_VY_RANGE_END),
	[XCTL_DELETE_VY_RANGE]		= (1 << XCTL_KEY_VY_RANGE_ID),
	[XCTL_PREPARE_VY_RUN]		= (1 << XCTL_KEY_VY_INDEX_ID) |
					  (1 << XCTL_KEY_VY_RUN_ID),
	[XCTL_INSERT_VY_RUN]		= (1 << XCTL_KEY_VY_RANGE_ID) |
					  (1 << XCTL_KEY_VY_RUN_ID),
	[XCTL_DELETE_VY_RUN]		= (1 << XCTL_KEY_VY_RUN_ID),
	[XCTL_FORGET_VY_RUN]		= (1 << XCTL_KEY_VY_RUN_ID),
};

/** xctl_key -> human readable name. */
static const char *xctl_key_name[] = {
	[XCTL_KEY_VY_INDEX_ID]		= "vy_index_id",
	[XCTL_KEY_VY_RANGE_ID]		= "vy_range_id",
	[XCTL_KEY_VY_RUN_ID]		= "vy_run_id",
	[XCTL_KEY_VY_RANGE_BEGIN]	= "vy_range_begin",
	[XCTL_KEY_VY_RANGE_END]		= "vy_range_end",
	[XCTL_KEY_IID]			= "iid",
	[XCTL_KEY_SPACE_ID]		= "space_id",
	[XCTL_KEY_PATH]			= "path",
};

/** xctl_type -> human readable name. */
static const char *xctl_type_name[] = {
	[XCTL_CREATE_VY_INDEX]		= "create_vy_index",
	[XCTL_DROP_VY_INDEX]		= "drop_vy_index",
	[XCTL_INSERT_VY_RANGE]		= "insert_vy_range",
	[XCTL_DELETE_VY_RANGE]		= "delete_vy_range",
	[XCTL_PREPARE_VY_RUN]		= "prepare_vy_run",
	[XCTL_INSERT_VY_RUN]		= "insert_vy_run",
	[XCTL_DELETE_VY_RUN]		= "delete_vy_run",
	[XCTL_FORGET_VY_RUN]		= "forget_vy_run",
};

struct xctl_recovery;

/**
 * Max number of records in the log buffer.
 * This limits the size of a transaction.
 */
enum { XCTL_TX_BUF_SIZE = 64 };

/** Metadata log object. */
struct xctl {
	/** The directory where log files are stored. */
	char log_dir[PATH_MAX];
	/** The vinyl directory. Used for garbage collection. */
	char vinyl_dir[PATH_MAX];
	/** Vector clock sum from the time of the log creation. */
	int64_t signature;
	/** Recovery context. */
	struct xctl_recovery *recovery;
	/** Latch protecting the log buffer. */
	struct latch latch;
	/**
	 * Next ID to use for a vinyl range.
	 * Used by xctl_next_vy_range_id().
	 */
	int64_t next_vy_range_id;
	/**
	 * Next ID to use for a vinyl run.
	 * Used by xctl_next_vy_run_id().
	 */
	int64_t next_vy_run_id;
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
	struct xctl_record tx_buf[XCTL_TX_BUF_SIZE];
};
static struct xctl xctl;

/** Recovery context. */
struct xctl_recovery {
	/** ID -> xctl_vy_index_recovery_info. */
	struct mh_i64ptr_t *vy_index_hash;
	/** ID -> xctl_vy_range_recovery_info. */
	struct mh_i64ptr_t *vy_range_hash;
	/** ID -> xctl_vy_run_recovery_info. */
	struct mh_i64ptr_t *vy_run_hash;
	/**
	 * Maximal vinyl range ID, according to the metadata log,
	 * or -1 in case no ranges were recovered.
	 */
	int64_t vy_range_id_max;
	/**
	 * Maximal vinyl run ID, according to the metadata log,
	 * or -1 in case no runs were recovered.
	 */
	int64_t vy_run_id_max;
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

static struct xctl_recovery *
xctl_recovery_new(int64_t log_signature, int64_t recovery_signature);
static void
xctl_recovery_delete(struct xctl_recovery *recovery);
static struct vy_index_recovery_info *
xctl_recovery_lookup_vy_index(struct xctl_recovery *recovery,
			      int64_t vy_index_id);
static int
xctl_recovery_iterate_vy_index(struct vy_index_recovery_info *index,
		bool include_deleted, xctl_recovery_cb cb, void *cb_arg);
static int
xctl_recovery_iterate(struct xctl_recovery *recovery, bool include_deleted,
		      xctl_recovery_cb cb, void *cb_arg);

/** An snprint-style function to print a path to a metadata log file. */
static int
xctl_snprint_path(char *buf, size_t size, int64_t signature)
{
	return snprintf(buf, size, "%s/%020lld.%s",
			xctl.log_dir, (long long)signature, XCTL_SUFFIX);
}

const char *
xctl_path(void)
{
	char *filename = tt_static_buf();
	xctl_snprint_path(filename, TT_STATIC_BUF_LEN, xctl.signature);
	return filename;
}

/** Check if an xlog meta belongs to a metadata log file. */
static int
xctl_type_check(struct xlog_meta *meta)
{
	if (strcmp(meta->filetype, XCTL_TYPE) != 0) {
		diag_set(ClientError, ER_INVALID_XLOG_TYPE,
			 XCTL_TYPE, meta->filetype);
		return -1;
	}
	return 0;
}

/** An snprint-style function to print a log record. */
static int
xctl_record_snprint(char *buf, int size, const struct xctl_record *record)
{
	int total = 0;
	assert(record->type < xctl_record_type_MAX);
	unsigned long key_mask = xctl_key_mask[record->type];
	SNPRINT(total, snprintf, buf, size, "%s{",
		xctl_type_name[record->type]);
	SNPRINT(total, snprintf, buf, size,
		"signature=%"PRIi64", ", record->signature);
	if (key_mask & (1 << XCTL_KEY_VY_INDEX_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			xctl_key_name[XCTL_KEY_VY_INDEX_ID],
			record->vy_index_id);
	if (key_mask & (1 << XCTL_KEY_VY_RANGE_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			xctl_key_name[XCTL_KEY_VY_RANGE_ID],
			record->vy_range_id);
	if (key_mask & (1 << XCTL_KEY_VY_RUN_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			xctl_key_name[XCTL_KEY_VY_RUN_ID],
			record->vy_run_id);
	if (key_mask & (1 << XCTL_KEY_VY_RANGE_BEGIN)) {
		SNPRINT(total, snprintf, buf, size, "%s=",
			xctl_key_name[XCTL_KEY_VY_RANGE_BEGIN]);
		if (record->vy_range_begin != NULL)
			SNPRINT(total, mp_snprint, buf, size,
				record->vy_range_begin);
		else
			SNPRINT(total, snprintf, buf, size, "[]");
		SNPRINT(total, snprintf, buf, size, ", ");
	}
	if (key_mask & (1 << XCTL_KEY_VY_RANGE_END)) {
		SNPRINT(total, snprintf, buf, size, "%s=",
			xctl_key_name[XCTL_KEY_VY_RANGE_END]);
		if (record->vy_range_end != NULL)
			SNPRINT(total, mp_snprint, buf, size,
				record->vy_range_end);
		else
			SNPRINT(total, snprintf, buf, size, "[]");
		SNPRINT(total, snprintf, buf, size, ", ");
	}
	if (key_mask & (1 << XCTL_KEY_IID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIu32", ",
			xctl_key_name[XCTL_KEY_IID], record->iid);
	if (key_mask & (1 << XCTL_KEY_SPACE_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIu32", ",
			xctl_key_name[XCTL_KEY_SPACE_ID], record->space_id);
	if (key_mask & (1 << XCTL_KEY_PATH))
		SNPRINT(total, snprintf, buf, size, "%s=%.*s, ",
			xctl_key_name[XCTL_KEY_PATH],
			record->path_len, record->path);
	SNPRINT(total, snprintf, buf, size, "}");
	return total;
}

/**
 * Return a string containing a human readable representation
 * of a log record.
 */
static const char *
xctl_record_str(const struct xctl_record *record)
{
	char *buf = tt_static_buf();
	if (xctl_record_snprint(buf, TT_STATIC_BUF_LEN, record) < 0)
		return "<failed to format xctl log record>";
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
 * 'type': see xctl_record_type enum
 * 'key': see xctl_key enum
 * 'value': depends on 'key'
 */
static int
xctl_record_encode(const struct xctl_record *record,
		   struct xrow_header *row)
{
	assert(record->type < xctl_record_type_MAX);
	unsigned long key_mask = xctl_key_mask[record->type];

	/*
	 * Calculate record size.
	 */
	size_t size = 0;
	size += mp_sizeof_array(2);
	size += mp_sizeof_uint(record->type);
	size_t n_keys = 0;
	if (key_mask & (1 << XCTL_KEY_VY_INDEX_ID)) {
		assert(record->vy_index_id >= 0);
		size += mp_sizeof_uint(XCTL_KEY_VY_INDEX_ID);
		size += mp_sizeof_uint(record->vy_index_id);
		n_keys++;
	}
	if (key_mask & (1 << XCTL_KEY_VY_RANGE_ID)) {
		assert(record->vy_range_id >= 0);
		size += mp_sizeof_uint(XCTL_KEY_VY_RANGE_ID);
		size += mp_sizeof_uint(record->vy_range_id);
		n_keys++;
	}
	if (key_mask & (1 << XCTL_KEY_VY_RUN_ID)) {
		assert(record->vy_run_id >= 0);
		size += mp_sizeof_uint(XCTL_KEY_VY_RUN_ID);
		size += mp_sizeof_uint(record->vy_run_id);
		n_keys++;
	}
	if (key_mask & (1 << XCTL_KEY_VY_RANGE_BEGIN)) {
		size += mp_sizeof_uint(XCTL_KEY_VY_RANGE_BEGIN);
		if (record->vy_range_begin != NULL) {
			const char *p = record->vy_range_begin;
			assert(mp_typeof(*p) == MP_ARRAY);
			mp_next(&p);
			size += p - record->vy_range_begin;
		} else
			size += mp_sizeof_array(0);
		n_keys++;
	}
	if (key_mask & (1 << XCTL_KEY_VY_RANGE_END)) {
		size += mp_sizeof_uint(XCTL_KEY_VY_RANGE_END);
		if (record->vy_range_end != NULL) {
			const char *p = record->vy_range_end;
			assert(mp_typeof(*p) == MP_ARRAY);
			mp_next(&p);
			size += p - record->vy_range_end;
		} else
			size += mp_sizeof_array(0);
		n_keys++;
	}
	if (key_mask & (1 << XCTL_KEY_IID)) {
		size += mp_sizeof_uint(XCTL_KEY_IID);
		size += mp_sizeof_uint(record->iid);
		n_keys++;
	}
	if (key_mask & (1 << XCTL_KEY_SPACE_ID)) {
		size += mp_sizeof_uint(XCTL_KEY_SPACE_ID);
		size += mp_sizeof_uint(record->space_id);
		n_keys++;
	}
	if (key_mask & (1 << XCTL_KEY_PATH)) {
		size += mp_sizeof_uint(XCTL_KEY_PATH);
		size += mp_sizeof_str(record->path_len);
		n_keys++;
	}
	size += mp_sizeof_map(n_keys);

	/*
	 * Encode record.
	 */
	char *tuple = region_alloc(&fiber()->gc, size);
	if (tuple == NULL) {
		diag_set(OutOfMemory, size, "region", "xctl record");
		return -1;
	}
	char *pos = tuple;
	pos = mp_encode_array(pos, 2);
	pos = mp_encode_uint(pos, record->type);
	pos = mp_encode_map(pos, n_keys);
	if (key_mask & (1 << XCTL_KEY_VY_INDEX_ID)) {
		pos = mp_encode_uint(pos, XCTL_KEY_VY_INDEX_ID);
		pos = mp_encode_uint(pos, record->vy_index_id);
	}
	if (key_mask & (1 << XCTL_KEY_VY_RANGE_ID)) {
		pos = mp_encode_uint(pos, XCTL_KEY_VY_RANGE_ID);
		pos = mp_encode_uint(pos, record->vy_range_id);
	}
	if (key_mask & (1 << XCTL_KEY_VY_RUN_ID)) {
		pos = mp_encode_uint(pos, XCTL_KEY_VY_RUN_ID);
		pos = mp_encode_uint(pos, record->vy_run_id);
	}
	if (key_mask & (1 << XCTL_KEY_VY_RANGE_BEGIN)) {
		pos = mp_encode_uint(pos, XCTL_KEY_VY_RANGE_BEGIN);
		if (record->vy_range_begin != NULL) {
			const char *p = record->vy_range_begin;
			mp_next(&p);
			memcpy(pos, record->vy_range_begin,
			       p - record->vy_range_begin);
			pos += p - record->vy_range_begin;
		} else
			pos = mp_encode_array(pos, 0);
	}
	if (key_mask & (1 << XCTL_KEY_VY_RANGE_END)) {
		pos = mp_encode_uint(pos, XCTL_KEY_VY_RANGE_END);
		if (record->vy_range_end != NULL) {
			const char *p = record->vy_range_end;
			mp_next(&p);
			memcpy(pos, record->vy_range_end,
			       p - record->vy_range_end);
			pos += p - record->vy_range_end;
		} else
			pos = mp_encode_array(pos, 0);
	}
	if (key_mask & (1 << XCTL_KEY_IID)) {
		pos = mp_encode_uint(pos, XCTL_KEY_IID);
		pos = mp_encode_uint(pos, record->iid);
	}
	if (key_mask & (1 << XCTL_KEY_SPACE_ID)) {
		pos = mp_encode_uint(pos, XCTL_KEY_SPACE_ID);
		pos = mp_encode_uint(pos, record->space_id);
	}
	if (key_mask & (1 << XCTL_KEY_PATH)) {
		pos = mp_encode_uint(pos, XCTL_KEY_PATH);
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
xctl_record_decode(struct xctl_record *record,
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
	if (record->type >= xctl_record_type_MAX)
		goto fail;

	unsigned long key_mask = 0;
	uint32_t n_keys = mp_decode_map(&pos);
	for (uint32_t i = 0; i < n_keys; i++) {
		uint32_t key = mp_decode_uint(&pos);
		switch (key) {
		case XCTL_KEY_VY_INDEX_ID:
			record->vy_index_id = mp_decode_uint(&pos);
			break;
		case XCTL_KEY_VY_RANGE_ID:
			record->vy_range_id = mp_decode_uint(&pos);
			break;
		case XCTL_KEY_VY_RUN_ID:
			record->vy_run_id = mp_decode_uint(&pos);
			break;
		case XCTL_KEY_VY_RANGE_BEGIN:
			record->vy_range_begin = pos;
			mp_next(&pos);
			break;
		case XCTL_KEY_VY_RANGE_END:
			record->vy_range_end = pos;
			mp_next(&pos);
			break;
		case XCTL_KEY_IID:
			record->iid = mp_decode_uint(&pos);
			break;
		case XCTL_KEY_SPACE_ID:
			record->space_id = mp_decode_uint(&pos);
			break;
		case XCTL_KEY_PATH:
			record->path = mp_decode_str(&pos, &record->path_len);
			break;
		default:
			goto fail;
		}
		key_mask |= 1 << key;
	}
	if ((key_mask & xctl_key_mask[record->type]) !=
			xctl_key_mask[record->type])
		goto fail;
	return 0;
fail:
	buf = tt_static_buf();
	mp_snprint(buf, TT_STATIC_BUF_LEN, req.tuple);
	say_error("invalid record in metadata log: %s", buf);
	diag_set(ClientError, ER_VINYL, "invalid xctl record");
	return -1;
}

void
xctl_init(void)
{
	snprintf(xctl.log_dir, sizeof(xctl.log_dir), "%s",
		 cfg_gets("wal_dir"));
	snprintf(xctl.vinyl_dir, sizeof(xctl.vinyl_dir), "%s",
		 cfg_gets("vinyl_dir"));
	latch_create(&xctl.latch);
}

/**
 * Try to flush the log buffer to disk.
 *
 * We always flush the entire xctl buffer as a single xlog
 * transaction, since we do not track boundaries of @no_discard
 * buffered transactions, and want to avoid a partial write.
 */
static int
xctl_flush(void)
{
	if (xctl.tx_end == 0)
		return 0; /* nothing to do */

	struct wal_request *req;
	req = region_aligned_alloc(&fiber()->gc,
				   sizeof(struct wal_request) +
				   xctl.tx_end * sizeof(req->rows[0]),
				   alignof(struct wal_request));
	if (req == NULL)
		return -1;

	req->n_rows = 0;

	struct xrow_header *rows;
	rows = region_aligned_alloc(&fiber()->gc,
				    xctl.tx_end * sizeof(struct xrow_header),
				    alignof(struct xrow_header));
	if (rows == NULL)
		return -1;

	/*
	 * Encode buffered records.
	 */
	for (int i = 0; i < xctl.tx_end; i++) {
		struct xrow_header *row = &rows[req->n_rows];
		if (xctl_record_encode(&xctl.tx_buf[i], row) < 0)
			return -1;
		req->rows[req->n_rows++] = row;
	}
	/*
	 * Do actual disk writes on behalf of the WAL
	 * so as not to block the tx thread.
	 */
	if (wal_write_xctl(req) != 0)
		return -1;

	/* Success. Reset the buffer. */
	xctl.tx_end = 0;
	return 0;
}

void
xctl_free(void)
{
	if (xctl.recovery != NULL)
		xctl_recovery_delete(xctl.recovery);
	latch_destroy(&xctl.latch);
}

int64_t
xctl_next_vy_run_id(void)
{
	return xctl.next_vy_run_id++;
}

int64_t
xctl_next_vy_range_id(void)
{
	return xctl.next_vy_range_id++;
}

/**
 * Try to delete files of a vinyl run.
 * Return 0 on success, -1 on failure.
 */
static int
vy_run_unlink_files(const char *vinyl_dir, uint32_t space_id, uint32_t iid,
		    const char *index_path, int64_t run_id)
{
	static const char *suffix[] = { "index", "run" };

	ERROR_INJECT(ERRINJ_VY_GC,
		     {say_error("error injection: run %lld not deleted",
				(long long)run_id); return -1;});
	int rc = 0;
	char path[PATH_MAX];
	for (int type = 0; type < (int)nelem(suffix); type++) {
		/*
		 * TODO: File name formatting does not belong here.
		 * We should move it to a shared header and use it
		 * both in vinyl.c and here.
		 */
		if (index_path[0] != '\0') {
			snprintf(path, sizeof(path), "%s/%020lld.%s",
				 index_path, (long long)run_id, suffix[type]);
		} else {
			/* Default path. */
			snprintf(path, sizeof(path), "%s/%u/%u/%020lld.%s",
				 vinyl_dir, (unsigned)space_id, (unsigned)iid,
				 (long long)run_id, suffix[type]);
		}
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
xctl_vy_run_gc(const struct xctl_record *record)
{
	if (vy_run_unlink_files(xctl.vinyl_dir, record->space_id, record->iid,
				record->path, record->vy_run_id) == 0) {
		struct xctl_record gc_record = {
			.type = XCTL_FORGET_VY_RUN,
			.signature = record->signature,
			.vy_run_id = record->vy_run_id,
		};
		xctl_tx_begin();
		xctl_write(&gc_record);
		if (xctl_tx_commit() < 0) {
			say_warn("failed to log vinyl run %lld cleanup: %s",
				 (long long)record->vy_run_id,
				 diag_last_error(diag_get())->errmsg);
		}
	}
}

int
xctl_begin_recovery(int64_t signature)
{
	assert(xctl.recovery == NULL);

	struct xctl_recovery *recovery;
	recovery = xctl_recovery_new(signature, INT64_MAX);
	if (recovery == NULL)
		return -1;

	xctl.next_vy_range_id = recovery->vy_range_id_max + 1;
	xctl.next_vy_run_id = recovery->vy_run_id_max + 1;

	xctl.recovery = recovery;
	xctl.signature = signature;
	return 0;
}

/**
 * Callback passed to xctl_recovery_iterate() to remove files
 * left from incomplete vinyl runs.
 */
static int
xctl_incomplete_vy_run_gc(const struct xctl_record *record, void *cb_arg)
{
	(void)cb_arg;
	if (record->type == XCTL_PREPARE_VY_RUN)
		xctl_vy_run_gc(record);
	return 0;
}

int
xctl_end_recovery(void)
{
	assert(xctl.recovery != NULL);

	/* Flush all pending records. */
	if (xctl_flush() < 0)
		return -1;

	/*
	 * Reset xctl.recovery before getting to garbage collection
	 * so that xctl_commit() called by xctl_vy_run_gc() writes
	 * "forget" records to disk instead of accumulating them in
	 * the log buffer.
	 */
	struct xctl_recovery *recovery = xctl.recovery;
	xctl.recovery = NULL;

	/*
	 * If the instance is shut down while a dump/compaction task
	 * is in progress, we'll get an unfinished run file on disk,
	 * i.e. a run file which was either not written to the end
	 * or not inserted into a range. We need to delete such runs
	 * on recovery.
	 */
	xctl_recovery_iterate(recovery, true,
			      xctl_incomplete_vy_run_gc, NULL);
	xctl_recovery_delete(recovery);
	return 0;
}

int
xctl_recover_vy_index(int64_t vy_index_id,
		      xctl_recovery_cb cb, void *cb_arg)
{
	assert(xctl.recovery != NULL);
	struct vy_index_recovery_info *index;
	index = xctl_recovery_lookup_vy_index(xctl.recovery, vy_index_id);
	if (index == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown vinyl index id");
		return -1;
	}
	return xctl_recovery_iterate_vy_index(index, false, cb, cb_arg);
}

/** Argument passed to xctl_rotate_cb_func(). */
struct xctl_rotate_cb_arg {
	/** The xlog created during rotation. */
	struct xlog xlog;
	/** Set if the xlog was created. */
	bool xlog_is_open;
	/** Path to the xlog. */
	const char *xlog_path;
};

/** Callback passed to xctl_recovery_iterate() for log rotation. */
static int
xctl_rotate_cb_func(const struct xctl_record *record, void *cb_arg)
{
	struct xctl_rotate_cb_arg *arg = cb_arg;
	struct xrow_header row;

	/*
	 * Only create the new xlog if we have something to write
	 * so as not to pollute the filesystem with metadata logs
	 * if vinyl is not used.
	 */
	if (!arg->xlog_is_open) {
		struct xlog_meta meta = {
			.filetype = XCTL_TYPE,
		};
		if (xlog_create(&arg->xlog, arg->xlog_path, &meta) < 0)
			return -1;
		arg->xlog_is_open = true;
	}
	if (xctl_record_encode(record, &row) < 0 ||
	    xlog_write_row(&arg->xlog, &row) < 0)
		return -1;
	return 0;
}

/**
 * Callback passed to xctl_recovery_iterate() to remove files
 * left from deleted runs.
 */
static int
xctl_deleted_vy_run_gc(const struct xctl_record *record, void *cb_arg)
{
	(void)cb_arg;
	if (record->type == XCTL_DELETE_VY_RUN)
		xctl_vy_run_gc(record);
	return 0;
}

/**
 * This function does the actual log rotation work. It loads the
 * current log file to a xctl_recovery struct, creates a new xlog, and
 * then writes the records returned by xctl_recovery to the new xlog.
 */
static ssize_t
xctl_rotate_f(va_list ap)
{
	int64_t signature = va_arg(ap, int64_t);

	struct xctl_recovery *recovery;
	recovery = xctl_recovery_new(xctl.signature, INT64_MAX);
	if (recovery == NULL)
		goto err_recovery;

	char path[PATH_MAX];
	xctl_snprint_path(path, sizeof(path), signature);

	struct xctl_rotate_cb_arg arg = {
		.xlog_is_open = false,
		.xlog_path = path,
	};
	if (xctl_recovery_iterate(recovery, true,
				  xctl_rotate_cb_func, &arg) < 0)
		goto err_write_xlog;

	if (!arg.xlog_is_open)
		goto out; /* no records in the log */

	/* Finalize the new xlog. */
	if (xlog_flush(&arg.xlog) < 0 ||
	    xlog_sync(&arg.xlog) < 0 ||
	    xlog_rename(&arg.xlog) < 0)
		goto err_write_xlog;

	xlog_close(&arg.xlog, false);
out:
	xctl_recovery_delete(recovery);
	return 0;

err_write_xlog:
	if (arg.xlog_is_open) {
		/* Delete the unfinished xlog. */
		if (unlink(arg.xlog.filename) < 0)
			say_syserror("failed to delete file '%s'",
				     arg.xlog.filename);
		xlog_close(&arg.xlog, false);
	}
	xctl_recovery_delete(recovery);
err_recovery:
	return -1;
}

int
xctl_rotate(int64_t signature)
{
	assert(xctl.recovery == NULL);

	/*
	 * This function is called right after bootstrap (by snapshot),
	 * in which case old and new signatures coincide and there's
	 * nothing we need to do.
	 */
	assert(signature >= xctl.signature);
	if (signature == xctl.signature)
		return 0;

	say_debug("%s: signature %lld", __func__, (long long)signature);

	/*
	 * Lock out all concurrent log writers while we are rotating it.
	 * This effectively stalls the vinyl scheduler for a while, but
	 * this is acceptable, because (1) the log file is small and
	 * hence can be rotated fairly quickly so the stall isn't going
	 * to take too long and (2) dumps/compactions, which are scheduled
	 * by the scheduler, are rare events so there shouldn't be too
	 * many of them piling up due to log rotation.
	 */
	latch_lock(&xctl.latch);

	/*
	 * Before proceeding to log rotation, make sure that all
	 * pending records have been flushed out.
	 */
	if (xctl_flush() < 0)
		goto fail;

	/* Do actual work from coeio so as not to stall tx thread. */
	if (coio_call(xctl_rotate_f, signature) < 0)
		goto fail;

	/*
	 * Success. Close the old log. The new one will be opened
	 * automatically on the first write (see wal_write_xctl()).
	 */
	wal_rotate_xctl();
	xctl.signature = signature;

	latch_unlock(&xctl.latch);
	say_debug("%s: complete", __func__);
	return 0;

fail:
	latch_unlock(&xctl.latch);
	say_debug("%s: failed", __func__);
	say_error("failed to rotate metadata log: %s",
		  diag_last_error(diag_get())->errmsg);
	return -1;
}

static ssize_t
xctl_recovery_new_f(va_list ap)
{
	int64_t log_signature = va_arg(ap, int64_t);
	int64_t recovery_signature = va_arg(ap, int64_t);
	struct xctl_recovery **p_recovery = va_arg(ap, struct xctl_recovery **);

	struct xctl_recovery *recovery;
	recovery = xctl_recovery_new(log_signature, recovery_signature);
	if (recovery == NULL)
		return -1;
	*p_recovery = recovery;
	return 0;
}

void
xctl_collect_garbage(int64_t signature)
{
	say_debug("%s: signature %lld", __func__, (long long)signature);

	/* Lock out concurrent writers while we are loading the log. */
	latch_lock(&xctl.latch);
	/* Load the log from coeio so as not to stall tx thread. */
	struct xctl_recovery *recovery;
	int rc = coio_call(xctl_recovery_new_f, xctl.signature,
			   signature, &recovery);
	latch_unlock(&xctl.latch);

	if (rc == 0) {
		/* Cleanup unused runs. */
		xctl_recovery_iterate(recovery, true,
				      xctl_deleted_vy_run_gc, NULL);
		xctl_recovery_delete(recovery);
	} else {
		say_warn("garbage collection failed: %s",
			 diag_last_error(diag_get())->errmsg);
	}

	say_debug("%s: done", __func__);
}

/** Argument passed to xctl_relay_f(). */
struct xctl_relay_arg {
	/** The recovery context to relay. */
	struct xctl_recovery *recovery;
	/** The relay callback. */
	xctl_recovery_cb cb;
	/** The relay callback argument. */
	void *cb_arg;
};

/** Relay cord function. */
static int
xctl_relay_f(va_list ap)
{
	struct xctl_relay_arg *arg = va_arg(ap, struct xctl_relay_arg *);
	return xctl_recovery_iterate(arg->recovery, false,
				     arg->cb, arg->cb_arg);
}

int
xctl_relay(xctl_recovery_cb cb, void *cb_arg)
{
	/*
	 * First, load the latest snapshot of the metadata log.
	 * Use coeio in order not ot block tx thread.
	 */
	latch_lock(&xctl.latch);
	struct xctl_recovery *recovery;
	int rc = coio_call(xctl_recovery_new_f, xctl.signature,
			   xctl.signature, &recovery);
	latch_unlock(&xctl.latch);
	if (rc != 0)
		return -1;

	/*
	 * Second, relay the state stored in the log via
	 * the provided callback.
	 */
	struct xctl_relay_arg arg = {
		.recovery = recovery,
		.cb = cb,
		.cb_arg = cb_arg,
	};
	struct cord cord;
	if (cord_costart(&cord, "initial_join", xctl_relay_f, &arg) != 0) {
		xctl_recovery_delete(recovery);
		return -1;
	}
	if (cord_cojoin(&cord) != 0)
		return -1;

	return 0;
}

void
xctl_tx_begin(void)
{
	latch_lock(&xctl.latch);
	xctl.tx_begin = xctl.tx_end;
	say_debug("%s", __func__);
}

/**
 * Commit a transaction started with xctl_tx_begin().
 *
 * If @no_discard is set, pending records won't be expunged from the
 * buffer on failure, so that the next transaction will retry to write
 * them to disk.
 */
static int
xctl_tx_do_commit(bool no_discard)
{
	int rc = 0;

	assert(latch_owner(&xctl.latch) == fiber());
	/*
	 * During recovery, we may replay records we failed to commit
	 * before restart (e.g. drop index). Since the log isn't open
	 * yet, simply leave them in the tx buffer to be flushed upon
	 * recovery completion.
	 */
	if (xctl.recovery != NULL)
		goto out;

	rc = xctl_flush();
	/*
	 * Rollback the transaction on failure unless
	 * we were explicitly told not to.
	 */
	if (rc != 0 && !no_discard)
		xctl.tx_end = xctl.tx_begin;
out:
	say_debug("%s(no_discard=%d): %s", __func__, no_discard,
		  rc == 0 ? "success" : "fail");
	latch_unlock(&xctl.latch);
	return rc;
}

int
xctl_tx_commit(void)
{
	return xctl_tx_do_commit(false);
}

int
xctl_tx_try_commit(void)
{
	return xctl_tx_do_commit(true);
}

void
xctl_write(const struct xctl_record *record)
{
	assert(latch_owner(&xctl.latch) == fiber());

	say_debug("%s: %s", __func__, xctl_record_str(record));
	if (xctl.tx_end >= XCTL_TX_BUF_SIZE) {
		latch_unlock(&xctl.latch);
		panic("metadata log buffer overflow");
	}

	struct xctl_record *tx_record = &xctl.tx_buf[xctl.tx_end++];
	*tx_record = *record;
	if (tx_record->signature < 0)
		tx_record->signature = xctl.signature;
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

/** Lookup a vinyl index in xctl_recovery::vy_index_hash map. */
static struct vy_index_recovery_info *
xctl_recovery_lookup_vy_index(struct xctl_recovery *recovery,
			      int64_t vy_index_id)
{
	struct mh_i64ptr_t *h = recovery->vy_index_hash;
	mh_int_t k = mh_i64ptr_find(h, vy_index_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup a vinyl range in xctl_recovery::vy_range_hash map. */
static struct vy_range_recovery_info *
xctl_recovery_lookup_vy_range(struct xctl_recovery *recovery,
			      int64_t vy_range_id)
{
	struct mh_i64ptr_t *h = recovery->vy_range_hash;
	mh_int_t k = mh_i64ptr_find(h, vy_range_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup a vinyl run in xctl_recovery::vy_run_hash map. */
static struct vy_run_recovery_info *
xctl_recovery_lookup_vy_run(struct xctl_recovery *recovery,
			    int64_t vy_run_id)
{
	struct mh_i64ptr_t *h = recovery->vy_run_hash;
	mh_int_t k = mh_i64ptr_find(h, vy_run_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/**
 * Handle a XCTL_CREATE_VY_INDEX log record.
 * This function allocates a new vinyl index with ID @vy_index_id
 * and inserts it to the hash.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
xctl_recovery_create_vy_index(struct xctl_recovery *recovery,
			      int64_t signature, int64_t vy_index_id,
			      uint32_t iid, uint32_t space_id,
			      const char *path, uint32_t path_len)
{
	if (xctl_recovery_lookup_vy_index(recovery, vy_index_id) != NULL) {
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
	struct mh_i64ptr_t *h = recovery->vy_index_hash;
	struct mh_i64ptr_node_t node = { vy_index_id, index };
	if (mh_i64ptr_put(h, &node, NULL, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put", "mh_i64ptr_node_t");
		free(index);
		return -1;
	}
	index->id = vy_index_id;
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
 * Handle a XCTL_DROP_VY_INDEX log record.
 * This function marks the vinyl index with ID @vy_index_id as dropped.
 * If the index has no ranges and runs, it is freed.
 * Returns 0 on success, -1 if ID not found or index is already marked.
 */
static int
xctl_recovery_drop_vy_index(struct xctl_recovery *recovery,
			    int64_t signature, int64_t vy_index_id)
{
	struct mh_i64ptr_t *h = recovery->vy_index_hash;
	mh_int_t k = mh_i64ptr_find(h, vy_index_id, NULL);
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
 * Allocate a vinyl run with ID @vy_run_id and insert it to the hash.
 * Return the new run on success, NULL on OOM.
 */
static struct vy_run_recovery_info *
xctl_recovery_create_vy_run(struct xctl_recovery *recovery, int64_t vy_run_id)
{
	struct vy_run_recovery_info *run = malloc(sizeof(*run));
	if (run == NULL) {
		diag_set(OutOfMemory, sizeof(*run),
			 "malloc", "struct vy_run_recovery_info");
		return NULL;
	}
	struct mh_i64ptr_t *h = recovery->vy_run_hash;
	struct mh_i64ptr_node_t node = { vy_run_id, run };
	struct mh_i64ptr_node_t *old_node;
	if (mh_i64ptr_put(h, &node, &old_node, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put", "mh_i64ptr_node_t");
		free(run);
		return NULL;
	}
	assert(old_node == NULL);
	run->id = vy_run_id;
	run->is_deleted = false;
	run->signature = -1;
	rlist_create(&run->in_range);
	rlist_create(&run->in_incomplete);
	if (recovery->vy_run_id_max < vy_run_id)
		recovery->vy_run_id_max = vy_run_id;
	return run;
}

/**
 * Handle a XCTL_PREPARE_VY_RUN log record.
 * This function creates a new vinyl run with ID @vy_run_id and adds it
 * to the list of incomplete runs of the index with ID @vy_index_id.
 * Return 0 on success, -1 if run already exists, index not found,
 * or OOM.
 */
static int
xctl_recovery_prepare_vy_run(struct xctl_recovery *recovery,
		int64_t signature, int64_t vy_index_id, int64_t vy_run_id)
{
	struct vy_index_recovery_info *index;
	index = xctl_recovery_lookup_vy_index(recovery, vy_index_id);
	if (index == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown vinyl index id");
		return -1;
	}
	if (xctl_recovery_lookup_vy_run(recovery, vy_run_id) != NULL) {
		diag_set(ClientError, ER_VINYL, "duplicate vinyl run id");
		return -1;
	}
	struct vy_run_recovery_info *run;
	run = xctl_recovery_create_vy_run(recovery, vy_run_id);
	if (run == NULL)
		return -1;
	run->signature = signature;
	rlist_add_entry(&index->incomplete_runs, run, in_incomplete);
	return 0;
}

/**
 * Handle a XCTL_INSERT_VY_RUN log record.
 * This function inserts the vinyl run with ID @vy_run_id to the range
 * with ID @vy_range_id. If the run does not exist, it will be created.
 * Return 0 on success, -1 if range not found, run or range is
 * deleted, or OOM.
 */
static int
xctl_recovery_insert_vy_run(struct xctl_recovery *recovery,
		int64_t signature, int64_t vy_range_id, int64_t vy_run_id)
{
	struct vy_range_recovery_info *range;
	range = xctl_recovery_lookup_vy_range(recovery, vy_range_id);
	if (range == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown vinyl range id");
		return -1;
	}
	if (range->is_deleted) {
		diag_set(ClientError, ER_VINYL, "vinyl range is deleted");
		return -1;
	}
	struct vy_run_recovery_info *run;
	run = xctl_recovery_lookup_vy_run(recovery, vy_run_id);
	if (run != NULL && run->is_deleted) {
		diag_set(ClientError, ER_VINYL, "vinyl run is deleted");
		return -1;
	}
	if (run == NULL) {
		run = xctl_recovery_create_vy_run(recovery, vy_run_id);
		if (run == NULL)
			return -1;
	}
	run->signature = signature;
	rlist_del_entry(run, in_incomplete);
	rlist_move_entry(&range->runs, run, in_range);
	return 0;
}

/**
 * Handle a XCTL_DELETE_VY_RUN log record.
 * This function marks the vinyl run with ID @vy_run_id as deleted.
 * Note, the run is not removed from the recovery context until it is
 * "forgotten", because it is still needed for garbage collection.
 * Return 0 on success, -1 if run not found or already deleted.
 */
static int
xctl_recovery_delete_vy_run(struct xctl_recovery *recovery,
			    int64_t signature, int64_t vy_run_id)
{
	struct vy_run_recovery_info *run;
	run = xctl_recovery_lookup_vy_run(recovery, vy_run_id);
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
 * Handle a XCTL_FORGET_VY_RUN log record.
 * This function frees the vinyl run with ID @vy_run_id.
 * Return 0 on success, -1 if run not found.
 */
static int
xctl_recovery_forget_vy_run(struct xctl_recovery *recovery, int64_t vy_run_id)
{
	struct mh_i64ptr_t *h = recovery->vy_run_hash;
	mh_int_t k = mh_i64ptr_find(h, vy_run_id, NULL);
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
 * Handle a XCTL_INSERT_VY_RANGE log record.
 * This function allocates a new vinyl range with ID @vy_range_id,
 * inserts it to the hash, and adds it to the list of ranges of the
 * index with ID @vy_index_id.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
xctl_recovery_insert_vy_range(struct xctl_recovery *recovery,
		int64_t signature, int64_t vy_index_id, int64_t vy_range_id,
		const char *begin, const char *end)
{
	if (xctl_recovery_lookup_vy_range(recovery, vy_range_id) != NULL) {
		diag_set(ClientError, ER_VINYL, "duplicate vinyl range id");
		return -1;
	}
	struct vy_index_recovery_info *index;
	index = xctl_recovery_lookup_vy_index(recovery, vy_index_id);
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
	struct mh_i64ptr_t *h = recovery->vy_range_hash;
	struct mh_i64ptr_node_t node = { vy_range_id, range };
	if (mh_i64ptr_put(h, &node, NULL, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put", "mh_i64ptr_node_t");
		free(range);
		return -1;
	}
	range->id = vy_range_id;
	range->begin = (void *)range + sizeof(*range);
	memcpy(range->begin, begin, begin_size);
	range->end = (void *)range + sizeof(*range) + begin_size;
	memcpy(range->end, end, end_size);
	range->is_deleted = false;
	range->signature = signature;
	rlist_create(&range->runs);
	rlist_add_entry(&index->ranges, range, in_index);
	if (recovery->vy_range_id_max < vy_range_id)
		recovery->vy_range_id_max = vy_range_id;
	return 0;
}

/**
 * Handle a XCTL_DELETE_VY_RANGE log record.
 * This function marks the range with ID @vy_range_id as deleted.
 * If the range has no runs, it is freed.
 * Return 0 on success, -1 if range not found or already deleted.
 */
static int
xctl_recovery_delete_vy_range(struct xctl_recovery *recovery,
			      int64_t signature, int64_t vy_range_id)
{
	struct mh_i64ptr_t *h = recovery->vy_range_hash;
	mh_int_t k = mh_i64ptr_find(h, vy_range_id, NULL);
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
xctl_recovery_process_record(struct xctl_recovery *recovery,
			     const struct xctl_record *record)
{
	say_debug("%s: %s", __func__, xctl_record_str(record));

	int rc;
	switch (record->type) {
	case XCTL_CREATE_VY_INDEX:
		rc = xctl_recovery_create_vy_index(recovery,
				record->signature, record->vy_index_id,
				record->iid, record->space_id,
				record->path, record->path_len);
		break;
	case XCTL_DROP_VY_INDEX:
		rc = xctl_recovery_drop_vy_index(recovery, record->signature,
						 record->vy_index_id);
		break;
	case XCTL_INSERT_VY_RANGE:
		rc = xctl_recovery_insert_vy_range(recovery, record->signature,
				record->vy_index_id, record->vy_range_id,
				record->vy_range_begin, record->vy_range_end);
		break;
	case XCTL_DELETE_VY_RANGE:
		rc = xctl_recovery_delete_vy_range(recovery, record->signature,
						   record->vy_range_id);
		break;
	case XCTL_PREPARE_VY_RUN:
		rc = xctl_recovery_prepare_vy_run(recovery, record->signature,
				record->vy_index_id, record->vy_run_id);
		break;
	case XCTL_INSERT_VY_RUN:
		rc = xctl_recovery_insert_vy_run(recovery, record->signature,
				record->vy_range_id, record->vy_run_id);
		break;
	case XCTL_DELETE_VY_RUN:
		rc = xctl_recovery_delete_vy_run(recovery, record->signature,
						 record->vy_run_id);
		break;
	case XCTL_FORGET_VY_RUN:
		rc = xctl_recovery_forget_vy_run(recovery, record->vy_run_id);
		break;
	default:
		unreachable();
	}
	return rc;
}

/**
 * Load records having signatures < @recovery_signature from
 * the metadata log with signature @log_signature and return
 * the recovery context.
 *
 * Returns NULL on failure.
 */
static struct xctl_recovery *
xctl_recovery_new(int64_t log_signature, int64_t recovery_signature)
{
	struct xctl_recovery *recovery = malloc(sizeof(*recovery));
	if (recovery == NULL) {
		diag_set(OutOfMemory, sizeof(*recovery),
			 "malloc", "struct xctl_recovery");
		goto fail;
	}

	recovery->vy_index_hash = NULL;
	recovery->vy_range_hash = NULL;
	recovery->vy_run_hash = NULL;
	recovery->vy_range_id_max = -1;
	recovery->vy_run_id_max = -1;

	recovery->vy_index_hash = mh_i64ptr_new();
	recovery->vy_range_hash = mh_i64ptr_new();
	recovery->vy_run_hash = mh_i64ptr_new();
	if (recovery->vy_index_hash == NULL ||
	    recovery->vy_range_hash == NULL ||
	    recovery->vy_run_hash == NULL) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_new", "mh_i64ptr_t");
		goto fail_free;
	}

	char path[PATH_MAX];
	xctl_snprint_path(path, sizeof(path), log_signature);

	if (access(path, F_OK) < 0 && errno == ENOENT) {
		/* No log file, nothing to do. */
		goto out;
	}

	struct xlog_cursor cursor;
	if (xlog_cursor_open(&cursor, path) < 0)
		goto fail_free;
	if (xctl_type_check(&cursor.meta) < 0)
		goto fail_close;

	int rc;
	struct xrow_header row;
	while ((rc = xlog_cursor_next(&cursor, &row, true)) == 0) {
		struct xctl_record record;
		rc = xctl_record_decode(&record, &row);
		if (rc < 0)
			break;
		if (record.signature >= recovery_signature)
			continue;
		rc = xctl_recovery_process_record(recovery, &record);
		if (rc < 0)
			break;
	}
	if (rc < 0)
		goto fail_close;

	xlog_cursor_close(&cursor, false);
out:
	return recovery;

fail_close:
	xlog_cursor_close(&cursor, false);
fail_free:
	xctl_recovery_delete(recovery);
fail:
	return NULL;
}

/** Helper to delete mh_i64ptr_t along with all its records. */
static void
xctl_recovery_delete_hash(struct mh_i64ptr_t *h)
{
	mh_int_t i;
	mh_foreach(h, i)
		free(mh_i64ptr_node(h, i)->val);
	mh_i64ptr_delete(h);
}

/** Free recovery context created by xctl_recovery_new(). */
static void
xctl_recovery_delete(struct xctl_recovery *recovery)
{
	if (recovery->vy_index_hash != NULL)
		xctl_recovery_delete_hash(recovery->vy_index_hash);
	if (recovery->vy_range_hash != NULL)
		xctl_recovery_delete_hash(recovery->vy_range_hash);
	if (recovery->vy_run_hash != NULL)
		xctl_recovery_delete_hash(recovery->vy_run_hash);
	TRASH(recovery);
	free(recovery);
}

/** Helper to call a recovery callback and log the event if debugging. */
static int
xctl_recovery_cb_call(xctl_recovery_cb cb, void *cb_arg,
		      const struct xctl_record *record)
{
	say_debug("%s: %s", __func__, xctl_record_str(record));
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
xctl_recovery_iterate_vy_index(struct vy_index_recovery_info *index,
		bool include_deleted, xctl_recovery_cb cb, void *cb_arg)
{
	struct vy_range_recovery_info *range;
	struct vy_run_recovery_info *run;
	struct xctl_record record;
	const char *tmp;

	record.type = XCTL_CREATE_VY_INDEX;
	record.signature = index->signature;
	record.vy_index_id = index->id;
	record.iid = index->iid;
	record.space_id = index->space_id;
	record.path = index->path;
	record.path_len = strlen(index->path);

	if (xctl_recovery_cb_call(cb, cb_arg, &record) != 0)
		return -1;

	if (!include_deleted && index->is_dropped) {
		/*
		 * Do not load the index as it is going to be
		 * dropped on WAL recovery anyway. Just create
		 * an initial range to make vy_get() happy.
		 */
		record.type = XCTL_INSERT_VY_RANGE;
		record.vy_range_id = INT64_MAX; /* fake id */
		record.vy_range_begin = record.vy_range_end = NULL;
		if (xctl_recovery_cb_call(cb, cb_arg, &record) != 0)
			return -1;
		record.type = XCTL_DROP_VY_INDEX;
		if (xctl_recovery_cb_call(cb, cb_arg, &record) != 0)
			return -1;
		return 0;
	}

	rlist_foreach_entry(range, &index->ranges, in_index) {
		if (!include_deleted && range->is_deleted)
			continue;
		record.type = XCTL_INSERT_VY_RANGE;
		record.signature = range->signature;
		record.vy_range_id = range->id;
		record.vy_range_begin = tmp = range->begin;
		if (mp_decode_array(&tmp) == 0)
			record.vy_range_begin = NULL;
		record.vy_range_end = tmp = range->end;
		if (mp_decode_array(&tmp) == 0)
			record.vy_range_end = NULL;
		if (xctl_recovery_cb_call(cb, cb_arg, &record) != 0)
			return -1;
		/*
		 * Newer runs are stored closer to the head of the list,
		 * while we are supposed to return runs in chronological
		 * order, so use reverse iterator.
		 */
		rlist_foreach_entry_reverse(run, &range->runs, in_range) {
			if (!include_deleted && run->is_deleted)
				continue;
			record.type = XCTL_INSERT_VY_RUN;
			record.signature = run->signature;
			record.vy_range_id = range->id;
			record.vy_run_id = run->id;
			if (xctl_recovery_cb_call(cb, cb_arg, &record) != 0)
				return -1;
			if (run->is_deleted) {
				record.type = XCTL_DELETE_VY_RUN;
				if (xctl_recovery_cb_call(cb, cb_arg,
							  &record) != 0)
					return -1;
			}
		}
		if (range->is_deleted) {
			record.type = XCTL_DELETE_VY_RANGE;
			record.signature = range->signature;
			if (xctl_recovery_cb_call(cb, cb_arg, &record) != 0)
				return -1;
		}
	}

	if (include_deleted) {
		rlist_foreach_entry(run, &index->incomplete_runs,
				    in_incomplete) {
			record.type = XCTL_PREPARE_VY_RUN;
			record.signature = run->signature;
			record.vy_run_id = run->id;
			if (xctl_recovery_cb_call(cb, cb_arg, &record) != 0)
				return -1;
			if (index->is_dropped || run->is_deleted) {
				record.type = XCTL_DELETE_VY_RUN;
				if (xctl_recovery_cb_call(cb, cb_arg,
							  &record) != 0)
					return -1;
			}
		}
	}

	if (index->is_dropped) {
		record.type = XCTL_DROP_VY_INDEX;
		record.signature = index->signature;
		if (xctl_recovery_cb_call(cb, cb_arg, &record) != 0)
			return -1;
	}
	return 0;
}

/**
 * Given a recovery context, iterate over all indexes stored in it.
 * See xctl_recovery_iterate_vy_index() for more details.
 */
static int
xctl_recovery_iterate(struct xctl_recovery *recovery, bool include_deleted,
		      xctl_recovery_cb cb, void *cb_arg)
{
	mh_int_t i;
	mh_foreach(recovery->vy_index_hash, i) {
		struct vy_index_recovery_info *index;
		index = mh_i64ptr_node(recovery->vy_index_hash, i)->val;
		if (xctl_recovery_iterate_vy_index(index, include_deleted,
						   cb, cb_arg) < 0)
			return -1;
	}
	return 0;
}
