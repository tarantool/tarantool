
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

int
memtx_index_min(struct index *index, const char *key,
		uint32_t part_count, struct tuple **result)
{
	struct iterator *it = index_create_iterator(index, ITER_GE,
						    key, part_count);
	if (it == NULL)
		return -1;
	int rc = it->next(it, result);
	it->free(it);
	return rc;
}

int
memtx_index_max(struct index *index, const char *key,
		uint32_t part_count, struct tuple **result)
{
	struct iterator *it = index_create_iterator(index, ITER_LE,
						    key, part_count);
	if (it == NULL)
		return -1;
	int rc = it->next(it, result);
	it->free(it);
	return rc;
}

ssize_t
memtx_index_count(struct index *index, enum iterator_type type,
		  const char *key, uint32_t part_count)
{
	if (type == ITER_ALL)
		return index_size(index); /* optimization */
	struct iterator *it = index_create_iterator(index, type,
						    key, part_count);
	if (it == NULL)
		return -1;
	int rc = 0;
	size_t count = 0;
	struct tuple *tuple = NULL;
	while ((rc = it->next(it, &tuple)) == 0 && tuple != NULL)
		++count;
	it->free(it);
	if (rc < 0)
		return rc;
	return count;
}
