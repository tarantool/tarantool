/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "memtx_index.h"

/** Wrapper around `replace_tuple`. */
static int
memtx_index_replace_impl(struct index *index, struct tuple *old_tuple,
			 struct tuple *new_tuple, enum dup_replace_mode mode,
			 struct tuple **result, struct tuple **successor)
{
	assert(new_tuple != NULL);
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	return vtab->replace_tuple(index, old_tuple, new_tuple, mode, result,
				   successor);
}

/** Wrapper around `delete_tuple`. */
static int
memtx_index_delete_impl(struct index *index, struct tuple *tuple,
			struct tuple **result)
{
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	return vtab->delete_tuple(index, tuple, result);
}

int
memtx_index_replace(struct index *index, struct tuple *old_tuple,
		    struct tuple *new_tuple, enum dup_replace_mode mode,
		    struct tuple **result, struct tuple **successor)
{
	*result = NULL;
	*successor = NULL;
	struct tuple *replaced = NULL;
	if (new_tuple != NULL &&
	    memtx_index_replace_impl(index, old_tuple, new_tuple, mode,
				     &replaced, successor) != 0)
		return -1;
	if (old_tuple == NULL) {
		*result = replaced;
		return 0;
	}
	bool is_mk_or_func = index->def->key_def->is_multikey ||
			     index->def->key_def->for_func_index;
	if (replaced != NULL && !is_mk_or_func) {
		*result = replaced;
		return 0;
	}
	struct tuple *deleted = NULL;
	if (memtx_index_delete_impl(index, old_tuple, &deleted) != 0) {
		if (new_tuple != NULL) {
			struct tuple *unused;
			VERIFY(memtx_index_delete_impl(index, new_tuple,
						       &unused) == 0);
		}
		if (replaced != NULL) {
			struct tuple *unused, *unused_successor;
			VERIFY(memtx_index_replace_impl(
				index, NULL, replaced, DUP_INSERT, &unused,
				&unused_successor) == 0);
		}
		return -1;
	}
	*result = replaced != NULL ? replaced : deleted;
	return 0;
}

int
generic_memtx_index_get_internal(struct index *index, const char *key,
				 uint32_t part_count, struct tuple **result,
				 bool is_rw)
{
	(void)key;
	(void)part_count;
	(void)result;
	(void)is_rw;
	diag_set(UnsupportedIndexFeature, index->def, "get_internal()");
	return -1;
}

void
generic_memtx_index_begin_build(struct index *index)
{
	(void)index;
}

int
generic_memtx_index_reserve(struct index *index, uint32_t size_hint)
{
	(void)index;
	(void)size_hint;
	return 0;
}

int
generic_memtx_index_build_next(struct index *index, struct tuple *tuple)
{
	struct tuple *unused;
	/*
	 * Note this is not no-op call in case of rtee index:
	 * reserving 0 bytes is required during rtree recovery.
	 * For details see memtx_rtree_index_reserve().
	 */
	if (memtx_index_reserve(index, 0) != 0)
		return -1;
	return memtx_index_replace(index, NULL, tuple, DUP_INSERT, &unused,
				   &unused);
}

void
generic_memtx_index_end_build(struct index *index)
{
	(void)index;
}
