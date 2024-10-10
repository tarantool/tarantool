/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "mp_arrow.h"

#include <string.h>
#include "core/arrow_record_batch.h"

int
mp_validate_arrow(const char *data, uint32_t len)
{
	struct arrow_record_batch arrow, *ret;
	memset(&arrow, 0, sizeof(arrow));
	ret = arrow_record_batch_unpack(&data, len, &arrow);
	if (ret == NULL)
		return 1;
	arrow.array.release(&arrow.array);
	arrow.schema.release(&arrow.schema);
	return 0;
}
