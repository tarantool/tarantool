#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum compression_type {
        COMPRESSION_TYPE_NONE = 0,
        COMPRESSION_TYPE_ZSTD5,
        compression_type_MAX
};

/**
 * Structure describing data for compression or
 * decompression.
 */
struct tt_compression {
        /** Type of compression. */
        enum compression_type type;
        /**
         * Size of data for compression in case when
         * this structure used for data compression or
         * expected size of decompressed data in case
         * when this struct used for decompression.
         */
        uint32_t size;
        /**
         * Array of uncompressed data. In case of compression,
         * data that is subject to compression is stored here.
         * In case of decompression, this is where the data is
         * saved after unpacking.
         */
        char data[0];
};

/**
 * Allocate and return new `tt_compression` structure,
 * with appropriate type and size.
 */
struct tt_compression *
tt_compression_new(uint32_t size, enum compression_type type);

/**
 * Free @a ttc and all associated resources.
 */
void
tt_compression_delete(struct tt_compression *ttc);

/**
 * Calculate @a size of data from @a ttc, that they
 * will occupy after compression. Return 0 if success
 * otherwise return -1.
 */
int
tt_compression_compressed_data_size(const struct tt_compression *ttc,
                                    uint32_t *size);

/**
 * Compress data from @a ttc structure and save new compressed
 * data in @a data and size in @a size. Return 0 if success,
 * otherwise return -1. It is caller responsibility to ensure
 * that data has enought bytes.
 */
int
tt_compression_compress_data(const struct tt_compression *ttc, char *data,
                             uint32_t *size);

/**
 * Decompress @a data into data array in @a ttc structure. Return
 * 0 if success otherwise return -1. It is caller responsibility
 * to ensure that data array in @a ttc structure has enought bytes.
 */
int
tt_compression_decompress_data(const char **data, uint32_t size,
                               struct tt_compression *ttc);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
