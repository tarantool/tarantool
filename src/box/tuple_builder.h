/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "salad/stailq.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct region;

/**
 * A builder that helps to construct a tuple by concatenating chunks of data.
 * A chunk represents one or more tuple fields (MsgPack objects).
 *
 * First, chunks are added to a builder object. The builder doesn't allocate
 * any memory for the MsgPack, and doesn't copy it, only pointers to the start
 * and to the end of the data are preserved.
 *
 * Once all chunks have been added, the builder can be used to encode them into
 * the final MsgPack array.
 */
struct tuple_builder {
	/** List of chunks, linked by `tuple_chunk::in_builder`. */
	struct stailq chunks;
	/**
	 * Number of tuple fields. It can be greater than the number of
	 * elements in the list of chunks.
	 */
	uint32_t field_count;
	/** Total size of memory required to encode chunks from the list. */
	size_t size;
	/** The region used to perform memory allocation. */
	struct region *region;
};

/**
 * Initialize the builder. The region argument is saved to perform memory
 * allocation for internal structures and for the resulting MsgPack array.
 */
void
tuple_builder_new(struct tuple_builder *builder, struct region *region);

/**
 * Add a NULL tuple field to the builder.
 */
void
tuple_builder_add_nil(struct tuple_builder *builder);

/**
 * Add a chunk of data with `field_count` tuple fields to the builder.
 * If the chunk is adjacent to the previous one, only single pointer is updated,
 * otherwise a new list element is allocated on builder->region and added to
 * builder->chunks.
 */
void
tuple_builder_add(struct tuple_builder *builder, const char *data,
		  size_t data_size, uint32_t field_count);

/**
 * Encode tuple fields added to the builder into the new MsgPack array.
 * The buffer is allocated on builder->region, and the address is returned
 * in data and data_end.
 */
void
tuple_builder_finalize(struct tuple_builder *builder, const char **data,
		       const char **data_end);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
