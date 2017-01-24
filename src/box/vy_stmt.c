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

#include "error.h"
#include "tuple_format.h"
#include "xrow.h"

struct tuple *
vy_tuple_new(struct tuple_format *format, const char *data, const char *end)
{
	size_t tuple_len = end - data;
	assert(mp_typeof(*data) == MP_ARRAY);
	uint32_t total =
		tuple_len + sizeof(struct vy_stmt) + format->field_map_size;
	struct tuple *new_tuple = malloc(total);
	if (new_tuple == NULL) {
		diag_set(OutOfMemory, total, "malloc", "struct tuple");
		return NULL;
	}
	new_tuple->bsize = tuple_len;
	new_tuple->format_id = tuple_format_id(format);
	tuple_format_ref(format, 1);
	new_tuple->data_offset = sizeof(struct vy_stmt) + format->field_map_size;
	char *raw = (char *) new_tuple + new_tuple->data_offset;
	uint32_t *field_map = (uint32_t *) raw;
	memcpy(raw, data, tuple_len);
	if (tuple_init_field_map(format, field_map, raw)) {
		vy_tuple_delete(format, new_tuple);
		return NULL;
	}
	new_tuple->refs = 0;
	return new_tuple;
}

void
vy_tuple_delete(struct tuple_format *format, struct tuple *tuple)
{
	say_debug("%s(%p)", __func__, tuple);
	assert(tuple->refs == 0);
	tuple_format_ref(format, -1);
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
struct tuple *
vy_stmt_alloc(struct tuple_format *format, uint32_t size)
{
	struct tuple *tuple = malloc(sizeof(struct vy_stmt) + size);
	if (unlikely(tuple == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_stmt) + size,
			 "malloc", "struct vy_stmt");
		return NULL;
	}
	tuple->refs = 1;
	tuple->format_id = tuple_format_id(format);
	tuple_format_ref(format, 1);
	tuple->bsize = 0;
	tuple->data_offset = 0;
	vy_stmt_set_lsn(tuple, 0);
	vy_stmt_set_type(tuple, 0);
	vy_stmt_set_n_upserts(tuple, 0);
	return tuple;
}

struct tuple *
vy_stmt_dup(const struct tuple *stmt)
{
	/*
	 * Need to subtract sizeof(struct tuple), because
	 * vy_stmt_alloc adds it.
	 * We don't use tuple_new() to avoid the initializing of
	 * tuple field map. This map can be simple memcopied from
	 * the original tuple.
	 */
	uint32_t size = tuple_size(stmt);
	struct tuple *res = vy_stmt_alloc(tuple_format_by_id(stmt->format_id),
					  size - sizeof(struct vy_stmt));
	if (res == NULL)
		return NULL;
	memcpy(res, stmt, size);
	res->refs = 1;
	return res;
}

/**
 * Create the key statement from raw MessagePack data.
 * @param format     Format of an index.
 * @param key        MessagePack data that contain an array of
 *                   fields WITHOUT the array header.
 * @param part_count Count of the key fields that will be saved as
 *                   result.
 * @param type       Type of the key statement.
 *
 * @retval not NULL Success.
 * @retval     NULL Memory allocation error.
 */
struct tuple *
vy_stmt_new_key(struct tuple_format *format, const char *key,
		uint32_t part_count, uint8_t type)
{
	assert(part_count == 0 || key != NULL);

	/* Calculate key length */
	const char *key_end = key;
	for (uint32_t i = 0; i < part_count; i++)
		mp_next(&key_end);

	/* Allocate stmt */
	uint32_t key_size = key_end - key;
	uint32_t size = mp_sizeof_array(part_count) + key_size;
	struct tuple *stmt = vy_stmt_alloc(format, size);
	if (stmt == NULL)
		return NULL;
	stmt->data_offset = sizeof(struct vy_stmt);
	stmt->bsize = size;
	/* Copy MsgPack data */
	char *raw = (char *) stmt + sizeof(struct vy_stmt);
	char *data = mp_encode_array(raw, part_count);
	memcpy(data, key, key_size);
	assert(data + key_size == raw + size);
	vy_stmt_set_type(stmt, type);
	return stmt;
}

struct tuple *
vy_stmt_new_select(struct tuple_format *format, const char *key,
		   uint32_t part_count)
{
	return vy_stmt_new_key(format, key, part_count, IPROTO_SELECT);
}

struct tuple *
vy_stmt_new_delete(struct tuple_format *format, const char *key,
		   uint32_t part_count)
{
	return vy_stmt_new_key(format, key, part_count, IPROTO_DELETE);
}

/**
 * Create a statement without type and with reserved space for operations.
 * Operations can be saved in the space available by @param extra.
 * For details @sa struct vy_stmt comment.
 */
struct tuple *
vy_stmt_new_with_ops(const char *tuple_begin, const char *tuple_end,
		     uint8_t type, struct tuple_format *format,
		     uint32_t part_count,
		     struct iovec *operations, uint32_t iovcnt)
{
	(void) part_count; /* unused in release. */
#ifndef NDEBUG
	const char *tuple_end_must_be = tuple_begin;
	mp_next(&tuple_end_must_be);
	assert(tuple_end == tuple_end_must_be);
#endif

	uint32_t field_count = mp_decode_array(&tuple_begin);
	assert(field_count >= part_count);

	uint32_t extra_size = 0;
	for (uint32_t i = 0; i < iovcnt; ++i) {
		extra_size += operations[i].iov_len;
	}

	/*
	 * Allocate stmt. Offsets: one per key part + offset of the
	 * statement end.
	 */
	uint32_t offsets_size = format->field_map_size;
	uint32_t bsize = tuple_end - tuple_begin;
	uint32_t size = offsets_size + mp_sizeof_array(field_count) +
			bsize + extra_size;
	struct tuple *stmt = vy_stmt_alloc(format, size);
	if (stmt == NULL)
		return NULL;
	stmt->bsize = bsize +  mp_sizeof_array(field_count);
	/* Copy MsgPack data */
	stmt->data_offset = offsets_size + sizeof(struct vy_stmt);
	char *raw = (char *) stmt + stmt->data_offset;
	char *wpos = mp_encode_array(raw, field_count);
	memcpy(wpos, tuple_begin, bsize);
	wpos += bsize;
	assert(wpos == raw + stmt->bsize);
	for (struct iovec *op = operations, *end = operations + iovcnt;
	     op != end; ++op) {

		memcpy(wpos, op->iov_base, op->iov_len);
		wpos += op->iov_len;
	}
	stmt->bsize += extra_size;
	vy_stmt_set_type(stmt, type);

	/* Calculate offsets for key parts */
	if (tuple_init_field_map(format, (uint32_t *) raw, raw)) {
		tuple_unref(stmt);
		return NULL;
	}
	return stmt;
}

struct tuple *
vy_stmt_new_upsert(const char *tuple_begin, const char *tuple_end,
		   struct tuple_format *format, uint32_t part_count,
		   struct iovec *operations, uint32_t ops_cnt)
{
	return vy_stmt_new_with_ops(tuple_begin, tuple_end, IPROTO_UPSERT,
				    format, part_count, operations, ops_cnt);
}

struct tuple *
vy_stmt_new_replace(const char *tuple_begin, const char *tuple_end,
		    struct tuple_format *format, uint32_t part_count)
{
	return vy_stmt_new_with_ops(tuple_begin, tuple_end, IPROTO_REPLACE,
				    format, part_count, NULL, 0);
}

struct tuple *
vy_stmt_replace_from_upsert(const struct tuple *upsert)
{
	assert(vy_stmt_type(upsert) == IPROTO_UPSERT);
	/* Get statement size without UPSERT operations */
	uint32_t bsize;
	vy_upsert_data_range(upsert, &bsize);
	assert(bsize <= upsert->bsize);
	uint32_t size = bsize + upsert->data_offset - sizeof(struct vy_stmt);

	/* Copy statement data excluding UPSERT operations */
	struct tuple *replace =
		vy_stmt_alloc(tuple_format_by_id(upsert->format_id), size);
	if (replace == NULL)
		return NULL;
	memcpy((char *) replace + sizeof(struct vy_stmt),
	       (char *) upsert + sizeof(struct vy_stmt), size);
	replace->bsize = bsize;
	vy_stmt_set_type(replace, IPROTO_REPLACE);
	vy_stmt_set_lsn(replace, vy_stmt_lsn(upsert));
	replace->data_offset = upsert->data_offset;
	return replace;
}

struct tuple *
vy_stmt_extract_key(const struct tuple *stmt, const struct key_def *key_def,
		    struct region *region)
{
	uint8_t type = vy_stmt_type(stmt);
	struct tuple_format *format = tuple_format_by_id(stmt->format_id);
	if (type == IPROTO_SELECT || type == IPROTO_DELETE) {
		/*
		 * The statement already is a key, so simply copy it in new
		 * struct vy_stmt as SELECT.
		 */
		struct tuple *res = vy_stmt_dup(stmt);
		if (res != NULL)
			vy_stmt_set_type(res, IPROTO_SELECT);
		return res;
	}
	assert(type == IPROTO_REPLACE || type == IPROTO_UPSERT);
	uint32_t size;
	size_t region_svp = region_used(region);
	char *key = tuple_extract_key(stmt, key_def, &size);
	if (key == NULL)
		goto error;
	struct tuple *ret = vy_stmt_alloc(format, size);
	if (ret == NULL)
		goto error;
	memcpy((char *) ret + sizeof(struct vy_stmt), key, size);
	region_truncate(region, region_svp);
	vy_stmt_set_type(ret, IPROTO_SELECT);
	ret->data_offset = sizeof(struct vy_stmt);
	ret->bsize = size;
	return ret;
error:
	region_truncate(region, region_svp);
	return NULL;
}

int
vy_stmt_encode(const struct tuple *value, const struct key_def *key_def,
	       struct xrow_header *xrow)
{
	memset(xrow, 0, sizeof(*xrow));
	uint8_t type = vy_stmt_type(value);
	xrow->type = type;
	xrow->lsn = vy_stmt_lsn(value);

	struct request request;
	request_create(&request, type);
	request.space_id = key_def->space_id;
	request.index_id = key_def->iid;
	uint32_t size;
	if (type == IPROTO_REPLACE) {
		request.tuple = tuple_data_range(value, &size);
		request.tuple_end = request.tuple + size;
	} else if (type == IPROTO_UPSERT) {
		request.tuple = vy_upsert_data_range(value, &size);
		request.tuple_end = request.tuple + size;

		/* extract operations */
		request.ops = vy_stmt_upsert_ops(value, &size);
		request.ops_end = request.ops + size;
	}
	if (type == IPROTO_DELETE) {
		/* extract key */
		request.key = tuple_data_range(value, &size);
		request.key_end = request.key + size;
	}
	xrow->bodycnt = request_encode(&request, xrow->body);
	return xrow->bodycnt >= 0 ? 0: -1;
}

struct tuple *
vy_stmt_decode(struct xrow_header *xrow, struct tuple_format *format,
	       uint32_t part_count)
{
	struct request request;
	request_create(&request, xrow->type);
	if (request_decode(&request, xrow->body->iov_base,
			   xrow->body->iov_len) < 0)
		return NULL;
	struct tuple *stmt = NULL;
	uint32_t field_count;
	struct iovec ops;
	switch (request.type) {
	case IPROTO_DELETE:
		/* extract key */
		field_count = mp_decode_array(&request.key);
		assert(field_count == part_count);
		stmt = vy_stmt_new_delete(format, request.key, field_count);
		break;
	case IPROTO_REPLACE:
		stmt = vy_stmt_new_replace(request.tuple,
					   request.tuple_end,
					   format, part_count);
		break;
	case IPROTO_UPSERT:
		ops.iov_base = (char *)request.ops;
		ops.iov_len = request.ops_end - request.ops;
		stmt = vy_stmt_new_upsert(request.tuple,
					  request.tuple_end,
					  format, part_count, &ops, 1);
		break;
	default:
		diag_set(ClientError, ER_VINYL, "unknown request type");
		return NULL;
	}

	if (stmt == NULL)
		return NULL; /* OOM */

	vy_stmt_set_lsn(stmt, xrow->lsn);
	return stmt;
}

int
vy_key_snprint(char *buf, int size, const char *key)
{
	if (key == NULL)
		return snprintf(buf, size, "[]");

	int total = 0;
	SNPRINT(total, snprintf, buf, size, "[");
	uint32_t count = mp_decode_array(&key);
	for (uint32_t i = 0; i < count; i++) {
		if (i > 0)
			SNPRINT(total, snprintf, buf, size, ", ");
		SNPRINT(total, mp_snprint, buf, size, key);
		mp_next(&key);
	}
	SNPRINT(total, snprintf, buf, size, "]");
	return total;
}

int
vy_stmt_snprint(char *buf, int size, const struct tuple *stmt)
{
	int total = 0;
	uint32_t mp_size;
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
vy_key_str(const char *key)
{
	char *buf = tt_static_buf();
	if (vy_key_snprint(buf, TT_STATIC_BUF_LEN, key) < 0)
		return "<failed to format key>";
	return buf;
}

const char *
vy_stmt_str(const struct tuple *stmt)
{
	char *buf = tt_static_buf();
	if (vy_stmt_snprint(buf, TT_STATIC_BUF_LEN, stmt) < 0)
		return "<failed to format statement>";
	return buf;
}
