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
#include <fcntl.h>
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
#include "coeio.h"
#include "diag.h"
#include "errcode.h"
#include "fiber.h"
#include "iproto_constants.h" /* IPROTO_INSERT */
#include "latch.h"
#include "say.h"
#include "trivia/util.h"
#include "xlog.h"
#include "xrow.h"

/** Vinyl metadata log file name. */
#define VY_LOG_FILE			"vinyl.meta"
/** Xlog type of vinyl metadata log. */
#define VY_LOG_TYPE			"VYMETA"

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
};

/**
 * Bit mask of keys that must be present in a record
 * of a particular type.
 */
static const unsigned long vy_log_key_mask[] = {
	[VY_LOG_NEW_INDEX]		= (1 << VY_LOG_KEY_INDEX_ID),
	[VY_LOG_INSERT_RANGE]		= (1 << VY_LOG_KEY_INDEX_ID) |
					  (1 << VY_LOG_KEY_RANGE_ID) |
					  (1 << VY_LOG_KEY_RANGE_BEGIN) |
					  (1 << VY_LOG_KEY_RANGE_END),
	[VY_LOG_DELETE_RANGE]		= (1 << VY_LOG_KEY_RANGE_ID),
	[VY_LOG_INSERT_RUN]		= (1 << VY_LOG_KEY_RANGE_ID) |
					  (1 << VY_LOG_KEY_RUN_ID),
	[VY_LOG_DELETE_RUN]		= (1 << VY_LOG_KEY_RUN_ID),
};

/** vy_log_key -> human readable name. */
static const char *vy_log_key_name[] = {
	[VY_LOG_KEY_INDEX_ID]		= "index_id",
	[VY_LOG_KEY_RANGE_ID]		= "range_id",
	[VY_LOG_KEY_RUN_ID]		= "run_id",
	[VY_LOG_KEY_RANGE_BEGIN]	= "range_begin",
	[VY_LOG_KEY_RANGE_END]		= "range_end",
};

/** vy_log_type -> human readable name. */
static const char *vy_log_type_name[] = {
	[VY_LOG_NEW_INDEX]		= "new_index",
	[VY_LOG_INSERT_RANGE]		= "insert_range",
	[VY_LOG_DELETE_RANGE]		= "delete_range",
	[VY_LOG_INSERT_RUN]		= "insert_run",
	[VY_LOG_DELETE_RUN]		= "delete_run",
};

/** Vinyl metadata log object. */
struct vy_log {
	/** Xlog object used for writing the log. */
	struct xlog xlog;
	/**
	 * Latch protecting the xlog.
	 *
	 * We need to lock the log before writing to xlog, because
	 * xlog object doesn't support concurrent accesses.
	 */
	struct latch latch;
};

/** Index info stored in a recovery context. */
struct vy_index_recovery_info {
	/** ID of the index. */
	int64_t id;
	/**
	 * List of all ranges in the index, linked by
	 * vy_range_recovery_info::in_index.
	 */
	struct rlist ranges;
};

/** Range info stored in a recovery context. */
struct vy_range_recovery_info {
	/** Link in vy_index_recovery_info::ranges. */
	struct rlist in_index;
	/** ID of the range. */
	int64_t id;
	/** Start of the range, stored in MsgPack array. */
	char *begin;
	/** End of the range, stored in MsgPack array. */
	char *end;
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
	/** ID of the run. */
	int64_t id;
};

/** Check if an xlog meta belongs to a vinyl metadata log. */
static int
vy_log_type_check(struct xlog_meta *meta)
{
	if (strcmp(meta->filetype, VY_LOG_TYPE) != 0) {
		diag_set(ClientError, ER_INVALID_XLOG_TYPE,
			 VY_LOG_TYPE, meta->filetype);
		return -1;
	}
	return 0;
}

/** An snprint-style function to print a log record. */
static int
vy_log_record_snprint(char *buf, int size, const struct vy_log_record *record)
{
	int total = 0;
	assert(record->type < vy_log_MAX);
	unsigned long key_mask = vy_log_key_mask[record->type];
	SNPRINT(total, snprintf, buf, size, "%s{",
		vy_log_type_name[record->type]);
	if (key_mask & (1 << VY_LOG_KEY_INDEX_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_INDEX_ID], record->index_id);
	if (key_mask & (1 << VY_LOG_KEY_RANGE_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_RANGE_ID], record->range_id);
	if (key_mask & (1 << VY_LOG_KEY_RUN_ID))
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_RUN_ID], record->run_id);
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
		return "<failed to format vinyl log record>";
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
 * 'type': see vy_log_type enum
 * 'key': see vy_log_key enum
 * 'value': depends on 'key'
 */
static int
vy_log_record_encode(const struct vy_log_record *record,
		     struct xrow_header *row)
{
	assert(record->type < vy_log_MAX);
	unsigned long key_mask = vy_log_key_mask[record->type];

	/*
	 * Calculate record size.
	 */
	size_t size = 0;
	size += mp_sizeof_array(2);
	size += mp_sizeof_uint(record->type);
	size_t n_keys = 0;
	if (key_mask & (1 << VY_LOG_KEY_INDEX_ID)) {
		size += mp_sizeof_uint(VY_LOG_KEY_INDEX_ID);
		size += mp_sizeof_uint(record->index_id);
		n_keys++;
	}
	if (key_mask & (1 << VY_LOG_KEY_RANGE_ID)) {
		size += mp_sizeof_uint(VY_LOG_KEY_RANGE_ID);
		size += mp_sizeof_uint(record->range_id);
		n_keys++;
	}
	if (key_mask & (1 << VY_LOG_KEY_RUN_ID)) {
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
	size += mp_sizeof_map(n_keys);

	/*
	 * Encode record.
	 */
	char *tuple = region_alloc(&fiber()->gc, size);
	if (tuple == NULL) {
		diag_set(OutOfMemory, size, "region", "vinyl log record");
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
	assert(pos == tuple + size);

	/*
	 * Store record in xrow.
	 */
	struct request req;
	request_create(&req, IPROTO_INSERT);
	req.tuple = tuple;
	req.tuple_end = pos;
	memset(row, 0, sizeof(*row));
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

	struct request req;
	request_create(&req, row->type);
	if (request_decode(&req, row->body->iov_base,
			   row->body->iov_len) < 0)
		return -1;

	const char *pos = req.tuple;

	if (mp_typeof(*pos) != MP_ARRAY ||
	    mp_decode_array(&pos) != 2)
		goto fail;

	record->type = mp_decode_uint(&pos);
	if (record->type >= vy_log_MAX)
		goto fail;

	unsigned long key_mask = 0;
	uint32_t n_keys = mp_decode_map(&pos);
	for (uint32_t i = 0; i < n_keys; i++) {
		uint32_t key = mp_decode_uint(&pos);
		switch (key) {
		case VY_LOG_KEY_INDEX_ID:
			if (mp_typeof(*pos) != MP_UINT)
				goto fail;
			record->index_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_RANGE_ID:
			if (mp_typeof(*pos) != MP_UINT)
				goto fail;
			record->range_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_RUN_ID:
			if (mp_typeof(*pos) != MP_UINT)
				goto fail;
			record->run_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_RANGE_BEGIN:
			if (mp_typeof(*pos) != MP_ARRAY)
				goto fail;
			record->range_begin = pos;
			mp_next(&pos);
			break;
		case VY_LOG_KEY_RANGE_END:
			if (mp_typeof(*pos) != MP_ARRAY)
				goto fail;
			record->range_end = pos;
			mp_next(&pos);
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
	say_error("Invalid record in vinyl metadata log: %s", buf);
	diag_set(ClientError, ER_VINYL, "invalid vinyl log record");
	return -1;
}

struct vy_log *
vy_log_new(const char *dir)
{
	struct vy_log *log = malloc(sizeof(*log));
	if (log == NULL) {
		diag_set(OutOfMemory, sizeof(*log), "malloc", "struct vy_log");
		goto fail;
	}

	memset(log, 0, sizeof(*log));
	latch_create(&log->latch);

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", dir, VY_LOG_FILE);

	if (access(path, F_OK) == 0) {
		/* The log already exists, open it for appending. */
		if (xlog_open(&log->xlog, path) < 0)
			goto fail_free;
		if (vy_log_type_check(&log->xlog.meta) < 0)
			goto fail_close;
	} else {
		/* No log. Try to create a new one. */
		struct xlog_meta meta = {
			.filetype = VY_LOG_TYPE,
		};
		if (xlog_create(&log->xlog, path, &meta) < 0)
			goto fail_free;
		if (xlog_rename(&log->xlog) < 0)
			goto fail_close;
	}
	return log;

fail_close:
	xlog_close(&log->xlog, false);
fail_free:
	free(log);
fail:
	return NULL;
}

void
vy_log_delete(struct vy_log *log)
{
	latch_destroy(&log->latch);
	xlog_close(&log->xlog, false);
	TRASH(log);
	free(log);
}

void
vy_log_tx_begin(struct vy_log *log)
{
	latch_lock(&log->latch);
	xlog_tx_begin(&log->xlog);
	say_debug("%s", __func__);
}

static ssize_t
vy_log_tx_commit_f(va_list ap)
{
	struct vy_log *log = va_arg(ap, struct vy_log *);
	if (xlog_tx_commit(&log->xlog) < 0 ||
	    xlog_flush(&log->xlog) < 0)
		return -1;
	return 0;
}

int
vy_log_tx_commit(struct vy_log *log)
{
	say_debug("%s", __func__);
	int rc = 0;
	/*
	 * Do actual disk writes in a background fiber
	 * so as not to block the tx thread.
	 */
	if (coio_call(vy_log_tx_commit_f, log) < 0)
		rc = -1;
	latch_unlock(&log->latch);
	return rc;
}

void
vy_log_tx_rollback(struct vy_log *log)
{
	xlog_tx_rollback(&log->xlog);
	say_debug("%s", __func__);
	latch_unlock(&log->latch);
}

int
vy_log_write(struct vy_log *log, const struct vy_log_record *record)
{
	int rc = 0;

	bool autocommit = (latch_owner(&log->latch) != fiber());
	if (autocommit) {
		/*
		 * The caller doesn't bother to call vy_log_tx_begin()
		 * and vy_log_tx_commit(), do her a favor.
		 */
		vy_log_tx_begin(log);
	}

	say_debug("%s: %s", __func__, vy_log_record_str(record));

	struct xrow_header row;
	if (vy_log_record_encode(record, &row) < 0 ||
	    xlog_write_row(&log->xlog, &row) < 0)
		rc = -1;

	if (autocommit) {
		if (rc == 0)
			rc = vy_log_tx_commit(log);
		else
			vy_log_tx_rollback(log);
	}

	return rc;
}

/** Lookup an index in vy_recovery::index_hash map. */
static struct vy_index_recovery_info *
vy_recovery_lookup_index(struct vy_recovery *recovery, int64_t index_id)
{
	struct mh_i64ptr_t *h = recovery->index_hash;
	mh_int_t k = mh_i64ptr_find(h, index_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup a range in vy_recovery::range_hash map. */
static struct vy_range_recovery_info *
vy_recovery_lookup_range(struct vy_recovery *recovery, int64_t range_id)
{
	struct mh_i64ptr_t *h = recovery->range_hash;
	mh_int_t k = mh_i64ptr_find(h, range_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup a run in vy_recovery::run_hash map. */
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
 * Register an index with a recovery context.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
vy_recovery_hash_index(struct vy_recovery *recovery, int64_t index_id)
{
	if (vy_recovery_lookup_index(recovery, index_id) != NULL) {
		diag_set(ClientError, ER_VINYL, "duplicate index id");
		return -1;
	}
	struct vy_index_recovery_info *index = malloc(sizeof(*index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(*index),
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
	rlist_create(&index->ranges);
	return 0;
}

/**
 * Register a run with a recovery context.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
vy_recovery_hash_run(struct vy_recovery *recovery,
		     int64_t range_id, int64_t run_id)
{
	if (vy_recovery_lookup_run(recovery, run_id) != NULL) {
		diag_set(ClientError, ER_VINYL, "duplicate run id");
		return -1;
	}
	struct vy_range_recovery_info *range;
	range = vy_recovery_lookup_range(recovery, range_id);
	if (range == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown range id");
		return -1;
	}
	struct vy_run_recovery_info *run = malloc(sizeof(*run));
	if (run == NULL) {
		diag_set(OutOfMemory, sizeof(*run),
			 "malloc", "struct vy_run_recovery_info");
		return -1;
	}
	struct mh_i64ptr_t *h = recovery->run_hash;
	struct mh_i64ptr_node_t node = { run_id, run };
	if (mh_i64ptr_put(h, &node, NULL, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put", "mh_i64ptr_node_t");
		free(run);
		return -1;
	}
	run->id = run_id;
	rlist_add_entry(&range->runs, run, in_range);
	if (recovery->run_id_max < run_id)
		recovery->run_id_max = run_id;
	return 0;
}

/**
 * Delete a run info from a recovery context.
 * Return 0 on success, -1 if ID not found.
 */
static int
vy_recovery_unhash_run(struct vy_recovery *recovery, int64_t run_id)
{
	struct mh_i64ptr_t *h = recovery->run_hash;
	mh_int_t k = mh_i64ptr_find(h, run_id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_VINYL, "unknown run id");
		return -1;
	}
	struct vy_run_recovery_info *run = mh_i64ptr_node(h, k)->val;
	mh_i64ptr_del(h, k, NULL);
	rlist_del_entry(run, in_range);
	free(run);
	return 0;
}

/**
 * Register a range with a recovery context.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
vy_recovery_hash_range(struct vy_recovery *recovery,
		       int64_t index_id, int64_t range_id,
		       const char *begin, const char *end)
{
	if (vy_recovery_lookup_range(recovery, range_id) != NULL) {
		diag_set(ClientError, ER_VINYL, "duplicate range id");
		return -1;
	}
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index(recovery, index_id);
	if (index == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown index id");
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
	rlist_create(&range->runs);
	rlist_add_entry(&index->ranges, range, in_index);
	if (recovery->range_id_max < range_id)
		recovery->range_id_max = range_id;
	return 0;
}

/**
 * Delete info about a range and all its runs from a recovery context.
 * Return 0 on success, -1 if ID not found.
 */
static int
vy_recovery_unhash_range(struct vy_recovery *recovery, int64_t range_id)
{
	struct mh_i64ptr_t *h = recovery->range_hash;
	mh_int_t k = mh_i64ptr_find(h, range_id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_VINYL, "unknown range id");
		return -1;
	}
	struct vy_range_recovery_info *range = mh_i64ptr_node(h, k)->val;
	mh_i64ptr_del(h, k, NULL);
	rlist_del_entry(range, in_index);
	struct vy_run_recovery_info *run, *tmp;
	rlist_foreach_entry_safe(run, &range->runs, in_range, tmp) {
		if (vy_recovery_unhash_run(recovery, run->id) < 0)
			assert(0);
	}
	free(range);
	return 0;
}

/**
 * Update a recovery context with a new log record.
 * Return 0 on success, -1 on failure.
 *
 * The purpose of this function is to restore the latest consistent
 * view of vinyl by replaying the metadata log.
 */
static int
vy_recovery_process_record(struct vy_recovery *recovery,
			   const struct vy_log_record *record)
{
	say_debug("%s: %s", __func__, vy_log_record_str(record));

	int rc;
	switch (record->type) {
	case VY_LOG_NEW_INDEX:
		rc = vy_recovery_hash_index(recovery, record->index_id);
		break;
	case VY_LOG_INSERT_RANGE:
		rc = vy_recovery_hash_range(recovery, record->index_id,
					    record->range_id,
					    record->range_begin,
					    record->range_end);
		break;
	case VY_LOG_DELETE_RANGE:
		rc = vy_recovery_unhash_range(recovery, record->range_id);
		break;
	case VY_LOG_INSERT_RUN:
		rc = vy_recovery_hash_run(recovery, record->range_id,
					  record->run_id);
		break;
	case VY_LOG_DELETE_RUN:
		rc = vy_recovery_unhash_run(recovery, record->run_id);
		break;
	default:
		unreachable();
	}
	return rc;
}

struct vy_recovery *
vy_recovery_new(const char *dir)
{
	struct vy_recovery *recovery = malloc(sizeof(*recovery));
	if (recovery == NULL) {
		diag_set(OutOfMemory, sizeof(*recovery),
			 "malloc", "struct vy_recovery");
		goto fail;
	}

	memset(recovery, 0, sizeof(*recovery));

	recovery->index_hash = mh_i64ptr_new();
	recovery->range_hash = mh_i64ptr_new();
	recovery->run_hash = mh_i64ptr_new();
	if (recovery->index_hash == NULL ||
	    recovery->range_hash == NULL ||
	    recovery->run_hash == NULL) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_new", "mh_i64ptr_t");
		goto fail_free;
	}

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", dir, VY_LOG_FILE);

	struct xlog_cursor cursor;
	if (xlog_cursor_open(&cursor, path) < 0)
		goto fail_free;
	if (vy_log_type_check(&cursor.meta) < 0)
		goto fail_close;

	int rc;
	struct xrow_header row;
	while ((rc = xlog_cursor_next(&cursor, &row, true)) == 0) {
		struct vy_log_record record;
		rc = vy_log_record_decode(&record, &row);
		if (rc < 0)
			break;
		rc = vy_recovery_process_record(recovery, &record);
		if (rc < 0)
			break;
	}
	if (rc < 0)
		goto fail_close;

	xlog_cursor_close(&cursor, false);
	return recovery;

fail_close:
	xlog_cursor_close(&cursor, false);
fail_free:
	vy_recovery_delete(recovery);
fail:
	return NULL;
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

void
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

int
vy_recovery_load_index(struct vy_recovery *recovery, int64_t index_id,
		       vy_recovery_cb cb, void *cb_arg)
{
	say_debug("%s: index_id=%"PRIi64, __func__, index_id);

	struct vy_index_recovery_info *index;
	struct vy_range_recovery_info *range;
	struct vy_run_recovery_info *run;
	struct vy_log_record record = { .index_id = index_id };
	const char *tmp;

	index = vy_recovery_lookup_index(recovery, index_id);
	if (index == NULL) {
		diag_set(ClientError, ER_VINYL, "unknown index id");
		return -1;
	}

	rlist_foreach_entry(range, &index->ranges, in_index) {
		record.type = VY_LOG_INSERT_RANGE;
		record.range_id = range->id;
		record.range_begin = tmp = range->begin;
		if (mp_decode_array(&tmp) == 0)
			record.range_begin = NULL;
		record.range_end = tmp = range->end;
		if (mp_decode_array(&tmp) == 0)
			record.range_end = NULL;
		say_debug("%s: %s", __func__,
			  vy_log_record_str(&record));
		if (cb(&record, cb_arg) != 0)
			return -1;
		/*
		 * Newer runs are stored closer to the head of the list,
		 * while we are supposed to return runs in chronological
		 * order, so use reverse iterator.
		 */
		rlist_foreach_entry_reverse(run, &range->runs, in_range) {
			record.type = VY_LOG_INSERT_RUN;
			record.range_id = range->id;
			record.run_id = run->id;
			say_debug("%s: %s", __func__,
				  vy_log_record_str(&record));
			if (cb(&record, cb_arg) != 0)
				return -1;
		}
	}
	return 0;
}
