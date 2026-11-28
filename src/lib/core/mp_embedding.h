/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>
#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum { EMBEDDING_MAX_DIMENSION = 4096 };

/** Convert a finite in-range double to float32 using ties to even. */
int
embedding_f32_from_double(double value, float *out);

/** Read an unaligned little-endian float32 coordinate. */
float
embedding_f32le_load(const void *data, uint32_t index);

/** Write a little-endian float32 coordinate. */
void
embedding_f32le_store(void *data, uint32_t index, float value);

/** Validate an MP_EMBEDDING payload independently of an index metric. */
int
mp_validate_embedding(const char *data, uint32_t len);

/** Return the encoded size including the extension header. */
uint32_t
mp_sizeof_embedding(uint32_t dimension);

/** Encode validated little-endian coordinates as MP_EMBEDDING. */
char *
mp_encode_embedding(char *data, const char *coordinates, uint32_t dimension);

/** Decode an extension, returning its coordinates or NULL on invalid data. */
const char *
mp_decode_embedding(const char **data, uint32_t *dimension);

/** Print a payload and advance data by len bytes. */
int
mp_snprint_embedding(char *buf, int size, const char **data, uint32_t len);

/** Print a payload to a stream and advance data by len bytes. */
int
mp_fprint_embedding(FILE *file, const char **data, uint32_t len);

#if defined(__cplusplus)
}
#endif
