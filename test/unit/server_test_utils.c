/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "trivia/util.h"

void *
fiber_alloc_data(size_t size)
{
	return xmalloc(size);
}

void
fiber_free_data(void *data, size_t size)
{
	(void)size;
	return free(data);
}
