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

int
field_map_builder_create(struct field_map_builder *builder,
			 uint32_t minimal_field_map_size,
			 struct region *region)
{
	builder->slot_count = minimal_field_map_size / sizeof(uint32_t);
	if (minimal_field_map_size == 0) {
		builder->slots = NULL;
		return 0;
	}
	uint32_t sz = builder->slot_count * sizeof(builder->slots[0]);
	builder->slots = region_alloc(region, sz);
	if (builder->slots == NULL) {
		diag_set(OutOfMemory, sz, "region_alloc", "field_map");
		return -1;
	}
	memset((char *)builder->slots, 0, sz);
	builder->slots = builder->slots + builder->slot_count;
	return 0;
}

void
field_map_build(struct field_map_builder *builder, char *buffer)
{
	uint32_t field_map_size = field_map_build_size(builder);
	memcpy(buffer, (char *)builder->slots - field_map_size, field_map_size);
}
