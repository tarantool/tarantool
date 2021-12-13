/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "compression.h"
#include "fiber.h"
#include "small/region.h"
#include <zstd.h>

int
zstd_compressed_data_size(const char *data, const uint32_t data_size,
                          int level, uint32_t *new_data_size)
{
        size_t max_size = ZSTD_compressBound(data_size);
        if (max_size > UINT32_MAX)
                return -1;
        uint32_t used = region_used(&fiber()->gc);
        void *tmp = region_alloc(&fiber()->gc, max_size);
        if (tmp == NULL)
                return -1;
        size_t real_size = ZSTD_compress(tmp, max_size, data,
                                         data_size, level);
        region_truncate(&fiber()->gc, used);
        if (ZSTD_isError(real_size) || real_size > UINT32_MAX)
                return -1;
        *new_data_size = real_size;
        return 0;
}

int
zstd_compress_data(const char *data, const uint32_t data_size,
                   char *new_data, uint32_t *new_data_size,
                   int level)
{
        size_t max_size = ZSTD_compressBound(data_size);
        if (max_size > UINT32_MAX)
                return -1;
        uint32_t used = region_used(&fiber()->gc);
        void *tmp = region_alloc(&fiber()->gc, max_size);
        if (tmp == NULL)
                return -1;
        size_t real_size = ZSTD_compress(tmp, max_size, data,
                                         data_size, level);
        if (ZSTD_isError(real_size) || real_size > UINT32_MAX) {
                region_truncate(&fiber()->gc, used);
                return -1;
        }
        *new_data_size = real_size;
        memcpy(new_data, tmp, *new_data_size);
        region_truncate(&fiber()->gc, used);
        return 0;
}

int
zstd_decompress_data(const char **data, const uint32_t data_size,
                     char *new_data)
{
        size_t max_size = ZSTD_getFrameContentSize(*data, data_size);
        if (ZSTD_isError(max_size) || max_size > UINT32_MAX)
                return -1;
        uint32_t used = region_used(&fiber()->gc);
        void *tmp = region_alloc(&fiber()->gc, max_size);
        if (tmp == NULL)
                return -1;
        size_t real_size = ZSTD_decompress(tmp, max_size,
                                           *data, data_size);
        if (ZSTD_isError(real_size) || real_size > UINT32_MAX) {
                region_truncate(&fiber()->gc, used);
                return -1;
        }
        memcpy(new_data, tmp, real_size);
        region_truncate(&fiber()->gc, used);
        *data += data_size;
        return 0;
}
