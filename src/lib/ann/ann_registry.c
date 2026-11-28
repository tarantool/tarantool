/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ann_backend.h"

#include <string.h>

extern const struct ann_backend_ops ann_flat_ops;

const struct ann_backend_ops *
ann_backend_find(const char *name)
{
	if (name != NULL && strcmp(name, ann_flat_ops.name) == 0)
		return &ann_flat_ops;
	return NULL;
}
