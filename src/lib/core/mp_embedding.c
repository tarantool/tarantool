/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "mp_embedding.h"

#include <assert.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include "msgpuck.h"
#include "mp_extension_types.h"
#include "trivia/util.h"

int
embedding_f32_from_double(double value, float *out)
{
	if (out == NULL || !isfinite(value) || fabs(value) > FLT_MAX)
		return -1;
	int old_round = fegetround();
	if (old_round < 0)
		return -1;
	if (old_round != FE_TONEAREST && fesetround(FE_TONEAREST) != 0)
		return -1;
	/* Keep the cast inside the temporary rounding mode. */
	__asm__ volatile("" : "+m"(value) : : "memory");
	float rounded = (float)value;
	__asm__ volatile("" : "+m"(rounded) : : "memory");
	if (old_round != FE_TONEAREST && fesetround(old_round) != 0)
		return -1;
	*out = rounded;
	return 0;
}

float
embedding_f32le_load(const void *data, uint32_t index)
{
	const unsigned char *p = (const unsigned char *)data + index * 4;
	uint32_t bits = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
		(uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

void
embedding_f32le_store(void *data, uint32_t index, float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	unsigned char *p = (unsigned char *)data + index * 4;
	for (uint32_t i = 0; i < 4; ++i)
		p[i] = (unsigned char)(bits >> (8 * i));
}

int
mp_validate_embedding(const char *data, uint32_t len)
{
	if (data == NULL || len == 0 || len % 4 != 0 ||
	    len / 4 > EMBEDDING_MAX_DIMENSION)
		return -1;
	for (uint32_t i = 0; i < len / 4; ++i) {
		if (!isfinite(embedding_f32le_load(data, i)))
			return -1;
	}
	return 0;
}

uint32_t
mp_sizeof_embedding(uint32_t dimension)
{
	return mp_sizeof_ext(dimension * 4);
}

char *
mp_encode_embedding(char *data, const char *coordinates, uint32_t dimension)
{
	assert(mp_validate_embedding(coordinates, dimension * 4) == 0);
	data = mp_encode_extl(data, MP_EMBEDDING, dimension * 4);
	memcpy(data, coordinates, dimension * 4);
	return data + dimension * 4;
}

const char *
mp_decode_embedding(const char **data, uint32_t *dimension)
{
	if (mp_typeof(**data) != MP_EXT)
		return NULL;
	const char *p = *data;
	int8_t type;
	uint32_t len = mp_decode_extl(&p, &type);
	if (type != MP_EMBEDDING || mp_validate_embedding(p, len) != 0)
		return NULL;
	*dimension = len / 4;
	*data = p + len;
	return p;
}

int
mp_snprint_embedding(char *buf, int size, const char **data, uint32_t len)
{
	int total = 0;
	SNPRINT(total, snprintf, buf, size, "embedding([");
	for (uint32_t i = 0; i < len / 4; ++i) {
		SNPRINT(total, snprintf, buf, size, "%s%.9g",
			i == 0 ? "" : ", ",
			(double)embedding_f32le_load(*data, i));
	}
	SNPRINT(total, snprintf, buf, size, "])");
	*data += len;
	return total;
}

int
mp_fprint_embedding(FILE *file, const char **data, uint32_t len)
{
	int total = fprintf(file, "embedding([");
	if (total < 0)
		return -1;
	for (uint32_t i = 0; i < len / 4; ++i) {
		int n = fprintf(file, "%s%.9g", i == 0 ? "" : ", ",
				(double)embedding_f32le_load(*data, i));
		if (n < 0)
			return -1;
		total += n;
	}
	int n = fprintf(file, "])");
	if (n < 0)
		return -1;
	*data += len;
	return total + n;
}
