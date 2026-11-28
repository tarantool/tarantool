/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ann_vector.h"

#include <assert.h>
#include <string.h>

/** Coordinate owner independent of any ANN backend or graph generation. */
struct ann_vector {
	/** Allocator of the owner and optional copied payload. */
	struct ann_memory memory;
	/** References in the storage layer and backend generations. */
	uint32_t refs;
	/** Number of referenced coordinates. */
	uint32_t dimension;
	/** Native owned coordinates or borrowed little-endian bytes. */
	const void *data;
	/** External lifetime owner, or NULL for copied coordinates. */
	void *owner;
	/** Release the external owner, or NULL for copied coordinates. */
	void (*release)(void *owner);
};

struct ann_vector *
ann_vector_new(const struct ann_memory *memory, const float *data,
	       uint32_t dimension)
{
	if (memory == NULL || memory->alloc == NULL || memory->free == NULL ||
	    data == NULL || dimension == 0)
		return NULL;
	size_t bytes = (size_t)dimension * sizeof(float);
	if (bytes / sizeof(float) != dimension ||
	    bytes > SIZE_MAX - sizeof(struct ann_vector))
		return NULL;
	struct ann_vector *vector =
		memory->alloc(memory->ctx, sizeof(*vector) + bytes);
	if (vector == NULL)
		return NULL;
	vector->memory = *memory;
	vector->refs = 1;
	vector->dimension = dimension;
	vector->data = vector + 1;
	vector->owner = NULL;
	vector->release = NULL;
	memcpy(vector + 1, data, bytes);
	return vector;
}

void
ann_vector_ref(struct ann_vector *vector)
{
	assert(vector->refs > 0 && vector->refs < UINT32_MAX);
	++vector->refs;
}

void
ann_vector_unref(struct ann_vector *vector)
{
	if (vector == NULL)
		return;
	assert(vector->refs > 0);
	if (--vector->refs != 0)
		return;
	struct ann_memory memory = vector->memory;
	size_t size = sizeof(*vector);
	if (vector->release != NULL)
		vector->release(vector->owner);
	else
		size += (size_t)vector->dimension * sizeof(float);
	memory.free(memory.ctx, vector, size);
}

const void *
ann_vector_data(const struct ann_vector *vector)
{
	return vector->data;
}

uint32_t
ann_vector_dimension(const struct ann_vector *vector)
{
	return vector->dimension;
}

struct ann_vector *
ann_vector_borrow(const struct ann_memory *memory, const void *data,
		  uint32_t dimension, void *owner, void (*release)(void *))
{
	if (memory == NULL || memory->alloc == NULL || memory->free == NULL ||
	    data == NULL || release == NULL || dimension == 0)
		return NULL;
	struct ann_vector *vector =
		memory->alloc(memory->ctx, sizeof(*vector));
	if (vector == NULL)
		return NULL;
	vector->memory = *memory;
	vector->refs = 1;
	vector->dimension = dimension;
	vector->data = data;
	vector->owner = owner;
	vector->release = release;
	return vector;
}

bool
ann_vector_is_borrowed(const struct ann_vector *vector)
{
	return vector->release != NULL;
}

size_t
ann_vector_overhead(void)
{
	return sizeof(struct ann_vector);
}

void *
ann_vector_owner(const struct ann_vector *vector)
{
	return vector->owner;
}
