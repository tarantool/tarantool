/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ann_tuple_map.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/** Mix the pointer bits, including bits above the alignment. */
static size_t
ann_tuple_hash(const void *tuple)
{
	uint64_t value = (uintptr_t)tuple;
	value ^= value >> 30;
	value *= UINT64_C(0xbf58476d1ce4e5b9);
	value ^= value >> 27;
	value *= UINT64_C(0x94d049bb133111eb);
	return value ^ (value >> 31);
}

/** Publish a tuple-to-label mapping in a preallocated table. */
static void
ann_tuple_hash_insert(uint32_t *hash, size_t capacity,
		      const void *tuple, uint32_t label)
{
	size_t at = ann_tuple_hash(tuple) & (capacity - 1);
	while (hash[at] != 0)
		at = (at + 1) & (capacity - 1);
	hash[at] = label;
}

void
ann_tuple_map_create(struct ann_tuple_map *map,
		     const struct ann_memory *memory)
{
	memset(map, 0, sizeof(*map));
	map->memory = *memory;
}

void
ann_tuple_map_destroy(struct ann_tuple_map *map)
{
	if (map->hash != NULL)
		map->memory.free(map->memory.ctx, map->hash,
				 map->hash_capacity * sizeof(*map->hash));
	if (map->values != NULL)
		map->memory.free(map->memory.ctx, map->values,
				 (size_t)map->capacity * sizeof(*map->values));
	if (map->tuples != NULL)
		map->memory.free(map->memory.ctx, (void *)map->tuples,
				 (size_t)map->capacity * sizeof(*map->tuples));
	memset(map, 0, sizeof(*map));
}

uint64_t
ann_tuple_map_find(const struct ann_tuple_map *map, const void *tuple)
{
	if (map->hash_capacity == 0)
		return 0;
	size_t at = ann_tuple_hash(tuple) & (map->hash_capacity - 1);
	while (map->hash[at] != 0) {
		uint32_t label = map->hash[at];
		if (map->tuples[label - 1] == tuple)
			return label;
		at = (at + 1) & (map->hash_capacity - 1);
	}
	return 0;
}

enum ann_status
ann_tuple_map_prepare(struct ann_tuple_map *map)
{
	if (map->free_head != 0)
		return ANN_OK;
	if (map->count == UINT32_MAX)
		return ANN_INVALID;
	if (map->count < map->capacity)
		return ANN_OK;
	uint32_t capacity = map->capacity == 0 ? 8 :
		map->capacity > UINT32_MAX / 2 ? UINT32_MAX :
		map->capacity * 2;
#if SIZE_MAX < UINT64_MAX
	if (capacity > SIZE_MAX / sizeof(*map->tuples))
		return ANN_INVALID;
#endif
	size_t tuple_size = (size_t)capacity * sizeof(*map->tuples);
	const void **tuples = map->memory.alloc(map->memory.ctx, tuple_size);
	if (tuples == NULL)
		return ANN_OUT_OF_MEMORY;
	if (map->count != 0)
		memcpy(tuples, map->tuples,
		       (size_t)map->count * sizeof(*tuples));
	size_t value_size = (size_t)capacity * sizeof(*map->values);
	uintptr_t *values = map->memory.alloc(map->memory.ctx, value_size);
	if (values == NULL) {
		map->memory.free(map->memory.ctx, (void *)tuples, tuple_size);
		return ANN_OUT_OF_MEMORY;
	}
	if (map->count != 0)
		memcpy(values, map->values,
		       (size_t)map->count * sizeof(*values));

	size_t hash_capacity = 16;
	while (hash_capacity / 2 < capacity) {
		if (hash_capacity > SIZE_MAX / 2) {
			map->memory.free(map->memory.ctx, values, value_size);
			map->memory.free(map->memory.ctx, (void *)tuples,
					 tuple_size);
			return ANN_INVALID;
		}
		hash_capacity *= 2;
	}
	if (hash_capacity > SIZE_MAX / sizeof(*map->hash)) {
		map->memory.free(map->memory.ctx, values, value_size);
		map->memory.free(map->memory.ctx, (void *)tuples, tuple_size);
		return ANN_INVALID;
	}
	size_t hash_size = hash_capacity * sizeof(*map->hash);
	uint32_t *hash = map->memory.alloc(map->memory.ctx, hash_size);
	if (hash == NULL) {
		map->memory.free(map->memory.ctx, values, value_size);
		map->memory.free(map->memory.ctx, (void *)tuples, tuple_size);
		return ANN_OUT_OF_MEMORY;
	}
	memset(hash, 0, hash_size);
	for (uint32_t i = 0; i < map->count; ++i) {
		if (tuples[i] != NULL)
			ann_tuple_hash_insert(hash, hash_capacity,
					      tuples[i], i + 1);
	}
	if (map->values != NULL)
		map->memory.free(map->memory.ctx, map->values,
				 (size_t)map->capacity * sizeof(*map->values));
	if (map->tuples != NULL)
		map->memory.free(map->memory.ctx, (void *)map->tuples,
				 (size_t)map->capacity * sizeof(*map->tuples));
	if (map->hash != NULL)
		map->memory.free(map->memory.ctx, map->hash,
				 map->hash_capacity * sizeof(*map->hash));
	map->tuples = tuples;
	map->values = values;
	map->hash = hash;
	map->capacity = capacity;
	map->hash_capacity = hash_capacity;
	return ANN_OK;
}

uint64_t
ann_tuple_map_insert(struct ann_tuple_map *map, const void *tuple)
{
	assert(tuple != NULL &&
	       (map->free_head != 0 || map->count < map->capacity) &&
	       ann_tuple_map_find(map, tuple) == 0);
	uint32_t label;
	if (map->free_head != 0) {
		label = map->free_head;
		map->free_head = map->values[label - 1];
	} else {
		label = ++map->count;
	}
	++map->live_count;
	map->values[label - 1] = 0;
	map->tuples[label - 1] = tuple;
	ann_tuple_hash_insert(map->hash, map->hash_capacity, tuple, label);
	return label;
}

const void *
ann_tuple_map_get(const struct ann_tuple_map *map, uint64_t label)
{
	return label == 0 || label > map->count ? NULL :
	       map->tuples[label - 1];
}

uint64_t
ann_tuple_map_remove(struct ann_tuple_map *map, const void *tuple)
{
	if (map->hash_capacity == 0)
		return 0;
	size_t mask = map->hash_capacity - 1;
	size_t at = ann_tuple_hash(tuple) & mask;
	while (map->hash[at] != 0) {
		uint32_t label = map->hash[at];
		if (map->tuples[label - 1] == tuple) {
			map->tuples[label - 1] = NULL;
			map->values[label - 1] = map->free_head;
			map->free_head = label;
			--map->live_count;
			map->hash[at] = 0;
			at = (at + 1) & mask;
			while (map->hash[at] != 0) {
				uint32_t moved = map->hash[at];
				map->hash[at] = 0;
				ann_tuple_hash_insert(map->hash,
						      map->hash_capacity,
						      map->tuples[moved - 1],
						      moved);
				at = (at + 1) & mask;
			}
			return label;
		}
		at = (at + 1) & mask;
	}
	return 0;
}
