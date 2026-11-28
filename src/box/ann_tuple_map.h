/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "../lib/ann/ann_backend.h"

#if defined(__cplusplus)
extern "C" {
#endif

/** Recyclable label and tuple-address binding for one memtx VECTOR index. */
struct ann_tuple_map {
	/** Allocation owner shared with the ANN backend. */
	struct ann_memory memory;
	/** Tuple addresses indexed by label minus one. */
	const void **tuples;
	/** Caller values for live slots; free-list links for vacant slots. */
	uintptr_t *values;
	/** Head of the vacant-slot list, or zero. */
	uint32_t free_head;
	/** Number of currently occupied bindings. */
	uint32_t live_count;
	/** Open-addressed table of labels indexed by tuple address. */
	uint32_t *hash;
	/** High-water mark of assigned slots. */
	uint32_t count;
	/** Allocated tuple-address slots. */
	uint32_t capacity;
	/** Power-of-two hash table size. */
	size_t hash_capacity;
};

/** Initialize a map without allocating. */
void
ann_tuple_map_create(struct ann_tuple_map *map,
		     const struct ann_memory *memory);

/** Release arrays, leaving tuple references to the caller. */
void
ann_tuple_map_destroy(struct ann_tuple_map *map);

/** Find a tuple address, returning zero when absent. */
uint64_t
ann_tuple_map_find(const struct ann_tuple_map *map, const void *tuple);

/** Reserve space for one new binding before graph mutation. */
enum ann_status
ann_tuple_map_prepare(struct ann_tuple_map *map);

/** Publish a new tuple address without allocation and return its label. */
uint64_t
ann_tuple_map_insert(struct ann_tuple_map *map, const void *tuple);

/** Resolve an assigned label to its retained tuple address. */
const void *
ann_tuple_map_get(const struct ann_tuple_map *map, uint64_t label);

/** Release a binding after version GC and recycle its slot. */
uint64_t
ann_tuple_map_remove(struct ann_tuple_map *map, const void *tuple);

#if defined(__cplusplus)
} /* extern "C" */
#endif
