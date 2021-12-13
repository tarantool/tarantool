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

/**
 * Calculate the @a new_data_size that will take @a data array
 * with @a data_size after "zstd" compression with @a level.
 * Return 0 if success, otherwise return -1.
 */
int
zstd_compressed_data_size(const char *data, const uint32_t data_size,
                          int level, uint32_t *new_data_size);

/**
 * Compress @a data array with @a data_size size into @a new_data array
 * according to the "zstd" algorithm with @a level. Save @a new_data array
 * size in @a new_data_size. Return 0 if success, otherwise return -1.
 */
int
zstd_compress_data(const char *data, const uint32_t data_size,
                   char *new_data, uint32_t *new_data_size,
                   int level);

/**
 * Decompress @data array with size @a data_size into @a new_data array
 * according to "zstd" algorithm. Return 0 if success, otherwise return
 * -1.
 */
int
zstd_decompress_data(const char **data, const uint32_t data_size,
                     char *new_data);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
