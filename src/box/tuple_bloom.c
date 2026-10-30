/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include "tuple_bloom.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <msgpuck.h>
#include "coll/coll.h"
#include "diag.h"
#include "errcode.h"
#include "key_def.h"
#include "tuple.h"
#include "salad/bloom.h"
#include "trivia/util.h"
#include <PMurHash.h>

enum { HASH_SEED = 13U };

/**
 * An older implementation of field hashing which handles suboptimally encoded
 * MessagePack integers incorrectly.
 */
static uint32_t
tuple_hash_field_bloom_v2(uint32_t *ph1, uint32_t *pcarry, const char **field,
			  enum field_type type, struct coll *coll)
{
	char buf[9];
	const char *f = *field;
	uint32_t size;
	/*
	 * MsgPack values of double key field are cast to double, encoded
	 * as msgpack double and hashed. This assures the same value being
	 * written as int, uint, float or double has the same hash for this
	 * type of key.
	 *
	 * We create and hash msgpack instead of just hashing the double itself
	 * for backward compatibility: so a user having a vinyl database with
	 * double-key index won't have to rebuild it after tarantool update.
	 */
	if (type == FIELD_TYPE_DOUBLE) {
		double value = 0;
		/*
		 * This will only fail if the mp_type is not numeric, which is
		 * impossible here (see field_mp_plain_type_is_compatible).
		 */
		VERIFY(mp_read_double_lossy(field, &value) == 0);
		char *double_msgpack_end = mp_encode_double(buf, value);
		size = double_msgpack_end - buf;
		assert(size <= sizeof(buf));
		PMurHash32_Process(ph1, pcarry, buf, size);
		return size;
	}

	switch (mp_typeof(**field)) {
	case MP_STR:
		/*
		 * (!) MP_STR fields hashed **excluding** MsgPack format
		 * identifier. We have to do that to keep compatibility
		 * with old third-party MsgPack (spec-old.md) implementations.
		 * \sa https://github.com/tarantool/tarantool/issues/522
		 */
		f = mp_decode_str(field, &size);
		if (coll != NULL)
			return coll->hash(f, size, ph1, pcarry, coll);
		break;
	case MP_FLOAT:
	case MP_DOUBLE: {
		/*
		 * If a floating point number can be stored as an integer,
		 * convert it to MP_INT/MP_UINT before hashing so that we
		 * can select integer values by floating point keys and
		 * vice versa.
		 */
		double iptr;
		double val = mp_typeof(**field) == MP_FLOAT ?
			     mp_decode_float(field) :
			     mp_decode_double(field);
		if (!isfinite(val) || modf(val, &iptr) != 0 ||
		    val < -exp2(63) || val >= exp2(64)) {
			size = *field - f;
			break;
		}
		char *data;
		if (val >= 0)
			data = mp_encode_uint(buf, (uint64_t)val);
		else
			data = mp_encode_int(buf, (int64_t)val);
		size = data - buf;
		assert(size <= sizeof(buf));
		f = buf;
		break;
	}
	default:
		mp_next(field);
		size = *field - f;
		/*
		 * (!) All other fields hashed **including** MsgPack format
		 * identifier (e.g. 0xcc). This was done **intentionally**
		 * for performance reasons. Please follow MsgPack specification
		 * and pack all your numbers to the most compact representation.
		 * If you still want to add support for broken MsgPack,
		 * please don't forget to patch tuple_compare_field().
		 */
		break;
	}
	assert(size < INT32_MAX);
	PMurHash32_Process(ph1, pcarry, f, size);
	return size;
}

/**
 * An older implementation of key part hashing which handles suboptimally
 * encoded MessagePack integers incorrectly.
 */
static uint32_t
tuple_hash_key_part_bloom_v2(uint32_t *ph1, uint32_t *pcarry,
			     struct tuple *tuple, struct key_part *part,
			     int multikey_idx)
{
	const char *field = tuple_field_by_part(tuple, part, multikey_idx);
	if (field == NULL)
		return tuple_hash_null(ph1, pcarry);
	return tuple_hash_field_bloom_v2(ph1, pcarry, &field, part->type,
					 part->coll);
}

struct tuple_bloom_builder *
tuple_bloom_builder_new(uint32_t part_count)
{
	size_t size = sizeof(struct tuple_bloom_builder) +
		part_count * sizeof(struct tuple_hash_array);
	struct tuple_bloom_builder *builder = malloc(size);
	if (builder == NULL) {
		diag_set(OutOfMemory, size, "malloc", "tuple bloom builder");
		return NULL;
	}
	memset(builder, 0, size);
	builder->part_count = part_count;
	return builder;
}

void
tuple_bloom_builder_delete(struct tuple_bloom_builder *builder)
{
	for (uint32_t i = 0; i < builder->part_count; i++)
		free(builder->parts[i].values);
	free(builder);
}

/**
 * Add a tuple hash to a hash array unless it's already there.
 * Reallocate the array if necessary.
 */
static int
tuple_hash_array_add(struct tuple_hash_array *hash_arr, uint32_t hash)
{
	if (hash_arr->count > 0 &&
	    hash_arr->values[hash_arr->count - 1] == hash) {
		/*
		 * This part is already in the bloom, proceed
		 * to the next one. Note, this check only works
		 * if tuples are added in the order defined by
		 * the key definition.
		 */
		return 0;
	}
	if (hash_arr->count >= hash_arr->capacity) {
		uint32_t capacity = MAX(hash_arr->capacity * 2, 1024U);
		uint32_t *values = realloc(hash_arr->values,
					   capacity * sizeof(*values));
		if (values == NULL) {
			diag_set(OutOfMemory, capacity * sizeof(*values),
				 "malloc", "tuple hash array");
			return -1;
		}
		hash_arr->capacity = capacity;
		hash_arr->values = values;
	}
	hash_arr->values[hash_arr->count++] = hash;
	return 0;
}

int
tuple_bloom_builder_add(struct tuple_bloom_builder *builder,
			struct tuple *tuple, struct key_def *key_def,
			int multikey_idx)
{
	assert(builder->part_count == key_def->part_count);
	assert(!key_def->is_multikey || multikey_idx != MULTIKEY_NONE);

	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;

	for (uint32_t i = 0; i < key_def->part_count; i++) {
		total_size += tuple_hash_key_part(&h, &carry, tuple,
						  &key_def->parts[i],
						  multikey_idx);
		uint32_t hash = PMurHash32_Result(h, carry, total_size);
		if (tuple_hash_array_add(&builder->parts[i], hash) != 0)
			return -1;
	}
	return 0;
}

int
tuple_bloom_builder_add_key(struct tuple_bloom_builder *builder,
			    const char *key, uint32_t part_count,
			    struct key_def *key_def)
{
	(void)part_count;
	assert(part_count >= key_def->part_count);
	assert(builder->part_count == key_def->part_count);

	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;

	for (uint32_t i = 0; i < key_def->part_count; i++) {
		total_size += tuple_hash_field(&h, &carry, &key,
					       key_def->parts[i].coll);
		uint32_t hash = PMurHash32_Result(h, carry, total_size);
		if (tuple_hash_array_add(&builder->parts[i], hash) != 0)
			return -1;
	}
	return 0;
}

struct tuple_bloom *
tuple_bloom_new(struct tuple_bloom_builder *builder, double fpr)
{
	uint32_t part_count = builder->part_count;
	size_t size = sizeof(struct tuple_bloom) +
			part_count * sizeof(struct tuple_bloom_part);
	struct tuple_bloom *bloom = xmalloc(size);
	bloom->version = TUPLE_BLOOM_VERSION_V3;
	bloom->part_count = 0;

	for (uint32_t i = 0; i < part_count; i++) {
		struct tuple_hash_array *hash_arr = &builder->parts[i];
		uint32_t count = hash_arr->count;
		/*
		 * When we check if a key is stored in a bloom
		 * filter, we check all its sub keys as well,
		 * which reduces the resulting false positive
		 * rate. Take this into account and adjust fpr
		 * accordingly when constructing a bloom filter
		 * for keys of a higher rank.
		 */
		double part_fpr = fpr;
		for (uint32_t j = 0; j < i; j++)
			part_fpr /= bloom_fpr(&bloom->parts[j].bloom,
					      count);
		part_fpr = MIN(part_fpr, 0.5);
		bloom_create(&bloom->parts[i].bloom, count, part_fpr);
		bloom->parts[i].data = xcalloc(
			1, bloom_data_size(&bloom->parts[i].bloom));
		bloom->part_count++;
		for (uint32_t k = 0; k < count; k++)
			bloom_add(&bloom->parts[i].bloom,
				  bloom->parts[i].data,
				  hash_arr->values[k]);
	}
	return bloom;
}

void
tuple_bloom_delete(struct tuple_bloom *bloom)
{
	for (uint32_t i = 0; i < bloom->part_count; i++)
		free(bloom->parts[i].data);
	free(bloom);
}

bool
tuple_bloom_maybe_has(const struct tuple_bloom *bloom, struct tuple *tuple,
		      struct key_def *key_def, int multikey_idx)
{
	assert(!key_def->is_multikey || multikey_idx != MULTIKEY_NONE);

	if (bloom->version == TUPLE_BLOOM_VERSION_V1) {
		return bloom_maybe_has(&bloom->parts[0].bloom,
				       bloom->parts[0].data,
				       tuple_hash(tuple, key_def));
	}

	assert(bloom->part_count == key_def->part_count);

	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;
	if (bloom->version == TUPLE_BLOOM_VERSION_V2) {
		for (uint32_t i = 0; i < key_def->part_count; i++) {
			total_size += tuple_hash_key_part_bloom_v2(
				&h, &carry, tuple, &key_def->parts[i],
				multikey_idx);
			uint32_t hash = PMurHash32_Result(h, carry, total_size);
			if (!bloom_maybe_has(&bloom->parts[i].bloom,
					     bloom->parts[i].data,
					     hash))
				return false;
		}
		return true;
	}
	assert(bloom->version == TUPLE_BLOOM_VERSION_V3);
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		total_size += tuple_hash_key_part(&h, &carry, tuple,
						  &key_def->parts[i],
						  multikey_idx);
		uint32_t hash = PMurHash32_Result(h, carry, total_size);
		if (!bloom_maybe_has(&bloom->parts[i].bloom,
				     bloom->parts[i].data,
				     hash))
			return false;
	}
	return true;
}

bool
tuple_bloom_maybe_has_key(const struct tuple_bloom *bloom,
			  const char *key, uint32_t part_count,
			  struct key_def *key_def)
{
	if (bloom->version == TUPLE_BLOOM_VERSION_V1) {
		if (part_count < key_def->part_count)
			return true;
		return bloom_maybe_has(&bloom->parts[0].bloom,
				       bloom->parts[0].data,
				       key_hash(key, key_def));
	}

	assert(part_count <= key_def->part_count);
	assert(bloom->part_count == key_def->part_count);

	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;
	if (bloom->version == TUPLE_BLOOM_VERSION_V2) {
		for (uint32_t i = 0; i < part_count; i++) {
			total_size += tuple_hash_field_bloom_v2(
				&h, &carry, &key, key_def->parts[i].type,
				key_def->parts[i].coll);
			uint32_t hash = PMurHash32_Result(h, carry, total_size);
			if (!bloom_maybe_has(&bloom->parts[i].bloom,
					     bloom->parts[i].data, hash))
				return false;
		}
		return true;
	}
	assert(bloom->version == TUPLE_BLOOM_VERSION_V3);
	for (uint32_t i = 0; i < part_count; i++) {
		total_size += tuple_hash_field(&h, &carry, &key,
					       key_def->parts[i].coll);
		uint32_t hash = PMurHash32_Result(h, carry, total_size);
		if (!bloom_maybe_has(&bloom->parts[i].bloom,
				     bloom->parts[i].data, hash))
			return false;
	}
	return true;
}

/** Returns amount of bytes required to encode the part to MsgPack. */
static size_t
tuple_bloom_sizeof_part(const struct tuple_bloom_part *part)
{
	size_t size = 0;
	size += mp_sizeof_array(3);
	size += mp_sizeof_uint(part->bloom.table_size);
	size += mp_sizeof_uint(part->bloom.hash_count);
	size += mp_sizeof_bin(bloom_data_size(&part->bloom));
	return size;
}

/** Encodes the part to MsgPack. */
static char *
tuple_bloom_encode_part(const struct tuple_bloom_part *part, char *buf)
{
	buf = mp_encode_array(buf, 3);
	buf = mp_encode_uint(buf, part->bloom.table_size);
	buf = mp_encode_uint(buf, part->bloom.hash_count);
	buf = mp_encode_bin(buf, part->data, bloom_data_size(&part->bloom));
	return buf;
}

/** Decodes the part from MsgPack. */
static void
tuple_bloom_decode_part(struct tuple_bloom_part *part, const char **data)
{
	memset(part, 0, sizeof(*part));
	if (mp_decode_array(data) != 3)
		unreachable();
	part->bloom.table_size = mp_decode_uint(data);
	part->bloom.hash_count = mp_decode_uint(data);
	size_t store_size = mp_decode_binl(data);
	assert(store_size == bloom_data_size(&part->bloom));
	part->data = xmalloc(store_size);
	memcpy(part->data, *data, store_size);
	*data += store_size;
}

size_t
tuple_bloom_size(const struct tuple_bloom *bloom)
{
	size_t size = 0;
	size += mp_sizeof_array(bloom->part_count);
	for (uint32_t i = 0; i < bloom->part_count; i++)
		size += tuple_bloom_sizeof_part(&bloom->parts[i]);
	return size;
}

char *
tuple_bloom_encode(const struct tuple_bloom *bloom, char *buf)
{
	buf = mp_encode_array(buf, bloom->part_count);
	for (uint32_t i = 0; i < bloom->part_count; i++)
		buf = tuple_bloom_encode_part(&bloom->parts[i], buf);
	return buf;
}

struct tuple_bloom *
tuple_bloom_decode(const char **data, enum tuple_bloom_version version)
{
	uint32_t part_count = mp_decode_array(data);
	struct tuple_bloom *bloom =
		xmalloc(sizeof(*bloom) + part_count * sizeof(*bloom->parts));
	bloom->version = version;
	switch (version) {
	case TUPLE_BLOOM_VERSION_V1:
		bloom->part_count = 1;
		if (mp_decode_array(data) != 4)
			unreachable();
		if (mp_decode_uint(data) != 0) /* version */
			unreachable();
		bloom->parts[0].bloom.table_size = mp_decode_uint(data);
		bloom->parts[0].bloom.hash_count = mp_decode_uint(data);
		size_t store_size = mp_decode_binl(data);
		assert(store_size ==
		       bloom_data_size(&bloom->parts[0].bloom));
		bloom->parts[0].data = xmalloc(store_size);
		memcpy(bloom->parts[0].data, *data, store_size);
		*data += store_size;
		break;
	case TUPLE_BLOOM_VERSION_V2:
	case TUPLE_BLOOM_VERSION_V3:
		bloom->part_count = 0;
		for (uint32_t i = 0; i < part_count; i++) {
			tuple_bloom_decode_part(&bloom->parts[i], data);
			bloom->part_count++;
		}
		break;
	}
	return bloom;
}
