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

struct vy_stmt *
vy_stmt_alloc(uint32_t size)
{
	struct vy_stmt *v = malloc(sizeof(struct vy_stmt) + size);
	if (unlikely(v == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_stmt) + size,
			 "malloc", "struct vy_stmt");
		return NULL;
	}
	v->size = size;
	v->lsn  = 0;
	v->type = 0;
	v->n_upserts = 0;
	v->refs = 1;
	return v;
}

struct vy_stmt *
vy_stmt_dup(const struct vy_stmt *stmt)
{
	struct vy_stmt *res = vy_stmt_alloc(stmt->size);
	if (res == NULL)
		return NULL;
	memcpy(res, stmt, vy_stmt_size(stmt));
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

	/* Copy MsgPack data */
	char *data = stmt->raw;
	data = mp_encode_array(data, part_count);
	memcpy(data, key, key_size);
	assert(data + key_size == stmt->raw + size);
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
	uint32_t offsets_size = sizeof(uint32_t) * part_count;
	uint32_t data_size = tuple_end - tuple_begin;
	uint32_t size = offsets_size + mp_sizeof_array(field_count) +
			data_size + extra_size;
	struct vy_stmt *stmt = vy_stmt_alloc(size);
	if (stmt == NULL)
		return NULL;

	/* Copy MsgPack data */
	char *wpos = stmt->raw + offsets_size;
	wpos = mp_encode_array(wpos, field_count);
	memcpy(wpos, tuple_begin, data_size);
	wpos += data_size;
	assert(wpos == stmt->raw + size - extra_size);
	for (struct iovec *op = operations, *end = operations + iovcnt;
	     op != end; ++op) {

		memcpy(wpos, op->iov_base, op->iov_len);
		wpos += op->iov_len;
	}
	stmt->type = type;

	/* Calculate offsets for key parts */
	if (tuple_init_field_map(format,
				 (uint32_t *) (stmt->raw + offsets_size),
				 stmt->raw + offsets_size)) {
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
vy_stmt_replace_from_upsert(const struct vy_stmt *upsert,
			    const struct key_def *key_def)
{
	assert(upsert->type == IPROTO_UPSERT);
	/* Get statement size without UPSERT operations */
	const char *mp = vy_tuple_data(upsert, key_def);
	mp_next(&mp);
	size_t size = mp - upsert->raw;
	assert(size <= upsert->size);

	/* Copy statement data excluding UPSERT operations */
	struct vy_stmt *replace = vy_stmt_alloc(size);
	if (replace == NULL)
		return NULL;
	memcpy(replace->raw, upsert->raw, size);
	replace->type = IPROTO_REPLACE;
	replace->lsn = upsert->lsn;
	return replace;
}

struct vy_stmt *
vy_stmt_extract_key_raw(const char *stmt, uint8_t type,
		        const struct key_def *key_def)
{
	uint32_t part_count;
	if (type == IPROTO_SELECT || type == IPROTO_DELETE) {
		/*
		 * The statement already is a key, so simply copy it in new
		 * struct vy_stmt as SELECT.
		 */
		part_count = mp_decode_array(&stmt);
		assert(part_count <= key_def->part_count);
		return vy_stmt_new_select(stmt, part_count);
	}
	assert(type == IPROTO_REPLACE || type == IPROTO_UPSERT);
	part_count = key_def->part_count;
	uint32_t offsets_size = sizeof(uint32_t) * part_count;
	const char *mp = stmt + offsets_size;
	assert(mp_typeof(*mp) == MP_ARRAY);
	const char *mp_end = mp;
	mp_next(&mp_end);
	uint32_t size;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	char *key = tuple_extract_key_raw(mp, mp_end, key_def, &size);
	if (key == NULL) {                      /* out of memory */
		region_truncate(region, region_svp);
		return NULL;
	}
	struct vy_stmt *ret = vy_stmt_alloc(size);
	if (ret == NULL) {
		region_truncate(region, region_svp);
		return NULL;
	}
	memcpy(ret->raw, key, size);
	region_truncate(region, region_svp);
	ret->type = IPROTO_SELECT;
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
		request.tuple = vy_tuple_data_range(value, key_def, &tuple_size);
		request.tuple_end = request.tuple + tuple_size;
	}
	if (value->type == IPROTO_UPSERT) {
		/* extract operations */
		uint32_t ops_size;
		request.ops = vy_stmt_upsert_ops(value, key_def,
						  &ops_size);
		request.ops_end = request.ops + ops_size;
	}
	if (value->type == IPROTO_DELETE) {
		/* extract key */
		uint32_t bsize;
		request.key = vy_key_data_range(value, &bsize);
		request.key = value->raw;
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
vy_stmt_snprint(char *buf, int size, const struct vy_stmt *stmt,
		const struct key_def *key_def)
{
	int total = 0;
	uint32_t mp_size;
	SNPRINT(total, snprintf, buf, size, "%s(",
		iproto_type_name(stmt->type));
	switch (stmt->type) {
	case IPROTO_SELECT:
	case IPROTO_DELETE:
		SNPRINT(total, mp_snprint, buf, size, vy_key_data(stmt));
		break;
	case IPROTO_REPLACE:
		SNPRINT(total, mp_snprint, buf, size,
			vy_tuple_data(stmt, key_def));
		break;
	case IPROTO_UPSERT:
		SNPRINT(total, mp_snprint, buf, size,
			vy_tuple_data(stmt, key_def));
		SNPRINT(total, snprintf, buf, size, ", ops=");
		SNPRINT(total, mp_snprint, buf, size,
			vy_stmt_upsert_ops(stmt, key_def, &mp_size));
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
vy_stmt_str(const struct vy_stmt *stmt, const struct key_def *key_def)
{
	char *buf = tt_static_buf();
	if (vy_stmt_snprint(buf, TT_STATIC_BUF_LEN, stmt, key_def) < 0)
		return "<failed to format statement>";
	return buf;
}
