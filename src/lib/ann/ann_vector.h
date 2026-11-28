/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "ann_backend.h"

#if defined(__cplusplus)
extern "C" {
#endif

/** Immutable coordinates shared by tuple versions and backend generations. */
struct ann_vector;

/** Copy native float32 coordinates, returning one owned reference. */
struct ann_vector *
ann_vector_new(const struct ann_memory *memory, const float *data,
	       uint32_t dimension);

/** Retain coordinates without allocation while already holding a reference. */
void
ann_vector_ref(struct ann_vector *vector);

/** Release a reference, freeing coordinates or their external owner on zero. */
void
ann_vector_unref(struct ann_vector *vector);

/** Access coordinates while holding a reference. */
const void *
ann_vector_data(const struct ann_vector *vector);

/** Return the component count. */
uint32_t
ann_vector_dimension(const struct ann_vector *vector);

/**
 * Borrow packed little-endian float32 coordinates, with one owned reference.
 * On success release(owner) runs after the last reference; on failure the
 * caller retains ownership and release is not called.
 */
struct ann_vector *
ann_vector_borrow(const struct ann_memory *memory, const void *data,
		  uint32_t dimension, void *owner, void (*release)(void *));

/** Whether coordinates are borrowed packed bytes rather than native f32. */
bool
ann_vector_is_borrowed(const struct ann_vector *vector);

/** External owner retained by borrowed coordinates, otherwise NULL. */
void *
ann_vector_owner(const struct ann_vector *vector);

/** Bytes per coordinate owner, excluding optional owned payload. */
size_t
ann_vector_overhead(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif
