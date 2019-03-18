#ifndef TARANTOOL_BOX_FIELD_MAP_H_INCLUDED
#define TARANTOOL_BOX_FIELD_MAP_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <assert.h>
#include <stdint.h>

struct region;

/**
 * A field map is a special area is reserved before tuple's
 * MessagePack data. It is a sequence of the 32-bit unsigned
 * offsets of tuple's indexed fields.
 *
 * These slots are numbered with negative indices called
 * offset_slot(s) starting with -1 (this is necessary to organize
 * the inheritance of tuples). Allocation and assignment of
 * offset_slot(s) is performed on tuple_format creation on index
 * create or alter (see tuple_format_create()).
 *
 *              4b          4b       MessagePack data.
 *           +------+----+------+---------------------------+
 *    tuple: | offN | .. | off1 | header ..|key1|..|keyN|.. |
 *           +--+---+----+--+---+---------------------------+
 *           |     ...    |                 ^       ^
 *           |            +-----------------+       |
 *           +--------------------------------------+
 *
 * This field_map_builder class is used for tuple field_map
 * construction. It encapsulates field_map build logic and size
 * estimation implementation-specific details.
 *
 * Each field offset is a positive number, except the case when
 * a field is not in the tuple. In this case offset is 0.
 */
struct field_map_builder {
	/**
	 * The pointer to the end of field_map allocation.
	 * Its elements are accessible by negative indexes
	 * that coinciding with offset_slot(s).
	 */
	uint32_t *slots;
	/**
	 * The count of slots in field_map_builder::slots
	 * allocation.
	 */
	uint32_t slot_count;
};

/**
 * Get offset of the field in tuple data MessagePack using
 * tuple's field_map and required field's offset_slot.
 *
 * When a field is not in the data tuple, its offset is 0.
 */
static inline uint32_t
field_map_get_offset(const uint32_t *field_map, int32_t offset_slot)
{
	return field_map[offset_slot];
}

/**
 * Initialize field_map_builder.
 *
 * The field_map_size argument is a size of the minimal field_map
 * allocation where each indexed field has own offset slot.
 *
 * Routine uses region to perform memory allocation for internal
 * structures.
 *
 * Returns 0 on success. In case of memory allocation error sets
 * diag message and returns -1.
 */
int
field_map_builder_create(struct field_map_builder *builder,
			 uint32_t minimal_field_map_size,
			 struct region *region);

/**
 * Set data offset for a field identified by unique offset_slot.
 *
 * The offset_slot argument must be negative and offset must be
 * positive (by definition).
 */
static inline void
field_map_builder_set_slot(struct field_map_builder *builder,
			   int32_t offset_slot, uint32_t offset)
{
	assert(offset_slot < 0);
	assert((uint32_t)-offset_slot <= builder->slot_count);
	assert(offset > 0);
	builder->slots[offset_slot] = offset;
}

/**
 * Calculate the size of tuple field_map to be built.
 */
static inline uint32_t
field_map_build_size(struct field_map_builder *builder)
{
	return builder->slot_count * sizeof(uint32_t);
}

/**
 * Write constructed field_map to the destination buffer field_map.
 *
 * The buffer must have at least field_map_build_size(builder) bytes.
 */
void
field_map_build(struct field_map_builder *builder, char *buffer);

#endif /* TARANTOOL_BOX_FIELD_MAP_H_INCLUDED */
