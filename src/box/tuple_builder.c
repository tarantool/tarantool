/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <stddef.h>
#include <stdint.h>
#include "tuple.h"
#include "msgpuck.h"
#include "small/region.h"
#include "salad/stailq.h"
#include "tuple_builder.h"

/**
 * A chunk of data with tuple fields.
 */
struct tuple_chunk {
	/** Start of the data. */
	const char *data;
	/** End of the data. */
	const char *data_end;
	/** Number of NULL fields. If > 0 then data/data_end are not used. */
	uint32_t null_count;
	/** Link in `tuple_builder::chunks`. */
	struct stailq_entry in_builder;
};

void
tuple_builder_new(struct tuple_builder *builder, struct region *region)
{
	stailq_create(&builder->chunks);
	builder->field_count = 0;
	builder->size = 0;
	builder->region = region;
}

void
tuple_builder_add_nil(struct tuple_builder *builder)
{
	builder->field_count++;
	builder->size += mp_sizeof_nil();

	struct tuple_chunk *chunk;
	if (!stailq_empty(&builder->chunks)) {
		chunk = stailq_last_entry(&builder->chunks, struct tuple_chunk,
					  in_builder);
		/* Avoid unnecessary allocation. */
		if (chunk->null_count > 0) {
			chunk->null_count++;
			return;
		}
	}
	chunk = xregion_alloc_object(builder->region, typeof(*chunk));
	chunk->data = NULL;
	chunk->data_end = NULL;
	chunk->null_count = 1;
	stailq_add_tail_entry(&builder->chunks, chunk, in_builder);
}

void
tuple_builder_add(struct tuple_builder *builder, const char *data,
		  size_t data_size, uint32_t field_count)
{
	const char *data_end = data + data_size;
	builder->field_count += field_count;
	builder->size += data_size;

	struct tuple_chunk *chunk;
	if (!stailq_empty(&builder->chunks)) {
		chunk = stailq_last_entry(&builder->chunks, struct tuple_chunk,
					  in_builder);
		/* Avoid unnecessary allocation. */
		if (chunk->data_end == data) {
			chunk->data_end = data_end;
			return;
		}
	}
	chunk = xregion_alloc_object(builder->region, typeof(*chunk));
	chunk->data = data;
	chunk->data_end = data_end;
	chunk->null_count = 0;
	stailq_add_tail_entry(&builder->chunks, chunk, in_builder);
}

void
tuple_builder_finalize(struct tuple_builder *builder, const char **data,
		       const char **data_end)
{
	size_t data_size = builder->size +
			   mp_sizeof_array(builder->field_count);
	char *buf = xregion_alloc(builder->region, data_size);
	*data = buf;
	*data_end = buf + data_size;
	buf = mp_encode_array(buf, builder->field_count);

	struct tuple_chunk *chunk;
	stailq_foreach_entry(chunk, &builder->chunks, in_builder) {
		if (chunk->null_count == 0) {
			uint32_t size = chunk->data_end - chunk->data;
			memcpy(buf, chunk->data, size);
			buf += size;
		} else {
			for (uint32_t i = 0; i < chunk->null_count; i++)
				buf = mp_encode_nil(buf);
		}
	}
	assert(buf == *data_end);
	mp_tuple_assert(*data, *data_end);
}
