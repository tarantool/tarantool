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
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <msgpuck/msgpuck.h>
#include <small/mempool.h>
#include <small/region.h>
#include <small/rlist.h>

#include "assoc.h"
#include "coio_task.h"
#include "diag.h"
#include "errcode.h"
#include "errinj.h"
#include "fiber.h"
#include "fiber_cond.h"
#include "iproto_constants.h" /* IPROTO_INSERT */
#include "key_def.h"
#include "latch.h"
#include "replication.h" /* INSTANCE_UUID */
#include "salad/stailq.h"
#include "say.h"
#include "tt_static.h"
#include "wal.h"
#include "vclock/vclock.h"
#include "xlog.h"
#include "xrow.h"

/**
 * Integer key of a field in the vy_log_record structure.
 * Used for packing a record in MsgPack.
 */
enum vy_log_key {
	VY_LOG_KEY_LSM_ID		= 0,
	VY_LOG_KEY_RANGE_ID		= 1,
	VY_LOG_KEY_RUN_ID		= 2,
	VY_LOG_KEY_BEGIN		= 3,
	VY_LOG_KEY_END			= 4,
	VY_LOG_KEY_INDEX_ID		= 5,
	VY_LOG_KEY_SPACE_ID		= 6,
	VY_LOG_KEY_DEF			= 7,
	VY_LOG_KEY_SLICE_ID		= 8,
	VY_LOG_KEY_DUMP_LSN		= 9,
	VY_LOG_KEY_GC_LSN		= 10,
	VY_LOG_KEY_TRUNCATE_COUNT	= 11,
	VY_LOG_KEY_CREATE_LSN		= 12,
	VY_LOG_KEY_MODIFY_LSN		= 13,
	VY_LOG_KEY_DROP_LSN		= 14,
	VY_LOG_KEY_GROUP_ID		= 15,
	VY_LOG_KEY_DUMP_COUNT		= 16,
};

/** vy_log_key -> human readable name. */
static const char *vy_log_key_name[] = {
	[VY_LOG_KEY_LSM_ID]		= "lsm_id",
	[VY_LOG_KEY_RANGE_ID]		= "range_id",
	[VY_LOG_KEY_RUN_ID]		= "run_id",
	[VY_LOG_KEY_BEGIN]		= "begin",
	[VY_LOG_KEY_END]		= "end",
	[VY_LOG_KEY_INDEX_ID]		= "index_id",
	[VY_LOG_KEY_SPACE_ID]		= "space_id",
	[VY_LOG_KEY_DEF]		= "key_def",
	[VY_LOG_KEY_SLICE_ID]		= "slice_id",
	[VY_LOG_KEY_DUMP_LSN]		= "dump_lsn",
	[VY_LOG_KEY_GC_LSN]		= "gc_lsn",
	[VY_LOG_KEY_TRUNCATE_COUNT]	= "truncate_count",
	[VY_LOG_KEY_CREATE_LSN]		= "create_lsn",
	[VY_LOG_KEY_MODIFY_LSN]		= "modify_lsn",
	[VY_LOG_KEY_DROP_LSN]		= "drop_lsn",
	[VY_LOG_KEY_GROUP_ID]		= "group_id",
	[VY_LOG_KEY_DUMP_COUNT]		= "dump_count",
};

/** vy_log_type -> human readable name. */
static const char *vy_log_type_name[] = {
	[VY_LOG_CREATE_LSM]		= "create_lsm",
	[VY_LOG_DROP_LSM]		= "drop_lsm",
	[VY_LOG_INSERT_RANGE]		= "insert_range",
	[VY_LOG_DELETE_RANGE]		= "delete_range",
	[VY_LOG_PREPARE_RUN]		= "prepare_run",
	[VY_LOG_CREATE_RUN]		= "create_run",
	[VY_LOG_DROP_RUN]		= "drop_run",
	[VY_LOG_FORGET_RUN]		= "forget_run",
	[VY_LOG_INSERT_SLICE]		= "insert_slice",
	[VY_LOG_DELETE_SLICE]		= "delete_slice",
	[VY_LOG_DUMP_LSM]		= "dump_lsm",
	[VY_LOG_SNAPSHOT]		= "snapshot",
	[VY_LOG_TRUNCATE_LSM]		= "truncate_lsm",
	[VY_LOG_MODIFY_LSM]		= "modify_lsm",
	[VY_LOG_FORGET_LSM]		= "forget_lsm",
	[VY_LOG_PREPARE_LSM]		= "prepare_lsm",
	[VY_LOG_REBOOTSTRAP]		= "rebootstrap",
	[VY_LOG_ABORT_REBOOTSTRAP]	= "abort_rebootstrap",
};

/** Batch of vylog records that must be written in one go. */
struct vy_log_tx {
	/** Link in vy_log::pending_tx. */
	struct stailq_entry in_pending;
	/** Region used for allocating records. */
	struct region region;
	/** List of records, linked by vy_log_record::in_tx. */
	struct stailq records;
};

/** Metadata log object. */
struct vy_log {
	/**
	 * The directory where log files are stored.
	 * Note, dir.index contains vclocks of all snapshots,
	 * even those that didn't result in file creation.
	 */
	struct xdir dir;
	/** Last checkpoint vclock. */
	struct vclock last_checkpoint;
	/** Recovery context. */
	struct vy_recovery *recovery;
	/**
	 * Latch that syncs log writers against readers.
	 * Needed so that we don't miss any records during
	 * log rotation.
	 */
	struct latch latch;
	/**
	 * Background fiber flushing pending transactions.
	 * Lives throughout the vinyl engine lifetime. Note,
	 * we don't stop it in destructor, because the event
	 * loop is dead at that time so we can't properly
	 * join it.
	 */
	struct fiber *flusher;
	/** Condition variable used for signalling the flusher. */
	struct fiber_cond flusher_cond;
	/**
	 * Next ID to use for a vinyl object.
	 * Used by vy_log_next_id().
	 */
	int64_t next_id;
	/** Pool of vy_log_tx objects. */
	struct mempool tx_pool;
	/** Current transaction or NULL. */
	struct vy_log_tx *tx;
	/**
	 * List of transactions awaiting to be flushed to disk,
	 * linked by vy_log_tx::in_pending.
	 */
	struct stailq pending_tx;
	/**
	 * Flag set if vy_log_write() failed.
	 *
	 * It indicates that that the current transaction must be
	 * aborted on vy_log_commit(). Thanks to this flag, we don't
	 * need to add error handling code after each invocation of
	 * vy_log_write(), instead we only check vy_log_commit()
	 * return code.
	 */
	bool tx_failed;
	/**
	 * Diagnostic area where vy_log_write() error is stored,
	 * only relevant if @tx_failed is set.
	 */
	struct diag tx_diag;
};
static struct vy_log vy_log;

static int
vy_log_flusher_f(va_list va);

static struct vy_recovery *
vy_recovery_new_locked(int64_t signature, int flags);

static int
vy_recovery_process_record(struct vy_recovery *recovery,
			   const struct vy_log_record *record);

static int
vy_log_create(const struct vclock *vclock, struct vy_recovery *recovery);

int
vy_log_rotate(const struct vclock *vclock);

/**
 * Return the name of the vylog file that has the given signature.
 */
static inline const char *
vy_log_filename(int64_t signature)
{
	return xdir_format_filename(&vy_log.dir, signature, NONE);
}

/**
 * Return the vclock of the checkpoint that was taken
 * before the given vclock.
 */
static const struct vclock *
vy_log_prev_checkpoint(const struct vclock *vclock)
{
	struct vclock *prev = vclockset_psearch(&vy_log.dir.index, vclock);
	if (prev != NULL && vclock_sum(prev) == vclock_sum(vclock))
		prev = vclockset_prev(&vy_log.dir.index, prev);
	return prev;
}

/** An snprint-style function to print a log record. */
static int
vy_log_record_snprint(char *buf, int size, const struct vy_log_record *record)
{
	int total = 0;
	assert(record->type < vy_log_record_type_MAX);
	SNPRINT(total, snprintf, buf, size, "%s{",
		vy_log_type_name[record->type]);
	if (record->lsm_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_LSM_ID], record->lsm_id);
	if (record->range_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_RANGE_ID],
			record->range_id);
	if (record->run_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_RUN_ID],
			record->run_id);
	if (record->begin != NULL) {
		SNPRINT(total, snprintf, buf, size, "%s=",
			vy_log_key_name[VY_LOG_KEY_BEGIN]);
		SNPRINT(total, mp_snprint, buf, size, record->begin);
		SNPRINT(total, snprintf, buf, size, ", ");
	}
	if (record->end != NULL) {
		SNPRINT(total, snprintf, buf, size, "%s=",
			vy_log_key_name[VY_LOG_KEY_END]);
		SNPRINT(total, mp_snprint, buf, size, record->end);
		SNPRINT(total, snprintf, buf, size, ", ");
	}
	if (record->index_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIu32", ",
			vy_log_key_name[VY_LOG_KEY_INDEX_ID], record->index_id);
	if (record->space_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIu32", ",
			vy_log_key_name[VY_LOG_KEY_SPACE_ID], record->space_id);
	if (record->group_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIu32", ",
			vy_log_key_name[VY_LOG_KEY_GROUP_ID], record->group_id);
	if (record->key_parts != NULL) {
		SNPRINT(total, snprintf, buf, size, "%s=",
			vy_log_key_name[VY_LOG_KEY_DEF]);
		SNPRINT(total, key_def_snprint_parts, buf, size,
			record->key_parts, record->key_part_count);
		SNPRINT(total, snprintf, buf, size, ", ");
	}
	if (record->slice_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_SLICE_ID],
			record->slice_id);
	if (record->create_lsn > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_CREATE_LSN],
			record->create_lsn);
	if (record->modify_lsn > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_MODIFY_LSN],
			record->modify_lsn);
	if (record->drop_lsn > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_DROP_LSN],
			record->drop_lsn);
	if (record->dump_lsn > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_DUMP_LSN],
			record->dump_lsn);
	if (record->gc_lsn > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_GC_LSN],
			record->gc_lsn);
	if (record->dump_count > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIu32", ",
			vy_log_key_name[VY_LOG_KEY_DUMP_COUNT],
			record->dump_count);
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

	/*
	 * Calculate record size.
	 */
	size_t size = 0;
	size += mp_sizeof_array(2);
	size += mp_sizeof_uint(record->type);
	size_t n_keys = 0;
	if (record->lsm_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_LSM_ID);
		size += mp_sizeof_uint(record->lsm_id);
		n_keys++;
	}
	if (record->range_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_RANGE_ID);
		size += mp_sizeof_uint(record->range_id);
		n_keys++;
	}
	if (record->run_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_RUN_ID);
		size += mp_sizeof_uint(record->run_id);
		n_keys++;
	}
	if (record->begin != NULL) {
		size += mp_sizeof_uint(VY_LOG_KEY_BEGIN);
		const char *p = record->begin;
		assert(mp_typeof(*p) == MP_ARRAY);
		mp_next(&p);
		size += p - record->begin;
		n_keys++;
	}
	if (record->end != NULL) {
		size += mp_sizeof_uint(VY_LOG_KEY_END);
		const char *p = record->end;
		assert(mp_typeof(*p) == MP_ARRAY);
		mp_next(&p);
		size += p - record->end;
		n_keys++;
	}
	if (record->index_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_INDEX_ID);
		size += mp_sizeof_uint(record->index_id);
		n_keys++;
	}
	if (record->space_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_SPACE_ID);
		size += mp_sizeof_uint(record->space_id);
		n_keys++;
	}
	if (record->group_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_GROUP_ID);
		size += mp_sizeof_uint(record->group_id);
		n_keys++;
	}
	if (record->key_parts != NULL) {
		size += mp_sizeof_uint(VY_LOG_KEY_DEF);
		size += mp_sizeof_array(record->key_part_count);
		size += key_def_sizeof_parts(record->key_parts,
					     record->key_part_count);
		n_keys++;
	}
	if (record->slice_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_SLICE_ID);
		size += mp_sizeof_uint(record->slice_id);
		n_keys++;
	}
	if (record->create_lsn > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_CREATE_LSN);
		size += mp_sizeof_uint(record->create_lsn);
		n_keys++;
	}
	if (record->modify_lsn > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_MODIFY_LSN);
		size += mp_sizeof_uint(record->modify_lsn);
		n_keys++;
	}
	if (record->drop_lsn > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_DROP_LSN);
		size += mp_sizeof_uint(record->drop_lsn);
		n_keys++;
	}
	if (record->dump_lsn > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_DUMP_LSN);
		size += mp_sizeof_uint(record->dump_lsn);
		n_keys++;
	}
	if (record->gc_lsn > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_GC_LSN);
		size += mp_sizeof_uint(record->gc_lsn);
		n_keys++;
	}
	if (record->dump_count > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_DUMP_COUNT);
		size += mp_sizeof_uint(record->dump_count);
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
	if (record->lsm_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_LSM_ID);
		pos = mp_encode_uint(pos, record->lsm_id);
	}
	if (record->range_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_RANGE_ID);
		pos = mp_encode_uint(pos, record->range_id);
	}
	if (record->run_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_RUN_ID);
		pos = mp_encode_uint(pos, record->run_id);
	}
	if (record->begin != NULL) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_BEGIN);
		const char *p = record->begin;
		mp_next(&p);
		memcpy(pos, record->begin, p - record->begin);
		pos += p - record->begin;
	}
	if (record->end != NULL) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_END);
		const char *p = record->end;
		mp_next(&p);
		memcpy(pos, record->end, p - record->end);
		pos += p - record->end;
	}
	if (record->index_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_INDEX_ID);
		pos = mp_encode_uint(pos, record->index_id);
	}
	if (record->space_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_SPACE_ID);
		pos = mp_encode_uint(pos, record->space_id);
	}
	if (record->group_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_GROUP_ID);
		pos = mp_encode_uint(pos, record->group_id);
	}
	if (record->key_parts != NULL) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_DEF);
		pos = mp_encode_array(pos, record->key_part_count);
		pos = key_def_encode_parts(pos, record->key_parts,
					   record->key_part_count);
	}
	if (record->slice_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_SLICE_ID);
		pos = mp_encode_uint(pos, record->slice_id);
	}
	if (record->create_lsn > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_CREATE_LSN);
		pos = mp_encode_uint(pos, record->create_lsn);
	}
	if (record->modify_lsn > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_MODIFY_LSN);
		pos = mp_encode_uint(pos, record->modify_lsn);
	}
	if (record->drop_lsn > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_DROP_LSN);
		pos = mp_encode_uint(pos, record->drop_lsn);
	}
	if (record->dump_lsn > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_DUMP_LSN);
		pos = mp_encode_uint(pos, record->dump_lsn);
	}
	if (record->gc_lsn > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_GC_LSN);
		pos = mp_encode_uint(pos, record->gc_lsn);
	}
	if (record->dump_count > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_DUMP_COUNT);
		pos = mp_encode_uint(pos, record->dump_count);
	}
	assert(pos == tuple + size);

	/*
	 * Store record in xrow.
	 */
	struct request req;
	memset(&req, 0, sizeof(req));
	req.type = IPROTO_INSERT;
	req.tuple = tuple;
	req.tuple_end = pos;
	memset(row, 0, sizeof(*row));
	row->type = req.type;
	row->bodycnt = xrow_encode_dml(&req, &fiber()->gc, row->body);
	return 0;
}

/**
 * Decode a log record from an xrow.
 * Return 0 on success, -1 on failure.
 */
static int
vy_log_record_decode(struct vy_log_record *record,
		     struct xrow_header *row)
{
	char *buf;

	memset(record, 0, sizeof(*record));

	struct request req;
	if (xrow_decode_dml(row, &req, 1ULL << IPROTO_TUPLE) != 0) {
		diag_log();
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 "Bad record: failed to decode request");
		return -1;
	}

	const char *tmp, *pos = req.tuple;

	uint32_t array_size = mp_decode_array(&pos);
	if (array_size != 2) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Bad record: wrong array size "
				    "(expected %d, got %u)",
				    2, (unsigned)array_size));
		goto fail;
	}

	record->type = mp_decode_uint(&pos);
	if (record->type >= vy_log_record_type_MAX) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Bad record: unknown record type %d",
				    record->type));
		goto fail;
	}

	uint32_t n_keys = mp_decode_map(&pos);
	for (uint32_t i = 0; i < n_keys; i++) {
		uint32_t key = mp_decode_uint(&pos);
		switch (key) {
		case VY_LOG_KEY_LSM_ID:
			record->lsm_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_RANGE_ID:
			record->range_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_RUN_ID:
			record->run_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_BEGIN:
			tmp = pos;
			record->begin = mp_decode_array(&tmp) > 0 ? pos : NULL;
			mp_next(&pos);
			break;
		case VY_LOG_KEY_END:
			tmp = pos;
			record->end = mp_decode_array(&tmp) > 0 ? pos : NULL;
			mp_next(&pos);
			break;
		case VY_LOG_KEY_INDEX_ID:
			record->index_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_SPACE_ID:
			record->space_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_GROUP_ID:
			record->group_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_DEF: {
			struct region *region = &fiber()->gc;
			uint32_t part_count = mp_decode_array(&pos);
			size_t size;
			struct key_part_def *parts =
				region_alloc_array(region, typeof(parts[0]),
						   part_count, &size);
			if (parts == NULL) {
				diag_set(OutOfMemory, size,
					 "region_alloc_array", "parts");
				return -1;
			}
			if (key_def_decode_parts(parts, part_count, &pos,
						 NULL, 0, region) != 0) {
				diag_log();
				diag_set(ClientError, ER_INVALID_VYLOG_FILE,
					 "Bad record: failed to decode "
					 "index key definition");
				goto fail;
			}
			record->key_parts = parts;
			record->key_part_count = part_count;
			break;
		}
		case VY_LOG_KEY_SLICE_ID:
			record->slice_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_CREATE_LSN:
			record->create_lsn = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_MODIFY_LSN:
			record->modify_lsn = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_DROP_LSN:
			record->drop_lsn = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_DUMP_LSN:
			record->dump_lsn = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_GC_LSN:
			record->gc_lsn = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_DUMP_COUNT:
			record->dump_count = mp_decode_uint(&pos);
			break;
		default:
			mp_next(&pos); /* unknown key, ignore */
			break;
		}
	}
	if (record->type == VY_LOG_CREATE_LSM) {
		/*
		 * We used to use LSN as unique LSM tree identifier
		 * and didn't store LSN separately so if there's
		 * no 'create_lsn' field in the record, we are
		 * recovering from an old vylog and 'id' is in
		 * fact the LSN of the WAL record that committed
		 * the LSM tree.
		 */
		if (record->create_lsn == 0)
			record->create_lsn = record->lsm_id;
		/*
		 * If the LSM tree has never been modified, initialize
		 * 'modify_lsn' with 'create_lsn'.
		 */
		if (record->modify_lsn == 0)
			record->modify_lsn = record->create_lsn;
	}
	return 0;
fail:
	buf = tt_static_buf();
	mp_snprint(buf, TT_STATIC_BUF_LEN, req.tuple);
	say_error("failed to decode vylog record: %s", buf);
	return -1;
}

/**
 * Duplicate a log record. All objects refered to by the record
 * are duplicated as well.
 */
static struct vy_log_record *
vy_log_record_dup(struct region *pool, const struct vy_log_record *src)
{
	size_t used = region_used(pool);
	size_t size;
	struct vy_log_record *dst = region_alloc_object(pool, typeof(*dst),
							&size);
	if (dst == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_object", "dst");
		goto err;
	}
	*dst = *src;
	if (src->begin != NULL) {
		const char *data = src->begin;
		mp_next(&data);
		size = data - src->begin;
		dst->begin = region_alloc(pool, size);
		if (dst->begin == NULL) {
			diag_set(OutOfMemory, size, "region",
				 "vy_log_record::begin");
			goto err;
		}
		memcpy((char *)dst->begin, src->begin, size);
	}
	if (src->end != NULL) {
		const char *data = src->end;
		mp_next(&data);
		size = data - src->end;
		dst->end = region_alloc(pool, size);
		if (dst->end == NULL) {
			diag_set(OutOfMemory, size, "region",
				 "struct vy_log_record");
			goto err;
		}
		memcpy((char *)dst->end, src->end, size);
	}
	if (src->key_def != NULL) {
		dst->key_parts =
			region_alloc_array(pool, typeof(dst->key_parts[0]),
					   src->key_def->part_count, &size);
		if (dst->key_parts == NULL) {
			diag_set(OutOfMemory, size, "region_alloc_array",
				 "def->key_parts");
			goto err;
		}
		if (key_def_dump_parts(src->key_def, dst->key_parts, pool) != 0)
			goto err;
		dst->key_part_count = src->key_def->part_count;
		dst->key_def = NULL;
	}
	return dst;

err:
	region_truncate(pool, used);
	return NULL;
}

void
vy_log_init(const char *dir)
{
	xdir_create(&vy_log.dir, dir, VYLOG, &INSTANCE_UUID,
		    &xlog_opts_default);
	latch_create(&vy_log.latch);
	mempool_create(&vy_log.tx_pool, cord_slab_cache(),
		       sizeof(struct vy_log_tx));
	stailq_create(&vy_log.pending_tx);
	diag_create(&vy_log.tx_diag);
	wal_init_vy_log();
	fiber_cond_create(&vy_log.flusher_cond);
	vy_log.flusher = fiber_new_system("vinyl.vylog_flusher",
					  vy_log_flusher_f);
	if (vy_log.flusher == NULL)
		panic("failed to allocate vylog flusher fiber");
	fiber_wakeup(vy_log.flusher);
}

static struct vy_log_tx *
vy_log_tx_new(void)
{
	struct vy_log_tx *tx = mempool_alloc(&vy_log.tx_pool);
	if (tx == NULL) {
		diag_set(OutOfMemory, sizeof(*tx), "mempool", "vy log tx");
		return NULL;
	}
	region_create(&tx->region, cord_slab_cache());
	stailq_create(&tx->records);
	tx->in_pending.next = NULL;
	return tx;
}

static void
vy_log_tx_delete(struct vy_log_tx *tx)
{
	region_destroy(&tx->region);
	mempool_free(&vy_log.tx_pool, tx);
}

/**
 * Write a given transaction to disk.
 */
static int
vy_log_tx_flush(struct vy_log_tx *tx)
{
	if (stailq_empty(&tx->records))
		return 0; /* nothing to do */

	ERROR_INJECT(ERRINJ_VY_LOG_FLUSH, {
		diag_set(ClientError, ER_INJECTION, "vinyl log flush");
		return -1;
	});
	ERROR_INJECT_YIELD(ERRINJ_VY_LOG_FLUSH_DELAY);

	int tx_size = 0;
	struct vy_log_record *record;
	stailq_foreach_entry(record, &tx->records, in_tx)
		tx_size++;

	size_t used = region_used(&fiber()->gc);

	struct journal_entry *entry;
	entry = journal_entry_new(tx_size, &fiber()->gc, NULL, NULL);
	if (entry == NULL)
		goto err;

	struct xrow_header *rows;
	rows = region_aligned_alloc(&fiber()->gc,
				    tx_size * sizeof(struct xrow_header),
				    alignof(struct xrow_header));
	if (rows == NULL)
		goto err;

	/*
	 * Encode buffered records.
	 */
	int i = 0;
	stailq_foreach_entry(record, &tx->records, in_tx) {
		if (record->gc_lsn == VY_LOG_GC_LSN_CURRENT)
			record->gc_lsn = vy_log_signature();
		assert(i < tx_size);
		struct xrow_header *row = &rows[i];
		if (vy_log_record_encode(record, row) < 0)
			goto err;
		entry->rows[i] = row;
		i++;
	}
	assert(i == tx_size);

	/*
	 * Do actual disk writes on behalf of the WAL
	 * so as not to block the tx thread.
	 */
	if (wal_write_vy_log(entry) != 0)
		goto err;

	region_truncate(&fiber()->gc, used);
	return 0;
err:
	region_truncate(&fiber()->gc, used);
	return -1;
}

/**
 * Write all pending transaction to disk.
 */
static int
vy_log_flush(void)
{
	/*
	 * vy_log_tx_try_commit() can add a new transaction to
	 * the list while we are writing to disk. This is okay -
	 * we'll flush it next time. If we fail, we put remaining
	 * transactions back to the head of the list to preserve
	 * the commit order.
	 */
	struct stailq pending;
	stailq_create(&pending);
	stailq_concat(&pending, &vy_log.pending_tx);

	int rc = 0;
	while (!stailq_empty(&pending)) {
		struct vy_log_tx *tx = stailq_first_entry(&pending,
					struct vy_log_tx, in_pending);
		rc = vy_log_tx_flush(tx);
		if (rc != 0)
			break;
		stailq_shift(&pending);
		vy_log_tx_delete(tx);
	}
	stailq_concat(&pending, &vy_log.pending_tx);
	stailq_concat(&vy_log.pending_tx, &pending);
	return rc;
}

static int
vy_log_flusher_f(va_list va)
{
	(void)va;
	while (!fiber_is_cancelled()) {
		/*
		 * Disable writes during local recovery.
		 * See vy_log_tx_commit().
		 */
		if (vy_log.recovery != NULL ||
		    stailq_empty(&vy_log.pending_tx)) {
			fiber_cond_wait(&vy_log.flusher_cond);
			continue;
		}
		latch_lock(&vy_log.latch);
		int rc = vy_log_flush();
		latch_unlock(&vy_log.latch);
		if (rc != 0) {
			diag_log();
			say_error("failed to flush vylog");
			/*
			 * Don't retry immediately after a failure
			 * since the next write is likely to fail
			 * as well. Instead wait for the next signal.
			 */
			fiber_cond_wait(&vy_log.flusher_cond);
		}
	}
	return 0;
}

void
vy_log_free(void)
{
	struct vy_log_tx *tx, *next_tx;
	stailq_foreach_entry_safe(tx, next_tx, &vy_log.pending_tx, in_pending)
		vy_log_tx_delete(tx);
	stailq_create(&vy_log.pending_tx);
	mempool_destroy(&vy_log.tx_pool);
	xdir_destroy(&vy_log.dir);
	diag_destroy(&vy_log.tx_diag);
}

int
vy_log_open(struct xlog *xlog)
{
	/*
	 * Open the current log file or create a new one
	 * if it doesn't exist.
	 */
	const char *path = vy_log_filename(vclock_sum(&vy_log.last_checkpoint));
	if (access(path, F_OK) == 0)
		return xlog_open(xlog, path, &vy_log.dir.opts);

	if (errno != ENOENT) {
		diag_set(SystemError, "failed to access file '%s'", path);
		goto fail;
	}

	if (xdir_create_xlog(&vy_log.dir, xlog,
			     &vy_log.last_checkpoint) < 0)
		goto fail;

	struct xrow_header row;
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_SNAPSHOT;
	if (vy_log_record_encode(&record, &row) < 0 ||
	    xlog_write_row(xlog, &row) < 0)
		goto fail_close_xlog;

	if (xlog_rename(xlog) < 0)
		goto fail_close_xlog;

	return 0;

fail_close_xlog:
	if (unlink(xlog->filename) < 0)
		say_syserror("failed to delete file '%s'", xlog->filename);
	xlog_close(xlog, false);
fail:
	return -1;
}

int64_t
vy_log_next_id(void)
{
	return vy_log.next_id++;
}

/**
 * If a vylog file already exists, we are doing a rebootstrap:
 * - Load the vylog to find out the id to start indexing new
 *   objects with.
 * - Mark the beginning of a new rebootstrap attempt by writing
 *   VY_LOG_REBOOTSTRAP record.
 */
static int
vy_log_rebootstrap(void)
{
	struct vy_recovery *recovery;
	recovery = vy_recovery_new(vclock_sum(&vy_log.last_checkpoint),
				   VY_RECOVERY_ABORT_REBOOTSTRAP);
	if (recovery == NULL)
		return -1;

	vy_log.next_id = recovery->max_id + 1;
	vy_recovery_delete(recovery);

	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_REBOOTSTRAP;
	vy_log_tx_begin();
	vy_log_write(&record);
	if (vy_log_tx_commit() != 0)
		return -1;

	return 0;
}

int
vy_log_bootstrap(void)
{
	if (xdir_scan(&vy_log.dir, false) < 0)
		return -1;
	if (xdir_last_vclock(&vy_log.dir, &vy_log.last_checkpoint) >= 0)
		return vy_log_rebootstrap();

	/* Add initial vclock to the xdir. */
	struct vclock vclock;
	vclock_create(&vclock);
	xdir_add_vclock(&vy_log.dir, &vclock);
	return 0;
}

/**
 * Return true if the last vylog is new and contains no user data
 * (i.e. last entry is VY_LOG_SNAPSHOT).
 * In case of any errors log them and return false.
 */
static bool
vy_log_last_entry_is_snapshot(void)
{
	const char *path =
		vy_log_filename(vclock_sum(&vy_log.last_checkpoint));
	if (access(path, F_OK) < 0) {
		say_error("Failed to access last vylog");
		return false;
	}
	struct xlog_cursor cursor;
	if (xdir_open_cursor(&vy_log.dir,
			     vclock_sum(&vy_log.last_checkpoint),
			     &cursor) < 0) {
		diag_log();
		diag_clear(diag_get());
		return false;
	}
	int rc;
	struct xrow_header row;
	while ((rc = xlog_cursor_next(&cursor, &row, false)) == 0) {
		struct vy_log_record record;
		rc = vy_log_record_decode(&record, &row);
		if (rc < 0)
			break;
		if (record.type == VY_LOG_SNAPSHOT) {
			rc = xlog_cursor_next(&cursor, &row, false);
			if (rc <= 0)
				break;
			xlog_cursor_close(&cursor, false);
			return true;
		}
	}
	xlog_cursor_close(&cursor, false);
	if (rc < 0) {
		diag_log();
		diag_clear(diag_get());
	}
	return false;
}

struct vy_recovery *
vy_log_begin_recovery(const struct vclock *vclock, bool force_recovery)
{
	assert(vy_log.recovery == NULL);

	/*
	 * Do not fail recovery if vinyl directory does not exist,
	 * because vinyl might not be even in use. Complain only
	 * on an attempt to write a vylog.
	 */
	if (xdir_scan(&vy_log.dir, false) < 0)
		return NULL;

	if (xdir_last_vclock(&vy_log.dir, &vy_log.last_checkpoint) < 0) {
		/*
		 * Even if there's no vylog (i.e. vinyl isn't in use),
		 * we still have to add the vclock to the xdir index,
		 * because we may need it for garbage collection or
		 * backup in case the user starts using vinyl after
		 * recovery.
		 */
		xdir_add_vclock(&vy_log.dir, vclock);
		vclock_copy(&vy_log.last_checkpoint, vclock);
	}

	int cmp = vclock_compare(&vy_log.last_checkpoint, vclock);
	if (cmp > 0) {
		/*
		 * Last vy_log log is newer than the last snapshot.
		 * This can't normally happen, as vy_log is rotated
		 * in a short gap between checkpoint wait and commit.
		 * However, if memtx for some reason fails to commit its
		 * changes, instance will crash leaving .inprogress snap
		 * and corresponding (already rotated) vylog.
		 * Another and simpler reason is the case when somebody
		 * deleted snap file, but forgot to delete vy_log.
		 * So in case we are anyway in force recovery mode, let's
		 * try to delete last .vylog file and continue recovery process.
		 */
		bool is_vylog_empty = vy_log_last_entry_is_snapshot();
		if (!is_vylog_empty) {
			say_info("Last vylog is not empty. Its removal "
				 "may cause data loss!");
		}
		if (!force_recovery && !is_vylog_empty) {
			diag_set(ClientError, ER_MISSING_SNAPSHOT);
			say_info("To bootstrap instance try to remove last "
				 ".vylog file or run in force_recovery mode");
			return NULL;
		}
		if (xdir_remove_file_by_vclock(&vy_log.dir,
					       &vy_log.last_checkpoint) != 0) {
			say_info(".vylog is newer than snapshot. Failed to "
				 "remove it. Try to delete last .vylog "
				 "manually");
			return NULL;
		}
		const struct vclock *prev_checkpoint =
			vy_log_prev_checkpoint(&vy_log.last_checkpoint);
		if (prev_checkpoint == NULL) {
			say_info("Can't find previous vylog");
			return NULL;
		}
		vclock_copy(&vy_log.last_checkpoint, prev_checkpoint);
		assert(vclock_compare(&vy_log.last_checkpoint, vclock) == 0);
	}
	if (cmp < 0) {
		/*
		 * Last vy_log log is older than the last snapshot.
		 * This happens if we are recovering from a backup.
		 * Rotate the log to keep its signature in sync with
		 * checkpoint.
		 */
		if (vy_log_rotate(vclock) != 0)
			return NULL;
	}

	/*
	 * If we are recovering from a vylog that has an unfinished
	 * rebootstrap section, checkpoint (and hence rebootstrap)
	 * failed, and we need to mark rebootstrap as aborted.
	 */
	struct vy_recovery *recovery;
	recovery = vy_recovery_new(vclock_sum(&vy_log.last_checkpoint),
				   VY_RECOVERY_ABORT_REBOOTSTRAP);
	if (recovery == NULL)
		return NULL;

	if (recovery->in_rebootstrap) {
		struct vy_log_record record;
		vy_log_record_init(&record);
		record.type = VY_LOG_ABORT_REBOOTSTRAP;
		vy_log_tx_begin();
		vy_log_write(&record);
		if (vy_log_tx_commit() != 0) {
			vy_recovery_delete(recovery);
			return NULL;
		}
	}

	vy_log.next_id = recovery->max_id + 1;
	vy_log.recovery = recovery;
	return recovery;
}

int
vy_log_end_recovery(void)
{
	assert(vy_log.recovery != NULL);

	/*
	 * Update the recovery context with records written during
	 * recovery - we will need them for garbage collection.
	 */
	struct vy_log_tx *tx;
	stailq_foreach_entry(tx, &vy_log.pending_tx, in_pending) {
		struct vy_log_record *record;
		stailq_foreach_entry(record, &tx->records, in_tx)
			vy_recovery_process_record(vy_log.recovery, record);
	}

	/* Flush all pending records. */
	if (vy_log_flush() < 0) {
		diag_log();
		say_error("failed to flush vylog after recovery");
		return -1;
	}

	xdir_collect_inprogress(&vy_log.dir);
	vy_log.recovery = NULL;
	return 0;
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
	int64_t signature = vclock_sum(vclock);
	int64_t prev_signature = vclock_sum(&vy_log.last_checkpoint);

	assert(vy_log.recovery == NULL);

	/*
	 * This function is called right after bootstrap (by snapshot),
	 * in which case old and new signatures coincide and there's
	 * nothing we need to do.
	 */
	if (signature == prev_signature)
		return 0;

	assert(signature > prev_signature);
	say_verbose("rotating vylog %lld => %lld",
		    (long long)prev_signature, (long long)signature);

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

	struct vy_recovery *recovery;
	recovery = vy_recovery_new_locked(prev_signature, 0);
	if (recovery == NULL)
		goto fail;

	/* Do actual work from coio so as not to stall tx thread. */
	int rc = coio_call(vy_log_rotate_f, recovery, vclock);
	vy_recovery_delete(recovery);
	if (rc < 0) {
		diag_log();
		say_error("failed to write `%s'", vy_log_filename(signature));
		goto fail;
	}

	/*
	 * Success. Close the old log. The new one will be opened
	 * automatically on the first write (see wal_write_vy_log()).
	 */
	wal_rotate_vy_log();
	vclock_copy(&vy_log.last_checkpoint, vclock);

	/* Add the new vclock to the xdir so that we can track it. */
	xdir_add_vclock(&vy_log.dir, vclock);

	latch_unlock(&vy_log.latch);
	say_verbose("done rotating vylog");
	return 0;
fail:
	latch_unlock(&vy_log.latch);
	return -1;
}

void
vy_log_collect_garbage(const struct vclock *vclock)
{
	/*
	 * Always keep the previous file, because
	 * it is still needed for backups.
	 */
	vclock = vy_log_prev_checkpoint(vclock);
	if (vclock == NULL)
		return;
	xdir_collect_garbage(&vy_log.dir, vclock_sum(vclock), XDIR_GC_ASYNC);
}

int64_t
vy_log_signature(void)
{
	return vclock_sum(&vy_log.last_checkpoint);
}

const char *
vy_log_backup_path(const struct vclock *vclock)
{
	/*
	 * Use the previous log file, because the current one
	 * contains records written after the last checkpoint.
	 */
	vclock = vy_log_prev_checkpoint(vclock);
	if (vclock == NULL)
		return NULL;
	const char *path = vy_log_filename(vclock_sum(vclock));
	if (access(path, F_OK) == -1 && errno == ENOENT)
		return NULL; /* vinyl not used */
	return path;
}

void
vy_log_tx_begin(void)
{
	assert(!vy_log.tx_failed);
	assert(vy_log.tx == NULL);
	vy_log.tx = vy_log_tx_new();
	if (vy_log.tx == NULL) {
		diag_move(diag_get(), &vy_log.tx_diag);
		vy_log.tx_failed = true;
	}
	say_verbose("begin vylog transaction");
}

int
vy_log_tx_commit(void)
{
	/*
	 * During recovery, we may replay records we failed to commit
	 * before restart (e.g. drop LSM tree). Since the log isn't open
	 * yet, simply leave them in the tx buffer to be flushed upon
	 * recovery completion.
	 */
	if (vy_log.recovery != NULL) {
		vy_log_tx_try_commit();
		return 0;
	}

	struct vy_log_tx *tx = vy_log.tx;
	vy_log.tx = NULL;

	if (vy_log.tx_failed) {
		diag_move(&vy_log.tx_diag, diag_get());
		vy_log.tx_failed = false;
		if (tx != NULL)
			vy_log_tx_delete(tx);
		goto err;
	}

	assert(tx != NULL);
	/*
	 * Before writing this transaction, flush all pending ones
	 * if any, because they were committed first.
	 */
	latch_lock(&vy_log.latch);
	int rc = vy_log_flush();
	if (rc == 0)
		rc = vy_log_tx_flush(tx);
	latch_unlock(&vy_log.latch);

	vy_log_tx_delete(tx);
	if (rc != 0)
		goto err;

	say_verbose("commit vylog transaction");
	return 0;
err:
	say_verbose("rollback vylog transaction");
	return -1;
}

void
vy_log_tx_try_commit(void)
{
	if (vy_log.tx_failed) {
		diag_move(&vy_log.tx_diag, diag_get());
		diag_log();
		panic("non-discardable vylog transaction failed");
	}
	assert(vy_log.tx != NULL);
	stailq_add_tail_entry(&vy_log.pending_tx, vy_log.tx, in_pending);
	fiber_cond_signal(&vy_log.flusher_cond);
	vy_log.tx = NULL;
	say_verbose("commit vylog transaction");
}

void
vy_log_write(const struct vy_log_record *record)
{
	say_verbose("write vylog record: %s", vy_log_record_str(record));

	if (vy_log.tx_failed)
		return;

	assert(vy_log.tx != NULL);
	struct vy_log_record *tx_record = vy_log_record_dup(&vy_log.tx->region,
							    record);
	if (tx_record == NULL) {
		diag_move(diag_get(), &vy_log.tx_diag);
		vy_log.tx_failed = true;
		return;
	}
	stailq_add_tail_entry(&vy_log.tx->records, tx_record, in_tx);
}

/**
 * Given space_id and index_id, return the corresponding key in
 * vy_recovery::index_id_hash map.
 */
static inline int64_t
vy_recovery_index_id_hash(uint32_t space_id, uint32_t index_id)
{
	return ((uint64_t)space_id << 32) + index_id;
}

/** Lookup an LSM tree in vy_recovery::index_id_hash map. */
struct vy_lsm_recovery_info *
vy_recovery_lsm_by_index_id(struct vy_recovery *recovery,
			    uint32_t space_id, uint32_t index_id)
{
	int64_t key = vy_recovery_index_id_hash(space_id, index_id);
	struct mh_i64ptr_t *h = recovery->index_id_hash;
	mh_int_t k = mh_i64ptr_find(h, key, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup an LSM tree in vy_recovery::index_hash map. */
static struct vy_lsm_recovery_info *
vy_recovery_lookup_lsm(struct vy_recovery *recovery, int64_t id)
{
	struct mh_i64ptr_t *h = recovery->lsm_hash;
	mh_int_t k = mh_i64ptr_find(h, id, NULL);
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

/** Lookup a vinyl slice in vy_recovery::slice_hash map. */
static struct vy_slice_recovery_info *
vy_recovery_lookup_slice(struct vy_recovery *recovery, int64_t slice_id)
{
	struct mh_i64ptr_t *h = recovery->slice_hash;
	mh_int_t k = mh_i64ptr_find(h, slice_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/**
 * Allocate duplicate of the data of key_part_count
 * key_part_def objects. This function is required because the
 * original key_part passed as an argument can have non-NULL
 * path fields referencing other memory fragments.
 *
 * Returns the key_part_def on success, NULL on error.
 */
struct key_part_def *
vy_recovery_alloc_key_parts(const struct key_part_def *key_parts,
			    uint32_t key_part_count)
{
	uint32_t new_parts_sz = sizeof(*key_parts) * key_part_count;
	for (uint32_t i = 0; i < key_part_count; i++) {
		new_parts_sz += key_parts[i].path != NULL ?
				strlen(key_parts[i].path) + 1 : 0;
	}
	struct key_part_def *new_parts = malloc(new_parts_sz);
	if (new_parts == NULL) {
		diag_set(OutOfMemory, sizeof(*key_parts) * key_part_count,
			 "malloc", "struct key_part_def");
		return NULL;
	}
	memcpy(new_parts, key_parts, sizeof(*key_parts) * key_part_count);
	char *path_pool =
		(char *)new_parts + sizeof(*key_parts) * key_part_count;
	for (uint32_t i = 0; i < key_part_count; i++) {
		if (key_parts[i].path == NULL)
			continue;
		char *path = path_pool;
		uint32_t path_len = strlen(key_parts[i].path);
		path_pool += path_len + 1;
		memcpy(path, key_parts[i].path, path_len);
		path[path_len] = '\0';
		new_parts[i].path = path;
	}
	assert(path_pool == (char *)new_parts + new_parts_sz);
	return new_parts;
}

/**
 * Allocate a new LSM tree with the given ID and add it to
 * the recovery context.
 *
 * Returns the new LSM tree on success, NULL on error.
 */
static struct vy_lsm_recovery_info *
vy_recovery_do_create_lsm(struct vy_recovery *recovery, int64_t id,
			  uint32_t space_id, uint32_t index_id,
			  uint32_t group_id,
			  const struct key_part_def *key_parts,
			  uint32_t key_part_count)
{
	if (key_parts == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Missing key definition for LSM tree %lld",
				    (long long)id));
		return NULL;
	}
	struct vy_lsm_recovery_info *lsm = malloc(sizeof(*lsm));
	if (lsm == NULL) {
		diag_set(OutOfMemory, sizeof(*lsm),
			 "malloc", "struct vy_lsm_recovery_info");
		return NULL;
	}
	lsm->key_parts = vy_recovery_alloc_key_parts(key_parts, key_part_count);
	if (lsm->key_parts == NULL) {
		free(lsm);
		return NULL;
	}
	struct mh_i64ptr_t *h = recovery->lsm_hash;
	struct mh_i64ptr_node_t node = { id, lsm };
	struct mh_i64ptr_node_t *old_node = NULL;
	mh_i64ptr_put(h, &node, &old_node, NULL);
	assert(old_node == NULL);
	lsm->id = id;
	lsm->space_id = space_id;
	lsm->index_id = index_id;
	lsm->group_id = group_id;
	lsm->key_part_count = key_part_count;
	lsm->create_lsn = -1;
	lsm->modify_lsn = -1;
	lsm->drop_lsn = -1;
	lsm->dump_lsn = -1;
	lsm->prepared = NULL;
	rlist_create(&lsm->ranges);
	rlist_create(&lsm->runs);
	/*
	 * Keep newer LSM trees closer to the tail of the list
	 * so that on log rotation we create/drop past incarnations
	 * before the final version.
	 */
	rlist_add_tail_entry(&recovery->lsms, lsm, in_recovery);
	lsm->in_rebootstrap = recovery->in_rebootstrap;
	if (recovery->max_id < id)
		recovery->max_id = id;
	return lsm;
}

/**
 * Handle a VY_LOG_PREPARE_LSM log record.
 *
 * This function allocates a new LSM tree with the given ID and
 * either associates it with the existing LSM tree hashed under
 * the same space_id/index_id or inserts it into the hash if
 * there's none.
 *
 * Note, we link incomplete LSM trees to index_id_hash (either
 * directly or indirectly via vy_lsm_recovery_info::prepared),
 * because an LSM tree may have been fully built and logged in
 * WAL, but not committed to vylog. We need to be able to identify
 * such LSM trees during local recovery so that instead of
 * rebuilding them we can simply retry vylog write.
 *
 * Returns 0 on success, -1 on error.
 */
static int
vy_recovery_prepare_lsm(struct vy_recovery *recovery, int64_t id,
			uint32_t space_id, uint32_t index_id,
			uint32_t group_id,
			const struct key_part_def *key_parts,
			uint32_t key_part_count)
{
	if (vy_recovery_lookup_lsm(recovery, id) != NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Duplicate LSM tree id %lld",
				    (long long)id));
		return -1;
	}
	if (vy_recovery_do_create_lsm(recovery, id, space_id, index_id,
				      group_id, key_parts,
				      key_part_count) == NULL)
		return -1;
	return 0;
}

/**
 * Handle a VY_LOG_CREATE_LSM log record.
 *
 * Depending on whether the LSM tree was previously prepared,
 * this function either commits it or allocates a new one and
 * inserts it into the recovery hash.
 *
 * Returns 0 on success, -1 on error.
 */
static int
vy_recovery_create_lsm(struct vy_recovery *recovery, int64_t id,
		       uint32_t space_id, uint32_t index_id, uint32_t group_id,
		       const struct key_part_def *key_parts,
		       uint32_t key_part_count, int64_t create_lsn,
		       int64_t modify_lsn, int64_t dump_lsn)
{
	struct vy_lsm_recovery_info *lsm;
	lsm = vy_recovery_lookup_lsm(recovery, id);
	if (lsm != NULL) {
		/*
		 * If the LSM tree already exists, it must be in
		 * the prepared state (i.e. not committed or dropped).
		 */
		if (lsm->create_lsn >= 0 || lsm->drop_lsn >= 0) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Duplicate LSM tree id %lld",
					    (long long)id));
			return -1;
		}
	} else {
		lsm = vy_recovery_do_create_lsm(recovery, id, space_id,
						index_id, group_id,
						key_parts, key_part_count);
		if (lsm == NULL)
			return -1;
		lsm->dump_lsn = dump_lsn;
	}

	/* Mark the LSM tree committed by assigning LSN. */
	lsm->create_lsn = create_lsn;
	lsm->modify_lsn = modify_lsn;
	return 0;
}

/**
 * Handle a VY_LOG_MODIFY_LSM log record.
 * This function updates key definition of the LSM tree with ID @id.
 * Return 0 on success, -1 on failure.
 */
static int
vy_recovery_modify_lsm(struct vy_recovery *recovery, int64_t id,
		       const struct key_part_def *key_parts,
		       uint32_t key_part_count, int64_t modify_lsn)
{
	struct vy_lsm_recovery_info *lsm;
	lsm = vy_recovery_lookup_lsm(recovery, id);
	if (lsm == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Update of unregistered LSM tree %lld",
				    (long long)id));
		return -1;
	}
	if (lsm->drop_lsn >= 0) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Update of deleted LSM tree %lld",
				    (long long)id));
		return -1;
	}
	free(lsm->key_parts);
	lsm->key_parts = vy_recovery_alloc_key_parts(key_parts, key_part_count);
	if (lsm->key_parts == NULL)
		return -1;
	lsm->key_part_count = key_part_count;
	lsm->modify_lsn = modify_lsn;
	return 0;
}

/**
 * Handle a VY_LOG_DROP_LSM log record.
 * This function marks the LSM tree with ID @id as dropped.
 * Returns 0 on success, -1 if ID not found or LSM tree is already marked.
 */
static int
vy_recovery_drop_lsm(struct vy_recovery *recovery, int64_t id, int64_t drop_lsn)
{
	struct vy_lsm_recovery_info *lsm;
	lsm = vy_recovery_lookup_lsm(recovery, id);
	if (lsm == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("LSM tree %lld deleted but not registered",
				    (long long)id));
		return -1;
	}
	if (lsm->drop_lsn >= 0) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("LSM tree %lld deleted twice",
				    (long long)id));
		return -1;
	}
	assert(drop_lsn >= 0);
	lsm->drop_lsn = drop_lsn;
	return 0;
}

/**
 * Handle a VY_LOG_FORGET_LSM log record.
 * This function removes the LSM tree with ID @id from the context.
 * All ranges and runs of the LSM tree must have been deleted by now.
 * Returns 0 on success, -1 if ID was not found or there are objects
 * associated with the LSM tree.
 */
static int
vy_recovery_forget_lsm(struct vy_recovery *recovery, int64_t id)
{
	struct mh_i64ptr_t *h = recovery->lsm_hash;
	mh_int_t k = mh_i64ptr_find(h, id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("LSM tree %lld forgotten but not registered",
				    (long long)id));
		return -1;
	}
	struct vy_lsm_recovery_info *lsm = mh_i64ptr_node(h, k)->val;
	if (!rlist_empty(&lsm->ranges) || !rlist_empty(&lsm->runs)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Forgotten LSM tree %lld has ranges/runs",
				    (long long)id));
		return -1;
	}
	mh_i64ptr_del(h, k, NULL);
	rlist_del_entry(lsm, in_recovery);
	free(lsm->key_parts);
	free(lsm);
	return 0;
}

/**
 * Handle a VY_LOG_DUMP_LSM log record.
 * This function updates LSN of the last dump of the LSM tree
 * with ID @id.
 * Returns 0 on success, -1 if ID not found or LSM tree is dropped.
 */
static int
vy_recovery_dump_lsm(struct vy_recovery *recovery,
		     int64_t id, int64_t dump_lsn)
{
	struct vy_lsm_recovery_info *lsm;
	lsm = vy_recovery_lookup_lsm(recovery, id);
	if (lsm == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Dump of unregistered LSM tree %lld",
				    (long long)id));
		return -1;
	}
	lsm->dump_lsn = MAX(lsm->dump_lsn, dump_lsn);
	return 0;
}

/**
 * Allocate a vinyl run with ID @run_id and insert it to the hash.
 * Return the new run on success, NULL on OOM.
 */
static struct vy_run_recovery_info *
vy_recovery_do_create_run(struct vy_recovery *recovery, int64_t run_id)
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
	mh_i64ptr_put(h, &node, &old_node, NULL);
	assert(old_node == NULL);
	run->id = run_id;
	run->dump_lsn = -1;
	run->gc_lsn = -1;
	run->dump_count = 0;
	run->is_incomplete = false;
	run->is_dropped = false;
	run->data = NULL;
	rlist_create(&run->in_lsm);
	if (recovery->max_id < run_id)
		recovery->max_id = run_id;
	return run;
}

/**
 * Handle a VY_LOG_PREPARE_RUN log record.
 * This function creates a new incomplete vinyl run with ID @run_id
 * and adds it to the list of runs of the LSM tree with ID @lsm_id.
 * Return 0 on success, -1 if run already exists, LSM tree not found,
 * or OOM.
 */
static int
vy_recovery_prepare_run(struct vy_recovery *recovery, int64_t lsm_id,
			int64_t run_id)
{
	struct vy_lsm_recovery_info *lsm;
	lsm = vy_recovery_lookup_lsm(recovery, lsm_id);
	if (lsm == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld created for unregistered "
				    "LSM tree %lld", (long long)run_id,
				    (long long)lsm_id));
		return -1;
	}
	if (vy_recovery_lookup_run(recovery, run_id) != NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Duplicate run id %lld",
				    (long long)run_id));
		return -1;
	}
	struct vy_run_recovery_info *run;
	run = vy_recovery_do_create_run(recovery, run_id);
	if (run == NULL)
		return -1;
	run->is_incomplete = true;
	rlist_add_entry(&lsm->runs, run, in_lsm);
	return 0;
}

/**
 * Handle a VY_LOG_CREATE_RUN log record.
 * This function adds the vinyl run with ID @run_id to the list
 * of runs of the LSM tree with ID @sm_id and marks it committed.
 * If the run does not exist, it will be created.
 * Return 0 on success, -1 if LSM tree not found, run or LSM tree
 * is dropped, or OOM.
 */
static int
vy_recovery_create_run(struct vy_recovery *recovery, int64_t lsm_id,
		       int64_t run_id, int64_t dump_lsn, uint32_t dump_count)
{
	struct vy_lsm_recovery_info *lsm;
	lsm = vy_recovery_lookup_lsm(recovery, lsm_id);
	if (lsm == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld created for unregistered "
				    "LSM tree %lld", (long long)run_id,
				    (long long)lsm_id));
		return -1;
	}
	struct vy_run_recovery_info *run;
	run = vy_recovery_lookup_run(recovery, run_id);
	if (run != NULL && run->is_dropped) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld committed after deletion",
				    (long long)run_id));
		return -1;
	}
	if (run == NULL) {
		run = vy_recovery_do_create_run(recovery, run_id);
		if (run == NULL)
			return -1;
	}
	run->dump_lsn = dump_lsn;
	run->dump_count = dump_count;
	run->is_incomplete = false;
	rlist_move_entry(&lsm->runs, run, in_lsm);
	return 0;
}

/**
 * Handle a VY_LOG_DROP_RUN log record.
 * This function marks the vinyl run with ID @run_id as deleted.
 * Note, the run is not removed from the recovery context until it is
 * "forgotten", because it is still needed for garbage collection.
 * Return 0 on success, -1 if run not found or already deleted.
 */
static int
vy_recovery_drop_run(struct vy_recovery *recovery, int64_t run_id,
		     int64_t gc_lsn)
{
	struct vy_run_recovery_info *run;
	run = vy_recovery_lookup_run(recovery, run_id);
	if (run == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld deleted but not registered",
				    (long long)run_id));
		return -1;
	}
	if (run->is_dropped) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld deleted twice",
				    (long long)run_id));
		return -1;
	}
	run->is_dropped = true;
	run->gc_lsn = gc_lsn;
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
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld forgotten but not registered",
				    (long long)run_id));
		return -1;
	}
	struct vy_run_recovery_info *run = mh_i64ptr_node(h, k)->val;
	mh_i64ptr_del(h, k, NULL);
	rlist_del_entry(run, in_lsm);
	free(run);
	return 0;
}

/**
 * Handle a VY_LOG_INSERT_RANGE log record.
 * This function allocates a new vinyl range with ID @range_id,
 * inserts it to the hash, and adds it to the list of ranges of the
 * LSM tree with ID @lsm_id.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
vy_recovery_insert_range(struct vy_recovery *recovery, int64_t lsm_id,
			 int64_t range_id, const char *begin, const char *end)
{
	if (vy_recovery_lookup_range(recovery, range_id) != NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Duplicate range id %lld",
				    (long long)range_id));
		return -1;
	}
	struct vy_lsm_recovery_info *lsm;
	lsm = vy_recovery_lookup_lsm(recovery, lsm_id);
	if (lsm == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Range %lld created for unregistered "
				    "LSM tree %lld", (long long)range_id,
				    (long long)lsm_id));
		return -1;
	}

	size_t size = sizeof(struct vy_range_recovery_info);
	const char *data;
	data = begin;
	if (data != NULL)
		mp_next(&data);
	size_t begin_size = data - begin;
	size += begin_size;
	data = end;
	if (data != NULL)
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
	mh_i64ptr_put(h, &node, NULL, NULL);
	range->id = range_id;
	if (begin != NULL) {
		range->begin = (void *)range + sizeof(*range);
		memcpy(range->begin, begin, begin_size);
	} else
		range->begin = NULL;
	if (end != NULL) {
		range->end = (void *)range + sizeof(*range) + begin_size;
		memcpy(range->end, end, end_size);
	} else
		range->end = NULL;
	rlist_create(&range->slices);
	rlist_add_entry(&lsm->ranges, range, in_lsm);
	if (recovery->max_id < range_id)
		recovery->max_id = range_id;
	return 0;
}

/**
 * Handle a VY_LOG_DELETE_RANGE log record.
 * This function frees the vinyl range with ID @range_id.
 * All slices of the range must have been deleted by now.
 * Return 0 on success, -1 if range not found.
 */
static int
vy_recovery_delete_range(struct vy_recovery *recovery, int64_t range_id)
{
	struct mh_i64ptr_t *h = recovery->range_hash;
	mh_int_t k = mh_i64ptr_find(h, range_id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Range %lld deleted but not registered",
				    (long long)range_id));
		return -1;
	}
	struct vy_range_recovery_info *range = mh_i64ptr_node(h, k)->val;
	if (!rlist_empty(&range->slices)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Deleted range %lld has run slices",
				    (long long)range_id));
		return -1;
	}
	mh_i64ptr_del(h, k, NULL);
	rlist_del_entry(range, in_lsm);
	free(range);
	return 0;
}

/**
 * Handle a VY_LOG_INSERT_SLICE log record.
 * This function allocates a new slice with ID @slice_id for
 * the run with ID @run_id, inserts it into the hash, and adds
 * it to the list of slices of the range with ID @range_id.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
vy_recovery_insert_slice(struct vy_recovery *recovery, int64_t range_id,
			 int64_t run_id, int64_t slice_id,
			 const char *begin, const char *end)
{
	if (vy_recovery_lookup_slice(recovery, slice_id) != NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Duplicate slice id %lld",
				    (long long)slice_id));
		return -1;
	}
	struct vy_range_recovery_info *range;
	range = vy_recovery_lookup_range(recovery, range_id);
	if (range == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Slice %lld created for unregistered "
				    "range %lld", (long long)slice_id,
				    (long long)range_id));
		return -1;
	}
	struct vy_run_recovery_info *run;
	run = vy_recovery_lookup_run(recovery, run_id);
	if (run == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Slice %lld created for unregistered "
				    "run %lld", (long long)slice_id,
				    (long long)run_id));
		return -1;
	}

	size_t size = sizeof(struct vy_slice_recovery_info);
	const char *data;
	data = begin;
	if (data != NULL)
		mp_next(&data);
	size_t begin_size = data - begin;
	size += begin_size;
	data = end;
	if (data != NULL)
		mp_next(&data);
	size_t end_size = data - end;
	size += end_size;

	struct vy_slice_recovery_info *slice = malloc(size);
	if (slice == NULL) {
		diag_set(OutOfMemory, size,
			 "malloc", "struct vy_slice_recovery_info");
		return -1;
	}
	struct mh_i64ptr_t *h = recovery->slice_hash;
	struct mh_i64ptr_node_t node = { slice_id, slice };
	mh_i64ptr_put(h, &node, NULL, NULL);
	slice->id = slice_id;
	slice->run = run;
	if (begin != NULL) {
		slice->begin = (void *)slice + sizeof(*slice);
		memcpy(slice->begin, begin, begin_size);
	} else
		slice->begin = NULL;
	if (end != NULL) {
		slice->end = (void *)slice + sizeof(*slice) + begin_size;
		memcpy(slice->end, end, end_size);
	} else
		slice->end = NULL;
	/*
	 * If dump races with compaction, an older slice created by
	 * compaction may be added after a newer slice created by
	 * dump. Make sure that the list stays sorted by LSN in any
	 * case.
	 */
	struct vy_slice_recovery_info *next_slice;
	rlist_foreach_entry(next_slice, &range->slices, in_range) {
		if (next_slice->run->dump_lsn < slice->run->dump_lsn)
			break;
	}
	rlist_add_tail(&next_slice->in_range, &slice->in_range);
	if (recovery->max_id < slice_id)
		recovery->max_id = slice_id;
	return 0;
}

/**
 * Handle a VY_LOG_DELETE_SLICE log record.
 * This function frees the vinyl slice with ID @slice_id.
 * Return 0 on success, -1 if slice not found.
 */
static int
vy_recovery_delete_slice(struct vy_recovery *recovery, int64_t slice_id)
{
	struct mh_i64ptr_t *h = recovery->slice_hash;
	mh_int_t k = mh_i64ptr_find(h, slice_id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Slice %lld deleted but not registered",
				    (long long)slice_id));
		return -1;
	}
	struct vy_slice_recovery_info *slice = mh_i64ptr_node(h, k)->val;
	mh_i64ptr_del(h, k, NULL);
	rlist_del_entry(slice, in_range);
	free(slice);
	return 0;
}

/**
 * Mark all LSM trees created during rebootstrap as dropped so
 * that they will be purged on the next garbage collection.
 */
static void
vy_recovery_do_abort_rebootstrap(struct vy_recovery *recovery)
{
	struct vy_lsm_recovery_info *lsm;
	rlist_foreach_entry(lsm, &recovery->lsms, in_recovery) {
		if (lsm->in_rebootstrap) {
			lsm->in_rebootstrap = false;
			lsm->create_lsn = -1;
			lsm->modify_lsn = -1;
			lsm->drop_lsn = 0;
		}
	}
}

/** Handle a VY_LOG_REBOOTSTRAP log record. */
static void
vy_recovery_rebootstrap(struct vy_recovery *recovery)
{
	if (recovery->in_rebootstrap)
		vy_recovery_do_abort_rebootstrap(recovery);
	recovery->in_rebootstrap = true;
}

/** Handle VY_LOG_ABORT_REBOOTSTRAP record. */
static void
vy_recovery_abort_rebootstrap(struct vy_recovery *recovery)
{
	if (recovery->in_rebootstrap)
		vy_recovery_do_abort_rebootstrap(recovery);
	recovery->in_rebootstrap = false;
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
	int rc = 0;
	switch (record->type) {
	case VY_LOG_PREPARE_LSM:
		rc = vy_recovery_prepare_lsm(recovery, record->lsm_id,
				record->space_id, record->index_id,
				record->group_id, record->key_parts,
				record->key_part_count);
		break;
	case VY_LOG_CREATE_LSM:
		rc = vy_recovery_create_lsm(recovery, record->lsm_id,
				record->space_id, record->index_id,
				record->group_id, record->key_parts,
				record->key_part_count, record->create_lsn,
				record->modify_lsn, record->dump_lsn);
		break;
	case VY_LOG_MODIFY_LSM:
		rc = vy_recovery_modify_lsm(recovery, record->lsm_id,
				record->key_parts, record->key_part_count,
				record->modify_lsn);
		break;
	case VY_LOG_DROP_LSM:
		rc = vy_recovery_drop_lsm(recovery, record->lsm_id,
					  record->drop_lsn);
		break;
	case VY_LOG_FORGET_LSM:
		rc = vy_recovery_forget_lsm(recovery, record->lsm_id);
		break;
	case VY_LOG_INSERT_RANGE:
		rc = vy_recovery_insert_range(recovery, record->lsm_id,
				record->range_id, record->begin, record->end);
		break;
	case VY_LOG_DELETE_RANGE:
		rc = vy_recovery_delete_range(recovery, record->range_id);
		break;
	case VY_LOG_PREPARE_RUN:
		rc = vy_recovery_prepare_run(recovery, record->lsm_id,
					     record->run_id);
		break;
	case VY_LOG_CREATE_RUN:
		rc = vy_recovery_create_run(recovery, record->lsm_id,
					    record->run_id, record->dump_lsn,
					    record->dump_count);
		break;
	case VY_LOG_DROP_RUN:
		rc = vy_recovery_drop_run(recovery, record->run_id,
					  record->gc_lsn);
		break;
	case VY_LOG_FORGET_RUN:
		rc = vy_recovery_forget_run(recovery, record->run_id);
		break;
	case VY_LOG_INSERT_SLICE:
		rc = vy_recovery_insert_slice(recovery, record->range_id,
					      record->run_id, record->slice_id,
					      record->begin, record->end);
		break;
	case VY_LOG_DELETE_SLICE:
		rc = vy_recovery_delete_slice(recovery, record->slice_id);
		break;
	case VY_LOG_DUMP_LSM:
		rc = vy_recovery_dump_lsm(recovery, record->lsm_id,
					    record->dump_lsn);
		break;
	case VY_LOG_TRUNCATE_LSM:
		/* Not used anymore, ignore. */
		rc = 0;
		break;
	case VY_LOG_REBOOTSTRAP:
		vy_recovery_rebootstrap(recovery);
		break;
	case VY_LOG_ABORT_REBOOTSTRAP:
		vy_recovery_abort_rebootstrap(recovery);
		break;
	default:
		unreachable();
	}
	if (rc != 0)
		say_error("failed to process vylog record: %s",
			  vy_log_record_str(record));
	return rc;
}

/**
 * Commit the last rebootstrap attempt - drop all objects created
 * before rebootstrap.
 */
static void
vy_recovery_commit_rebootstrap(struct vy_recovery *recovery)
{
	assert(recovery->in_rebootstrap);
	struct vy_lsm_recovery_info *lsm;
	rlist_foreach_entry(lsm, &recovery->lsms, in_recovery) {
		if (!lsm->in_rebootstrap && lsm->drop_lsn < 0) {
			/*
			 * The files will be removed when the current
			 * checkpoint is purged by garbage collector.
			 */
			lsm->drop_lsn = vy_log_signature();
		}
	}
}

/**
 * Fill index_id_hash with LSM trees recovered from vylog.
 */
static int
vy_recovery_build_index_id_hash(struct vy_recovery *recovery)
{
	struct mh_i64ptr_t *h = recovery->index_id_hash;
	struct vy_lsm_recovery_info *lsm;

	rlist_foreach_entry(lsm, &recovery->lsms, in_recovery) {
		/*
		 * If an LSM tree was dropped but was not committed,
		 * it must be a product of aborted ALTER, in which
		 * case it won't be recovered and hence shouldn't be
		 * inserted into the hash.
		 */
		if (lsm->create_lsn < 0 && lsm->drop_lsn >= 0)
			continue;

		uint32_t space_id = lsm->space_id;
		uint32_t index_id = lsm->index_id;
		struct vy_lsm_recovery_info *hashed_lsm;
		hashed_lsm = vy_recovery_lsm_by_index_id(recovery,
						space_id, index_id);
		/*
		 * If there's no LSM tree for these space_id/index_id
		 * or it was dropped, simply replace it with the latest
		 * committed LSM tree version.
		 */
		if (hashed_lsm == NULL ||
		    (hashed_lsm->drop_lsn >= 0 && lsm->create_lsn >= 0)) {
			struct mh_i64ptr_node_t node;
			node.key = vy_recovery_index_id_hash(space_id, index_id);
			node.val = lsm;
			mh_i64ptr_put(h, &node, NULL, NULL);
			continue;
		}
		/*
		 * If there's an LSM tree with the same space_id/index_id
		 * and it isn't dropped, the new LSM tree must have been
		 * prepared by ALTER but not committed. In this case the
		 * old LSM tree must be committed and not have a prepared
		 * LSM tree. Check that and link the new LSM tree to the
		 * old one.
		 */
		if (lsm->create_lsn >= 0) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("LSM tree %u/%u created twice",
					    (unsigned)space_id,
					    (unsigned)index_id));
			return -1;
		}
		if (hashed_lsm->create_lsn < 0 ||
		    hashed_lsm->prepared != NULL) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("LSM tree %u/%u prepared twice",
					    (unsigned)space_id,
					    (unsigned)index_id));
			return -1;
		}
		hashed_lsm->prepared = lsm;
	}
	return 0;
}

static ssize_t
vy_recovery_new_f(va_list ap)
{
	int64_t signature = va_arg(ap, int64_t);
	int flags = va_arg(ap, int);
	struct vy_recovery **p_recovery = va_arg(ap, struct vy_recovery **);

	say_verbose("loading vylog %lld", (long long)signature);

	struct vy_recovery *recovery = malloc(sizeof(*recovery));
	if (recovery == NULL) {
		diag_set(OutOfMemory, sizeof(*recovery),
			 "malloc", "struct vy_recovery");
		goto fail;
	}

	rlist_create(&recovery->lsms);
	recovery->index_id_hash = NULL;
	recovery->lsm_hash = NULL;
	recovery->range_hash = NULL;
	recovery->run_hash = NULL;
	recovery->slice_hash = NULL;
	recovery->max_id = -1;
	recovery->in_rebootstrap = false;

	recovery->index_id_hash = mh_i64ptr_new();
	recovery->lsm_hash = mh_i64ptr_new();
	recovery->range_hash = mh_i64ptr_new();
	recovery->run_hash = mh_i64ptr_new();
	recovery->slice_hash = mh_i64ptr_new();

	/*
	 * We don't create a log file if there are no objects to
	 * be stored in it, so if the log doesn't exist, assume
	 * the recovery context is empty.
	 */
	const char *path = vy_log_filename(signature);
	if (access(path, F_OK) < 0 && errno == ENOENT)
		goto out;

	struct xlog_cursor cursor;
	if (xdir_open_cursor(&vy_log.dir, signature, &cursor) < 0)
		goto fail_free;

	int rc;
	struct xrow_header row;
	while ((rc = xlog_cursor_next(&cursor, &row, false)) == 0) {
		struct vy_log_record record;
		rc = vy_log_record_decode(&record, &row);
		if (rc < 0)
			break;
		say_verbose("load vylog record: %s",
			    vy_log_record_str(&record));
		if (record.type == VY_LOG_SNAPSHOT) {
			if ((flags & VY_RECOVERY_LOAD_CHECKPOINT) != 0)
				break;
			continue;
		}
		rc = vy_recovery_process_record(recovery, &record);
		if (rc < 0)
			break;
		fiber_gc();
	}
	fiber_gc();
	if (rc < 0)
		goto fail_close;

	xlog_cursor_close(&cursor, false);

	if (recovery->in_rebootstrap) {
		if ((flags & VY_RECOVERY_ABORT_REBOOTSTRAP) != 0)
			vy_recovery_do_abort_rebootstrap(recovery);
		else
			vy_recovery_commit_rebootstrap(recovery);
	}

	if (vy_recovery_build_index_id_hash(recovery) != 0)
		goto fail_free;
out:
	say_verbose("done loading vylog");
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
 * Load the metadata log and return a recovery context.
 * Must be called with the log latch held.
 */
static struct vy_recovery *
vy_recovery_new_locked(int64_t signature, int flags)
{
	int rc;
	struct vy_recovery *recovery;

	assert(latch_owner(&vy_log.latch) == fiber());
	/*
	 * Before proceeding to log recovery, make sure that all
	 * pending records have been flushed out.
	 */
	rc = vy_log_flush();
	if (rc != 0) {
		diag_log();
		say_error("failed to flush vylog for recovery");
		return NULL;
	}

	/* Load the log from coio so as not to stall tx thread. */
	rc = coio_call(vy_recovery_new_f, signature, flags, &recovery);
	if (rc != 0) {
		diag_log();
		say_error("failed to load `%s'", vy_log_filename(signature));
		return NULL;
	}
	return recovery;
}

struct vy_recovery *
vy_recovery_new(int64_t signature, int flags)
{
	/* Lock out concurrent writers while we are loading the log. */
	latch_lock(&vy_log.latch);
	struct vy_recovery *recovery;
	recovery = vy_recovery_new_locked(signature, flags);
	latch_unlock(&vy_log.latch);
	return recovery;
}

void
vy_recovery_delete(struct vy_recovery *recovery)
{
	struct vy_lsm_recovery_info *lsm, *next_lsm;
	struct vy_range_recovery_info *range, *next_range;
	struct vy_slice_recovery_info *slice, *next_slice;
	struct vy_run_recovery_info *run, *next_run;

	rlist_foreach_entry_safe(lsm, &recovery->lsms, in_recovery, next_lsm) {
		rlist_foreach_entry_safe(range, &lsm->ranges,
					 in_lsm, next_range) {
			rlist_foreach_entry_safe(slice, &range->slices,
						 in_range, next_slice)
				free(slice);
			free(range);
		}
		rlist_foreach_entry_safe(run, &lsm->runs, in_lsm, next_run)
			free(run);
		free(lsm->key_parts);
		free(lsm);
	}
	if (recovery->index_id_hash != NULL)
		mh_i64ptr_delete(recovery->index_id_hash);
	if (recovery->lsm_hash != NULL)
		mh_i64ptr_delete(recovery->lsm_hash);
	if (recovery->range_hash != NULL)
		mh_i64ptr_delete(recovery->range_hash);
	if (recovery->run_hash != NULL)
		mh_i64ptr_delete(recovery->run_hash);
	if (recovery->slice_hash != NULL)
		mh_i64ptr_delete(recovery->slice_hash);
	TRASH(recovery);
	free(recovery);
}

/** Write a record to vylog. */
static int
vy_log_append_record(struct xlog *xlog, struct vy_log_record *record)
{
	say_verbose("save vylog record: %s", vy_log_record_str(record));

	struct xrow_header row;
	if (vy_log_record_encode(record, &row) < 0)
		return -1;
	if (xlog_write_row(xlog, &row) < 0)
		return -1;
	return 0;
}

/** Write all records corresponding to an LSM tree to vylog. */
static int
vy_log_append_lsm(struct xlog *xlog, struct vy_lsm_recovery_info *lsm)
{
	struct vy_range_recovery_info *range;
	struct vy_slice_recovery_info *slice;
	struct vy_run_recovery_info *run;
	struct vy_log_record record;

	vy_log_record_init(&record);
	record.type = lsm->create_lsn < 0 ?
		VY_LOG_PREPARE_LSM : VY_LOG_CREATE_LSM;
	record.lsm_id = lsm->id;
	record.index_id = lsm->index_id;
	record.space_id = lsm->space_id;
	record.group_id = lsm->group_id;
	record.key_parts = lsm->key_parts;
	record.key_part_count = lsm->key_part_count;
	record.create_lsn = lsm->create_lsn;
	record.modify_lsn = lsm->modify_lsn;
	record.dump_lsn = lsm->dump_lsn;
	if (vy_log_append_record(xlog, &record) != 0)
		return -1;

	rlist_foreach_entry(run, &lsm->runs, in_lsm) {
		vy_log_record_init(&record);
		if (run->is_incomplete) {
			record.type = VY_LOG_PREPARE_RUN;
		} else {
			record.type = VY_LOG_CREATE_RUN;
			record.dump_lsn = run->dump_lsn;
			record.dump_count = run->dump_count;
		}
		record.lsm_id = lsm->id;
		record.run_id = run->id;
		if (vy_log_append_record(xlog, &record) != 0)
			return -1;

		if (!run->is_dropped)
			continue;

		vy_log_record_init(&record);
		record.type = VY_LOG_DROP_RUN;
		record.run_id = run->id;
		record.gc_lsn = run->gc_lsn;
		if (vy_log_append_record(xlog, &record) != 0)
			return -1;
	}

	rlist_foreach_entry(range, &lsm->ranges, in_lsm) {
		vy_log_record_init(&record);
		record.type = VY_LOG_INSERT_RANGE;
		record.lsm_id = lsm->id;
		record.range_id = range->id;
		record.begin = range->begin;
		record.end = range->end;
		if (vy_log_append_record(xlog, &record) != 0)
			return -1;
		/*
		 * Newer slices are stored closer to the head of the list,
		 * while we are supposed to return slices in chronological
		 * order, so use reverse iterator.
		 */
		rlist_foreach_entry_reverse(slice, &range->slices, in_range) {
			vy_log_record_init(&record);
			record.type = VY_LOG_INSERT_SLICE;
			record.range_id = range->id;
			record.slice_id = slice->id;
			record.run_id = slice->run->id;
			record.begin = slice->begin;
			record.end = slice->end;
			if (vy_log_append_record(xlog, &record) != 0)
				return -1;
		}
	}

	if (lsm->drop_lsn >= 0) {
		vy_log_record_init(&record);
		record.type = VY_LOG_DROP_LSM;
		record.lsm_id = lsm->id;
		record.drop_lsn = lsm->drop_lsn;
		if (vy_log_append_record(xlog, &record) != 0)
			return -1;
	}
	return 0;
}

/** Create vylog from a recovery context. */
static int
vy_log_create(const struct vclock *vclock, struct vy_recovery *recovery)
{
	say_verbose("saving vylog %lld", (long long)vclock_sum(vclock));

	/*
	 * Only create the log file if we have something
	 * to write to it.
	 */
	struct xlog xlog;
	xlog_clear(&xlog);

	struct vy_lsm_recovery_info *lsm;
	rlist_foreach_entry(lsm, &recovery->lsms, in_recovery) {
		/* Create the log file on the first write. */
		if (!xlog_is_open(&xlog) &&
		    xdir_create_xlog(&vy_log.dir, &xlog, vclock) != 0)
			goto err_create_xlog;

		if (vy_log_append_lsm(&xlog, lsm) != 0)
			goto err_write_xlog;
	}
	if (!xlog_is_open(&xlog))
		goto done; /* nothing written */

	/* Mark the end of the snapshot. */
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_SNAPSHOT;
	if (vy_log_append_record(&xlog, &record) != 0)
		goto err_write_xlog;

	ERROR_INJECT(ERRINJ_VY_LOG_FILE_RENAME, {
		diag_set(ClientError, ER_INJECTION, "vinyl log file rename");
		xlog_close(&xlog, false);
		return -1;
	});

	/* Finalize the new xlog. */
	if (xlog_flush(&xlog) < 0 ||
	    xlog_sync(&xlog) < 0 ||
	    xlog_rename(&xlog) < 0)
		goto err_write_xlog;

	xlog_close(&xlog, false);
done:
	say_verbose("done saving vylog");
	return 0;

err_write_xlog:
	/* Delete the unfinished xlog. */
	assert(xlog_is_open(&xlog));
	if (unlink(xlog.filename) < 0)
		say_syserror("failed to delete file '%s'",
			     xlog.filename);
	xlog_close(&xlog, false);

err_create_xlog:
	return -1;
}
