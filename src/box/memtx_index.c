/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "memtx_index.h"

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
