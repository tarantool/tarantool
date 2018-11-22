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
#include "memory.h"
#include "fiber.h"
#include "tt_uuid.h"
#include "small/quota.h"
#include "small/small.h"

#include "tuple_update.h"
#include "coll_id_cache.h"

static struct mempool tuple_iterator_pool;
static struct small_alloc runtime_alloc;

enum {
	/** Lowest allowed slab_alloc_minimal */
	OBJSIZE_MIN = 16,
};

/**
 * Container for big reference counters. Contains array of big
 * reference counters, size of this array and number of non-zero
 * big reference counters. When reference counter of tuple becomes
 * more than 32767, field refs of this tuple becomes index of big
 * reference counter in big reference counter array and field
 * is_bigref is set true. The moment big reference becomes equal
 * 32767 it is set to 0, refs of the tuple becomes 32767 and
 * is_bigref becomes false. Big reference counter can be equal to
 * 0 or be more than 32767.
 */
static struct bigref_list {
	/** Free-list of big reference counters. */
	uint32_t *refs;
	/** Capacity of the array. */
	uint16_t capacity;
	/** Index of first free element. */
	uint16_t vacant_index;
} bigref_list;

static const double ALLOC_FACTOR = 1.05;

/**
 * Last tuple returned by public C API
 * \sa tuple_bless()
 */
struct tuple *box_tuple_last;

struct tuple_format *tuple_format_runtime;

static void
runtime_tuple_delete(struct tuple_format *format, struct tuple *tuple);

static struct tuple *
runtime_tuple_new(struct tuple_format *format, const char *data, const char *end);

/** A virtual method table for tuple_format_runtime */
static struct tuple_format_vtab tuple_format_runtime_vtab = {
	runtime_tuple_delete,
	runtime_tuple_new,
};

static struct tuple *
runtime_tuple_new(struct tuple_format *format, const char *data, const char *end)
{
	assert(format->vtab.tuple_delete == tuple_format_runtime_vtab.tuple_delete);

	mp_tuple_assert(data, end);
	size_t data_len = end - data;
	size_t total = sizeof(struct tuple) + format->field_map_size +
		data_len;

	struct tuple *tuple = (struct tuple *) smalloc(&runtime_alloc, total);
	if (tuple == NULL) {
		diag_set(OutOfMemory, (unsigned) total,
			 "malloc", "tuple");
		return NULL;
	}

	tuple->refs = 0;
	tuple->bsize = data_len;
	tuple->format_id = tuple_format_id(format);
	tuple_format_ref(format);
	tuple->data_offset = sizeof(struct tuple) + format->field_map_size;
	char *raw = (char *) tuple + tuple->data_offset;
	uint32_t *field_map = (uint32_t *) raw;
	memcpy(raw, data, data_len);
	if (tuple_init_field_map(format, field_map, raw, true)) {
		runtime_tuple_delete(format, tuple);
		return NULL;
	}
	say_debug("%s(%zu) = %p", __func__, data_len, tuple);
	return tuple;
}

static void
runtime_tuple_delete(struct tuple_format *format, struct tuple *tuple)
{
	assert(format->vtab.tuple_delete == tuple_format_runtime_vtab.tuple_delete);
	say_debug("%s(%p)", __func__, tuple);
	assert(tuple->refs == 0);
	size_t total = sizeof(struct tuple) + format->field_map_size +
		tuple->bsize;
	tuple_format_unref(format);
	smfree(&runtime_alloc, tuple, total);
}

int
tuple_validate_raw(struct tuple_format *format, const char *tuple)
{
	if (tuple_format_field_count(format) == 0)
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
	if (unlikely(field_count < format->min_field_count)) {
		diag_set(ClientError, ER_MIN_FIELD_COUNT,
			 (unsigned) field_count,
			 (unsigned) format->min_field_count);
		return -1;
	}

	/* Check field types */
	struct tuple_field *field = tuple_format_field(format, 0);
	uint32_t i = 0;
	uint32_t defined_field_count =
		MIN(field_count, tuple_format_field_count(format));
	for (; i < defined_field_count; ++i) {
		field = tuple_format_field(format, i);
		if (key_mp_type_validate(field->type, mp_typeof(*tuple),
					 ER_FIELD_TYPE, i + TUPLE_INDEX_BASE,
					 tuple_field_is_nullable(field)))
			return -1;
		mp_next(&tuple);
	}
	return 0;
}

/** Initialize big references container. */
static inline void
bigref_list_create(void)
{
	memset(&bigref_list, 0, sizeof(bigref_list));
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

int
tuple_init(field_name_hash_f hash)
{
	field_name_hash = hash;
	/*
	 * Create a format for runtime tuples
	 */
	tuple_format_runtime = tuple_format_new(&tuple_format_runtime_vtab,
						NULL, 0, NULL, 0, NULL);
	if (tuple_format_runtime == NULL)
		return -1;

	/* Make sure this one stays around. */
	tuple_format_ref(tuple_format_runtime);

	small_alloc_create(&runtime_alloc, &cord()->slabc, OBJSIZE_MIN,
			   ALLOC_FACTOR);

	mempool_create(&tuple_iterator_pool, &cord()->slabc,
		       sizeof(struct tuple_iterator));

	box_tuple_last = NULL;

	bigref_list_create();

	if (coll_id_cache_init() != 0)
		return -1;

	return 0;
}

void
tuple_arena_create(struct slab_arena *arena, struct quota *quota,
		   uint64_t arena_max_size, uint32_t slab_size,
		   const char *arena_name)
{
	/*
	 * Ensure that quota is a multiple of slab_size, to
	 * have accurate value of quota_used_ratio.
	 */
	size_t prealloc = small_align(arena_max_size, slab_size);

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

enum {
	BIGREF_FACTOR = 2,
	BIGREF_MAX = UINT32_MAX,
	BIGREF_MIN_CAPACITY = 16,
	/**
	 * Only 15 bits are available for bigref list index in
	 * struct tuple.
	 */
	BIGREF_MAX_CAPACITY = UINT16_MAX >> 1
};

/** Destroy big references and free memory that was allocated. */
static inline void
bigref_list_destroy(void)
{
	free(bigref_list.refs);
}

/**
 * Increase capacity of bigref_list.
 */
static inline void
bigref_list_increase_capacity(void)
{
	assert(bigref_list.capacity == bigref_list.vacant_index);
	uint32_t *refs = bigref_list.refs;
	uint16_t capacity = bigref_list.capacity;
	if (capacity == 0)
		capacity = BIGREF_MIN_CAPACITY;
	else if (capacity < BIGREF_MAX_CAPACITY)
		capacity = MIN(capacity * BIGREF_FACTOR, BIGREF_MAX_CAPACITY);
	else
		panic("Too many big references");
	refs = (uint32_t *) realloc(refs, capacity * sizeof(*refs));
	if (refs == NULL) {
		panic("failed to reallocate %zu bytes: Cannot allocate "\
		      "memory.", capacity * sizeof(*refs));
	}
	for (uint16_t i = bigref_list.capacity; i < capacity; ++i)
		refs[i] = i + 1;
	bigref_list.refs = refs;
	bigref_list.capacity = capacity;
}

/**
 * Return index for new big reference counter and allocate memory
 * if needed.
 * @retval index for new big reference counter.
 */
static inline uint16_t
bigref_list_new_index(void)
{
	if (bigref_list.vacant_index == bigref_list.capacity)
		bigref_list_increase_capacity();
	uint16_t vacant_index = bigref_list.vacant_index;
	bigref_list.vacant_index = bigref_list.refs[vacant_index];
	return vacant_index;
}

void
tuple_ref_slow(struct tuple *tuple)
{
	assert(tuple->is_bigref || tuple->refs == TUPLE_REF_MAX);
	if (! tuple->is_bigref) {
		tuple->ref_index = bigref_list_new_index();
		tuple->is_bigref = true;
		bigref_list.refs[tuple->ref_index] = TUPLE_REF_MAX;
	} else if (bigref_list.refs[tuple->ref_index] == BIGREF_MAX) {
		panic("Tuple big reference counter overflow");
	}
	bigref_list.refs[tuple->ref_index]++;
}

void
tuple_unref_slow(struct tuple *tuple)
{
	assert(tuple->is_bigref &&
	       bigref_list.refs[tuple->ref_index] > TUPLE_REF_MAX);
	if(--bigref_list.refs[tuple->ref_index] == TUPLE_REF_MAX) {
		bigref_list.refs[tuple->ref_index] = bigref_list.vacant_index;
		bigref_list.vacant_index = tuple->ref_index;
		tuple->ref_index = TUPLE_REF_MAX;
		tuple->is_bigref = false;
	}
}

void
tuple_arena_destroy(struct slab_arena *arena)
{
	slab_arena_destroy(arena);
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
	small_alloc_destroy(&runtime_alloc);

	tuple_format_free();

	coll_id_cache_destroy();

	bigref_list_destroy();
}

box_tuple_format_t *
box_tuple_format_default(void)
{
	return tuple_format_runtime;
}

box_tuple_format_t *
box_tuple_format_new(struct key_def **keys, uint16_t key_count)
{
	box_tuple_format_t *format =
		tuple_format_new(&tuple_format_runtime_vtab,
				 keys, key_count, NULL, 0, NULL);
	if (format != NULL)
		tuple_format_ref(format);
	return format;
}

int
box_tuple_ref(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	tuple_ref(tuple);
	return 0;
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
tuple_to_buf(const struct tuple *tuple, char *buf, size_t size)
{
	uint32_t bsize;
	const char *data = tuple_data_range(tuple, &bsize);
	if (likely(bsize <= size)) {
		memcpy(buf, data, bsize);
	}
	return bsize;
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
box_tuple_update(const box_tuple_t *tuple, const char *expr,
		 const char *expr_end)
{
	uint32_t new_size = 0, bsize;
	const char *old_data = tuple_data_range(tuple, &bsize);
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	const char *new_data =
		tuple_update_execute(region_aligned_alloc_cb, region, expr,
				     expr_end, old_data, old_data + bsize,
				     &new_size, 1, NULL);
	if (new_data == NULL) {
		region_truncate(region, used);
		return NULL;
	}
	struct tuple *ret = tuple_new(tuple_format(tuple), new_data,
				      new_data + new_size);
	region_truncate(region, used);
	if (ret != NULL)
		return tuple_bless(ret);
	return NULL;
}

box_tuple_t *
box_tuple_upsert(const box_tuple_t *tuple, const char *expr,
		 const char *expr_end)
{
	uint32_t new_size = 0, bsize;
	const char *old_data = tuple_data_range(tuple, &bsize);
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	const char *new_data =
		tuple_upsert_execute(region_aligned_alloc_cb, region, expr,
				     expr_end, old_data, old_data + bsize,
				     &new_size, 1, false, NULL);
	if (new_data == NULL) {
		region_truncate(region, used);
		return NULL;
	}

	struct tuple *ret = tuple_new(tuple_format(tuple),
				      new_data, new_data + new_size);
	region_truncate(region, used);
	if (ret != NULL)
		return tuple_bless(ret);
	return NULL;
}

box_tuple_t *
box_tuple_new(box_tuple_format_t *format, const char *data, const char *end)
{
	struct tuple *ret = tuple_new(format, data, end);
	if (ret == NULL)
		return NULL;
	return tuple_bless(ret);
}

int
tuple_snprint(char *buf, int size, const struct tuple *tuple)
{
	int total = 0;
	if (tuple == NULL) {
		SNPRINT(total, snprintf, buf, size, "<NULL>");
		return total;
	}
	SNPRINT(total, mp_snprint, buf, size, tuple_data(tuple));
	return total;
}

const char *
tuple_str(const struct tuple *tuple)
{
	char *buf = tt_static_buf();
	if (tuple_snprint(buf, TT_STATIC_BUF_LEN, tuple) < 0)
		return "<failed to format tuple>";
	return buf;
}

const char *
mp_str(const char *data)
{
	char *buf = tt_static_buf();
	if (mp_snprint(buf, TT_STATIC_BUF_LEN, data) < 0)
		return "<failed to format message pack>";
	return buf;
}
