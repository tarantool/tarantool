/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "mp_arrow.h"

#include "core/diag.h"
#include "core/arrow_ipc.h"

int
mp_validate_arrow(const char *data, uint32_t len)
{
	struct ArrowArray array;
	struct ArrowSchema schema;
	memset(&array, 0, sizeof(array));
	memset(&schema, 0, sizeof(schema));
	if (arrow_ipc_decode(&array, &schema, data, data + len) != 0)
		return 1;
	assert(array.release != NULL);
	assert(schema.release != NULL);
	array.release(&array);
	schema.release(&schema);
	return 0;
}

void
arrow_unpack(const char **data, uint32_t len, struct ArrowArray *array,
	     struct ArrowSchema *schema)
{
	/*
	 * The data must be validated in advance. The only reason to fail
	 * decoding here is out-of-memory.
	 */
	if (arrow_ipc_decode(array, schema, *data, *data + len) != 0) {
		diag_log();
		exit(EXIT_FAILURE);
	}
	*data += len;
}
