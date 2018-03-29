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
#include <msgpuck.h>
#include "diag.h"
#include "errcode.h"
#include "memory.h"
#include "key_def.h"
#include "tuple.h"
#include "tuple_hash.h"
#include "salad/bloom.h"
#include "trivia/util.h"
#include "third_party/PMurHash.h"

enum { HASH_SEED = 13U };

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

int
tuple_bloom_builder_add(struct tuple_bloom_builder *builder,
			const struct tuple *tuple,
			const struct key_def *key_def,
			uint32_t hashed_parts)
{
	assert(builder->part_count == key_def->part_count);

	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;

	for (uint32_t i = 0; i < key_def->part_count; i++) {
		total_size += tuple_hash_key_part(&h, &carry, tuple,
						  &key_def->parts[i]);
		if (i < hashed_parts) {
			/*
			 * This part is already in the bloom, proceed
			 * to the next one. Note, we can't skip to
			 * hashed_parts, as we need to compute the hash.
			 */
			continue;
		}
		struct tuple_hash_array *hash_arr = &builder->parts[i];
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
		uint32_t hash = PMurHash32_Result(h, carry, total_size);
		hash_arr->values[hash_arr->count++] = hash;
	}
	return 0;
}

struct tuple_bloom *
tuple_bloom_new(struct tuple_bloom_builder *builder, double fpr)
{
	uint32_t part_count = builder->part_count;
	size_t size = sizeof(struct tuple_bloom) +
			part_count * sizeof(struct bloom);
	struct tuple_bloom *bloom = malloc(size);
	if (bloom == NULL) {
		diag_set(OutOfMemory, size, "malloc", "tuple bloom");
		return NULL;
	}

	bloom->is_legacy = false;
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
			part_fpr /= bloom_fpr(&bloom->parts[j], count);
		part_fpr = MIN(part_fpr, 0.5);
		if (bloom_create(&bloom->parts[i], count,
				 part_fpr, runtime.quota) != 0) {
			diag_set(OutOfMemory, 0, "bloom_create",
				 "tuple bloom part");
			tuple_bloom_delete(bloom);
			return NULL;
		}
		bloom->part_count++;
		for (uint32_t k = 0; k < count; k++)
			bloom_add(&bloom->parts[i], hash_arr->values[k]);
	}
	return bloom;
}

void
tuple_bloom_delete(struct tuple_bloom *bloom)
{
	for (uint32_t i = 0; i < bloom->part_count; i++)
		bloom_destroy(&bloom->parts[i], runtime.quota);
	free(bloom);
}

bool
tuple_bloom_maybe_has(const struct tuple_bloom *bloom,
		      const struct tuple *tuple,
		      const struct key_def *key_def)
{
	if (bloom->is_legacy) {
		return bloom_maybe_has(&bloom->parts[0],
				       tuple_hash(tuple, key_def));
	}

	assert(bloom->part_count == key_def->part_count);

	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;

	for (uint32_t i = 0; i < key_def->part_count; i++) {
		total_size += tuple_hash_key_part(&h, &carry, tuple,
						  &key_def->parts[i]);
		uint32_t hash = PMurHash32_Result(h, carry, total_size);
		if (!bloom_maybe_has(&bloom->parts[i], hash))
			return false;
	}
	return true;
}

bool
tuple_bloom_maybe_has_key(const struct tuple_bloom *bloom,
			  const char *key, uint32_t part_count,
			  const struct key_def *key_def)
{
	if (bloom->is_legacy) {
		if (part_count < key_def->part_count)
			return true;
		return bloom_maybe_has(&bloom->parts[0],
				       key_hash(key, key_def));
	}

	assert(part_count <= key_def->part_count);
	assert(bloom->part_count == key_def->part_count);

	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;

	for (uint32_t i = 0; i < part_count; i++) {
		total_size += tuple_hash_field(&h, &carry, &key,
					       key_def->parts[i].coll);
		uint32_t hash = PMurHash32_Result(h, carry, total_size);
		if (!bloom_maybe_has(&bloom->parts[i], hash))
			return false;
	}
	return true;
}

static size_t
tuple_bloom_sizeof_part(const struct bloom *part)
{
	size_t size = 0;
	size += mp_sizeof_array(3);
	size += mp_sizeof_uint(part->table_size);
	size += mp_sizeof_uint(part->hash_count);
	size += mp_sizeof_bin(bloom_store_size(part));
	return size;
}

static char *
tuple_bloom_encode_part(const struct bloom *part, char *buf)
{
	buf = mp_encode_array(buf, 3);
	buf = mp_encode_uint(buf, part->table_size);
	buf = mp_encode_uint(buf, part->hash_count);
	buf = mp_encode_binl(buf, bloom_store_size(part));
	buf = bloom_store(part, buf);
	return buf;
}

static int
tuple_bloom_decode_part(struct bloom *part, const char **data)
{
	memset(part, 0, sizeof(*part));
	if (mp_decode_array(data) != 3)
		unreachable();
	part->table_size = mp_decode_uint(data);
	part->hash_count = mp_decode_uint(data);
	size_t store_size = mp_decode_binl(data);
	assert(store_size == bloom_store_size(part));
	if (bloom_load_table(part, *data, runtime.quota) != 0) {
		diag_set(OutOfMemory, store_size, "bloom_load_table",
			 "tuple bloom part");
		return -1;
	}
	*data += store_size;
	return 0;
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
tuple_bloom_decode(const char **data)
{
	uint32_t part_count = mp_decode_array(data);
	struct tuple_bloom *bloom = malloc(sizeof(*bloom) +
			part_count * sizeof(*bloom->parts));
	if (bloom == NULL) {
		diag_set(OutOfMemory, sizeof(*bloom) +
			 part_count * sizeof(*bloom->parts),
			 "malloc", "tuple bloom");
		return NULL;
	}

	bloom->is_legacy = false;
	bloom->part_count = 0;

	for (uint32_t i = 0; i < part_count; i++) {
		if (tuple_bloom_decode_part(&bloom->parts[i], data) != 0) {
			tuple_bloom_delete(bloom);
			return NULL;
		}
		bloom->part_count++;
	}
	return bloom;
}

struct tuple_bloom *
tuple_bloom_decode_legacy(const char **data)
{
	struct tuple_bloom *bloom = malloc(sizeof(*bloom) +
					   sizeof(*bloom->parts));
	if (bloom == NULL) {
		diag_set(OutOfMemory, sizeof(*bloom) + sizeof(*bloom->parts),
			 "malloc", "tuple bloom");
		return NULL;
	}

	bloom->is_legacy = true;
	bloom->part_count = 1;

	if (mp_decode_array(data) != 4)
		unreachable();
	if (mp_decode_uint(data) != 0) /* version */
		unreachable();

	bloom->parts[0].table_size = mp_decode_uint(data);
	bloom->parts[0].hash_count = mp_decode_uint(data);

	size_t store_size = mp_decode_binl(data);
	assert(store_size == bloom_store_size(&bloom->parts[0]));
	if (bloom_load_table(&bloom->parts[0], *data, runtime.quota) != 0) {
		diag_set(OutOfMemory, store_size, "bloom_load_table",
			 "tuple bloom part");
		free(bloom);
		return NULL;
	}
	*data += store_size;
	return bloom;
}
