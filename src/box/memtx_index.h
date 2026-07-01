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

/**
 * Positional result stream of an index replace.
 *
 * Every logical replace step appends one record to each list. Missing values,
 * including values of delete-only and excluded-key steps, are represented by a
 * record containing memtx_index_entry_null. Records are allocated on the fiber
 * region and remain valid until the owning caller truncates it.
 */
struct memtx_index_replace_result_set {
	/** Entry removed or replaced by each logical step. */
	struct rlist replaced;
	/** Entry following the inserted entry for each logical step. */
	struct rlist successors;
	/** Entry inserted by each logical step. */
	struct rlist inserted;
};

/**
 * One entry in a replace-result list.
 *
 * Replace-result records are allocated on the fiber region. The entry may be
 * null when the corresponding logical replace step has no value of this kind.
 * A non-null functional-key hint is referenced while stored in a result and
 * must be released by rollback or result cleanup.
 */
struct memtx_index_replace_result {
	/** Entry produced for this position in the replace-result stream. */
	struct memtx_index_entry entry;
	/** Link in one positional replace-result list. */
	struct rlist link;
};

/**
 * Result records allocated for one logical index-entry replace.
 *
 * Every field points to the record appended to the corresponding result list.
 * Allocating all records before changing the index makes rollback allocation
 * free and keeps the lists positional even when the step later stays empty.
 */
struct memtx_index_replace_step {
	/** Result slot for the entry removed or replaced by this step. */
	struct memtx_index_replace_result *replaced;
	/** Result slot for the successor of the inserted entry. */
	struct memtx_index_replace_result *successor;
	/** Result slot for the entry inserted by this step. */
	struct memtx_index_replace_result *inserted;
};

/** Typed iterator over complete logical steps of a replace-result set. */
struct memtx_index_replace_result_iterator {
	/** Result set being iterated. */
	struct memtx_index_replace_result_set *result;
	/** Cursor in the replaced-entry list. */
	struct memtx_index_replace_result *replaced;
	/** Cursor in the successor-entry list. */
	struct memtx_index_replace_result *successor;
	/** Cursor in the inserted-entry list. */
	struct memtx_index_replace_result *inserted;
};

/** Virtual function table for memtx-specific index operations. */
struct memtx_index_vtab {
	/** Base index virtual table for common index operations. */
	struct index_vtab base;
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

/** Initialize a typed iterator at the first result step. */
void
memtx_index_replace_result_iterator_create(
	struct memtx_index_replace_result_iterator *it,
	struct memtx_index_replace_result_set *result);

/**
 * Return the next complete logical replace step.
 *
 * The end-of-stream assertions enforce the result-set invariant that all three
 * positional lists have equal length and are consumed together.
 */
bool
memtx_index_replace_result_iterator_next(
	struct memtx_index_replace_result_iterator *it,
	struct memtx_index_replace_step *step);

/**
 * Replace a tuple in the index and return the replaced and successor entries.
 *
 * Records are allocated on the fiber region and remain valid until the caller
 * truncates it. The caller must either clean up or roll back the result set
 * before truncating the region.
 */
int
memtx_index_replace_with_results(struct index *index, struct tuple *old_tuple,
				 struct tuple *new_tuple,
				 enum dup_replace_mode mode,
				 struct memtx_index_replace_result_set *result);

/** Return the single tuple represented by a positional result list. */
struct tuple *
memtx_index_replace_result_list_to_tuple(struct index *index,
					 struct rlist *result_list);

/**
 * Release resources retained by a successful replace-result set.
 *
 * The physical index changes remain in place.
 */
void
memtx_index_replace_result_set_cleanup(
	struct index *index, struct memtx_index_replace_result_set *result);

/**
 * Replace a tuple in the index and return the replaced tuple.
 */
int
memtx_index_replace_with_single_result(struct index *index,
				       struct tuple *old_tuple,
				       struct tuple *new_tuple,
				       enum dup_replace_mode mode,
				       struct tuple **result);

/**
 * Replace a tuple in the index.
 */
static inline int
memtx_index_replace(struct index *index, struct tuple *old_tuple,
		    struct tuple *new_tuple, enum dup_replace_mode mode)
{
	struct tuple *unused;
	return memtx_index_replace_with_single_result(index, old_tuple,
						      new_tuple, mode, &unused);
}

/**
 * Rollback every complete step of a replace-result set.
 *
 * Handles both the complete prefix left by a failed replace and all steps of a
 * successful replace being reverted.
 */
void
memtx_index_replace_rollback(struct index *index,
			     struct memtx_index_replace_result_set *result);

/**
 * Rebind one exact logical index entry to another one.
 *
 * This wrapper must be used when the caller already knows the full old and new
 * entry identities, including multikey positions or functional-key hints. It
 * replaces that exact entry and finalizes ownership of functional keys held by
 * the removed and inserted entries. A null new entry represents deletion.
 */
int
memtx_index_replace_entry(struct index *index,
			  struct memtx_index_entry old_entry,
			  struct memtx_index_entry new_entry,
			  enum dup_replace_mode mode,
			  struct memtx_index_entry *result);

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
