/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "tt_compression.h"
#include "trivia/util.h"
#include "compression.h"
#include <trivia/util.h>

struct tt_compression *
tt_compression_new(uint32_t size, enum compression_type type)
{
	struct tt_compression *ttc =
		xmalloc(sizeof(struct tt_compression) + size);
	ttc->type = type;
	ttc->size = size;
	return ttc;
}

void
tt_compression_delete(struct tt_compression *ttc)
{
	TRASH(ttc);
	free(ttc);
}

int
tt_compression_compressed_data_size(const struct tt_compression *ttc,
				    uint32_t *size)
{
	switch (ttc->type) {
	case COMPRESSION_TYPE_NONE:
		*size = ttc->size;
		return 0;
	case COMPRESSION_TYPE_ZSTD5:
		return zstd_compressed_data_size(ttc->data, ttc->size, 5, size);
	default:
		;
	}
	return -1;
}

int
tt_compression_compress_data(const struct tt_compression *ttc, char *data,
			     uint32_t *size)
{
	switch (ttc->type) {
	case COMPRESSION_TYPE_NONE:
		*size = ttc->size;
		memcpy(data, ttc->data, *size);
		return 0;
	case COMPRESSION_TYPE_ZSTD5:
		return zstd_compress_data(ttc->data, ttc->size, data, size, 5);
	default:
		;
	}
	return -1;
}

int
tt_compression_decompress_data(const char **data, uint32_t size,
                               struct tt_compression *ttc)
{
	switch (ttc->type) {
	case COMPRESSION_TYPE_NONE:
		memcpy(ttc->data, *data, size);
		*data += size;
		return 0;
	case COMPRESSION_TYPE_ZSTD5:
		return zstd_decompress_data(data, size, ttc->data);
	default:
		;
	}
	return -1;
}
