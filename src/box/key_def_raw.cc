/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "key_def_raw.h"

#include <PMurHash.h>
#include <stddef.h>
#include <stdint.h>

#include "key_def.h"
#include "msgpuck.h"
#include "trivia/util.h"
#include "tuple.h"

enum { HASH_SEED = 13U };

/**
 * Trivial tuple field cache - caches the last accessed field.
 */
struct raw_tuple_field_cache {
	/** Number of tuple fields. */
	uint32_t field_count;
	/** Cached field number. */
	uint32_t fieldno;
	/** Cached field data. */
	const char *field;
};

static ALWAYS_INLINE void
raw_tuple_field_cache_create(struct tuple *tuple,
			     struct raw_tuple_field_cache *cache)
{
	cache->fieldno = 0;
	cache->field = tuple_data(tuple);
	cache->field_count = mp_decode_array(&cache->field);
}

/**
 * Returns a tuple field data by field number or NULL if there's no such field.
 *
 * This function uses the cache to speed up sequential field accesses: if the
 * requested field comes after the last accessed one, the function scans the
 * MsgPack starting from the last field, not from the beginning of the tuple.
 */
static ALWAYS_INLINE const char *
raw_tuple_field(struct tuple *tuple, uint32_t fieldno,
		struct raw_tuple_field_cache *cache)
{
	if (fieldno >= cache->field_count)
		return NULL;
	if (fieldno < cache->fieldno)
		raw_tuple_field_cache_create(tuple, cache);
	while (cache->fieldno != fieldno) {
		mp_next(&cache->field);
		++cache->fieldno;
	}
	return cache->field;
}

/**
 * Returns a tuple field data by key part or NULL if there's no such field.
 */
static ALWAYS_INLINE const char *
raw_tuple_field_by_part(struct tuple *tuple, struct key_part *part,
			int multikey_idx, struct raw_tuple_field_cache *cache)
{
	const char *field = raw_tuple_field(tuple, part->fieldno, cache);
	if (field != NULL && part->path != NULL &&
	    tuple_go_to_path(&field, part->path, part->path_len,
			     TUPLE_INDEX_BASE, multikey_idx) != 0)
		unreachable();
	return field;
}

/**
 * Compare tuple with key without using tuple format and skip compared parts
 * of the key using raw key_parts instead of key_def.
 */
template<bool has_desc_parts>
static ALWAYS_INLINE int
raw_tuple_compare_with_key_impl(struct tuple *tuple, int multikey_idx,
				const char **key, uint32_t part_count,
				struct key_part *key_parts,
				uint32_t key_parts_count, bool is_nullable)
{
	assert(part_count <= key_parts_count);
	(void)key_parts_count;
	int rc;
	struct raw_tuple_field_cache cache;
	raw_tuple_field_cache_create(tuple, &cache);
	for (uint32_t i = 0; i < part_count; i++, mp_next(key)) {
		struct key_part *part = &key_parts[i];
		bool is_asc = !has_desc_parts ||
			      part->sort_order != SORT_ORDER_DESC;
		int direction = is_asc ? 1 : -1;
		const char *field = raw_tuple_field_by_part(
			tuple, part, multikey_idx, &cache);
		if (!is_nullable) {
compare_field:
			rc = tuple_compare_field(field, *key, part->type,
						 part->coll);
			if (rc != 0)
				return direction * rc;
			continue;
		}
		enum mp_type field_type = field != NULL ?
					  mp_typeof(*field) : MP_NIL;
		enum mp_type key_type = mp_typeof(**key);
		if (field_type == MP_NIL) {
			if (key_type != MP_NIL)
				return direction * -1;
		} else if (key_type == MP_NIL) {
			return direction * 1;
		} else {
			goto compare_field;
		}
	}
	return 0;
}

/**
 * Implementation of key_def::tuple_compare that doesn't use tuple format.
 */
static int
raw_tuple_compare(struct tuple *tuple_a, hint_t tuple_a_hint,
		  struct tuple *tuple_b, hint_t tuple_b_hint,
		  struct key_def *key_def)
{
	assert(!key_def->for_func_index);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	int mk_idx_a, mk_idx_b;
	hint_t key_a_hint, key_b_hint;
	if (!key_def->is_multikey) {
		mk_idx_a = MULTIKEY_NONE;
		mk_idx_b = MULTIKEY_NONE;
		key_a_hint = tuple_a_hint;
		key_b_hint = tuple_b_hint;
	} else {
		mk_idx_a = tuple_a_hint;
		mk_idx_b = tuple_b_hint;
		key_a_hint = HINT_NONE;
		key_b_hint = HINT_NONE;
	}
	const char *key_a = tuple_extract_key(tuple_a, key_def, mk_idx_a, NULL);
	const char *key_b = tuple_extract_key(tuple_b, key_def, mk_idx_b, NULL);
	uint32_t part_count_a = mp_decode_array(&key_a);
	uint32_t part_count_b = mp_decode_array(&key_b);
	int rc = key_compare(key_a, part_count_a, key_a_hint,
			     key_b, part_count_b, key_b_hint, key_def);
	region_truncate(region, region_svp);
	return rc;
}

/**
 * Implementation of key_def::tuple_compare_with_key for general and multikey
 * indexes that doesn't use tuple format.
 */
template<bool has_desc_parts>
static int
raw_tuple_compare_with_key(struct tuple *tuple, hint_t tuple_hint,
			   const char *key, uint32_t part_count,
			   hint_t key_hint, struct key_def *key_def)
{
	assert(!key_def->for_func_index);
	int rc;
	int multikey_idx;
	if (!key_def->is_multikey) {
		rc = hint_cmp(tuple_hint, key_hint);
		if (rc != 0)
			return rc;
		multikey_idx = MULTIKEY_NONE;
	} else {
		multikey_idx = (int)tuple_hint;
	}
	return raw_tuple_compare_with_key_impl<has_desc_parts>(
		tuple, multikey_idx, &key, part_count, key_def->parts,
		key_def->part_count, key_def->is_nullable);
}

/**
 * Implementation of key_def::tuple_compare for functional indexes
 * that doesn't use tuple format.
 */
static int
raw_func_index_compare(struct tuple *tuple_a, hint_t tuple_a_hint,
		       struct tuple *tuple_b, hint_t tuple_b_hint,
		       struct key_def *key_def)
{
	assert(key_def->for_func_index);
	const char *key_a = tuple_data((struct tuple *)tuple_a_hint);
	const char *key_b = tuple_data((struct tuple *)tuple_b_hint);
	assert(mp_typeof(*key_a) == MP_ARRAY);
	assert(mp_typeof(*key_b) == MP_ARRAY);
	uint32_t part_count_a = mp_decode_array(&key_a);
	uint32_t part_count_b = mp_decode_array(&key_b);
	assert(part_count_a == part_count_b);
	uint32_t key_part_count = part_count_a;

	/* First compare the functional key. */
	int rc = key_compare(key_a, part_count_a, HINT_NONE,
			     key_b, part_count_b, HINT_NONE, key_def);
	if (rc != 0)
		return rc;

	/*
	 * Tuples with nullified fields may violate the uniqueness constraint,
	 * so let's proceed with primary key parts if the key is nullable even
	 * if it's unique.
	 */
	if (key_part_count == key_def->unique_part_count &&
	    !key_def->is_nullable)
		return rc;

	/* Compare the primary key parts. */
	assert(key_part_count <= key_def->part_count);
	struct raw_tuple_field_cache cache_a, cache_b;
	raw_tuple_field_cache_create(tuple_a, &cache_a);
	raw_tuple_field_cache_create(tuple_b, &cache_b);
	for (uint32_t i = key_part_count; i < key_def->part_count; i++) {
		struct key_part *part = &key_def->parts[i];
		bool is_asc = part->sort_order != SORT_ORDER_DESC;
		int direction = is_asc ? 1 : -1;
		const char *field_a = raw_tuple_field_by_part(
			tuple_a, part, MULTIKEY_NONE, &cache_a);
		const char *field_b = raw_tuple_field_by_part(
			tuple_b, part, MULTIKEY_NONE, &cache_b);
		/* The primary key can't be nullable. */
		assert(field_a != NULL);
		assert(field_b != NULL);
		rc = tuple_compare_field(field_a, field_b, part->type,
					 part->coll);
		if (rc != 0)
			return direction * rc;
	}
	return 0;
}

/**
 * Implementation of key_def::tuple_compare_with_key for functional indexes
 * that doesn't use tuple format. Compares both functional and primary keys.
 */
template<bool has_desc_parts>
static int
raw_func_index_compare_with_key(struct tuple *tuple, hint_t tuple_hint,
				const char *key, uint32_t key_part_count,
				hint_t key_hint, struct key_def *key_def)
{
	(void)tuple;
	(void)key_hint;
	assert(key_def->for_func_index);
	const char *tuple_key = tuple_data((struct tuple *)tuple_hint);
	assert(mp_typeof(*tuple_key) == MP_ARRAY);
	uint32_t tuple_key_count = mp_decode_array(&tuple_key);
	uint32_t part_count = MIN(key_part_count, key_def->part_count);
	uint32_t func_cmp_part_count = MIN(part_count, tuple_key_count);
	struct tuple *func_key = (struct tuple *)tuple_hint;
	int rc = raw_tuple_compare_with_key_impl<has_desc_parts>(
		func_key, MULTIKEY_NONE, &key, func_cmp_part_count,
		key_def->parts, key_def->part_count, key_def->is_nullable);
	if (rc != 0 || part_count <= tuple_key_count)
		return rc;
	part_count -= func_cmp_part_count;
	struct key_part *parts = &key_def->parts[tuple_key_count];
	uint32_t parts_count = key_def->part_count - tuple_key_count;
	return raw_tuple_compare_with_key_impl<has_desc_parts>(
		tuple, MULTIKEY_NONE, &key, part_count, parts, parts_count,
		false);
}

/**
 * Implementation of key_def::tuple_hash that doesn't use tuple format.
 */
static uint32_t
raw_tuple_hash(struct tuple *tuple, struct key_def *key_def)
{
	assert(!key_def->is_multikey);
	assert(!key_def->for_func_index);
	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;
	struct raw_tuple_field_cache cache;
	raw_tuple_field_cache_create(tuple, &cache);
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		struct key_part *part = &key_def->parts[i];
		const char *field = raw_tuple_field_by_part(
			tuple, part, MULTIKEY_NONE, &cache);
		if (field == NULL) {
			assert(mp_sizeof_nil() == 1);
			char nil;
			mp_encode_nil(&nil);
			PMurHash32_Process(&h, &carry, &nil, 1);
			total_size++;
		} else {
			total_size += tuple_hash_field(&h, &carry, &field,
						       part->coll);
		}
	}
	return PMurHash32_Result(h, carry, total_size);
}

/**
 * Implementation of key_def::tuple_hint that doesn't use tuple format.
 */
static hint_t
raw_tuple_hint(struct tuple *tuple, struct key_def *key_def)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	const char *key = tuple_extract_key(tuple, key_def,
					    MULTIKEY_NONE, NULL);
	uint32_t part_count = mp_decode_array(&key);
	hint_t result = key_hint(key, part_count, key_def);
	region_truncate(region, region_svp);
	return result;
}

/**
 * Implementation of the tuple hint for functional and multikey indexes, meant
 * to never be called.
 */
static hint_t
raw_tuple_hint_stub(struct tuple *tuple, struct key_def *key_def)
{
	(void)tuple;
	(void)key_def;
	unreachable();
	return HINT_NONE;
}

/**
 * Implementation of key_def::tuple_extract_key that doesn't use tuple format.
 * Wrapper over raw version of tuple_extract_key.
 */
static char *
raw_tuple_extract_key(struct tuple *tuple, struct key_def *key_def,
		      int multikey_idx, uint32_t *key_size,
		      struct region *region)
{
	uint32_t size;
	const char *data = tuple_data_range(tuple, &size);
	return tuple_extract_key_raw_to_region(data, data + size, key_def,
					       multikey_idx, key_size, region);
}

void
key_def_set_func_raw(struct key_def *def)
{
	/*
	 * key_hint and tuple_extract_key_raw work with raw MsgPack
	 * so we don't need to override them.
	 */
	if (def->for_func_index) {
		def->tuple_compare = raw_func_index_compare;
		def->tuple_compare_with_key = key_def_has_desc_parts(def) ?
			raw_func_index_compare_with_key<true> :
			raw_func_index_compare_with_key<false>;
	} else {
		def->tuple_compare = raw_tuple_compare;
		def->tuple_compare_with_key = key_def_has_desc_parts(def) ?
			raw_tuple_compare_with_key<true> :
			raw_tuple_compare_with_key<false>;
	}
	def->tuple_hash = raw_tuple_hash;
	if (def->for_func_index || def->is_multikey)
		def->tuple_hint = raw_tuple_hint_stub;
	else
		def->tuple_hint = raw_tuple_hint;
	def->tuple_extract_key = raw_tuple_extract_key;
}
