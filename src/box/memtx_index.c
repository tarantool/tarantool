/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "memtx_index.h"

#include "key_list.h"
#include "memtx_engine.h"
#include "memtx_tx.h"
#include "tuple.h"

/**
 * One entry in a replace-result list.
 *
 * Replace-result records are allocated on the fiber region. The entry may be
 * null when the corresponding logical replace step has no value of this kind.
 * A non-null functional-key hint is referenced while stored in a result and
 * must be released by rollback or result cleanup.
 */
struct memtx_index_replace_result {
	/** Replaced entry for this position in the replace-result list. */
	struct memtx_index_entry replaced;
	/** Successor entry for this position in the replace-result list. */
	struct memtx_index_entry successor;
	/** Inserted entry for this position in the replace-result list. */
	struct memtx_index_entry inserted;
	/** Link in the replace-result list. */
	struct rlist link;
};

/** Wrapper around `replace_entry`. */
static int
memtx_index_replace_impl(struct index *index,
			 struct tuple *old_tuple,
			 struct memtx_index_entry new_entry,
			 enum dup_replace_mode mode,
			 struct memtx_index_entry *result,
			 struct memtx_index_entry *successor)
{
	assert(new_entry.tuple != NULL);
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	return vtab->replace_entry(index, old_tuple, new_entry, mode, result,
				   successor);
}

/** Wrapper around `delete_entry`. */
static int
memtx_index_delete_impl(struct index *index, struct memtx_index_entry entry,
			struct memtx_index_entry *result)
{
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	return vtab->delete_entry(index, entry, result);
}

/**
 * Replace one exact physical entry with another one.
 */
static int
memtx_index_replace_entry_impl(struct index *index,
			       struct memtx_index_entry old_entry,
			       struct memtx_index_entry new_entry,
			       enum dup_replace_mode mode,
			       struct memtx_index_entry *result,
			       struct memtx_index_entry *successor)
{
	*result = memtx_index_entry_null;
	*successor = memtx_index_entry_null;
	if (new_entry.tuple != NULL) {
		if (memtx_index_replace_impl(index, old_entry.tuple, new_entry,
					     mode, result, successor) != 0)
			return -1;
		if (result->tuple != NULL)
			return 0;
	}
	if (old_entry.tuple == NULL)
		return 0;
	if (memtx_index_delete_impl(index, old_entry, result) != 0) {
		if (new_entry.tuple != NULL) {
			struct memtx_index_entry unused;
			VERIFY(memtx_index_delete_impl(index, new_entry,
						       &unused) == 0);
		}
		return -1;
	}
	return 0;
}

/** Replace or delete one exact logical index entry. */
static int
memtx_index_replace_entry(struct index *index,
			  struct memtx_index_entry old_entry,
			  struct memtx_index_entry new_entry,
			  enum dup_replace_mode mode,
			  struct tuple **result)
{
	struct memtx_index_entry result_entry;
	struct memtx_index_entry unused;
	if (memtx_index_replace_entry_impl(index, old_entry, new_entry, mode,
					   &result_entry, &unused) != 0)
		return -1;
	*result = result_entry.tuple;
	if (index->def->key_def->for_func_index) {
		if (result_entry.tuple != NULL)
			tuple_unref((struct tuple *)result_entry.hint);
		if (new_entry.tuple != NULL)
			tuple_ref((struct tuple *)new_entry.hint);
	}
	return 0;
}

/**
 * Rollback every complete step of a replace-result set.
 *
 * Handles both the complete prefix left by a failed replace and all steps of a
 * successful replace being reverted.
 */
static void
memtx_index_replace_rollback(struct index *index, struct rlist *results)
{
	bool is_mk_or_func = index->def->key_def->is_multikey ||
			     index->def->key_def->for_func_index;
	(void)is_mk_or_func;

	struct memtx_index_replace_result *result;
	rlist_foreach_entry(result, results, link) {
		struct tuple *removed;
		VERIFY(memtx_index_replace_entry(index, result->inserted,
						 result->replaced,
						 DUP_INSERT, &removed) == 0);
		if (index->def->key_def->for_func_index) {
			struct memtx_index_entry replaced = result->replaced;
			if (replaced.tuple != NULL)
				tuple_unref((struct tuple *)replaced.hint);
			struct memtx_index_entry successor = result->successor;
			if (successor.tuple != NULL)
				tuple_unref((struct tuple *)successor.hint);
		}
		assert(result->inserted.tuple == removed ||
		       (removed == NULL && is_mk_or_func));
	}
}

/**
 * Allocate the three result entries for one logical replace step.
 */
static struct memtx_index_replace_result *
memtx_index_replace_result_new(struct rlist *results)
{
	struct memtx_index_replace_result *result =
		xregion_alloc_object(&fiber()->gc, typeof(*result));
	result->replaced = memtx_index_entry_null;
	result->successor = memtx_index_entry_null;
	result->inserted = memtx_index_entry_null;
	rlist_add_tail(results, &result->link);
	return result;
}

/**
 * Resolve two keys of the new tuple that identify the same index entry.
 *
 * The later step replaces an entry inserted by an earlier step. Keep the later
 * inserted entry, transfer the entry replaced by the earlier step and its
 * successor to the later step, and null the earlier step. This preserves one
 * externally visible replace transition and keeps rollback positional.
 */
static void
memtx_index_replace_resolve_multikey_conflict(
	struct rlist *results, hint_t old_hint,
	struct memtx_index_replace_result *conflict)
{
	struct memtx_index_replace_result *old_result;
	rlist_foreach_entry(old_result, results, link) {
		if (old_result->inserted.hint == old_hint) {
			conflict->replaced = old_result->replaced;
			conflict->successor = old_result->successor;
			old_result->inserted = memtx_index_entry_null;
			old_result->replaced = memtx_index_entry_null;
			old_result->successor = memtx_index_entry_null;
			return;
		}
	}
}

/**
 * :replace() function for a multikey index: replace old tuple
 * index entries with ones from the new tuple.
 *
 * In a multikey index a single tuple is associated with 0..N keys
 * of the b+*tree. Imagine old tuple key set is called "old_keys"
 * and a new tuple set is called "new_keys". This function must
 * 1) delete all removed keys: (old_keys \ new_keys)
 * 2) update tuple pointer in all preserved keys: (old_keys & new_keys)
 * 3) insert data for all new keys (new_keys \ old_keys).
 *
 * Compare with a standard unique or non-unique index, when a key
 * is present only once, so whenever we encounter a duplicate, it
 * is guaranteed to point at the old tuple (in non-unique indexes
 * we augment the secondary key parts with primary key parts, so
 * b+*tree still contains unique entries only).
 *
 * To reduce the number of insert and delete operations on the
 * tree, this function attempts to optimistically add all keys
 * from the new tuple to the tree first.
 *
 * When this step finds a duplicate, it's either of the following:
 * - for a unique multikey index, it may be the old tuple or
 *   some other tuple. Since unique index forbids duplicates,
 *   this branch ends with an error unless we found the old tuple.
 * - for a non-unique multikey index, both secondary and primary
 *   key parts must match, so it's guaranteed to be the old tuple.
 *
 * In other words, when an optimistic insert finds a duplicate,
 * it's either an error, in which case we roll back all the new
 * keys from the tree and abort the procedure, or the old tuple,
 * which we save to get back to, later.
 *
 * When adding new keys finishes, we have completed steps
 * 2) and 3):
 * - added set (new_keys - old_keys) to the index
 * - updated set (new_keys ^ old_keys) with a new tuple pointer.
 *
 * We now must perform 1), which is remove (old_keys - new_keys).
 *
 * This is done by using the old tuple pointer saved from the
 * previous step. To not accidentally delete the common key
 * set of the old and the new tuple, we don't use key parts alone
 * to compare - we also look at b+* tree value that has the tuple
 * pointer, and delete old tuple entries only.
 */
static int
memtx_index_replace_multikey(struct index *index, struct tuple *old_tuple,
			     struct tuple *new_tuple,
			     enum dup_replace_mode mode, struct rlist *results)
{
	struct key_def *cmp_def = index->def->cmp_def;
	struct memtx_index_entry old_entry = {
		.tuple = old_tuple,
		.hint = HINT_NONE,
	};
	struct memtx_index_entry new_entry = {
		.tuple = new_tuple,
		.hint = HINT_NONE,
	};

	if (new_tuple != NULL) {
		uint32_t mk_count = tuple_multikey_count(new_tuple, cmp_def);
		for (size_t mk_idx = 0; mk_idx < mk_count; ++mk_idx) {
			new_entry.hint = mk_idx;
			struct memtx_index_replace_result *result =
				memtx_index_replace_result_new(results);
			if (tuple_key_is_excluded(new_tuple,
						  index->def->key_def,
						  (int)mk_idx))
				continue;
			struct memtx_index_entry unused;
			if (memtx_index_replace_impl(
					index, old_tuple, new_entry, mode,
					&result->replaced, &unused) != 0)
				goto rollback;
			result->inserted = new_entry;
			if (result->replaced.tuple == new_entry.tuple) {
				memtx_index_replace_resolve_multikey_conflict(
					results, result->replaced.hint, result);
			}
		}
	}
	if (old_tuple == NULL)
		return 0;
	uint32_t mk_count = tuple_multikey_count(old_tuple, cmp_def);
	for (size_t mk_idx = 0; mk_idx < mk_count; ++mk_idx) {
		old_entry.hint = mk_idx;
		struct memtx_index_replace_result *result =
				memtx_index_replace_result_new(results);
		if (tuple_key_is_excluded(old_tuple, index->def->key_def,
					  (int)mk_idx))
			continue;
		if (memtx_index_delete_impl(index, old_entry,
					    &result->replaced) != 0)
			goto rollback;
	}
	return 0;
rollback:
	memtx_index_replace_rollback(index, results);
	return -1;
}

/**
 * Replace all entries generated by a functional index definition.
 *
 * Each generated key tuple becomes the entry hint and is referenced after a
 * successful insertion. A replaced key hint is retained in the replaced list
 * for rollback or for the caller and is released when the result is cleaned
 * up. On failure, rollback removes inserted entries, releases their keys, and
 * restores replaced entries with their original key hints.
 */
static int
memtx_index_replace_func(struct index *index, struct tuple *old_tuple,
			 struct tuple *new_tuple, enum dup_replace_mode mode,
			 struct rlist *results)
{
	struct memtx_engine *memtx = (struct memtx_engine *)index->engine;
	struct index_def *index_def = index->def;
	assert(index_def->key_def->for_func_index);
	/* Make sure that key_def is not multikey - we rely on it below. */
	assert(!index_def->key_def->is_multikey);

	struct key_list_iterator it;

	struct memtx_index_entry old_entry = {
		.tuple = old_tuple,
		.hint = HINT_NONE,
	};
	struct memtx_index_entry new_entry = {
		.tuple = new_tuple,
		.hint = HINT_NONE,
	};
	if (new_tuple != NULL) {
		if (key_list_iterator_create(&it, new_tuple, index_def, true,
					     memtx->func_key_format) != 0)
			return -1;
		int err = 0;
		struct tuple *key;
		struct key_def *key_def = index_def->key_def;
		while ((err = key_list_iterator_next(&it, &key)) == 0 &&
		       key != NULL) {
			struct memtx_index_replace_result *result =
				memtx_index_replace_result_new(results);
			/* Save functional key to MVCC, even excluded one. */
			memtx_tx_save_func_key(new_tuple, index, key);
			if (tuple_key_is_excluded(key, key_def, MULTIKEY_NONE))
				continue;
			new_entry.hint = (uint64_t)key;
			err = memtx_index_replace_impl(index, old_tuple,
						       new_entry, mode,
						       &result->replaced,
						       &result->successor);
			if (err != 0)
				break;
			if (it.func_is_multikey)
				result->successor = memtx_index_entry_null;
			result->inserted = new_entry;
			tuple_ref(key);
			struct memtx_index_entry successor = result->successor;
			if (result->replaced.tuple == new_entry.tuple) {
				hint_t old_hint = result->replaced.hint;
				tuple_unref((struct tuple *)old_hint);
				memtx_index_replace_resolve_multikey_conflict(
					results, old_hint, result);
			} else if (successor.tuple != NULL) {
				tuple_ref((struct tuple *)successor.hint);
			}
		}
		assert(key == NULL || err != 0);
		if (err != 0)
			goto rollback;
	}
	if (old_tuple == NULL)
		return 0;
	/*
	 * Use the runtime format to avoid OOM while deleting a tuple
	 * from a space. It's okay, because we are not going to store
	 * the keys in the index.
	 */
	if (key_list_iterator_create(&it, old_tuple, index_def, false,
				     tuple_format_runtime) != 0)
		goto rollback;
	struct tuple *key;
	while (key_list_iterator_next(&it, &key) == 0 && key != NULL) {
		old_entry.hint = (hint_t)key;
		struct memtx_index_replace_result *result =
			memtx_index_replace_result_new(results);
		if (memtx_index_delete_impl(index, old_entry,
					    &result->replaced) != 0)
			goto rollback;
	}
	assert(key == NULL);
	return 0;
rollback:
	memtx_index_replace_rollback(index, results);
	return -1;
}

/**
 * Fold a replace-result list to one result.
 */
static struct memtx_index_replace_result *
memtx_index_replace_results_fold(struct index *index, struct rlist *results)
{
	if (!index->def->key_def->is_multikey &&
	    !index->def->key_def->for_func_index) {
		assert(!rlist_empty(results));
		struct memtx_index_replace_result *result =
			rlist_first_entry(results,
					  struct memtx_index_replace_result,
					  link);
		assert(rlist_next(&result->link) == results);
		return result;
	}

	struct memtx_index_replace_result *result = NULL;
	struct memtx_index_replace_result *current_result;
	rlist_foreach_entry(current_result, results, link) {
		if (current_result->replaced.tuple == NULL)
			continue;
		assert(result == NULL ||
		       result->replaced.tuple ==
		       current_result->replaced.tuple);
		result = current_result;
	}
	if (result == NULL)
		result = rlist_first_entry(results,
					   struct memtx_index_replace_result,
					   link);
	return result;
}

/**
 * Replace at most one regular-index entry and record one result step.
 */
static int
memtx_index_replace_regular(struct index *index, struct tuple *old_tuple,
			    struct tuple *new_tuple, enum dup_replace_mode mode,
			    struct rlist *results)
{
	struct memtx_index_entry old_entry = {
		.tuple = old_tuple == NULL ||
			tuple_key_is_excluded(old_tuple, index->def->key_def,
					      MULTIKEY_NONE) ? NULL : old_tuple,
		.hint = HINT_NONE,
	};
	struct memtx_index_entry new_entry = {
		.tuple = new_tuple == NULL ||
			tuple_key_is_excluded(new_tuple, index->def->key_def,
					      MULTIKEY_NONE) ? NULL : new_tuple,
		.hint = HINT_NONE,
	};
	struct memtx_index_replace_result *result =
		memtx_index_replace_result_new(results);
	result->inserted = new_entry;
	return memtx_index_replace_entry_impl(index, old_entry, new_entry,
					      mode, &result->replaced,
					      &result->successor);
}

/** Cleanup a replace-result list. */
static void
memtx_index_replace_results_cleanup(struct index *index, struct rlist *results)
{
	if (!index->def->key_def->for_func_index)
		return;
	struct memtx_index_replace_result *result;
	rlist_foreach_entry(result, results, link) {
		if (result->replaced.tuple != NULL)
			tuple_unref((struct tuple *)result->replaced.hint);
		if (result->successor.tuple != NULL)
			tuple_unref((struct tuple *)result->successor.hint);
	}
}

int
memtx_index_replace(struct index *index, struct tuple *old_tuple,
		    struct tuple *new_tuple, enum dup_replace_mode mode,
		    struct tuple **result, struct tuple **successor)
{
	struct tuple *unused_result;
	struct tuple *unused_successor;
	if (result == NULL)
		result = &unused_result;
	if (successor == NULL)
		successor = &unused_successor;
	size_t region_svp = region_used(&fiber()->gc);
	*result = NULL;
	*successor = NULL;
	struct rlist results;
	rlist_create(&results);
	int rc;
	if (index->def->key_def->is_multikey)
		rc = memtx_index_replace_multikey(index, old_tuple, new_tuple,
						  mode, &results);
	else if (index->def->key_def->for_func_index)
		rc = memtx_index_replace_func(index, old_tuple, new_tuple,
					      mode, &results);
	else
		rc = memtx_index_replace_regular(index, old_tuple, new_tuple,
						 mode, &results);
	if (rc == 0) {
		struct memtx_index_replace_result *replace_result =
			memtx_index_replace_results_fold(index, &results);
		*result = replace_result->replaced.tuple;
		*successor = replace_result->successor.tuple;
		memtx_index_replace_results_cleanup(index, &results);
	}
	region_truncate(&fiber()->gc, region_svp);
	return rc;
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
	/*
	 * Note this is not no-op call in case of rtee index:
	 * reserving 0 bytes is required during rtree recovery.
	 * For details see memtx_rtree_index_reserve().
	 */
	if (memtx_index_reserve(index, 0) != 0)
		return -1;
	return memtx_index_replace(index, NULL, tuple, DUP_INSERT, NULL, NULL);
}

void
generic_memtx_index_end_build(struct index *index)
{
	(void)index;
}
