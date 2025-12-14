/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "index.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * A logical entry in a memtx index.
 *
 * A tuple identifies an entry in a regular index. A multikey or functional
 * index may contain several entries for the same tuple, so the hint is part of
 * the entry identity there. The hint is meaningful only together with the
 * index whose operation produced the entry.
 */
struct memtx_index_entry {
	/** Tuple stored in the index, or NULL for an absent entry. */
	struct tuple *tuple;
	/**
	 * Index-specific entry discriminator. It is the multikey array position
	 * for a multikey index and a referenced functional-key tuple for a
	 * functional index. Regular entries crossing the generic memtx-index
	 * API must keep HINT_NONE; index implementations that need comparison
	 * hints compute them locally.
	 */
	hint_t hint;
};

static const struct memtx_index_entry memtx_index_entry_null = {
	.tuple = NULL,
	.hint = HINT_NONE,
};

/** Virtual function table for memtx-specific index operations. */
struct memtx_index_vtab {
	/** Base index virtual table for common index operations. */
	struct index_vtab base;
	/**
	 * Same as get(), but returns a tuple as it is stored in the index,
	 * without decompression, blessing, or space upgrade.
	 *
	 * @param is_rw - if true, the call is for a write statement and MVCC
	 *  clarification uses read-write semantics (sees prepared statements
	 *  regardless of isolation level), otherwise clarification respects
	 *  the read transaction's isolation level.
	 */
	int (*get_internal)(struct index *index, const char *key,
			    uint32_t part_count, struct tuple **result,
			    bool is_rw);
	/**
	 * Insert one physical index entry, replacing a duplicate if allowed.
	 *
	 * @param new_tuple must not be NULL.
	 * @param mode - @sa dup_replace_mode description
	 * @param result - here the replaced entry is placed.
	 * @param successor - if the index supports ordering, then in case of
	 *  insert (!) here the successor entry is returned. In other words,
	 *  here will be stored the entry, before which new entry is inserted.
	 *
	 * NB: do not use the same object for @a result and @a successor - they
	 *     are different returned values and implementation can rely on it.
	 */
	int (*replace_entry)(struct index *index,
			     struct tuple *old_tuple,
			     struct memtx_index_entry new_entry,
			     enum dup_replace_mode mode,
			     struct memtx_index_entry *result,
			     struct memtx_index_entry *successor);
	/**
	 * Delete one exact physical index entry and return the removed entry.
	 *
	 * @param result - here the deleted entry is placed.
	 */
	int (*delete_entry)(struct index *index, struct memtx_index_entry entry,
			    struct memtx_index_entry *result);
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

static inline int
memtx_index_get_internal(struct index *index, const char *key,
			 uint32_t part_count, struct tuple **result,
			 bool is_rw)
{
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	return vtab->get_internal(index, key, part_count, result, is_rw);
}

/**
 * Replace a tuple in the index.
 */
int
memtx_index_replace(struct index *index, struct tuple *old_tuple,
		    struct tuple *new_tuple, enum dup_replace_mode mode,
		    struct tuple **result, struct tuple **successor);

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

int
generic_memtx_index_get_internal(struct index *index, const char *key,
				 uint32_t part_count, struct tuple **result,
				 bool is_rw);

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
