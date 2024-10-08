/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "arrow_record_batch.h"

#include <assert.h>
#include <stddef.h>
#include "core/arrow_ipc.h"

struct arrow_record_batch *
arrow_record_batch_unpack(const char **data, uint32_t len,
			  struct arrow_record_batch *arrow)
{
	if (arrow_ipc_decode(&arrow->array, &arrow->schema, *data,
			     *data + len) != 0)
		return NULL;
	assert(arrow->array.release != NULL);
	assert(arrow->schema.release != NULL);
	*data += len;
	return arrow;
}
