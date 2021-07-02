#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <stdio.h>
#include "datetime.h"

#if defined(__cplusplus)
extern "C"
{
#endif /* defined(__cplusplus) */

/**
 * Unpack datetime data from MessagePack buffer.
 * @sa datetime_pack
 */
struct datetime *
datetime_unpack(const char **data, uint32_t len, struct datetime *date);

/**
 * Pack datetime data to MessagePack buffer.
 * @sa datetime_unpack
 */
char *
datetime_pack(char *data, const struct datetime *date);

/**
 * Calculate size of MessagePack buffer for datetime data.
 */
uint32_t
mp_sizeof_datetime(const struct datetime *date);

/**
 * Decode data from MessagePack buffer to datetime structure.
 */
struct datetime *
mp_decode_datetime(const char **data, struct datetime *date);

/**
 * Encode datetime structure to the MessagePack buffer.
 */
char *
mp_encode_datetime(char *data, const struct datetime *date);

/**
 * Print datetime's string representation into a given buffer.
 * @sa mp_snprint_decimal
 */
int
mp_snprint_datetime(char *buf, int size, const char **data, uint32_t len);

/**
 * Print datetime's string representation into a stream.
 * @sa mp_fprint_decimal
 */
int
mp_fprint_datetime(FILE *file, const char **data, uint32_t len);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
