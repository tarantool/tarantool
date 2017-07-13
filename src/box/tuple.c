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

#include "trivia/util.h"
#include "fiber.h"
#include "tt_uuid.h"
#include "small/quota.h"

enum {
	/** Lowest allowed slab_alloc_maximal. */
	TUPLE_MAX_SIZE_MIN = 16 * 1024,
	SLAB_SIZE_MIN = 1024 * 1024,
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

/**
 * Optimized version of tuple_extract_key_raw() for sequential key defs
 * @copydoc tuple_extract_key_raw()
 */
static char *
tuple_extract_key_sequential_raw(const char *data, const char *data_end,
				 const struct key_def *key_def,
				 uint32_t *key_size)
{
	assert(key_def_is_sequential(key_def));
	const char *field_start = data;
	uint32_t bsize = mp_sizeof_array(key_def->part_count);

	mp_decode_array(&field_start);
	const char *field_end = field_start;

	for (uint32_t i = 0; i < key_def->part_count; i++)
		mp_next(&field_end);
	bsize += field_end - field_start;

	assert(!data_end || (field_end - field_start <= data_end - data));
	(void) data_end;

	char *key = (char *) region_alloc(&fiber()->gc, bsize);
	if (key == NULL) {
		diag_set(OutOfMemory, bsize, "region",
			"tuple_extract_key_raw_sequential");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, key_def->part_count);
	memcpy(key_buf, field_start, field_end - field_start);

	if (key_size != NULL)
		*key_size = bsize;
	return key;
}

/**
 * Optimized version of tuple_extract_key() for sequential key defs
 * @copydoc tuple_extract_key()
 */
static inline char *
tuple_extract_key_sequential(const struct tuple *tuple,
			     const struct key_def *key_def,
			     uint32_t *key_size)
{
	assert(key_def_is_sequential(key_def));
	const char *data = tuple_data(tuple);
	return tuple_extract_key_sequential_raw(data, NULL, key_def, key_size);
}

/**
 * General-purpose implementation of tuple_extract_key()
 * @copydoc tuple_extract_key()
 */
static char *
tuple_extract_key_slowpath(const struct tuple *tuple,
			   const struct key_def *key_def, uint32_t *key_size)
{
	const char *data = tuple_data(tuple);
	uint32_t part_count = key_def->part_count;
	uint32_t bsize = mp_sizeof_array(part_count);
	const struct tuple_format *format = tuple_format(tuple);
	const uint32_t *field_map = tuple_field_map(tuple);

	/* Calculate the key size. */
	for (uint32_t i = 0; i < part_count; ++i) {
		const char *field =
			tuple_field_raw(format, data, field_map,
					key_def->parts[i].fieldno);
		const char *end = field;
		/*
		 * Skip sequential part in order to minimize
		 * tuple_field_raw() calls.
		 */
		for (; i < key_def->part_count - 1; i++) {
			if (key_def->parts[i].fieldno + 1 !=
				key_def->parts[i + 1].fieldno) {
				/* End of sequential part */
				break;
			}
			mp_next(&end);
		}
		mp_next(&end);
		bsize += end - field;
	}

	char *key = (char *) region_alloc(&fiber()->gc, bsize);
	if (key == NULL) {
		diag_set(OutOfMemory, bsize, "region", "tuple_extract_key");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, part_count);
	for (uint32_t i = 0; i < part_count; ++i) {
		const char *field =
			tuple_field_raw(format, data, field_map,
					key_def->parts[i].fieldno);
		const char *end = field;
		/*
		 * Skip sequential part in order to minimize
		 * tuple_field_raw() calls
		 */
		for (; i < key_def->part_count - 1; i++) {
			if (key_def->parts[i].fieldno + 1 !=
				key_def->parts[i + 1].fieldno) {
				/* End of sequential part */
				break;
			}
			mp_next(&end);
		}
		mp_next(&end);
		bsize = end - field;
		memcpy(key_buf, field, bsize);
		key_buf += bsize;
	}
	if (key_size != NULL)
		*key_size = key_buf - key;
	return key;
}

/**
 * General-purpose version of tuple_extract_key_raw()
 * @copydoc tuple_extract_key_raw()
 */
static char *
tuple_extract_key_slowpath_raw(const char *data, const char *data_end,
			       const struct key_def *key_def,
			       uint32_t *key_size)
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
		for (; i < key_def->part_count - 1; i++) {
			if (key_def->parts[i].fieldno + 1 !=
			    key_def->parts[i + 1].fieldno)
				break;
		}
		if (fieldno < current_fieldno) {
			/* Rewind. */
			field = field0;
			field_end = field0_end;
			current_fieldno = 0;
		}

		while (current_fieldno < fieldno) {
			/* search first field of key in tuple raw data */
			field = field_end;
			mp_next(&field_end);
			current_fieldno++;
		}

		while (current_fieldno < key_def->parts[i].fieldno) {
			/* search the last field in subsequence */
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

/**
 * Initialize tuple_extract_key() and tuple_extract_key_raw()
 */
void
tuple_extract_key_set(struct key_def *key_def)
{
	if (key_def_is_sequential(key_def)) {
		key_def->tuple_extract_key = tuple_extract_key_sequential;
		key_def->tuple_extract_key_raw = tuple_extract_key_sequential_raw;
	} else {
		key_def->tuple_extract_key = tuple_extract_key_slowpath;
		key_def->tuple_extract_key_raw = tuple_extract_key_slowpath_raw;
	}
}

void
tuple_init(void)
{
	tuple_format_init();

	mempool_create(&tuple_iterator_pool, &cord()->slabc,
		       sizeof(struct tuple_iterator));

	box_tuple_last = NULL;
}

void
tuple_arena_create(struct slab_arena *arena, struct quota *quota,
		   uint64_t arena_max_size, uint32_t tuple_max_size,
		   const char *arena_name)
{
	if (tuple_max_size < TUPLE_MAX_SIZE_MIN)
		tuple_max_size = TUPLE_MAX_SIZE_MIN;

	/* Calculate slab size for tuple arena. */
	size_t slab_size = small_round(tuple_max_size * 4);
	if (slab_size < SLAB_SIZE_MIN)
		slab_size = SLAB_SIZE_MIN;

	/*
	 * Ensure that quota is a multiple of slab_size, to
	 * have accurate value of quota_used_ratio.
	 */
	size_t prealloc = small_align(arena_max_size, slab_size);
	/** Preallocate entire quota. */
	quota_init(quota, prealloc);

	say_info("mapping %zu bytes for %s tuple arena...", prealloc,
		 arena_name);

	if (slab_arena_create(arena, quota, prealloc, slab_size,
			      MAP_PRIVATE) != 0) {
		if (errno == ENOMEM) {
			panic("failed to preallocate %zu bytes: Cannot "\
			      "allocate memory, check option '%s_memory' in box.cfg(..)", prealloc,
			      arena_name);
		} else {
			panic_syserror("failed to preallocate %zu bytes for %s"\
				       " tuple arena", prealloc, arena_name);
		}
	}
}

void
tuple_free(void)
{
	/* Unref last tuple returned by public C API */
	if (box_tuple_last != NULL) {
		tuple_unref(box_tuple_last);
		box_tuple_last = NULL;
	}

	mempool_destroy(&tuple_iterator_pool);

	tuple_format_free();
}

box_tuple_format_t *
box_tuple_format_default(void)
{
	return tuple_format_default;
}

int
box_tuple_ref(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_ref(tuple);
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
	struct tuple_iterator *it = (struct tuple_iterator *)
		mempool_alloc(&tuple_iterator_pool);
	if (it == NULL) {
		diag_set(OutOfMemory, tuple_iterator_pool.objsize,
			 "mempool", "new slab");
		return NULL;
	}
	if (tuple_ref(tuple) != 0) {
		mempool_free(&tuple_iterator_pool, it);
		return NULL;
	}
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
