
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdint.h>
#include <stddef.h>

#include "index.h"
#include "memtx_index.h"

struct tuple *
memtx_index_min(struct index *index,
		const char *key, uint32_t part_count)
{
	struct iterator *it = index_position_xc(index);
	index_init_iterator_xc(index, it, ITER_GE, key, part_count);
	return iterator_next_xc(it);
}

struct tuple *
memtx_index_max(struct index *index,
		const char *key, uint32_t part_count)
{
	struct iterator *it = index_position_xc(index);
	index_init_iterator_xc(index, it, ITER_LE, key, part_count);
	return iterator_next_xc(it);
}

size_t
memtx_index_count(struct index *index, enum iterator_type type,
		  const char *key, uint32_t part_count)
{
	if (type == ITER_ALL)
		return index_size_xc(index); /* optimization */
	struct iterator *it = index_position_xc(index);
	index_init_iterator_xc(index, it, type, key, part_count);
	size_t count = 0;
	struct tuple *tuple = NULL;
	while ((tuple = iterator_next_xc(it)) != NULL)
		++count;
	return count;
}
