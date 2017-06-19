
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
#include "index.h"
#include "memtx_index.h"
#include "tuple.h"
#include "say.h"
#include "schema.h"
#include "space.h"

void
MemtxIndex::truncate()
{
	if (index_def->iid != 0)
		return; /* nothing to do for secondary keys */

	struct iterator *it = position();
	initIterator(it, ITER_ALL, NULL, 0);
	struct tuple *tuple;
	while ((tuple = it->next(it)) != NULL)
		tuple_unref(tuple);
}

void
MemtxIndex::beginBuild()
{}

void
MemtxIndex::reserve(uint32_t /* size_hint */)
{}

void
MemtxIndex::buildNext(struct tuple *tuple)
{
	replace(NULL, tuple, DUP_INSERT);
}

void
MemtxIndex::endBuild()
{}

struct tuple *
MemtxIndex::min(const char *key, uint32_t part_count) const
{
	struct iterator *it = position();
	initIterator(it, ITER_GE, key, part_count);
	return it->next(it);
}

struct tuple *
MemtxIndex::max(const char *key, uint32_t part_count) const
{
	struct iterator *it = position();
	initIterator(it, ITER_LE, key, part_count);
	return it->next(it);
}

size_t
MemtxIndex::count(enum iterator_type type, const char *key,
		  uint32_t part_count) const
{
	if (type == ITER_ALL)
		return size(); /* optimization */
	struct iterator *it = position();
	initIterator(it, type, key, part_count);
	size_t count = 0;
	struct tuple *tuple = NULL;
	while ((tuple = it->next(it)) != NULL)
		++count;
	return count;
}

void
index_build(MemtxIndex *index, MemtxIndex *pk)
{
	uint32_t n_tuples = pk->size();
	uint32_t estimated_tuples = n_tuples * 1.2;

	index->beginBuild();
	index->reserve(estimated_tuples);

	if (n_tuples > 0) {
		say_info("Adding %" PRIu32 " keys to %s index '%s' ...",
			 n_tuples, index_type_strs[index->index_def->type],
			 index_name(index));
	}

	struct iterator *it = pk->position();
	pk->initIterator(it, ITER_ALL, NULL, 0);
	struct tuple *tuple;
	while ((tuple = it->next(it)))
		index->buildNext(tuple);

	index->endBuild();
}
