/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "index.h"
#include "field_map.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Generalization of tuple for memtx multikey and functional indexes that allows
 * to uniquely identify multikey or functional keys.
 */
struct memtx_index_entry {
	/** Tuple for operation. */
	struct tuple *tuple;
	/**
	 * Encoding of key for multikey and functional indexes. For multikey
	 * indexes it represents the index in the multikey array. For functional
	 * indexes it represents the pointer to the functional key. For other
	 * indexes, it is set to `MULTIKEY_NONE`. It is also set to
	 * `MULTIKEY_NONE` when the `replace` operation only requires the tuple.
	 */
	hint_t hint;
};

static const struct memtx_index_entry memtx_index_entry_null = {
	.tuple = NULL,
	.hint = (hint_t)MULTIKEY_NONE,
};

/** Virtual function table for memtx-specific index operations. */
struct memtx_index_vtab {
	/** Base index virtual table for common index operations. */
	struct index_vtab base;
	/**
	 * Main entrance point for changing data in index. Once built and
	 * before deletion this is the only way to insert, replace and delete
	 * data from the index.
	 * @param mode - @sa dup_replace_mode description
	 * @param result - here the replaced or deleted tuple is placed.
	 * @param successor - if the index supports ordering, then in case of
	 *  insert (!) here the successor tuple is returned. In other words,
	 *  here will be stored the tuple, before which new tuple is inserted.
	 *
	 * NB: do not use the same object for @a result and @a successor - they
	 *     are different returned values and implementation can rely on it.
	 */
	int (*replace)(struct index *index, struct memtx_index_entry old_entry,
		       struct memtx_index_entry new_entry,
		       enum dup_replace_mode mode,
		       struct memtx_index_entry *result,
		       struct memtx_index_entry *successor);
	/**
	 * Two-phase index creation: begin building, add tuples, finish.
	 */
	void (*begin_build)(struct index *index);
	/**
	 * Optional hint, given to the index, about
	 * the total size of the index. Called after
	 * begin_build().
	 */
	int (*reserve)(struct index *index, uint32_t size_hint);
	/** Add one tuple for index build. */
	int (*build_next)(struct index *index, struct tuple *tuple);
	/** Finish index build. */
	void (*end_build)(struct index *index);
};

/**
 * Wrapper around `memtx_index_vtab::replace` that collects replaced and
 * successor tuples.
 */
int
memtx_index_replace_with_results(struct index *index, struct tuple *old_tuple,
				 struct tuple *new_tuple,
				 enum dup_replace_mode mode,
				 struct tuple **result,
				 struct tuple **successor);

/**
 * Wrapper around `memtx_index_vtab::replace` that collects a single replaced
 * tuple. Can only be used for indexes that are not multikey.
 */
static inline int
memtx_index_replace_with_single_result(struct index *index,
				       struct tuple *old_tuple,
				       struct tuple *new_tuple,
				       enum dup_replace_mode mode,
				       struct tuple **result)
{
	struct tuple *unused;
	return memtx_index_replace_with_results(index, old_tuple, new_tuple,
						mode, result, &unused);
}

/**
 * Wrapper around `memtx_index_vtab::replace` that ignores replaced and
 * successor tuples.
 */
static inline int
memtx_index_replace(struct index *index, struct tuple *old_tuple,
		    struct tuple *new_tuple, enum dup_replace_mode mode)
{
	struct tuple *unused;
	return memtx_index_replace_with_results(index, old_tuple, new_tuple,
						mode, &unused, &unused);
}

static inline void
memtx_index_begin_build(struct index *index)
{
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	vtab->begin_build(index);
}

static inline int
memtx_index_reserve(struct index *index, uint32_t size_hint)
{
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	return vtab->reserve(index, size_hint);
}

static inline int
memtx_index_build_next(struct index *index, struct tuple *tuple)
{
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	return vtab->build_next(index, tuple);
}

static inline void
memtx_index_end_build(struct index *index)
{
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	vtab->end_build(index);
}

/** No-op stub for the `begin_build` operation. */
void
generic_memtx_index_begin_build(struct index *index);

/** No-op stub for the `reserve` operation. */
int
generic_memtx_index_reserve(struct index *index, uint32_t size_hint);

/**
 * Generic implementation of the `build_next` operation: reserves space in the
 * index and inserts the tuple into the index.
 */
int
generic_memtx_index_build_next(struct index *index, struct tuple *tuple);

/** No-op stub for the `end_build` operation. */
void
generic_memtx_index_end_build(struct index *index);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
