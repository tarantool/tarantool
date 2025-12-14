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
memtx_index_replace_regular(struct index *index, struct memtx_index_key old_key,
			    struct memtx_index_key new_key,
			    enum dup_replace_mode mode,
			    struct memtx_index_key *result,
			    struct memtx_index_key *successor)
{
	struct memtx_index_vtab *vtab = (struct memtx_index_vtab *)index->vtab;
	return vtab->replace(index, old_key, new_key, mode, result, successor);
}

/**
 * Rollback the sequence of memtx_tree_index_replace_multikey_one
 * insertions with multikey indexes [0, new_tuple_err_mk_idx - 1]
 * where the err_multikey_idx is the first multikey index where
 * error has been raised.
 *
 * This routine can't fail because all replaced_tuple (when
 * specified) nodes in tree are already allocated (they might be
 * overridden with new_tuple, but they definitely present) and
 * delete operation is fault-tolerant.
 */
static void
memtx_index_replace_multikey_rollback(struct index *index,
				      struct tuple *new_tuple,
				      uint32_t new_tuple_err_mk_idx,
				      struct tuple *replaced)
{
	struct key_def *cmp_def = index->def->cmp_def;
	struct memtx_index_key key = {
		.tuple = new_tuple,
		.hint = MULTIKEY_NONE,
	};
	struct memtx_index_key unused;
	if (new_tuple != NULL) {
		/*
		 * Rollback new_tuple insertion by multikey index
		 * [0, multikey_idx).
		 */
		for (; new_tuple_err_mk_idx > 0; --new_tuple_err_mk_idx) {
			key.hint = new_tuple_err_mk_idx - 1;
			VERIFY(memtx_index_replace_regular(index, key,
							   memtx_index_key_null,
							   DUP_INSERT, &unused,
							   &unused) == 0);
		}
	}
	if (replaced == NULL)
		return;
	/* Restore replaced tuple index occurrences. */
	key.tuple = replaced;
	uint32_t mk_count = tuple_multikey_count(replaced, cmp_def);
	for (uint32_t mk_idx = 0; mk_idx < mk_count; ++mk_idx) {
		key.hint = mk_idx;
		VERIFY(memtx_index_replace_regular(index, memtx_index_key_null,
						   key, DUP_INSERT, &unused,
						   &unused) == 0);
	}
}

/**
 * :replace() function for a multikey index: replace old tuple
 * index entries with ones from the new tuple.
 *
 * In a multikey index a single tuple is associated with 0..N keys
 * of the b+*tree. Imagine old tuple key set is called "old_keys"
 * and a new tuple set is called "new_keys". This function must
 * 1) delete all removed keys: (new_keys - old_keys)
 * 2) update tuple pointer in all preserved keys: (old_keys ^ new_keys)
 * 3) insert data for all new keys (new_keys - old_keys).
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
 * set of the old and the new tuple, we don't using key parts alone
 * to compare - we also look at b+* tree value that has the tuple
 * pointer, and delete old tuple entries only.
 */
static int
memtx_index_replace_multikey(struct index *index, struct tuple *old_tuple,
			     struct tuple *new_tuple,
			     enum dup_replace_mode mode, struct tuple **result)
{
	struct key_def *cmp_def = index->def->cmp_def;
	uint32_t new_tuple_mk_idx = 0;
	struct memtx_index_key old_key = {
		.tuple = old_tuple,
		.hint = MULTIKEY_NONE,
	};
	struct memtx_index_key new_key = {
		.tuple = new_tuple,
		.hint = MULTIKEY_NONE,
	};
	*result = NULL;
	struct memtx_index_key unused;

	if (new_tuple != NULL) {
		uint32_t mk_count = tuple_multikey_count(new_tuple, cmp_def);
		for (new_tuple_mk_idx = 0; new_tuple_mk_idx < mk_count;
		     ++new_tuple_mk_idx) {
			new_key.hint = new_tuple_mk_idx;
			struct memtx_index_key replaced;
			if (memtx_index_replace_regular(index, old_key, new_key,
							mode, &replaced,
							&unused) != 0) {
				memtx_index_replace_multikey_rollback(
					index, new_tuple, new_tuple_mk_idx,
					*result);
				return -1;
			}
			assert(replaced.tuple == NULL ||
			       replaced.tuple == old_tuple);
			if (replaced.tuple != NULL) {
				assert(*result == NULL ||
				       *result == replaced.tuple);
				*result = replaced.tuple;
			}
		}
		assert(*result == NULL || old_tuple == *result);
	}
	if (old_tuple == NULL)
		return 0;
	uint32_t mk_count = tuple_multikey_count(old_tuple, cmp_def);
	for (size_t old_tuple_mk_idx = 0; old_tuple_mk_idx < mk_count;
	     ++old_tuple_mk_idx) {
		old_key.hint = old_tuple_mk_idx;
		if (memtx_index_replace_regular(index, old_key,
						memtx_index_key_null,
						DUP_INSERT, &unused,
						&unused) != 0) {
			memtx_index_replace_multikey_rollback(index, new_tuple,
							      new_tuple_mk_idx,
							      old_tuple);
			return -1;
		}
	}
	*result = old_tuple;
	return 0;
}

/**
 * An undo entry for multikey functional index replace operation.
 * Used to roll back a failed insert/replace and restore the
 * original key_hint(s) and to commit a completed insert/replace
 * and destruct old tuple key_hint(s).
 */
struct func_key_undo {
	/** A link to organize entries in list. */
	struct rlist link;
	/** An inserted record copy. */
	struct memtx_index_key key;
};

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
			 struct tuple **result, struct tuple **successor)
{
	struct memtx_engine *memtx = (struct memtx_engine *)index->engine;
	struct index_def *index_def = index->def;
	assert(index_def->key_def->for_func_index);
	/* Make sure that key_def is not multikey - we rely on it below. */
	assert(!index_def->key_def->is_multikey);

	int rc = -1;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct key_list_iterator it;
	struct rlist old_keys, new_keys;
	rlist_create(&old_keys);
	rlist_create(&new_keys);
	struct memtx_index_key unused;
	struct func_key_undo *entry;

	struct memtx_index_key old_key = {
		.tuple = old_tuple,
		.hint = MULTIKEY_NONE,
	};
	struct memtx_index_key new_key = {
		.tuple = new_tuple,
		.hint = MULTIKEY_NONE,
	};
	*result = NULL;
	if (new_tuple != NULL) {
		if (key_list_iterator_create(&it, new_tuple, index_def, true,
					     memtx->func_key_format) != 0)
			goto end;
		int err = 0;
		struct tuple *key;
		struct func_key_undo *undo;
		struct key_def *key_def = index_def->key_def;
		while ((err = key_list_iterator_next(&it, &key)) == 0 &&
		       key != NULL) {
			/* Save functional key to MVCC, even excluded one. */
			memtx_tx_save_func_key(new_tuple, index, key);
			if (tuple_key_is_excluded(key, key_def, MULTIKEY_NONE))
				continue;
			new_key.hint = (uint64_t)key;
			struct memtx_index_key replaced;
			struct memtx_index_key successor_key;
			err = memtx_index_replace_regular(index, old_key,
							  new_key, mode,
							  &replaced,
							  &successor_key);
			if (err != 0)
				break;
			if (!it.func_is_multikey)
				*successor = successor_key.tuple;
			bool is_mk_conflict =
				replaced.tuple == new_key.tuple;
			undo = xregion_alloc_object(region, typeof(*undo));
			tuple_ref(key);
			undo->key.tuple = new_tuple;
			undo->key.hint = (uint64_t)key;
			rlist_add(&new_keys, &undo->link);
			if (replaced.tuple != NULL && !is_mk_conflict) {
				undo = xregion_alloc_object(region,
							    typeof(*undo));
				undo->key.tuple = replaced.tuple;
				undo->key.hint = replaced.hint;
				rlist_add(&old_keys, &undo->link);
				*result = replaced.tuple;
			} else if (is_mk_conflict) {
				/*
				 * Remove the replaced tuple undo
				 * from undo list.
				 */
				tuple_unref((struct tuple *)replaced.hint);
				rlist_foreach_entry(undo, &new_keys, link) {
					if (undo->key.hint == replaced.hint) {
						rlist_del(&undo->link);
						break;
					}
				}
			}
		}
		assert(key == NULL || err != 0);
		if (err != 0)
			goto rollback;
		if (*result != NULL) {
			assert(old_tuple == NULL || old_tuple == *result);
			old_tuple = *result;
			old_key.tuple = *result;
		}
	}
	if (old_tuple != NULL) {
		/*
		 * Use the runtime format to avoid OOM while deleting a tuple
		 * from a space. It's okay, because we are not going to store
		 * the keys in the index.
		 */
		if (key_list_iterator_create(&it, old_tuple, index_def, false,
					     tuple_format_runtime) != 0)
			goto end;
		struct tuple *key;
		while (key_list_iterator_next(&it, &key) == 0 && key != NULL) {
			old_key.hint = (uint64_t)key;
			struct memtx_index_key deleted;
			deleted.tuple = NULL;
			if (memtx_index_replace_regular(index, old_key,
							memtx_index_key_null,
							DUP_INSERT, &deleted,
							&unused) != 0)
				goto rollback;
			if (deleted.tuple != NULL) {
				struct func_key_undo *undo =
				xregion_alloc_object(region,
						     typeof(*undo));
				undo->key.tuple = deleted.tuple;
				undo->key.hint = deleted.hint;
				rlist_add(&old_keys, &undo->link);
			}
		}
		assert(key == NULL);
		*result = old_tuple;
	}
	/*
	 * Commit changes: release hints for
	 * replaced entries.
	 */
	struct func_key_undo *undo;
	rlist_foreach_entry(undo, &old_keys, link)
		tuple_unref((struct tuple *)undo->key.hint);
	rc = 0;
	goto end;
rollback:
	rlist_foreach_entry(entry, &new_keys, link) {
		VERIFY(memtx_index_replace_regular(index, entry->key,
						   memtx_index_key_null,
						   DUP_INSERT, &unused,
						   &unused) == 0);
		tuple_unref((struct tuple *)entry->key.hint);
	}
	rlist_foreach_entry(entry, &old_keys, link) {
		VERIFY(memtx_index_replace_regular(index, memtx_index_key_null,
						   entry->key, DUP_INSERT,
						   &unused, &unused) == 0);
	}
end:
	region_truncate(region, region_svp);
	return rc;
}

int
memtx_index_replace(struct index *index, struct tuple *old_tuple,
		    struct tuple *new_tuple, enum dup_replace_mode mode,
		    struct tuple **result, struct tuple **successor)
{
	int rc;
	if (index->def->key_def->is_multikey) {
		/* MUTLIKEY doesn't support successor for now. */
		*successor = NULL;
		return memtx_index_replace_multikey(index, old_tuple, new_tuple,
						    mode, result);
	}
	if (index->def->key_def->for_func_index) {
		/* Successor will be set only if function is not multikey. */
		*successor = NULL;
		return memtx_index_replace_func(index, old_tuple, new_tuple,
						mode, result, successor);
	}

	struct memtx_index_key old_key = {
		.tuple = old_tuple,
		.hint = MULTIKEY_NONE,
	};
	struct memtx_index_key new_key = {
		.tuple = new_tuple,
		.hint = MULTIKEY_NONE,
	};
	struct memtx_index_key result_key, successor_key;
	rc = memtx_index_replace_regular(index, old_key, new_key, mode,
					 &result_key, &successor_key);
	*result = result_key.tuple;
	*successor = successor_key.tuple;
	return rc;
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
