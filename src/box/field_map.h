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
#include <stddef.h>
#include "bit/bit.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct region;
struct field_map_builder_slot;

/**
 * A special value of multikey index that means that the key
 * definition is not multikey and no indirection is expected.
 */
enum { MULTIKEY_NONE = -1 };

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
 *        4b   4b      4b          4b       MessagePack data.
 *       +-----------+------+----+------+------------------------+
 *tuple: |cnt|off1|..| offN | .. | off1 | header ..|key1|..|keyN||
 *       +-----+-----+--+---+----+--+---+------------------------+
 * ext1  ^     |        |   ...     |                 ^       ^
 *       +-----|--------+           |                 |       |
 * indirection |                    +-----------------+       |
 *             +----------------------------------------------+
 *             (offset_slot = N, extent_slot = 1) --> offset
 *
 * This field_map_builder class is used for tuple field_map
 * construction. It encapsulates field_map build logic and size
 * estimation implementation-specific details.
 *
 * Each field offset is a positive number, except the case when
 * a field is not in the tuple. In this case offset is 0.
 *
 * In case of multikey index, the slot may refer to the
 * "field_map_extent" sequence that contains an additional
 * sequence of length defined before (the count of keys in the
 * multikey index for given tuple). In such case offset slot
 * represents int32_t negative value - the offset relative to
 * the field_map pointer. The i-th extent's slot contains the
 * positive offset of the i-th key field of the multikey index.
 */
struct field_map_builder {
	/**
	 * The pointer to the end of field_map allocation.
	 * Its elements are accessible by negative indexes
	 * that coinciding with offset_slot(s).
	 */
	struct field_map_builder_slot *slots;
	/**
	 * The count of slots in field_map_builder::slots
	 * allocation.
	 */
	uint32_t slot_count;
	/**
	 * Total size of memory is allocated for field_map
	 * extents.
	 */
	uint32_t extents_size;
};

/**
 * Internal stucture representing field_map extent.
 * (see field_map_builder description).
 */
struct field_map_builder_slot_extent {
	/**
	 * Count of field_map_builder_slot_extent::offset[]
	 * elements.
	 */
	uint32_t size;
	/** Data offset in tuple array. */
	uint32_t offset[0];
};

/**
 * Instead of using uint32_t offset slots directly the
 * field_map_builder uses this structure as a storage atom.
 * When there is a need to initialize an extent, the
 * field_map_builder allocates a new memory chunk and sets
 * field_map_builder_slot::pointer (instead of real field_map
 * reallocation).
 *
 * On field_map_build, all of the field_map_builder_slot_extent(s)
 * are dumped to the same memory chunk that the regular field_map
 * slots and corresponding slots are initialized with negative
 * extent offset.
 *
 * The allocated memory is accounted for in extents_size.
 */
struct field_map_builder_slot {
	/**
	 * True when this slot must be interpret as
	 * extent pointer.
	 */
	bool has_extent;
	union {
		/** Data offset in tuple. */
		uint32_t offset;
		/** Pointer to field_map extent. */
		struct field_map_builder_slot_extent *extent;
	};
};

/**
 * Get offset of the field in tuple data MessagePack using
 * tuple's field_map and required field's offset_slot.
 *
 * When a field is not in the data tuple, its offset is 0.
 */
static inline uint32_t
field_map_get_offset(const char *field_map_ptr, int32_t offset_slot,
		     int multikey_idx)
{
	const uint32_t *field_map = (const uint32_t *)field_map_ptr;
	/*
	 * Can not access field_map as a normal uint32 array
	 * because its alignment may be < 4 bytes. Need to use
	 * unaligned store-load operations explicitly.
	 */
	uint32_t offset = load_u32(&field_map[offset_slot]);
	if (multikey_idx != MULTIKEY_NONE && (int32_t)offset < 0) {
		/**
		 * The field_map extent has the following
		 * structure: [size=N|slot1|slot2|..|slotN]
		 */
		const uint32_t *extent = (const uint32_t *)
			((const char *)field_map + (int32_t)offset);
		if ((uint32_t)multikey_idx >= load_u32(&extent[0]))
			return 0;
		offset = load_u32(&extent[multikey_idx + 1]);
	}
	return offset;
}

/**
 * Initialize field_map_builder.
 *
 * The field_map_size argument is a size of the minimal field_map
 * allocation where each indexed field has own offset slot.
 *
 * Routine uses region to perform memory allocation for internal
 * structures.
 */
void
field_map_builder_create(struct field_map_builder *builder,
			 uint32_t minimal_field_map_size,
			 struct region *region);

/**
 * Internal function to allocate field map extent by offset_slot
 * and count of multikey keys.
 */
struct field_map_builder_slot_extent *
field_map_builder_slot_extent_new(struct field_map_builder *builder,
				  int32_t offset_slot, uint32_t multikey_count,
				  struct region *region);

/**
 * Set data offset for a field identified by unique offset_slot.
 *
 * When multikey_idx != MULTIKEY_NONE this routine initializes
 * corresponding field_map_builder_slot_extent identified by
 * multikey_idx and multikey_count. Performs allocation on region
 * when required.
 *
 * The offset_slot argument must be negative and offset must be
 * positive (by definition).
 */
static inline void
field_map_builder_set_slot(struct field_map_builder *builder,
			   int32_t offset_slot, uint32_t offset,
			   int32_t multikey_idx, uint32_t multikey_count,
			   struct region *region)
{
	assert(offset_slot < 0);
	assert((uint32_t)-offset_slot <= builder->slot_count);
	assert(offset > 0);
	if (multikey_idx == MULTIKEY_NONE) {
		builder->slots[offset_slot].offset = offset;
	} else {
		assert(multikey_idx >= 0);
		assert(multikey_idx < (int32_t)multikey_count);
		struct field_map_builder_slot_extent *extent;
		if (builder->slots[offset_slot].has_extent) {
			extent = builder->slots[offset_slot].extent;
			assert(extent != NULL);
			assert(extent->size == multikey_count);
		} else {
			extent = field_map_builder_slot_extent_new(builder,
					offset_slot, multikey_count, region);
		}
		extent->offset[multikey_idx] = offset;
	}
}

/**
 * Calculate the size of tuple field_map to be built.
 */
static inline uint32_t
field_map_build_size(struct field_map_builder *builder)
{
	return builder->slot_count * sizeof(uint32_t) +
	       builder->extents_size;
}

/**
 * Write constructed field_map to the destination buffer field_map.
 *
 * The buffer must have at least field_map_build_size(builder) bytes.
 */
void
field_map_build(struct field_map_builder *builder, char *field_map_ptr);

#endif /* TARANTOOL_BOX_FIELD_MAP_H_INCLUDED */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__plusplus) */
