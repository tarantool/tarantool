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

#include "vy_stmt.h"

#include <stdlib.h>
#include <string.h>
#include <sys/uio.h> /* struct iovec */
#include <pmatomic.h> /* for refs */

#include "diag.h"
#include <small/region.h>
#include <small/lsregion.h>

#include "error.h"
#include "tuple_format.h"
#include "xrow.h"
#include "fiber.h"

struct tuple_format_vtab vy_tuple_format_vtab = {
	vy_tuple_delete,
};

size_t vy_max_tuple_size = 1024 * 1024;

void
vy_tuple_delete(struct tuple_format *format, struct tuple *tuple)
{
	say_debug("%s(%p)", __func__, tuple);
	assert(tuple->refs == 0);
	/*
	 * Turn off formats referencing in worker threads to avoid
	 * multithread unsafe modifications of a reference
	 * counter.
	 */
	if (cord_is_main())
		tuple_format_unref(format);
#ifndef NDEBUG
	memset(tuple, '#', tuple_size(tuple)); /* fail early */
#endif
	free(tuple);
}

/**
 * Allocate a vinyl statement object on base of the struct tuple
 * with malloc() and the reference counter equal to 1.
 * @param format Format of an index.
 * @param size   Size of the variable part of the statement. It
 *               includes size of MessagePack tuple data and, for
 *               upserts, MessagePack array of operations.
 * @retval not NULL Success.
 * @retval     NULL Memory error.
 */
static struct tuple *
vy_stmt_alloc(struct tuple_format *format, uint32_t bsize)
{
	uint32_t meta_size = tuple_format_meta_size(format);
	uint32_t total_size = sizeof(struct vy_stmt) + meta_size + bsize;
	if (unlikely(total_size > vy_max_tuple_size)) {
		diag_set(ClientError, ER_VINYL_MAX_TUPLE_SIZE,
			 (unsigned) total_size);
		error_log(diag_last_error(diag_get()));
		return NULL;
	}
	struct tuple *tuple = malloc(total_size);
	if (unlikely(tuple == NULL)) {
		diag_set(OutOfMemory, total_size, "malloc", "struct vy_stmt");
		return NULL;
	}
	say_debug("vy_stmt_alloc(format = %d %u, bsize = %zu) = %p",
		format->id, tuple_format_meta_size(format), bsize, tuple);
	tuple->refs = 1;
	tuple->format_id = tuple_format_id(format);
	if (cord_is_main())
		tuple_format_ref(format);
	tuple->bsize = bsize;
	tuple->data_offset = sizeof(struct vy_stmt) + meta_size;;
	vy_stmt_set_lsn(tuple, 0);
	vy_stmt_set_type(tuple, 0);
	return tuple;
}

struct tuple *
vy_stmt_dup(const struct tuple *stmt)
{
	/*
	 * We don't use tuple_new() to avoid the initializing of
	 * tuple field map. This map can be simple memcopied from
	 * the original tuple.
	 */
	struct tuple *res = vy_stmt_alloc(tuple_format(stmt), stmt->bsize);
	if (res == NULL)
		return NULL;
	assert(tuple_size(res) == tuple_size(stmt));
	assert(res->data_offset == stmt->data_offset);
	memcpy(res, stmt, tuple_size(stmt));
	res->refs = 1;
	return res;
}

struct tuple *
vy_stmt_dup_lsregion(const struct tuple *stmt, struct lsregion *lsregion,
		     int64_t alloc_id)
{
	enum iproto_type type = vy_stmt_type(stmt);
	size_t size = tuple_size(stmt);
	size_t alloc_size = size;
	struct tuple *mem_stmt;

	/* Reserve one byte for UPSERT counter. */
	if (type == IPROTO_UPSERT)
		alloc_size++;

	mem_stmt = lsregion_alloc(lsregion, alloc_size, alloc_id);
	if (mem_stmt == NULL) {
		diag_set(OutOfMemory, size, "lsregion_alloc", "mem_stmt");
		return NULL;
	}

	if (type == IPROTO_UPSERT) {
		*(uint8_t *)mem_stmt = 0;
		mem_stmt = (struct tuple *)((uint8_t *)mem_stmt + 1);
	}

	memcpy(mem_stmt, stmt, size);
	/*
	 * Region allocated statements can't be referenced or unreferenced
	 * because they are located in monolithic memory region. Referencing has
	 * sense only for separately allocated memory blocks.
	 * The reference count here is set to 0 for an assertion if somebody
	 * will try to unreference this statement.
	 */
	mem_stmt->refs = 0;
	return mem_stmt;
}

/**
 * Create the key statement from raw MessagePack data.
 * @param format     Format of an index.
 * @param key        MessagePack data that contain an array of
 *                   fields WITHOUT the array header.
 * @param part_count Count of the key fields that will be saved as
 *                   result.
 *
 * @retval not NULL Success.
 * @retval     NULL Memory allocation error.
 */
struct tuple *
vy_stmt_new_select(struct tuple_format *format, const char *key,
		   uint32_t part_count)
{
	assert(part_count == 0 || key != NULL);
	/* Key don't have field map */
	assert(format->field_map_size == 0);

	/* Calculate key length */
	const char *key_end = key;
	for (uint32_t i = 0; i < part_count; i++)
		mp_next(&key_end);

	/* Allocate stmt */
	uint32_t key_size = key_end - key;
	uint32_t bsize = mp_sizeof_array(part_count) + key_size;
	struct tuple *stmt = vy_stmt_alloc(format, bsize);
	if (stmt == NULL)
		return NULL;
	/* Copy MsgPack data */
	char *raw = (char *) stmt + sizeof(struct vy_stmt);
	char *data = mp_encode_array(raw, part_count);
	memcpy(data, key, key_size);
	assert(data + key_size == raw + bsize);
	vy_stmt_set_type(stmt, IPROTO_SELECT);
	return stmt;
}

char *
vy_key_dup(const char *key)
{
	assert(mp_typeof(*key) == MP_ARRAY);
	const char *end = key;
	mp_next(&end);
	char *res = malloc(end - key);
	if (res == NULL) {
		diag_set(OutOfMemory, end - key, "malloc", "key");
		return NULL;
	}
	memcpy(res, key, end - key);
	return res;
}

/**
 * Create a statement without type and with reserved space for operations.
 * Operations can be saved in the space available by @param extra.
 * For details @sa struct vy_stmt comment.
 */
static struct tuple *
vy_stmt_new_with_ops(struct tuple_format *format, const char *tuple_begin,
		     const char *tuple_end, struct iovec *ops,
		     int op_count, enum iproto_type type)
{
	mp_tuple_assert(tuple_begin, tuple_end);

	const char *tmp = tuple_begin;
	uint32_t field_count = mp_decode_array(&tmp);
	assert(field_count >= format->min_field_count);
	(void) field_count;

	size_t ops_size = 0;
	for (int i = 0; i < op_count; ++i)
		ops_size += ops[i].iov_len;

	/*
	 * Allocate stmt. Offsets: one per key part + offset of the
	 * statement end.
	 */
	size_t mpsize = (tuple_end - tuple_begin);
	size_t bsize = mpsize + ops_size;
	struct tuple *stmt = vy_stmt_alloc(format, bsize);
	if (stmt == NULL)
		return NULL;
	/* Copy MsgPack data */
	char *raw = (char *) tuple_data(stmt);
	char *wpos = raw;
	memcpy(wpos, tuple_begin, mpsize);
	wpos += mpsize;
	for (struct iovec *op = ops, *end = ops + op_count;
	     op != end; ++op) {
		memcpy(wpos, op->iov_base, op->iov_len);
		wpos += op->iov_len;
	}
	vy_stmt_set_type(stmt, type);

	/* Calculate offsets for key parts */
	if (tuple_init_field_map(format, (uint32_t *) raw, raw)) {
		tuple_unref(stmt);
		return NULL;
	}
	return stmt;
}

struct tuple *
vy_stmt_new_upsert(struct tuple_format *format, const char *tuple_begin,
		   const char *tuple_end, struct iovec *operations,
		   uint32_t ops_cnt)
{
	return vy_stmt_new_with_ops(format, tuple_begin, tuple_end,
				    operations, ops_cnt, IPROTO_UPSERT);
}

struct tuple *
vy_stmt_new_replace(struct tuple_format *format, const char *tuple_begin,
		    const char *tuple_end)
{
	return vy_stmt_new_with_ops(format, tuple_begin, tuple_end,
				    NULL, 0, IPROTO_REPLACE);
}

struct tuple *
vy_stmt_new_insert(struct tuple_format *format, const char *tuple_begin,
		   const char *tuple_end)
{
	return vy_stmt_new_with_ops(format, tuple_begin, tuple_end,
				    NULL, 0, IPROTO_INSERT);
}

struct tuple *
vy_stmt_replace_from_upsert(const struct tuple *upsert)
{
	assert(vy_stmt_type(upsert) == IPROTO_UPSERT);
	/* Get statement size without UPSERT operations */
	uint32_t bsize;
	vy_upsert_data_range(upsert, &bsize);
	assert(bsize <= upsert->bsize);

	/* Copy statement data excluding UPSERT operations */
	struct tuple_format *format = tuple_format(upsert);
	struct tuple *replace = vy_stmt_alloc(format, bsize);
	if (replace == NULL)
		return NULL;
	/* Copy both data and field_map. */
	char *dst = (char *)replace + sizeof(struct vy_stmt);
	char *src = (char *)upsert + sizeof(struct vy_stmt);
	memcpy(dst, src, format->field_map_size + bsize);
	vy_stmt_set_type(replace, IPROTO_REPLACE);
	vy_stmt_set_lsn(replace, vy_stmt_lsn(upsert));
	return replace;
}

static struct tuple *
vy_stmt_new_surrogate_from_key(const char *key, enum iproto_type type,
			       const struct key_def *cmp_def,
			       struct tuple_format *format)
{
	/* UPSERT can't be surrogate. */
	assert(type != IPROTO_UPSERT);
	struct region *region = &fiber()->gc;

	uint32_t field_count = format->index_field_count;
	struct iovec *iov = region_alloc(region, sizeof(*iov) * field_count);
	if (iov == NULL) {
		diag_set(OutOfMemory, sizeof(*iov) * field_count,
			 "region", "iov for surrogate key");
		return NULL;
	}
	memset(iov, 0, sizeof(*iov) * field_count);
	uint32_t part_count = mp_decode_array(&key);
	assert(part_count == cmp_def->part_count);
	assert(part_count <= field_count);
	uint32_t nulls_count = field_count - cmp_def->part_count;
	uint32_t bsize = mp_sizeof_array(field_count) +
			 mp_sizeof_nil() * nulls_count;
	for (uint32_t i = 0; i < part_count; ++i) {
		const struct key_part *part = &cmp_def->parts[i];
		assert(part->fieldno < field_count);
		const char *svp = key;
		iov[part->fieldno].iov_base = (char *) key;
		mp_next(&key);
		iov[part->fieldno].iov_len = key - svp;
		bsize += key - svp;
	}

	struct tuple *stmt = vy_stmt_alloc(format, bsize);
	if (stmt == NULL)
		return NULL;

	char *raw = (char *) tuple_data(stmt);
	uint32_t *field_map = (uint32_t *) raw;
	char *wpos = mp_encode_array(raw, field_count);
	for (uint32_t i = 0; i < field_count; ++i) {
		const struct tuple_field *field = &format->fields[i];
		if (field->offset_slot != TUPLE_OFFSET_SLOT_NIL)
			field_map[field->offset_slot] = wpos - raw;
		if (iov[i].iov_base == NULL) {
			wpos = mp_encode_nil(wpos);
		} else {
			memcpy(wpos, iov[i].iov_base, iov[i].iov_len);
			wpos += iov[i].iov_len;
		}
	}
	assert(wpos == raw + bsize);
	vy_stmt_set_type(stmt, type);
	return stmt;
}

struct tuple *
vy_stmt_new_surrogate_delete_from_key(const char *key,
				      const struct key_def *cmp_def,
				      struct tuple_format *format)
{
	return vy_stmt_new_surrogate_from_key(key, IPROTO_DELETE,
					      cmp_def, format);
}

struct tuple *
vy_stmt_new_surrogate_delete(struct tuple_format *format,
			     const struct tuple *src)
{
	uint32_t src_size;
	const char *src_data = tuple_data_range(src, &src_size);
	uint32_t total_size = src_size + format->field_map_size;
	/* Surrogate tuple uses less memory than the original tuple */
	char *data = region_alloc(&fiber()->gc, total_size);
	if (data == NULL) {
		diag_set(OutOfMemory, src_size, "region", "tuple");
		return NULL;
	}
	char *field_map_begin = data + src_size;
	uint32_t *field_map = (uint32_t *) (data + total_size);

	const char *src_pos = src_data;
	uint32_t src_count = mp_decode_array(&src_pos);
	assert(src_count >= format->min_field_count);
	uint32_t field_count;
	if (src_count < format->index_field_count) {
		field_count = src_count;
		/*
		 * Nullify field map to be able to detect by 0,
		 * which key fields are absent in tuple_field().
		 */
		memset((char *)field_map - format->field_map_size, 0,
		       format->field_map_size);
	} else {
		field_count = format->index_field_count;
	}
	char *pos = mp_encode_array(data, field_count);
	for (uint32_t i = 0; i < field_count; ++i) {
		const struct tuple_field *field = &format->fields[i];
		if (! field->is_key_part) {
			/* Unindexed field - write NIL. */
			assert(i < src_count);
			pos = mp_encode_nil(pos);
			mp_next(&src_pos);
			continue;
		}
		/* Indexed field - copy */
		const char *src_field = src_pos;
		mp_next(&src_pos);
		memcpy(pos, src_field, src_pos - src_field);
		if (field->offset_slot != TUPLE_OFFSET_SLOT_NIL)
			field_map[field->offset_slot] = pos - data;
		pos += src_pos - src_field;
	}
	assert(pos <= data + src_size);
	uint32_t bsize = pos - data;
	struct tuple *stmt = vy_stmt_alloc(format, bsize);
	if (stmt == NULL)
		return NULL;
	char *stmt_data = (char *) tuple_data(stmt);
	char *stmt_field_map_begin = stmt_data - format->field_map_size;
	memcpy(stmt_data, data, bsize);
	memcpy(stmt_field_map_begin, field_map_begin, format->field_map_size);
	vy_stmt_set_type(stmt, IPROTO_DELETE);

	return stmt;
}

struct tuple *
vy_stmt_extract_key(const struct tuple *stmt, const struct key_def *key_def,
		    struct tuple_format *format)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	const char *key_raw = tuple_extract_key(stmt, key_def, NULL);
	if (key_raw == NULL)
		return NULL;
	uint32_t part_count = mp_decode_array(&key_raw);
	assert(part_count == key_def->part_count);
	struct tuple *key = vy_stmt_new_select(format, key_raw, part_count);
	/* Cleanup memory allocated by tuple_extract_key(). */
	region_truncate(region, region_svp);
	return key;
}

struct tuple *
vy_stmt_extract_key_raw(const char *data, const char *data_end,
			const struct key_def *key_def,
			struct tuple_format *format)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	const char *key_raw = tuple_extract_key_raw(data, data_end,
						    key_def, NULL);
	if (key_raw == NULL)
		return NULL;
	uint32_t part_count = mp_decode_array(&key_raw);
	assert(part_count == key_def->part_count);
	struct tuple *key = vy_stmt_new_select(format, key_raw, part_count);
	/* Cleanup memory allocated by tuple_extract_key_raw(). */
	region_truncate(region, region_svp);
	return key;
}

int
vy_stmt_encode_primary(const struct tuple *value,
		       const struct key_def *key_def, uint32_t space_id,
		       struct xrow_header *xrow)
{
	memset(xrow, 0, sizeof(*xrow));
	enum iproto_type type = vy_stmt_type(value);
	xrow->type = type;
	xrow->lsn = vy_stmt_lsn(value);

	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = type;
	request.space_id = space_id;
	uint32_t size;
	const char *extracted = NULL;
	switch (type) {
	case IPROTO_DELETE:
		/* extract key */
		extracted = tuple_extract_key(value, key_def, &size);
		if (extracted == NULL)
			return -1;
		request.key = extracted;
		request.key_end = request.key + size;
		break;
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
		request.tuple = tuple_data_range(value, &size);
		request.tuple_end = request.tuple + size;
		break;
	case IPROTO_UPSERT:
		request.tuple = vy_upsert_data_range(value, &size);
		request.tuple_end = request.tuple + size;
		/* extract operations */
		request.ops = vy_stmt_upsert_ops(value, &size);
		request.ops_end = request.ops + size;
		break;
	default:
		unreachable();
	}
	xrow->bodycnt = xrow_encode_dml(&request, xrow->body);
	if (xrow->bodycnt < 0)
		return -1;
	return 0;
}

int
vy_stmt_encode_secondary(const struct tuple *value,
			 const struct key_def *cmp_def,
			 struct xrow_header *xrow)
{
	memset(xrow, 0, sizeof(*xrow));
	enum iproto_type type = vy_stmt_type(value);
	xrow->type = type;
	xrow->lsn = vy_stmt_lsn(value);

	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = type;
	uint32_t size;
	const char *extracted = tuple_extract_key(value, cmp_def, &size);
	if (extracted == NULL)
		return -1;
	if (type == IPROTO_REPLACE || type == IPROTO_INSERT) {
		request.tuple = extracted;
		request.tuple_end = extracted + size;
	} else {
		assert(type == IPROTO_DELETE);
		request.key = extracted;
		request.key_end = extracted + size;
	}
	xrow->bodycnt = xrow_encode_dml(&request, xrow->body);
	if (xrow->bodycnt < 0)
		return -1;
	else
		return 0;
}

struct tuple *
vy_stmt_decode(struct xrow_header *xrow, const struct key_def *key_def,
	       struct tuple_format *format, bool is_primary)
{
	struct request request;
	uint64_t key_map = dml_request_key_map(xrow->type);
	key_map &= ~(1ULL << IPROTO_SPACE_ID); /* space_id is optional */
	if (xrow_decode_dml(xrow, &request, key_map) != 0)
		return NULL;
	struct tuple *stmt = NULL;
	const char *key;
	(void) key;
	struct iovec ops;
	switch (request.type) {
	case IPROTO_DELETE:
		/* extract key */
		stmt = vy_stmt_new_surrogate_from_key(request.key,
						      IPROTO_DELETE,
						      key_def, format);
		break;
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
		if (is_primary) {
			stmt = vy_stmt_new_with_ops(format, request.tuple,
						    request.tuple_end,
						    NULL, 0, request.type);
		} else {
			stmt = vy_stmt_new_surrogate_from_key(request.tuple,
							      request.type,
							      key_def, format);
		}
		break;
	case IPROTO_UPSERT:
		ops.iov_base = (char *)request.ops;
		ops.iov_len = request.ops_end - request.ops;
		stmt = vy_stmt_new_upsert(format, request.tuple,
					  request.tuple_end, &ops, 1);
		break;
	default:
		/* TODO: report filename. */
		diag_set(ClientError, ER_INVALID_RUN_FILE,
			 tt_sprintf("Can't decode statement: "
				    "unknown request type %u",
				    (unsigned)request.type));
		return NULL;
	}

	if (stmt == NULL)
		return NULL; /* OOM */

	vy_stmt_set_lsn(stmt, xrow->lsn);
	return stmt;
}

int
vy_stmt_snprint(char *buf, int size, const struct tuple *stmt)
{
	int total = 0;
	uint32_t mp_size;
	if (stmt == NULL) {
		SNPRINT(total, snprintf, buf, size, "<NULL>");
		return total;
	}
	SNPRINT(total, snprintf, buf, size, "%s(",
		iproto_type_name(vy_stmt_type(stmt)));
		SNPRINT(total, mp_snprint, buf, size, tuple_data(stmt));
	if (vy_stmt_type(stmt) == IPROTO_UPSERT) {
		SNPRINT(total, snprintf, buf, size, ", ops=");
		SNPRINT(total, mp_snprint, buf, size,
			vy_stmt_upsert_ops(stmt, &mp_size));
	}
	SNPRINT(total, snprintf, buf, size, ", lsn=%lld)",
		(long long) vy_stmt_lsn(stmt));
	return total;
}

const char *
vy_stmt_str(const struct tuple *stmt)
{
	char *buf = tt_static_buf();
	if (vy_stmt_snprint(buf, TT_STATIC_BUF_LEN, stmt) < 0)
		return "<failed to format statement>";
	return buf;
}

struct tuple_format *
vy_tuple_format_new_with_colmask(struct tuple_format *mem_format)
{
	struct tuple_format *format = tuple_format_dup(mem_format);
	if (format == NULL)
		return NULL;
	/* + size of column mask. */
	assert(format->extra_size == 0);
	format->extra_size = sizeof(uint64_t);
	return format;
}
