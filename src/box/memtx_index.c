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

/** Initialize an empty replace-result set. */
static void
memtx_index_replace_result_set_create(
	struct memtx_index_replace_result_set *set)
{
	rlist_create(&set->replaced);
	rlist_create(&set->successors);
	rlist_create(&set->inserted);
}

void
memtx_index_replace_result_iterator_create(
	struct memtx_index_replace_result_iterator *it,
	struct memtx_index_replace_result_set *result)
{
	it->result = result;
	it->replaced =
		rlist_first_entry(&result->replaced,
				  struct memtx_index_replace_result, link);
	it->successor =
		rlist_first_entry(&result->successors,
				  struct memtx_index_replace_result, link);
	it->inserted =
		rlist_first_entry(&result->inserted,
				  struct memtx_index_replace_result, link);
}

bool
memtx_index_replace_result_iterator_next(
	struct memtx_index_replace_result_iterator *it,
	struct memtx_index_replace_step *step)
{
	bool replaced_end =
		rlist_entry_is_head(it->replaced, &it->result->replaced, link);
	bool successor_end =
		rlist_entry_is_head(it->successor, &it->result->successors,
				    link);
	(void)successor_end;
	bool inserted_end =
		rlist_entry_is_head(it->inserted, &it->result->inserted, link);
	(void)inserted_end;
	assert(replaced_end == successor_end);
	assert(replaced_end == inserted_end);
	if (replaced_end)
		return false;
	step->replaced = it->replaced;
	step->successor = it->successor;
	step->inserted = it->inserted;
	it->replaced = rlist_next_entry(it->replaced, link);
	it->successor = rlist_next_entry(it->successor, link);
	it->inserted = rlist_next_entry(it->inserted, link);
	return true;
}

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

static bool
memtx_index_entry_has_identity_hint(struct index *index)
{
	return index->def->key_def->is_multikey ||
	       index->def->key_def->for_func_index;
}

/**
 * Cleanup an entry removed from an index and handed to the caller.
 *
 * Regular hints are comparison-only and must not escape the generic API.
 * Functional-key hints were owned by the index while the entry was present;
 * once the entry is removed, release that reference here.
 */
static void
memtx_index_replaced_entry_cleanup(struct index *index,
				   struct memtx_index_entry *entry)
{
	if (!memtx_index_entry_has_identity_hint(index))
		entry->hint = HINT_NONE;
	else if (index->def->key_def->for_func_index && entry->tuple != NULL)
		tuple_unref((struct tuple *)entry->hint);
}

int
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
	memtx_index_replaced_entry_cleanup(index, &result_entry);
	if (index->def->key_def->for_func_index && new_entry.tuple != NULL)
		tuple_ref((struct tuple *)new_entry.hint);
	return 0;
}

void
memtx_index_replace_rollback(
	struct index *index, struct memtx_index_replace_result_set *result)
{
	bool is_mk_or_func = index->def->key_def->is_multikey ||
			     index->def->key_def->for_func_index;
	(void)is_mk_or_func;
	struct memtx_index_replace_result_iterator it;
	memtx_index_replace_result_iterator_create(&it, result);
	struct memtx_index_replace_step step;
	while (memtx_index_replace_result_iterator_next(&it, &step)) {
		struct tuple *removed;
		VERIFY(memtx_index_replace_entry(index, step.inserted->entry,
						 step.replaced->entry,
						 DUP_INSERT, &removed) == 0);
		memtx_index_replaced_entry_cleanup(index,
						   &step.replaced->entry);
		assert(step.inserted->entry.tuple == removed ||
		       (removed == NULL && is_mk_or_func));
	}
}

static struct memtx_index_replace_result *
memtx_index_replace_result_new(struct rlist *list)
{
	struct memtx_index_replace_result *result =
		xregion_alloc_object(&fiber()->gc, typeof(*result));
	result->entry = memtx_index_entry_null;
	rlist_add_tail(list, &result->link);
	return result;
}

/**
 * Allocate the three result records for one logical replace step.
 *
 * This is the sole allocation path for a complete step. Records are allocated
 * on the caller-owned region and remain valid until that region is truncated.
 */
static struct memtx_index_replace_step
memtx_index_replace_step_new(struct memtx_index_replace_result_set *result)
{
	struct memtx_index_replace_step step;
	step.replaced = memtx_index_replace_result_new(&result->replaced);
	step.successor = memtx_index_replace_result_new(&result->successors);
	step.inserted = memtx_index_replace_result_new(&result->inserted);
	return step;
}

/**
 * Resolve two keys of the new tuple that identify the same index entry.
 *
 * The later step replaces an entry inserted by an earlier step. Keep the later
 * inserted entry, transfer the entry replaced by the earlier step and its
 * successor to the later step, and null the earlier step. This preserves one
 * externally visible replace transition and leaves rollback positional.
 */
static void
memtx_index_replace_resolve_multikey_conflict(
	struct memtx_index_replace_result_set *result,
	struct memtx_index_replace_step *step, hint_t old_hint)
{
	struct memtx_index_replace_result_iterator it;
	memtx_index_replace_result_iterator_create(&it, result);
	struct memtx_index_replace_step old_step;
	while (memtx_index_replace_result_iterator_next(&it, &old_step)) {
		if (old_step.inserted->entry.hint == old_hint) {
			step->replaced->entry = old_step.replaced->entry;
			step->successor->entry = old_step.successor->entry;
			old_step.inserted->entry = memtx_index_entry_null;
			old_step.replaced->entry = memtx_index_entry_null;
			old_step.successor->entry = memtx_index_entry_null;
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
			     enum dup_replace_mode mode,
			     struct memtx_index_replace_result_set *result)
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
			struct memtx_index_replace_step step =
				memtx_index_replace_step_new(result);
			if (tuple_key_is_excluded(new_tuple,
						  index->def->key_def,
						  (int)mk_idx))
				continue;
			if (memtx_index_replace_impl(
					index, old_tuple, new_entry, mode,
					&step.replaced->entry,
					&step.successor->entry) != 0)
				goto rollback;
			step.inserted->entry = new_entry;
			if (step.replaced->entry.tuple == new_entry.tuple) {
				memtx_index_replace_resolve_multikey_conflict(
					result, &step,
					step.replaced->entry.hint);
			}
		}
	}
	if (old_tuple == NULL)
		return 0;
	uint32_t mk_count = tuple_multikey_count(old_tuple, cmp_def);
	for (size_t mk_idx = 0; mk_idx < mk_count; ++mk_idx) {
		old_entry.hint = mk_idx;
		struct memtx_index_replace_step step =
			memtx_index_replace_step_new(result);
		if (tuple_key_is_excluded(old_tuple, index->def->key_def,
					  (int)mk_idx))
			continue;
		if (memtx_index_delete_impl(index, old_entry,
					    &step.replaced->entry) != 0)
			goto rollback;
	}
	return 0;
rollback:
	memtx_index_replace_rollback(index, result);
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
			 struct memtx_index_replace_result_set *result)
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
			struct memtx_index_replace_step step =
				memtx_index_replace_step_new(result);
			if (tuple_key_is_excluded(key, key_def, MULTIKEY_NONE))
				continue;
			new_entry.hint = (uint64_t)key;
			err = memtx_index_replace_impl(index, old_tuple,
						       new_entry, mode,
						       &step.replaced->entry,
						       &step.successor->entry);
			if (err != 0)
				break;
			step.inserted->entry = new_entry;
			tuple_ref(key);
			if (step.replaced->entry.tuple == new_entry.tuple) {
				hint_t old_hint = step.replaced->entry.hint;
				tuple_unref((struct tuple *)old_hint);
				memtx_index_replace_resolve_multikey_conflict(
					result, &step, old_hint);
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
		struct memtx_index_replace_step step =
			memtx_index_replace_step_new(result);
		if (memtx_index_delete_impl(index, old_entry,
					    &step.replaced->entry) != 0)
			goto rollback;
	}
	assert(key == NULL);
	return 0;
rollback:
	memtx_index_replace_rollback(index, result);
	return -1;
}

struct tuple *
memtx_index_replace_result_list_to_tuple(struct rlist *result_list)
{
	assert(!rlist_empty(result_list));
	struct memtx_index_replace_result *result =
		rlist_first_entry(result_list,
				  struct memtx_index_replace_result,
				  link);
	assert(rlist_next(&result->link) == result_list);
	return result->entry.tuple;
}

/** Build a regular-index entry, dropping NULL or excluded tuples. */
static struct memtx_index_entry
memtx_index_replace_regular_prepare_entry(struct index *index,
					  struct tuple *tuple)
{
	if (tuple == NULL || tuple_key_is_excluded(tuple, index->def->key_def,
						   MULTIKEY_NONE))
		return memtx_index_entry_null;
	return (struct memtx_index_entry) {
		.tuple = tuple,
		.hint = HINT_NONE,
	};
}

/**
 * Replace at most one regular-index entry and record one result step.
 */
static int
memtx_index_replace_regular(struct index *index, struct tuple *old_tuple,
			    struct tuple *new_tuple,
			    enum dup_replace_mode mode,
			    struct memtx_index_replace_result_set *result)
{
	struct memtx_index_entry old_entry =
		memtx_index_replace_regular_prepare_entry(index, old_tuple);
	struct memtx_index_entry new_entry =
		memtx_index_replace_regular_prepare_entry(index, new_tuple);
	struct memtx_index_replace_step step =
		memtx_index_replace_step_new(result);
	step.inserted->entry = new_entry;
	int rc = memtx_index_replace_entry_impl(index, old_entry, new_entry,
						mode, &step.replaced->entry,
						&step.successor->entry);
	step.replaced->entry.hint = HINT_NONE;
	step.successor->entry.hint = HINT_NONE;
	return rc;
}

/** Release resources retained by a successful replace-result set. */
void
memtx_index_replace_result_set_cleanup(
	struct index *index,
	struct memtx_index_replace_result_set *replace_result)
{
	if (!index->def->key_def->for_func_index)
		return;
	struct memtx_index_replace_result_iterator it;
	memtx_index_replace_result_iterator_create(&it, replace_result);
	struct memtx_index_replace_step step;
	while (memtx_index_replace_result_iterator_next(&it, &step)) {
		struct memtx_index_entry *replaced = &step.replaced->entry;
		memtx_index_replaced_entry_cleanup(index, replaced);
	}
}

int
memtx_index_replace_with_results(struct index *index, struct tuple *old_tuple,
				 struct tuple *new_tuple,
				 enum dup_replace_mode mode,
				 struct memtx_index_replace_result_set *result)
{
	memtx_index_replace_result_set_create(result);
	if (index->def->key_def->is_multikey)
		return memtx_index_replace_multikey(index, old_tuple, new_tuple,
						    mode, result);
	if (index->def->key_def->for_func_index)
		return memtx_index_replace_func(index, old_tuple, new_tuple,
						mode, result);
	return memtx_index_replace_regular(index, old_tuple, new_tuple, mode,
					   result);
}

int
memtx_index_replace_with_single_result(struct index *index,
				       struct tuple *old_tuple,
				       struct tuple *new_tuple,
				       enum dup_replace_mode mode,
				       struct tuple **result)
{
	struct memtx_index_replace_result_set replace_result;
	size_t region_svp = region_used(&fiber()->gc);
	int rc = memtx_index_replace_with_results(index, old_tuple, new_tuple,
						  mode, &replace_result);
	if (result != NULL) {
		assert(!index->def->key_def->is_multikey &&
		       !index->def->key_def->for_func_index);
		*result = memtx_index_replace_result_list_to_tuple(
			&replace_result.replaced);
	}
	memtx_index_replace_result_set_cleanup(index, &replace_result);
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
	return memtx_index_replace(index, NULL, tuple, DUP_INSERT);
}

void
generic_memtx_index_end_build(struct index *index)
{
	(void)index;
}
