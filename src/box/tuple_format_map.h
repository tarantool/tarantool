/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "core/assoc.h"

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#define TUPLE_FORMAT_MAP_CACHE_SIZE 4

struct tuple_format;
struct mpstream;
struct obuf;

/**
 * Mapping of format identifiers (local or coming from an external source, e.g.,
 * IPROTO) to tuple formats.
 */
struct tuple_format_map {
	/**
	 * FIFO cache of tuple formats for primary lookup.
	 */
	struct mh_i32ptr_node_t cache[TUPLE_FORMAT_MAP_CACHE_SIZE];
	/**
	 * Index of last cached element.
	 */
	ssize_t cache_last_index;
	/**
	 * Hash table of tuple formats. Used only if the map contains more
	 * than `TUPLE_FORMAT_MAP_CACHE_SIZE` formats.
	 */
	struct mh_i32ptr_t *hash_table;
};

/**
 * Create an empty tuple format map.
 */
void
tuple_format_map_create_empty(struct tuple_format_map *map);

/**
 * Create a tuple format map from MsgPack data. The data is expected to
 * contain an array of serialized tuple formats.
 *
 * Returns 0 on success, otherwise -1, diag is set.
 */
int
tuple_format_map_create_from_mp(struct tuple_format_map *map, const char *data);

/**
 * Destroy the tuple format map and dereference all the contained tuple formats.
 */
void
tuple_format_map_destroy(struct tuple_format_map *map);

static inline bool
tuple_format_map_is_empty(struct tuple_format_map *map)
{
	return map->cache_last_index == -1;
}

/**
 * Add a local tuple format to the tuple format map and reference it.
 */
void
tuple_format_map_add_format(struct tuple_format_map *map, uint16_t format_id);

/**
 * Serialize a tuple format map to a MsgPack stream.
 */
void
tuple_format_map_to_mpstream(struct tuple_format_map *map,
			     struct mpstream *stream);

/**
 * Find a format in the tuple format map.
 *
 * Returns the corresponding format or NULL if a format was not found.
 */
struct tuple_format *
tuple_format_map_find(struct tuple_format_map *map, uint16_t format_id);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
