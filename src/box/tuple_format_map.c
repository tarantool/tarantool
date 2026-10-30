/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "box/iproto_constants.h"
#include "box/tuple_format_map.h"
#include "box/tuple.h"
#include "diag.h"
#include "msgpuck/msgpuck.h"
#include "mpstream/mpstream.h"
#include "small/obuf.h"

/**
 * Look up @a map's cache for format with @a format_id.
 * Return the format or NULL if was not found.
 */
static struct tuple_format *
tuple_format_map_find_in_cache(struct tuple_format_map *map, uint16_t format_id)
{
	/*
	 * Perform lookup in reverse order for faster hit of the last recent
	 * used format. That seems to be the best strategy.
	 */
	for (ssize_t i = map->cache_last_index; i >= 0; i--)
		if (map->cache[i].key == format_id)
			return map->cache[i].val;
	if (map->hash_table != NULL) {
		for (ssize_t i = TUPLE_FORMAT_MAP_CACHE_SIZE - 1;
		     i > map->cache_last_index; i--)
			if (map->cache[i].key == format_id)
				return map->cache[i].val;
	}
	return NULL;
}

/**
 * Add a tuple format to the tuple format and reference the format.
 */
static void
tuple_format_map_add_format_impl(struct tuple_format_map *map,
				 uint16_t format_id,
				 struct tuple_format *format)
{
	struct tuple_format *in_cache =
		tuple_format_map_find_in_cache(map, format_id);
	if (in_cache != NULL) {
		assert(in_cache == format);
		return;
	}

	struct mh_i32ptr_node_t node = {
		.key = format_id,
		.val = format,
	};
	if (map->hash_table == NULL) {
		if (map->cache_last_index < TUPLE_FORMAT_MAP_CACHE_SIZE - 1) {
			tuple_format_ref(format);
			map->cache[++map->cache_last_index] = node;
			return;
		}
		map->hash_table = mh_i32ptr_new();
		for (size_t i = 0; i < TUPLE_FORMAT_MAP_CACHE_SIZE; ++i)
			mh_i32ptr_put(map->hash_table, &map->cache[i], NULL,
				      NULL);
	}
	struct mh_i32ptr_node_t old;
	struct mh_i32ptr_node_t *old_ptr = &old;
	mh_i32ptr_put(map->hash_table, &node, &old_ptr, NULL);
	if (old_ptr == NULL)
		tuple_format_ref(format);
	else
		assert(old.val == format);
	map->cache_last_index =
		(map->cache_last_index + 1) % TUPLE_FORMAT_MAP_CACHE_SIZE;
	map->cache[map->cache_last_index] = node;
}

void
tuple_format_map_create_empty(struct tuple_format_map *map)
{
	map->cache_last_index = -1;
	map->hash_table = NULL;
}

int
tuple_format_map_create_from_mp(struct tuple_format_map *map, const char *data)
{
	tuple_format_map_create_empty(map);
	if (mp_typeof(*data) != MP_MAP) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "format map");
		goto error;
	}
	uint32_t map_sz = mp_decode_map(&data);
	for (uint32_t i = 0; i < map_sz; ++i) {
		if (mp_typeof(*data) != MP_UINT) {
			diag_set(ClientError, ER_INVALID_MSGPACK, "format id");
			goto error;
		}
		uint16_t format_id = mp_decode_uint(&data);
		if (mp_typeof(*data) != MP_ARRAY) {
			diag_set(ClientError, ER_INVALID_MSGPACK,
				 "format array");
			goto error;
		}
		const char *format_data = data;
		mp_next(&data);
		size_t format_data_len = data - format_data;
		struct tuple_format *format =
			runtime_tuple_format_new(format_data, format_data_len,
						 /*names_only=*/true);
		if (format == NULL)
			goto error;
		tuple_format_map_add_format_impl(map, format_id, format);
	}
	return 0;
error:
	tuple_format_map_destroy(map);
	return -1;
}

void
tuple_format_map_destroy(struct tuple_format_map *map)
{
	if (map->hash_table == NULL) {
		for (ssize_t i = 0; i <= map->cache_last_index; ++i)
			tuple_format_unref(map->cache[i].val);
	} else {
		struct mh_i32ptr_t *h = map->hash_table;
		mh_int_t k;
		mh_foreach(h, k)
			tuple_format_unref(mh_i32ptr_node(h, k)->val);
		mh_i32ptr_delete(map->hash_table);
	}
	TRASH(map);
}

void
tuple_format_map_move(struct tuple_format_map *dst,
		      struct tuple_format_map *src)
{
	memcpy(dst, src, sizeof(*dst));
	src->cache_last_index = -1;
	src->hash_table = NULL;
}

void
tuple_format_map_add_format(struct tuple_format_map *map, uint16_t format_id)
{
	struct tuple_format *format = tuple_format_by_id(format_id);
	tuple_format_map_add_format_impl(map, format_id, format);
}

void
tuple_format_map_to_mpstream(struct tuple_format_map *map,
			     struct mpstream *stream)
{
	if (map->hash_table == NULL) {
		mpstream_encode_map(stream, map->cache_last_index + 1);
		for (ssize_t i = 0; i <= map->cache_last_index; ++i)
			tuple_format_to_mpstream(map->cache[i].val, stream);
	} else {
		mpstream_encode_map(stream, mh_size(map->hash_table));
		struct mh_i32ptr_t *h = map->hash_table;
		mh_int_t k;
		mh_foreach(h, k)
			tuple_format_to_mpstream(mh_i32ptr_node(h, k)->val,
						 stream);
	}
}

static void
mpstream_error(void *is_err)
{
	*(bool *)is_err = true;
}

int
tuple_format_map_to_iproto_obuf(struct tuple_format_map *map,
				struct obuf *obuf)
{
	struct mpstream stream;
	bool is_error = false;
	mpstream_init(&stream, obuf, obuf_reserve_cb, obuf_alloc_cb,
		      mpstream_error, &is_error);
	mpstream_encode_uint(&stream, IPROTO_TUPLE_FORMATS);
	tuple_format_map_to_mpstream(map, &stream);
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			 "mpstream_flush", "stream");
		return -1;
	}
	return 0;
}

struct tuple_format *
tuple_format_map_find(struct tuple_format_map *map, uint16_t format_id)
{
	struct tuple_format *in_cache =
		tuple_format_map_find_in_cache(map, format_id);
	if (in_cache != NULL || map->hash_table == NULL)
		return in_cache;

	mh_int_t k = mh_i32ptr_find(map->hash_table, format_id, NULL);
	if (k == mh_end(map->hash_table))
		return NULL;

	map->cache_last_index =
		(map->cache_last_index + 1) % TUPLE_FORMAT_MAP_CACHE_SIZE;
	map->cache[map->cache_last_index] = *mh_i32ptr_node(map->hash_table, k);
	return map->cache[map->cache_last_index].val;
}
