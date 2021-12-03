/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "msgpuck_compression.h"
#include "space.h"
#include "zstd.h"
#include "fiber.h"

#include <small/region.h>

static int
msgpuck_compress_field(const char *data, const char *data_end,
		       char **new_data, char **new_data_end,
		       enum compression_type type)
{
	size_t new_data_max_size, new_data_real_size;
	size_t data_size = data_end - data;
	switch (type) {
	case COMPRESSION_TYPE_ZSTD5:
		new_data_max_size = ZSTD_compressBound(data_size);
		if (ZSTD_isError(new_data_max_size))
			return -1;
		*new_data = region_alloc(&fiber()->gc, new_data_max_size);
		if (*new_data == NULL)
			return -1;
		new_data_real_size = ZSTD_compress(*new_data, new_data_max_size,
						   data, data_size, 1);
		if (ZSTD_isError(new_data_real_size))
			return -1;
		if (new_data_real_size > data_size)
			goto do_not_compress;
		*new_data_end = *new_data + new_data_real_size;
		break;
	case COMPRESSION_TYPE_LZ4:
	/** TODO */
	default:
		goto do_not_compress;
	}
	return 0;

do_not_compress:
	*new_data = (char *)data;
	*new_data_end = (char *)data_end;
	return 0;
}

static int
msgpuck_decompress_field(const char *data, const char *data_end,
			 char **new_data, char **new_data_end,
			 enum compression_type type)
{
	size_t new_data_max_size, new_data_real_size;
	size_t data_size = data_end - data;
	switch (type) {
	case COMPRESSION_TYPE_ZSTD5:
		new_data_max_size = ZSTD_getFrameContentSize(data, data_size);
		if (ZSTD_isError(new_data_max_size))
			return -1;
		*new_data = region_alloc(&fiber()->gc, new_data_max_size);
		if (*new_data == NULL)
			return -1;
		new_data_real_size =
			ZSTD_decompress(*new_data, new_data_max_size,
					data, data_size);
		if (ZSTD_isError(new_data_real_size))
			return -1;
		*new_data_end = *new_data + new_data_real_size;
		break;
	case COMPRESSION_TYPE_LZ4:
	/** TODO */
	default:
		goto do_not_decompress;
	}
	return 0;

do_not_decompress:
	*new_data = (char *)data;
	*new_data_end = (char *)data_end;
	return 0;
}

int
msgpuck_compress_fields(struct space *space, const char *data,
                	const char *data_end, char **new_data,
                	char **new_data_end)
{
	struct space_def *def = space->def;
	const char *current_field = data;
	if (mp_typeof(*current_field) != MP_ARRAY)
		return -1;
	uint32_t size = mp_decode_array(&current_field);
	if (size < def->field_count)
		return -1;
	*new_data = region_alloc(&fiber()->gc, data_end - data);
	if (*new_data == NULL)
		return -1;
	*new_data_end = *new_data;
	memcpy(*new_data_end, data, current_field - data);
	*new_data_end += current_field - data;
	for (uint32_t i = 0; i < def->field_count; i++) {
		enum compression_type type = def->fields[i].compression_type;
		const char *next_field = current_field;
		mp_next(&next_field);
		char *cfield, *cfield_end;
		int rc = msgpuck_compress_field(current_field, next_field,
						&cfield, &cfield_end,
						type);
		if (rc != 0)
			return -1;
		if (type == COMPRESSION_TYPE_NONE) {
			memcpy(*new_data_end, cfield, cfield_end - cfield);
			*new_data_end += cfield_end - cfield;
		} else {
			*new_data_end = mp_encode_bin(*new_data_end, cfield,
						      cfield_end - cfield);
		}
		current_field = next_field;
	}
	return 0;
}

int
msgpuck_decompress_fields(struct space *space, const char *data,
                          const char *data_end, char **new_data,
                          size_t new_data_size)
{
	(void)data_end;
	struct space_def *def = space->def;
	const char *current_field = data;
	if (mp_typeof(*current_field) != MP_ARRAY)
		return -1;
	uint32_t size = mp_decode_array(&current_field);
	if (size < def->field_count)
		return -1;
	*new_data = region_alloc(&fiber()->gc, new_data_size);
	if (*new_data == NULL)
		return -1;
	char *new_data_end = *new_data;
	memcpy(new_data_end, data, current_field - data);
	new_data_end += current_field - data;
	for (uint32_t i = 0; i < def->field_count; i++) {
		enum compression_type type = def->fields[i].compression_type;
		const char *next_field = current_field;
		mp_next(&next_field);
		if (type == COMPRESSION_TYPE_NONE) {
			if ((long)new_data_size - (new_data_end - *new_data) <
			    next_field - current_field)
				return -1;
			memcpy(new_data_end, current_field,
			       next_field - current_field);
			new_data_end += next_field - current_field;
		} else {
			uint32_t len;
			const char *tmp = current_field;
			const char *bin = mp_decode_bin(&tmp, &len);
			char *dcfield, *dcfield_end;
			int rc = msgpuck_decompress_field(bin, bin + len,
							  &dcfield, &dcfield_end,
							  type);
			if (rc != 0)
				return -1;
			if ((long)new_data_size - (new_data_end - *new_data) <
			    dcfield_end - dcfield)
				return -1;
			memcpy(new_data_end, dcfield, dcfield_end - dcfield);
			new_data_end += dcfield_end - dcfield;
		}
		current_field = next_field;
	}
	return 0;
}
