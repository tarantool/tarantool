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
#include <pmatomic.h> /* for refs */

#include "diag.h"
#include "fiber.h" /* fiber->gc */

#include "error.h"
#include "iproto_constants.h"
#include "tuple.h"
#include "tuple_format.h"
#include "tuple_compare.h"
#include "tuple_update.h"
#include "xrow.h"
#include "small/small.h"
#include "box.h"

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
	new_tuple->version = snapshot_version;
	new_tuple->bsize = tuple_len;
	new_tuple->format_id = tuple_format_id(format);
	tuple_format_ref(format, 1);
	new_tuple->data_offset = sizeof(struct tuple) + format->field_map_size;
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

struct vy_stmt *
vy_stmt_alloc(uint32_t size)
{
	struct vy_stmt *v = malloc(sizeof(struct vy_stmt) + size);
	if (unlikely(v == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_stmt) + size,
			 "malloc", "struct vy_stmt");
		return NULL;
	}
	v->bsize = 0;
	v->lsn  = 0;
	v->type = 0;
	v->n_upserts = 0;
	v->refs = 1;
	return v;
}

struct vy_stmt *
vy_stmt_dup(const struct vy_stmt *stmt)
{
	struct vy_stmt *res;
	/*
	 * Need to subtract sizeof(struct tuple), because
	 * vy_stmt_alloc adds it.
	 */
	uint32_t size = vy_stmt_size(stmt);
	res = vy_stmt_alloc(size - sizeof(struct vy_stmt));
	if (res == NULL)
		return NULL;
	memcpy(res, stmt, size);
	res->refs = 1;
	return res;
}

void
vy_stmt_ref(struct vy_stmt *stmt)
{
	assert(stmt != NULL);
	uint16_t old_refs =
		pm_atomic_fetch_add_explicit(&stmt->refs, 1,
					     pm_memory_order_relaxed);
	if (old_refs == 0)
		panic("this is broken by design");
}

void
vy_stmt_unref(struct vy_stmt *stmt)
{
	assert(stmt != NULL);
	uint16_t old_refs = pm_atomic_fetch_sub_explicit(&stmt->refs, 1,
		pm_memory_order_relaxed);
	assert(old_refs > 0);
	if (likely(old_refs > 1))
		return;

#ifndef NDEBUG
	memset(stmt, '#', vy_stmt_size(stmt)); /* fail early */
#endif
	free(stmt);
}

struct vy_stmt *
vy_stmt_new_key(const char *key, uint32_t part_count, uint8_t type)
{
	assert(part_count == 0 || key != NULL);

	/* Calculate key length */
	const char *key_end = key;
	for (uint32_t i = 0; i < part_count; i++)
		mp_next(&key_end);

	/* Allocate stmt */
	uint32_t key_size = key_end - key;
	uint32_t size = mp_sizeof_array(part_count) + key_size;
	struct vy_stmt *stmt = vy_stmt_alloc(size);
	if (stmt == NULL)
		return NULL;
	stmt->data_offset = sizeof(struct vy_stmt);
	stmt->bsize = size;
	/* Copy MsgPack data */
	char *raw = (char *) (stmt + 1);
	char *data = mp_encode_array(raw, part_count);
	memcpy(data, key, key_size);
	assert(data + key_size == raw + size);
	stmt->type = type;
	return stmt;
}

struct vy_stmt *
vy_stmt_new_select(const char *key, uint32_t part_count)
{
	return vy_stmt_new_key(key, part_count, IPROTO_SELECT);
}

struct vy_stmt *
vy_stmt_new_delete(const char *key, uint32_t part_count)
{
	return vy_stmt_new_key(key, part_count, IPROTO_DELETE);
}

/**
 * Create a statement without type and with reserved space for operations.
 * Operations can be saved in the space available by @param extra.
 * For details @sa struct vy_stmt comment.
 */
struct vy_stmt *
vy_stmt_new_with_ops(const char *tuple_begin, const char *tuple_end,
		     uint8_t type, const struct tuple_format *format,
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
	struct vy_stmt *stmt = vy_stmt_alloc(size);
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
	stmt->type = type;

	/* Calculate offsets for key parts */
	if (tuple_init_field_map(format, (uint32_t *) raw, raw)) {
		vy_stmt_unref(stmt);
		return NULL;
	}
	return stmt;
}

struct vy_stmt *
vy_stmt_new_upsert(const char *tuple_begin, const char *tuple_end,
		   const struct tuple_format *format, uint32_t part_count,
		   struct iovec *operations,
		   uint32_t ops_cnt)
{
	return vy_stmt_new_with_ops(tuple_begin, tuple_end, IPROTO_UPSERT,
				    format, part_count, operations, ops_cnt);
}

struct vy_stmt *
vy_stmt_new_replace(const char *tuple_begin, const char *tuple_end,
		    const struct tuple_format *format,
		    uint32_t part_count)
{
	return vy_stmt_new_with_ops(tuple_begin, tuple_end, IPROTO_REPLACE,
				    format, part_count, NULL, 0);
}

struct vy_stmt *
vy_stmt_replace_from_upsert(const struct vy_stmt *upsert)
{
	assert(upsert->type == IPROTO_UPSERT);
	/* Get statement size without UPSERT operations */
	uint32_t bsize;
	vy_tuple_data_range(upsert, &bsize);
	assert(bsize <= upsert->bsize);
	uint32_t size = bsize + upsert->data_offset - sizeof(struct vy_stmt);

	/* Copy statement data excluding UPSERT operations */
	struct vy_stmt *replace = vy_stmt_alloc(size);
	if (replace == NULL)
		return NULL;
	memcpy((char *) (replace + 1), (char *) (upsert + 1), size);
	replace->bsize = bsize;
	replace->type = IPROTO_REPLACE;
	replace->lsn = upsert->lsn;
	replace->data_offset = upsert->data_offset;
	return replace;
}

struct vy_stmt *
vy_stmt_extract_key(const struct vy_stmt *stmt, const struct key_def *key_def)
{
	const char *tuple = vy_stmt_data(stmt);
	uint8_t type = stmt->type;
	if (type == IPROTO_SELECT || type == IPROTO_DELETE) {
		/*
		 * The statement already is a key, so simply copy it in new
		 * struct vy_stmt as SELECT.
		 */
		uint32_t part_count = mp_decode_array(&tuple);
		assert(part_count <= key_def->part_count);
		return vy_stmt_new_select(tuple, part_count);
	}
	assert(type == IPROTO_REPLACE || type == IPROTO_UPSERT);
	assert(mp_typeof(*tuple) == MP_ARRAY);
	const char *tuple_end = tuple;
	mp_next(&tuple_end);
	uint32_t size;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	char *key = tuple_extract_key_raw(tuple, tuple_end, key_def, &size);
	if (key == NULL) {                      /* out of memory */
		region_truncate(region, region_svp);
		return NULL;
	}
	struct vy_stmt *ret = vy_stmt_alloc(size);
	if (ret == NULL) {
		region_truncate(region, region_svp);
		return NULL;
	}
	memcpy((char *) (ret + 1), key, size);
	region_truncate(region, region_svp);
	ret->type = IPROTO_SELECT;
	ret->data_offset = sizeof(struct vy_stmt);
	ret->bsize = size;
	return ret;
}

int
vy_stmt_encode(const struct vy_stmt *value, const struct key_def *key_def,
	       struct xrow_header *xrow)
{
	memset(xrow, 0, sizeof(*xrow));
	xrow->type = value->type;
	xrow->lsn = value->lsn;

	struct request request;
	request_create(&request, value->type);
	request.space_id = key_def->space_id;
	request.index_id = key_def->iid;
	if (value->type == IPROTO_UPSERT || value->type == IPROTO_REPLACE) {
		/* extract tuple */
		uint32_t tuple_size;
		request.tuple = vy_tuple_data_range(value, &tuple_size);
		request.tuple_end = request.tuple + tuple_size;
	}
	if (value->type == IPROTO_UPSERT) {
		/* extract operations */
		uint32_t ops_size;
		request.ops = vy_stmt_upsert_ops(value, &ops_size);
		request.ops_end = request.ops + ops_size;
	}
	if (value->type == IPROTO_DELETE) {
		/* extract key */
		uint32_t bsize;
		request.key = vy_key_data_range(value, &bsize);
		request.key_end = request.key + bsize;
	}
	xrow->bodycnt = request_encode(&request, xrow->body);
	return xrow->bodycnt >= 0 ? 0: -1;
}

struct vy_stmt *
vy_stmt_decode(struct xrow_header *xrow, const struct tuple_format *format,
	       uint32_t part_count)
{
	struct request request;
	request_create(&request, xrow->type);
	if (request_decode(&request, xrow->body->iov_base,
			   xrow->body->iov_len) < 0)
		return NULL;
	struct vy_stmt *stmt = NULL;
	uint32_t field_count;
	struct iovec ops;
	switch (request.type) {
	case IPROTO_DELETE:
		/* extract key */
		field_count = mp_decode_array(&request.key);
		assert(field_count == part_count);
		stmt = vy_stmt_new_delete(request.key, field_count);
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

	stmt->lsn = xrow->lsn;
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
vy_stmt_snprint(char *buf, int size, const struct vy_stmt *stmt)
{
	int total = 0;
	uint32_t mp_size;
	SNPRINT(total, snprintf, buf, size, "%s(",
		iproto_type_name(stmt->type));
	switch (stmt->type) {
	case IPROTO_SELECT:
	case IPROTO_DELETE:
		SNPRINT(total, mp_snprint, buf, size, vy_stmt_data(stmt));
		break;
	case IPROTO_REPLACE:
		SNPRINT(total, mp_snprint, buf, size,
			vy_stmt_data(stmt));
		break;
	case IPROTO_UPSERT:
		SNPRINT(total, mp_snprint, buf, size,
			vy_stmt_data(stmt));
		SNPRINT(total, snprintf, buf, size, ", ops=");
		SNPRINT(total, mp_snprint, buf, size,
			vy_stmt_upsert_ops(stmt, &mp_size));
		break;
	default:
		unreachable();
	}
	SNPRINT(total, snprintf, buf, size, ", lsn=%lld)",
		(long long) stmt->lsn);
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
vy_stmt_str(const struct vy_stmt *stmt)
{
	char *buf = tt_static_buf();
	if (vy_stmt_snprint(buf, TT_STATIC_BUF_LEN, stmt) < 0)
		return "<failed to format statement>";
	return buf;
}
