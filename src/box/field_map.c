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
#include "diag.h"
#include "field_map.h"
#include "small/region.h"

void
field_map_builder_create(struct field_map_builder *builder,
			 uint32_t minimal_field_map_size,
			 struct region *region)
{
	builder->extents_size = 0;
	builder->slot_count = minimal_field_map_size / sizeof(uint32_t);
	if (minimal_field_map_size == 0) {
		builder->slots = NULL;
		return;
	}
	builder->slots = xregion_alloc_array(region, typeof(builder->slots[0]),
					     builder->slot_count);
	uint32_t sz = sizeof(builder->slots[0]) * builder->slot_count;
	memset((char *)builder->slots, 0, sz);
	builder->slots = builder->slots + builder->slot_count;
}

struct field_map_builder_slot_extent *
field_map_builder_slot_extent_new(struct field_map_builder *builder,
				  int32_t offset_slot, uint32_t multikey_count,
				  struct region *region)
{
	struct field_map_builder_slot_extent *extent;
	assert(!builder->slots[offset_slot].has_extent);
	uint32_t sz = sizeof(*extent) +
		      multikey_count * sizeof(extent->offset[0]);
	extent = (struct field_map_builder_slot_extent *)
		xregion_aligned_alloc(region, sz, alignof(*extent));
	memset(extent, 0, sz);
	extent->size = multikey_count;
	builder->slots[offset_slot].extent = extent;
	builder->slots[offset_slot].has_extent = true;
	builder->extents_size += sz;
	return extent;
}

void
field_map_build(struct field_map_builder *builder, char *field_map_ptr)
{
	/*
	 * To initialize the field map and its extents, prepare
	 * the following memory layout with pointers:
	 *
	 *                      offset
	 * buffer       +---------------------+
	 * |            |                     |
	 * [extentK] .. [extent1][[slotN]..[slot2][slot1]]
	 * |            |                               |
	 * |extent_wptr |        |                      |field_map
	 * ->           ->                              <-
	 *
	 * The buffer size is assumed to be sufficient to write
	 * field_map_build_size(builder) bytes there.
	 */
	uint32_t *field_map = (uint32_t *)field_map_ptr;
	char *extent_wptr = field_map_ptr - field_map_build_size(builder);
	for (int32_t i = -1; i >= -(int32_t)builder->slot_count; i--) {
		/*
		 * Can not access field_map as a normal uint32
		 * array because its alignment may be < 4 bytes.
		 * Need to use unaligned store-load operations
		 * explicitly.
		 */
		if (!builder->slots[i].has_extent) {
			store_u32(&field_map[i], builder->slots[i].offset);
			continue;
		}
		struct field_map_builder_slot_extent *extent =
						builder->slots[i].extent;
		/** Retrive memory for the extent. */
		store_u32(&field_map[i], extent_wptr - (char *)field_map);
		store_u32(extent_wptr, extent->size);
		uint32_t extent_offset_sz = extent->size * sizeof(uint32_t);
		memcpy(&((uint32_t *) extent_wptr)[1], extent->offset,
			extent_offset_sz);
		extent_wptr += sizeof(uint32_t) + extent_offset_sz;
	}
	assert(extent_wptr == field_map_ptr - field_map_build_size(builder) +
			      builder->extents_size);
}
