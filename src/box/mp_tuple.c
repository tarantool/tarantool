/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "box/mp_tuple.h"
#include "box/tuple.h"
#include "box/tuple_format_map.h"

#include "core/mp_extension_types.h"

#include "mpstream/mpstream.h"

/** Length of packed tuple. */
static uint32_t
tuple_len(struct tuple *tuple)
{
	return mp_sizeof_uint(tuple->format_id) + tuple_bsize(tuple);
}

uint32_t
mp_sizeof_tuple(struct tuple *tuple)
{
	return mp_sizeof_ext(tuple_len(tuple));
}

/** Pack a tuple value to a buffer. */
static char *
tuple_pack(char *data, struct tuple *tuple)
{
	data = mp_encode_uint(data, tuple->format_id);
	uint32_t bsize = tuple_bsize(tuple);
	memcpy(data, tuple_data(tuple), bsize);
	return data + bsize;
}

struct tuple *
mp_decode_tuple(const char **data, struct tuple_format_map *format_map)
{
	if (mp_typeof(**data) != MP_EXT)
		return NULL;
	int8_t type;
	const char *svp = *data;
	mp_decode_extl(data, &type);
	if (type != MP_TUPLE)
		goto error;
	struct tuple *tuple = tuple_unpack(data, format_map);
	if (tuple == NULL)
		goto error;
	return tuple;
error:
	*data = svp;
	return NULL;
}

struct tuple *
tuple_unpack(const char **data, struct tuple_format_map *format_map)
{
	uint32_t format_id = mp_decode_uint(data);
	const char *tuple_data = *data;
	mp_next(data);
	struct tuple_format *format =
		tuple_format_map_find(format_map, format_id);
	if (format == NULL)
		return NULL;
	return tuple_new(format, tuple_data, *data);
}

struct tuple *
tuple_unpack_without_format(const char **data)
{
	/* Ignore the format identfier.  */
	mp_decode_uint(data);
	const char *tuple_data = *data;
	mp_next(data);
	return tuple_new(tuple_format_runtime, tuple_data, *data);
}

char *
mp_encode_tuple(char *data, struct tuple *tuple)
{
	data = mp_encode_extl(data, MP_TUPLE, tuple_len(tuple));
	return tuple_pack(data, tuple);
}

void
tuple_to_mpstream_as_ext(struct tuple *tuple, struct mpstream *stream)
{
	uint32_t tuple_sz = mp_sizeof_tuple(tuple);
	char *data = mpstream_reserve(stream, tuple_sz);
	if (data == NULL)
		return;
	char *pos = mp_encode_tuple(data, tuple);
	mpstream_advance(stream, pos - data);
}

int
mp_validate_tuple(const char *data, uint32_t len)
{
	/*
	 * MsgPack extensions have length greater or equal than 1 by
	 * specification.
	 */
	assert(len > 0);
	const char *end = data + len;
	enum mp_type type = mp_typeof(*data);
	if (type != MP_UINT || mp_check_uint(data, end) > 0)
		return -1;

	mp_next(&data);
	if (data == end)
		return -1;
	if (mp_typeof(*data) != MP_ARRAY)
		return -1;
	return mp_check(&data, end) != 0 || data != end;
}
