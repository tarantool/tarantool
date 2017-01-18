/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "vy_mem.h"

#include <stdlib.h>

#include <trivia/util.h>
#include <small/lsregion.h>
#include "diag.h"

static void *
vy_mem_tree_extent_alloc(void *ctx)
{
	struct vy_mem *mem = (struct vy_mem *) ctx;
	assert(mem->allocator != NULL && mem->allocator_lsn != NULL);
	void *ret = lsregion_alloc(mem->allocator, VY_MEM_TREE_EXTENT_SIZE,
				   *mem->allocator_lsn);
	if (ret == NULL)
		diag_set(OutOfMemory, VY_MEM_TREE_EXTENT_SIZE, "lsregion_alloc",
			 "ret");
	return ret;
}

static void
vy_mem_tree_extent_free(void *ctx, void *p)
{
	/* Can't free part of region allocated memory. */
	(void)ctx;
	(void)p;
}

struct vy_mem *
vy_mem_new(struct key_def *key_def, struct lsregion *allocator,
	   const int64_t *allocator_lsn)
{
	struct vy_mem *index = malloc(sizeof(*index));
	if (!index) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct vy_mem");
		return NULL;
	}
	index->min_lsn = INT64_MAX;
	index->used = 0;
	index->key_def = key_def;
	index->version = 0;
	index->allocator = allocator;
	index->allocator_lsn = allocator_lsn;
	vy_mem_tree_create(&index->tree, key_def, vy_mem_tree_extent_alloc,
			   vy_mem_tree_extent_free, index);
	rlist_create(&index->in_frozen);
	rlist_create(&index->in_dirty);
	return index;
}

void
vy_mem_delete(struct vy_mem *index)
{
	TRASH(index);
	free(index);
}

const struct tuple *
vy_mem_older_lsn(struct vy_mem *mem, const struct tuple *stmt)
{
	struct tree_mem_key tree_key;
	tree_key.stmt = stmt;
	tree_key.lsn = vy_stmt_lsn(stmt) - 1;
	bool exact = false;
	struct vy_mem_tree_iterator itr =
		vy_mem_tree_lower_bound(&mem->tree, &tree_key, &exact);

	if (vy_mem_tree_iterator_is_invalid(&itr))
		return NULL;

	const struct tuple *result;
	result = *vy_mem_tree_iterator_get_elem(&mem->tree, &itr);
	if (vy_stmt_compare(result, stmt, mem->key_def) != 0)
		return NULL;
	return result;
}

int
vy_mem_insert(struct vy_mem *mem, const struct tuple *stmt,
	      int64_t alloc_lsn)
{
	size_t size = tuple_size(stmt);
	struct tuple *mem_stmt;
	mem_stmt = lsregion_alloc(mem->allocator, size, alloc_lsn);
	if (mem_stmt == NULL) {
		diag_set(OutOfMemory, size, "lsregion_alloc", "mem_stmt");
		return -1;
	}
	memcpy(mem_stmt, stmt, size);
	/*
	 * Region allocated statements can't be referenced or unreferenced
	 * because they are located in monolithic memory region. Referencing has
	 * sense only for separately allocated memory blocks.
	 * The reference count here is set to 0 for an assertion if somebody
	 * will try to unreference this statement.
	 */
	mem_stmt->refs = 0;

	const struct tuple *replaced_stmt = NULL;
	int rc = vy_mem_tree_insert(&mem->tree, mem_stmt, &replaced_stmt);
	if (rc != 0)
		return -1;

	if (mem->used == 0)
		mem->min_lsn = vy_stmt_lsn(stmt);
	assert(mem->min_lsn <= vy_stmt_lsn(stmt));

	mem->used += size;
	mem->version++;

	return 0;
}

