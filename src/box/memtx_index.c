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

static int
memtx_index_replace_impl(struct index *index,
			 struct memtx_index_entry old_entry,
			 struct memtx_index_entry new_entry,
			 enum dup_replace_mode mode,
			 struct memtx_index_entry *result,
			 struct memtx_index_entry *successor)
{
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	return vtab->replace(index, old_entry, new_entry, mode, result,
			     successor);
}

static void
memtx_index_replace_multikey_rollback(struct index *index,
				      struct rlist *inserted,
				      struct rlist *result)
{
	struct memtx_index_replace_result *replace_result;
	struct memtx_index_entry unused;
	rlist_foreach_entry(replace_result, inserted, link) {
		VERIFY(memtx_index_replace_impl(index, replace_result->entry,
						memtx_index_entry_null,
						DUP_INSERT, &unused,
						&unused) == 0);
		if (index->def->key_def->for_func_index &&
		    replace_result->entry.tuple != NULL)
			tuple_unref((struct tuple *)replace_result->entry.hint);
	}
	rlist_foreach_entry(replace_result, result, link)
		VERIFY(memtx_index_replace_impl(index, memtx_index_entry_null,
						replace_result->entry,
						DUP_INSERT, &unused,
						&unused) == 0);
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
			     struct rlist *result,
			     struct rlist *successor,
			     struct rlist *inserted)
{
	struct key_def *cmp_def = index->def->cmp_def;
	struct memtx_index_entry old_entry = {
		.tuple = old_tuple,
		.hint = MULTIKEY_NONE,
	};
	struct memtx_index_entry new_entry = {
		.tuple = new_tuple,
		.hint = MULTIKEY_NONE,
	};
	struct region *region = &fiber()->gc;
	struct memtx_index_entry unused;
	struct memtx_index_replace_result *replace_result;
	struct rlist replaced;
	rlist_create(&replaced);
	struct rlist deleted;
	rlist_create(&deleted);

	if (new_tuple != NULL) {
		uint32_t mk_count = tuple_multikey_count(new_tuple, cmp_def);
		for (size_t new_tuple_mk_idx = 0; new_tuple_mk_idx < mk_count;
		     ++new_tuple_mk_idx) {
			new_entry.hint = new_tuple_mk_idx;
			struct memtx_index_replace_result *replaced_result =
				xregion_alloc_object(region,
						     typeof(*replaced_result));
			rlist_add_tail(&replaced, &replaced_result->link);
			struct memtx_index_replace_result *successor_result =
				xregion_alloc_object(region,
						     typeof(*successor_result));
			rlist_add_tail(successor, &successor_result->link);
			replace_result =
				xregion_alloc_object(region,
						     typeof(*replace_result));
			replace_result->entry = memtx_index_entry_null;
			rlist_add_tail(inserted, &replace_result->link);
			replace_result->entry = new_entry;
			if (tuple_key_is_excluded(new_tuple,
						  index->def->key_def,
						  new_tuple_mk_idx)) {
				replaced_result->entry = memtx_index_entry_null;
				successor_result->entry =
					memtx_index_entry_null;
				replace_result->entry = memtx_index_entry_null;
				continue;
			}
			if (memtx_index_replace_impl(
					index, old_entry, new_entry, mode,
					&replaced_result->entry,
					&successor_result->entry) != 0)
				goto rollback;
		}
	}
	if (old_tuple == NULL) {
		rlist_splice(result, &replaced);
		return 0;
	}
	uint32_t mk_count = tuple_multikey_count(old_tuple, cmp_def);
	for (size_t old_tuple_mk_idx = 0; old_tuple_mk_idx < mk_count;
	     ++old_tuple_mk_idx) {
		old_entry.hint = old_tuple_mk_idx;
		struct memtx_index_replace_result *deleted_result =
			xregion_alloc_object(region, typeof(*deleted_result));
		rlist_add_tail(&deleted, &deleted_result->link);
		if (tuple_key_is_excluded(old_tuple, index->def->key_def,
					  old_tuple_mk_idx)) {
			deleted_result->entry = memtx_index_entry_null;
			continue;
		}
		if (memtx_index_replace_impl(index, old_entry,
					     memtx_index_entry_null,
					     DUP_INSERT, &deleted_result->entry,
					     &unused) != 0)
			goto rollback;
	}
	if (new_tuple == NULL)
		rlist_splice(result, &deleted);
	else
		rlist_splice(result, &replaced);
	return 0;
rollback:
	rlist_splice(&deleted, &replaced);
	memtx_index_replace_multikey_rollback(index, inserted, &deleted);
	return -1;
}

/**
 * Use the functional index function from the key definition
 * to build a key list. Then each returned key is reallocated in
 * engine's memory as key_hint object and is used as comparison
 * hint.
 * To release key_hint memory in case of replace failure
 * we use a list of undo records which are allocated on region.
 * It is used to restore the original b+* entries with their
 * original key_hint(s) pointers in case of failure and release
 * the now useless hints of old items in case of success.
 */
static int
memtx_index_replace_func(struct index *index, struct tuple *old_tuple,
			 struct tuple *new_tuple, enum dup_replace_mode mode,
			 struct rlist *result, struct rlist *successor,
			 struct rlist *inserted)
{
	struct memtx_engine *memtx = (struct memtx_engine *)index->engine;
	struct index_def *index_def = index->def;
	assert(index_def->key_def->for_func_index);
	/* Make sure that key_def is not multikey - we rely on it below. */
	assert(!index_def->key_def->is_multikey);

	struct region *region = &fiber()->gc;
	struct key_list_iterator it;
	struct memtx_index_entry unused;
	struct memtx_index_replace_result *replace_result;
	struct rlist replaced;
	rlist_create(&replaced);
	struct rlist deleted;
	rlist_create(&deleted);

	struct memtx_index_entry old_entry = {
		.tuple = old_tuple,
		.hint = MULTIKEY_NONE,
	};
	struct memtx_index_entry new_entry = {
		.tuple = new_tuple,
		.hint = MULTIKEY_NONE,
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
			struct memtx_index_replace_result *replaced_result =
				xregion_alloc_object(region,
						     typeof(*replaced_result));
			replaced_result->entry = memtx_index_entry_null;
			rlist_add_tail(&replaced, &replaced_result->link);
			struct memtx_index_replace_result *successor_result =
				xregion_alloc_object(region,
						     typeof(*successor_result));
			successor_result->entry = memtx_index_entry_null;
			rlist_add_tail(successor, &successor_result->link);
			struct memtx_index_replace_result *inserted_result =
				xregion_alloc_object(region,
						     typeof(*inserted_result));
			inserted_result->entry = memtx_index_entry_null;
			rlist_add_tail(inserted, &inserted_result->link);
			if (tuple_key_is_excluded(key, key_def, MULTIKEY_NONE))
				continue;
			new_entry.hint = (uint64_t)key;
			err = memtx_index_replace_impl(
				index, old_entry, new_entry, mode,
				&replaced_result->entry,
				&successor_result->entry);
			if (err != 0)
				break;
			bool is_mk_conflict =
				replaced_result->entry.tuple == new_entry.tuple;
			tuple_ref(key);
			inserted_result->entry.tuple = new_tuple;
			inserted_result->entry.hint = (uint64_t)key;
			if (is_mk_conflict) {
				/*
				 * Remove the replaced tuple entry
				 * from inserted, result and successor lists.
				 */
				hint_t hint = replaced_result->entry.hint;
				tuple_unref((struct tuple *)hint);
				replaced_result->entry = memtx_index_entry_null;

				replaced_result =
					rlist_first_entry(
						&replaced,
						struct memtx_index_replace_result,
						link);
				successor_result =
					rlist_first_entry(
						successor,
						struct memtx_index_replace_result,
						link);
				inserted_result =
					rlist_first_entry(
						inserted,
						struct memtx_index_replace_result,
						link);
				while (!rlist_entry_is_head(replaced_result,
							    &replaced,
							    link)) {
					assert(!rlist_entry_is_head(
						successor_result, successor,
						link));
					assert(!rlist_entry_is_head(
						inserted_result, inserted,
						link));
					if (inserted_result->entry.hint ==
					    hint) {
						inserted_result->entry =
							memtx_index_entry_null;
						replaced_result->entry =
							memtx_index_entry_null;
						successor_result->entry =
							memtx_index_entry_null;
						break;
					}
					replaced_result =
						rlist_next_entry(
							replaced_result, link);
					successor_result =
						rlist_next_entry(
							successor_result, link);
					inserted_result =
						rlist_next_entry(
							inserted_result, link);
				}
			}
		}
		assert(key == NULL || err != 0);
		if (err != 0)
			goto rollback;
	}
	if (old_tuple == NULL) {
		rlist_splice(result, &replaced);
		return 0;
	}
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
		old_entry.hint = (uint64_t)key;
		struct memtx_index_replace_result *deleted_result =
			xregion_alloc_object(region, typeof(*deleted_result));
		deleted_result->entry = memtx_index_entry_null;
		rlist_add_tail(&deleted, &deleted_result->link);
		if (memtx_index_replace_impl(index, old_entry,
					     memtx_index_entry_null, DUP_INSERT,
					     &deleted_result->entry,
					     &unused) != 0)
			goto rollback;
	}
	assert(key == NULL);
	struct rlist *unref;
	if (new_tuple == NULL) {
		rlist_splice(result, &deleted);
		unref = &replaced;
	} else {
		rlist_splice(result, &replaced);
		unref = &deleted;
	}
	rlist_foreach_entry(replace_result, unref, link)
		if (replace_result->entry.tuple != NULL)
			tuple_unref((struct tuple *)replace_result->entry.hint);
	return 0;
rollback:
	rlist_splice(&deleted, &replaced);
	memtx_index_replace_multikey_rollback(index, inserted, &deleted);
	return -1;
}

int
memtx_index_replace_with_results(struct index *index, struct tuple *old_tuple,
				 struct tuple *new_tuple,
				 enum dup_replace_mode mode,
				 struct rlist *result, struct rlist *successor,
				 struct rlist *inserted)
{
	rlist_create(result);
	rlist_create(successor);
	rlist_create(inserted);
	if (index->def->key_def->is_multikey)
		return memtx_index_replace_multikey(index, old_tuple, new_tuple,
						    mode, result, successor,
						    inserted);
	if (index->def->key_def->for_func_index)
		return memtx_index_replace_func(index, old_tuple, new_tuple,
						mode, result, successor,
						inserted);

	struct memtx_index_entry old_entry = {
		.tuple = old_tuple,
		.hint = MULTIKEY_NONE,
	};
	struct memtx_index_entry new_entry = {
		.tuple = new_tuple,
		.hint = MULTIKEY_NONE,
	};
	struct memtx_index_replace_result *replaced_result =
		xregion_alloc_object(&fiber()->gc, typeof(*replaced_result));
	rlist_add(result, &replaced_result->link);
	struct memtx_index_replace_result *successor_result =
		xregion_alloc_object(&fiber()->gc, typeof(*successor_result));
	rlist_add(successor, &successor_result->link);
	struct memtx_index_replace_result *inserted_result =
		xregion_alloc_object(&fiber()->gc, typeof(*inserted_result));
	rlist_add(inserted, &inserted_result->link);
	inserted_result->entry = new_entry;
	if (new_tuple != NULL) {
		if (tuple_key_is_excluded(new_tuple, index->def->key_def,
				      MULTIKEY_NONE)) {
			new_entry = memtx_index_entry_null;
			inserted_result->entry = memtx_index_entry_null;
		} else if (index->def->opts.hint == INDEX_HINT_ON) {
			new_entry.hint = tuple_hint(new_tuple,
						    index->def->cmp_def);
		}
	}
	if (old_tuple != NULL) {
		if (tuple_key_is_excluded(old_tuple, index->def->key_def,
				  MULTIKEY_NONE))
			old_entry = memtx_index_entry_null;
		else if (index->def->opts.hint == INDEX_HINT_ON)
			old_entry.hint = tuple_hint(old_tuple,
						    index->def->cmp_def);
	}

	int rc = memtx_index_replace_impl(index, old_entry, new_entry, mode,
					  &replaced_result->entry,
					  &successor_result->entry);
	replaced_result->entry.hint = HINT_NONE;
	successor_result->entry.hint = HINT_NONE;
	return rc;
}

int
memtx_index_replace_entry(struct index *index,
			  struct memtx_index_entry old_entry,
			  struct memtx_index_entry new_entry,
			  enum dup_replace_mode mode,
			  struct memtx_index_entry *result)
{
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	struct memtx_index_entry unused;
	if (vtab->replace_entry(index, old_entry, new_entry, mode, result,
				&unused) != 0)
		return -1;
	if (!index->def->key_def->is_multikey &&
	    !index->def->key_def->for_func_index)
		result->hint = HINT_NONE;
	else if (index->def->key_def->for_func_index &&
		new_entry.tuple != NULL) {
		 // (old_entry.tuple != result->tuple ||
		 //  old_entry.hint != result->hint)) {
		tuple_ref((struct tuple *)new_entry.hint);
		// tuple_unref((struct tuple *)result->hint);
	}

	return 0;
}

void
memtx_index_replace_cleanup_result_list(struct index *index,
					struct rlist *result_list)
{
	struct memtx_index_replace_result *result;
	if (index->def->key_def->for_func_index)
		rlist_foreach_entry(result, result_list, link)
			if (result->entry.tuple != NULL)
				tuple_unref((struct tuple *)result->entry.hint);
}

void
memtx_index_replace_rollback(struct index *index, struct tuple *new_tuple,
			     struct rlist *result)
{
	struct memtx_index_replace_result *replace_result;
	struct tuple *old_tuple = NULL;
	rlist_foreach_entry(replace_result, result, link)
		if (replace_result->entry.tuple != NULL) {
			old_tuple = replace_result->entry.tuple;
			break;
		}
	VERIFY(memtx_index_replace(index, new_tuple, old_tuple,
				   DUP_INSERT) == 0);
	memtx_index_replace_cleanup_result_list(index, result);
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
