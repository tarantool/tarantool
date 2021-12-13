#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <stdint.h>
#include <stdio.h>
#include "tt_compression.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Calculate @a size of buffer enought to save compressed data from
 * data array from @a ttc structure. Return 0 if success, otherwise
 * return -1.
 */
int
mp_sizeof_for_compression(const struct tt_compression *ttc, uint32_t *size);

/**
 * Calculate @a size of buffer enought to save decompressed data from
 * compressed data array (with MP_EXT header). Return 0 if success,
 * otherwise return -1.
 */
int
mp_sizeof_for_decompression(const char **data, uint32_t *size);

/**
 * Encode @a ttc structure from @a data. Save compressed data in
 * data array in @a ttc structure.
 */
char *
mp_encode_compression(char *data, const struct tt_compression *ttc);

/**
 * Copy data from @adata into data array of @a ttc structure.
 * Checks that @ttc size is equal to data size and data is valid
 * msgpack.
 */
int
mp_set_data_for_compression(const char *data, uint32_t size,
                            struct tt_compression *ttc);

/**
 * Decode @a ttc structure from compressed msgpack field @a data. Save
 * decompressed data in data array in @a ttc structure.
 */
struct tt_compression *
mp_decode_compression(const char **data, struct tt_compression *ttc);

/**
 * Print compressed data string representation into a given buffer.
 */
int
mp_snprint_compression(char *buf, int size, const char **data, uint32_t len);

/**
 * Print compressed data string representation into a stream.
 */
int
mp_fprint_compression(FILE *file, const char **data, uint32_t len);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
