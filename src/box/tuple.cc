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
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "tuple.h"

#include "small/small.h"
#include "small/quota.h"

#include "trivia/util.h"
#include "fiber.h"
#include "tt_uuid.h"

uint32_t snapshot_version;

struct quota memtx_quota;

struct slab_arena memtx_arena;
static struct slab_cache memtx_slab_cache;
struct small_alloc memtx_alloc;

enum {
	/** Lowest allowed slab_alloc_minimal */
	OBJSIZE_MIN = 16,
	/** Lowest allowed slab_alloc_maximal */
	OBJSIZE_MAX_MIN = 16 * 1024,
	/** Lowest allowed slab size, for mmapped slabs */
	SLAB_SIZE_MIN = 1024 * 1024
};

static struct mempool tuple_iterator_pool;

/**
 * Last tuple returned by public C API
 * \sa tuple_bless()
 */
struct tuple *box_tuple_last;

int
tuple_validate_raw(struct tuple_format *format, const char *tuple)
{
	if (format->field_count == 0)
		return 0; /* Nothing to check */

	/* Check to see if the tuple has a sufficient number of fields. */
	uint32_t field_count = mp_decode_array(&tuple);
	if (format->exact_field_count > 0 &&
	    format->exact_field_count != field_count) {
		diag_set(ClientError, ER_EXACT_FIELD_COUNT,
			 (unsigned) field_count,
			 (unsigned) format->exact_field_count);
		return -1;
	}
	if (unlikely(field_count < format->field_count)) {
		diag_set(ClientError, ER_INDEX_FIELD_COUNT,
			 (unsigned) field_count,
			 (unsigned) format->field_count);
		return -1;
	}

	/* Check field types */
	for (uint32_t i = 0; i < format->field_count; i++) {
		if (key_mp_type_validate(format->fields[i].type,
					 mp_typeof(*tuple), ER_FIELD_TYPE,
					 i + TUPLE_INDEX_BASE))
			return -1;
		mp_next(&tuple);
	}
	return 0;
}

/**
 * Incremented on every snapshot and is used to distinguish tuples
 * which were created after start of a snapshot (these tuples can
 * be freed right away, since they are not used for snapshot) or
 * before start of a snapshot (these tuples can be freed only
 * after the snapshot has finished, otherwise it'll write bad data
 * to the snapshot file).
 */

const char *
tuple_seek(struct tuple_iterator *it, uint32_t fieldno)
{
	const char *field = tuple_field(it->tuple, fieldno);
	if (likely(field != NULL)) {
		it->pos = field;
		it->fieldno = fieldno;
		return tuple_next(it);
	} else {
		it->pos = it->end;
		it->fieldno = tuple_field_count(it->tuple);
		return NULL;
	}
}

const char *
tuple_next(struct tuple_iterator *it)
{
	if (it->pos < it->end) {
		const char *field = it->pos;
		mp_next(&it->pos);
		assert(it->pos <= it->end);
		it->fieldno++;
		return field;
	}
	return NULL;
}

extern inline uint32_t
tuple_next_u32(struct tuple_iterator *it);

const char *
tuple_field_to_cstr(const char *field, uint32_t len)
{
	char *buf = tt_static_buf();
	len = MIN(len, TT_STATIC_BUF_LEN - 1);
	memcpy(buf, field, len);
	buf[len] = '\0';
	return buf;
}

const char *
tuple_next_cstr(struct tuple_iterator *it)
{
	const char *field = tuple_next_check(it, MP_STR);
	uint32_t len = 0;
	const char *str = mp_decode_str(&field, &len);
	return tuple_field_to_cstr(str, len);
}

const char *
tuple_field_cstr(struct tuple *tuple, uint32_t fieldno)
{
	const char *field = tuple_field_check(tuple, fieldno, MP_STR);
	uint32_t len = 0;
	const char *str = mp_decode_str(&field, &len);
	return tuple_field_to_cstr(str, len);
}

void
tuple_field_uuid(struct tuple *tuple, int fieldno, struct tt_uuid *result)
{
	const char *value = tuple_field_cstr(tuple, fieldno);
	if (tt_uuid_from_string(value, result) != 0)
		tnt_raise(ClientError, ER_INVALID_UUID, value);
}

char *
tuple_extract_key(const struct tuple *tuple, const struct key_def *key_def,
		  uint32_t *key_size)
{
	uint32_t bsize;
	const char *data = tuple_data_range(tuple, &bsize);
	return tuple_extract_key_raw(data, data + bsize, key_def, key_size);
}

char *
tuple_extract_key_raw(const char *data, const char *data_end,
		      const struct key_def *key_def, uint32_t *key_size)
{
	/* allocate buffer with maximal possible size */
	char *key = (char *) region_alloc(&fiber()->gc, data_end - data);
	if (key == NULL) {
		diag_set(OutOfMemory, data_end - data, "region",
			 "tuple_extract_key_raw");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, key_def->part_count);
	const char *field0 = data;
	mp_decode_array(&field0);
	const char *field0_end = field0;
	mp_next(&field0_end);
	const char *field = field0;
	const char *field_end = field0_end;
	uint32_t current_fieldno = 0;
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		uint32_t fieldno = key_def->parts[i].fieldno;
		if (fieldno < current_fieldno) {
			/* Rewind. */
			field = field0;
			field_end = field0_end;
			current_fieldno = 0;
		}
		while (current_fieldno < fieldno) {
			field = field_end;
			mp_next(&field_end);
			current_fieldno++;
		}
		memcpy(key_buf, field, field_end - field);
		key_buf += field_end - field;
		assert(key_buf - key <= data_end - data);
	}
	if (key_size != NULL)
		*key_size = (uint32_t)(key_buf - key);
	return key;
}

struct tuple *
tuple_update(struct tuple_format *format,
	     tuple_update_alloc_func f, void *alloc_ctx,
	     const struct tuple *old_tuple, const char *expr,
	     const char *expr_end, int field_base, uint64_t *column_mask)
{
	uint32_t new_size = 0, bsize;
	const char *old_data = tuple_data_range(old_tuple, &bsize);
	const char *new_data =
		tuple_update_execute(f, alloc_ctx,
				     expr, expr_end, old_data, old_data + bsize,
				     &new_size, field_base, column_mask);
	if (new_data == NULL)
		diag_raise();

	struct tuple *ret = tuple_new_xc(format, new_data, new_data + new_size);
	return ret;
}

struct tuple *
tuple_upsert(struct tuple_format *format,
	     void *(*region_alloc)(void *, size_t), void *alloc_ctx,
	     const struct tuple *old_tuple,
	     const char *expr, const char *expr_end, int field_base)
{
	uint32_t new_size = 0, bsize;
	const char *old_data = tuple_data_range(old_tuple, &bsize);
	const char *new_data =
		tuple_upsert_execute(region_alloc, alloc_ctx, expr, expr_end,
				     old_data, old_data + bsize,
				     &new_size, field_base, false, NULL);
	if (new_data == NULL)
		diag_raise();

	struct tuple *ret = tuple_new_xc(format, new_data, new_data + new_size);
	return ret;
}

void
tuple_init(float tuple_arena_max_size, uint32_t objsize_min,
	   uint32_t objsize_max, float alloc_factor)
{
	tuple_format_init();

	/* Apply lowest allowed objsize bounds */
	if (objsize_min < OBJSIZE_MIN)
		objsize_min = OBJSIZE_MIN;
	if (objsize_max < OBJSIZE_MAX_MIN)
		objsize_max = OBJSIZE_MAX_MIN;

	/* Calculate slab size for tuple arena */
	size_t slab_size = small_round(objsize_max * 4);
	if (slab_size < SLAB_SIZE_MIN)
		slab_size = SLAB_SIZE_MIN;

	/*
	 * Ensure that quota is a multiple of slab_size, to
	 * have accurate value of quota_used_ratio
	 */
	size_t prealloc = small_align(tuple_arena_max_size * 1024
				      * 1024 * 1024, slab_size);
	/** Preallocate entire quota. */
	quota_init(&memtx_quota, prealloc);

	say_info("mapping %zu bytes for tuple arena...", prealloc);

	if (slab_arena_create(&memtx_arena, &memtx_quota,
			      prealloc, slab_size, MAP_PRIVATE)) {
		if (ENOMEM == errno) {
			panic("failed to preallocate %zu bytes: "
			      "Cannot allocate memory, check option "
			      "'slab_alloc_arena' in box.cfg(..)",
			      prealloc);
		} else {
			panic_syserror("failed to preallocate %zu bytes",
				       prealloc);
		}
	}
	slab_cache_create(&memtx_slab_cache, &memtx_arena);
	small_alloc_create(&memtx_alloc, &memtx_slab_cache,
			   objsize_min, alloc_factor);
	mempool_create(&tuple_iterator_pool, &cord()->slabc,
		       sizeof(struct tuple_iterator));

	box_tuple_last = NULL;
}

void
tuple_free()
{
	/* Unref last tuple returned by public C API */
	if (box_tuple_last != NULL) {
		tuple_unref(box_tuple_last);
		box_tuple_last = NULL;
	}

	mempool_destroy(&tuple_iterator_pool);

	tuple_format_free();
}

void
tuple_begin_snapshot()
{
	snapshot_version++;
	small_alloc_setopt(&memtx_alloc, SMALL_DELAYED_FREE_MODE, true);
}

void
tuple_end_snapshot()
{
	small_alloc_setopt(&memtx_alloc, SMALL_DELAYED_FREE_MODE, false);
}

box_tuple_format_t *
box_tuple_format_default(void)
{
	return tuple_format_default;
}

box_tuple_t *
box_tuple_new(box_tuple_format_t *format, const char *data, const char *end)
{
	struct tuple *ret = tuple_new(format, data, end);
	if (ret == NULL)
		return NULL;
	/* Can't throw on zero refs. */
	return tuple_bless(ret);
}

int
box_tuple_ref(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	try {
		tuple_ref(tuple);
		return 0;
	} catch (Exception *e) {
		return -1;
	}
}

void
box_tuple_unref(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_unref(tuple);
}

uint32_t
box_tuple_field_count(const box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_field_count(tuple);
}

size_t
box_tuple_bsize(const box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple->bsize;
}

ssize_t
box_tuple_to_buf(const box_tuple_t *tuple, char *buf, size_t size)
{
	assert(tuple != NULL);
	return tuple_to_buf(tuple, buf, size);
}

box_tuple_format_t *
box_tuple_format(const box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_format(tuple);
}

const char *
box_tuple_field(const box_tuple_t *tuple, uint32_t fieldno)
{
	assert(tuple != NULL);
	return tuple_field(tuple, fieldno);
}

typedef struct tuple_iterator box_tuple_iterator_t;

box_tuple_iterator_t *
box_tuple_iterator(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	struct tuple_iterator *it;
	try {
		it = (struct tuple_iterator *)
			mempool_alloc0_xc(&tuple_iterator_pool);
	} catch (Exception *e) {
		return NULL;
	}
	tuple_ref(tuple);
	tuple_rewind(it, tuple);
	return it;
}

void
box_tuple_iterator_free(box_tuple_iterator_t *it)
{
	tuple_unref(it->tuple);
	mempool_free(&tuple_iterator_pool, it);
}

uint32_t
box_tuple_position(box_tuple_iterator_t *it)
{
	return it->fieldno;
}

void
box_tuple_rewind(box_tuple_iterator_t *it)
{
	tuple_rewind(it, it->tuple);
}

const char *
box_tuple_seek(box_tuple_iterator_t *it, uint32_t fieldno)
{
	return tuple_seek(it, fieldno);
}

const char *
box_tuple_next(box_tuple_iterator_t *it)
{
	return tuple_next(it);
}

box_tuple_t *
box_tuple_update(const box_tuple_t *tuple, const char *expr, const char *expr_end)
{
	try {
		RegionGuard region_guard(&fiber()->gc);
		struct tuple *new_tuple = tuple_update(tuple_format_default,
			region_aligned_alloc_xc_cb, &fiber()->gc, tuple,
			expr, expr_end, 1, NULL);
		return tuple_bless(new_tuple);
	} catch (ClientError *e) {
		return NULL;
	}
}

box_tuple_t *
box_tuple_upsert(const box_tuple_t *tuple, const char *expr, const char *expr_end)
{
	try {
		RegionGuard region_guard(&fiber()->gc);
		struct tuple *new_tuple = tuple_upsert(tuple_format_default,
			region_aligned_alloc_xc_cb, &fiber()->gc, tuple,
			expr, expr_end, 1);
		return tuple_bless(new_tuple);
	} catch (ClientError *e) {
		return NULL;
	}
}
