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
#include "vy_cache.h"
#include "diag.h"

#ifndef CT_ASSERT_G
#define CT_ASSERT_G(e) typedef char CONCAT(__ct_assert_, __LINE__)[(e) ? 1 :-1]
#endif

CT_ASSERT_G(BOX_INDEX_PART_MAX <= UINT8_MAX);

enum {
	/* Flag in cache entry that means that there are no values in DB
	 * that less than the current and greater than the previous */
	VY_CACHE_LEFT_LINKED = 1,
	/* Flag in cache entry that means that there are no values in DB
	 * that greater than the current and less than the previous */
	VY_CACHE_RIGHT_LINKED = 2,
	/* Max number of deletes that are made by cleanup action per one
	 * cache operation */
	VY_CACHE_CLEANUP_MAX_STEPS = 10,
};

void
vy_cache_env_create(struct vy_cache_env *e, struct slab_cache *slab_cache,
		    uint64_t mem_quota)
{
	rlist_create(&e->cache_lru);
	vy_quota_init(&e->quota, NULL, NULL, NULL);
	vy_quota_set_limit(&e->quota, mem_quota);
	mempool_create(&e->cache_entry_mempool, slab_cache,
		       sizeof(struct vy_cache_entry));
	e->cached_count = 0;
}

void
vy_cache_env_destroy(struct vy_cache_env *e)
{
	mempool_destroy(&e->cache_entry_mempool);
}

static struct vy_cache_entry *
vy_cache_entry_new(struct vy_cache_env *env, struct vy_cache *cache,
		   struct tuple *stmt)
{
	struct vy_cache_entry *entry = (struct vy_cache_entry *)
		mempool_alloc(&env->cache_entry_mempool);
	if (entry == NULL)
		return NULL;
	tuple_ref(stmt);
	entry->cache = cache;
	entry->stmt = stmt;
	entry->flags = 0;
	entry->left_boundary_level = cache->key_def->part_count;
	entry->right_boundary_level = cache->key_def->part_count;
	rlist_add(&env->cache_lru, &entry->in_lru);
	size_t use = sizeof(struct vy_cache_entry) + tuple_size(stmt);
	vy_quota_force_use(&env->quota, use);
	env->cached_count++;
	return entry;
}

static void
vy_cache_entry_delete(struct vy_cache_env *env, struct vy_cache_entry *entry)
{
	struct tuple *stmt = entry->stmt;
	size_t put = sizeof(struct vy_cache_entry) + tuple_size(stmt);
	env->cached_count--;
	vy_quota_release(&env->quota, put);
	tuple_unref(stmt);
	rlist_del(&entry->in_lru);
	TRASH(entry);
	mempool_free(&env->cache_entry_mempool, entry);
}

static void *
vy_cache_tree_page_alloc(void *ctx)
{
	struct vy_env *env = (struct vy_env *)ctx;
	(void)env;
	void *ret = malloc(VY_CACHE_TREE_EXTENT_SIZE);
	if (ret == NULL)
		diag_set(OutOfMemory, VY_CACHE_TREE_EXTENT_SIZE, "malloc",
			 "ret");
	return ret;
}

static void
vy_cache_tree_page_free(void *ctx, void *p)
{
	struct vy_env *env = (struct vy_env *)ctx;
	(void)env;
	free(p);
}

void
vy_cache_create(struct vy_cache *cache, struct vy_cache_env *env,
		struct key_def *key_def)
{
	cache->env = env;
	cache->key_def = key_def;
	cache->version = 1;
	vy_cache_tree_create(&cache->cache_tree, key_def,
			     vy_cache_tree_page_alloc,
			     vy_cache_tree_page_free, env);
}

void
vy_cache_destroy(struct vy_cache *cache)
{
	struct vy_cache_tree_iterator itr =
		vy_cache_tree_iterator_first(&cache->cache_tree);
	while (!vy_cache_tree_iterator_is_invalid(&itr)) {
		struct vy_cache_entry **entry =
			vy_cache_tree_iterator_get_elem(&cache->cache_tree,
							&itr);
		assert(entry != NULL && *entry != NULL);
		vy_cache_entry_delete(cache->env, *entry);
		vy_cache_tree_iterator_next(&cache->cache_tree, &itr);
	}
	vy_cache_tree_destroy(&cache->cache_tree);
}

static void
vy_cache_gc_step(struct vy_cache_env *env)
{
	struct rlist *lru = &env->cache_lru;
	struct vy_cache_entry *entry =
	rlist_last_entry(lru, struct vy_cache_entry, in_lru);
	struct vy_cache *cache = entry->cache;
	struct vy_cache_tree *tree = &cache->cache_tree;
	if (entry->flags & (VY_CACHE_LEFT_LINKED |
			    VY_CACHE_RIGHT_LINKED)) {
		bool exact;
		struct vy_cache_tree_iterator itr =
			vy_cache_tree_lower_bound(tree, entry->stmt,
						  &exact);
		assert(exact);
		if (entry->flags & VY_CACHE_LEFT_LINKED) {
			struct vy_cache_tree_iterator prev = itr;
			vy_cache_tree_iterator_prev(tree, &prev);
			struct vy_cache_entry **prev_entry =
				vy_cache_tree_iterator_get_elem(tree, &prev);
			assert((*prev_entry)->flags & VY_CACHE_RIGHT_LINKED);
			(*prev_entry)->flags &= ~VY_CACHE_RIGHT_LINKED;
		}
		if (entry->flags & VY_CACHE_RIGHT_LINKED) {
			struct vy_cache_tree_iterator next = itr;
			vy_cache_tree_iterator_next(&cache->cache_tree,
						    &next);
			struct vy_cache_entry **next_entry =
				vy_cache_tree_iterator_get_elem(tree, &next);
			assert((*next_entry)->flags & VY_CACHE_LEFT_LINKED);
			(*next_entry)->flags &= ~VY_CACHE_LEFT_LINKED;
		}
	}
	cache->version++;
	vy_cache_tree_delete(&cache->cache_tree, entry);
	vy_cache_entry_delete(cache->env, entry);
}

static void
vy_cache_gc(struct vy_cache_env *env)
{
	struct vy_quota *q = &env->quota;
	for (uint32_t i = 0;
	     vy_quota_is_exceeded(q) && i < VY_CACHE_CLEANUP_MAX_STEPS;
	     i++) {
		vy_cache_gc_step(env);
	}
}

void
vy_cache_add(struct vy_cache *cache, struct tuple *stmt,
	     struct tuple *prev_stmt, const struct tuple *key,
	     enum iterator_type order)
{
	/* Delete some entries if quota overused */
	vy_cache_gc(cache->env);

	if (stmt != NULL && vy_stmt_lsn(stmt) == INT64_MAX) {
		/* Do not store a statement from write set of a tx */
		return;
	}

	/* The case of the first or the last result in key+order query */
	bool is_boundary = (stmt != NULL) != (prev_stmt != NULL);

	if (prev_stmt != NULL && vy_stmt_lsn(prev_stmt) == INT64_MAX) {
		/* Previous statement is from tx write set, can't store it */
		prev_stmt = NULL;
	}

	if (prev_stmt == NULL && stmt == NULL) {
		/* Do not store empty ranges */
		return;
	}

	int direction = iterator_direction(order);
	/**
	 * Let's determine boundary_level (left/right) of the new record
	 * in cache to be inserted.
	 */
	uint8_t boundary_level = cache->key_def->part_count;
	if (stmt != NULL) {
		if (is_boundary) {
			/**
			 * That means that the stmt is the first in a result.
			 * Regardless of order, the statement is the first in
			 * sequence of statements that is equal to the key.
			 */
			boundary_level = tuple_field_count(key);
		}
	} else {
		assert(prev_stmt != NULL);
		if (order == ITER_EQ) {
			/* that is the last statement that is equal to key */
			boundary_level = tuple_field_count(key);
		} else {
			/* that is the last statement */
			boundary_level = 0;
		}
		/**
		 * That means that the search was ended, and prev_stmt was
		 * the last statement of the result. It is equivalent to
		 * first found statement with a reverse order. Let's transform
		 * to the equivalent case in order of further simplification.
		 */
		direction = -direction;
		stmt = prev_stmt;
		prev_stmt = NULL;
	}
	TRASH(&order);

	assert(vy_stmt_type(stmt) == IPROTO_REPLACE);
	assert(prev_stmt == NULL || vy_stmt_type(prev_stmt) == IPROTO_REPLACE);
	cache->version++;

	/* Insert/replace new entry to the tree */
	struct vy_cache_entry *entry =
		vy_cache_entry_new(cache->env, cache, stmt);
	if (entry == NULL) {
		/* memory error, let's live without a cache */
		return;
	}
	struct vy_cache_entry *replaced = NULL;
	struct vy_cache_tree_iterator inserted;
	if (vy_cache_tree_insert_get_iterator(&cache->cache_tree, entry,
					      &replaced, &inserted) != 0) {
		/* memory error, let's live without a cache */
		vy_cache_entry_delete(cache->env, entry);
		return;
	}
	if (replaced != NULL) {
		entry->flags = replaced->flags;
		entry->left_boundary_level = replaced->left_boundary_level;
		entry->right_boundary_level = replaced->right_boundary_level;
		vy_cache_entry_delete(cache->env, replaced);
	}
	if (direction > 0 && boundary_level < entry->left_boundary_level)
		entry->left_boundary_level = boundary_level;
	else if (direction < 0 && boundary_level < entry->right_boundary_level)
		entry->right_boundary_level = boundary_level;

	/* Done if it's not a chain */
	if (prev_stmt == NULL)
		return;

	/* The flag that must be set in the inserted chain entry */
	uint32_t flag = direction > 0 ? VY_CACHE_LEFT_LINKED :
			VY_CACHE_RIGHT_LINKED;
	if (entry->flags & flag)
		return;

#ifndef NDEBUG
	assert(!vy_cache_tree_iterator_is_invalid(&inserted));
	if (direction > 0) {
		vy_cache_tree_iterator_prev(&cache->cache_tree, &inserted);
	} else {
		vy_cache_tree_iterator_next(&cache->cache_tree, &inserted);
	}
	/* Check that there are not statements between prev_stmt and stmt */
	if (!vy_cache_tree_iterator_is_invalid(&inserted)) {
		struct vy_cache_entry **prev_check_entry =
			vy_cache_tree_iterator_get_elem(&cache->cache_tree,
							&inserted);
		assert(*prev_check_entry != NULL);
		struct tuple *prev_check_stmt = (*prev_check_entry)->stmt;
		if (vy_stmt_compare(prev_stmt, prev_check_stmt,
		    cache->key_def) != 0)
			/* Failed to build chain. */
			unreachable();
	}
#endif

	/* Insert/replace entry with previous statement */
	struct vy_cache_entry *prev_entry =
		vy_cache_entry_new(cache->env, cache, prev_stmt);
	if (prev_entry == NULL) {
		/* memory error, let's live without a chain */
		return;
	}
	replaced = NULL;
	if (vy_cache_tree_insert(&cache->cache_tree, prev_entry, &replaced)) {
		/* memory error, let's live without a chain */
		vy_cache_entry_delete(cache->env, prev_entry);
		return;
	}
	if (replaced != NULL) {
		prev_entry->flags = replaced->flags;
		prev_entry->left_boundary_level = replaced->left_boundary_level;
		prev_entry->right_boundary_level = replaced->right_boundary_level;
		vy_cache_entry_delete(cache->env, replaced);
	}

	/* Set proper flags */
	entry->flags |= flag;
	/* Set inverted flag in the previous entry */
	prev_entry->flags |= (VY_CACHE_LEFT_LINKED |
			      VY_CACHE_RIGHT_LINKED) ^ flag;
}

void
vy_cache_on_write(struct vy_cache *cache, const struct tuple *stmt,
		  struct tuple **deleted)
{
	vy_cache_gc(cache->env);
	bool exact = false;
	struct vy_cache_tree_iterator itr;
	itr = vy_cache_tree_lower_bound(&cache->cache_tree, stmt, &exact);
	struct vy_cache_entry **entry =
		vy_cache_tree_iterator_get_elem(&cache->cache_tree, &itr);
	/*
	 * There are three cases possible
	 * (1) there's a value in cache that is equal to stmt.
	 *   ('exact' == true, 'entry' points the equal value in cache)
	 * (2) there's no value in cache that is equal to stmt, and lower_bound
	 *   returned the next record.
	 *   ('exact' == false, 'entry' points to the equal value in cache)
	 * (3) there's no value in cache that is equal to stmt, and lower_bound
	 *   returned invalid iterator, so there's no bigger value.
	 *   ('exact' == false, 'entry' == NULL)
	 */

	if (vy_stmt_type(stmt) == IPROTO_DELETE && !exact) {
		/* there was nothing and there is nothing now */
		return;
	}

	struct vy_cache_tree_iterator prev = itr;
	vy_cache_tree_iterator_prev(&cache->cache_tree, &prev);
	struct vy_cache_entry **prev_entry =
		vy_cache_tree_iterator_get_elem(&cache->cache_tree, &prev);

	if (entry && ((*entry)->flags & VY_CACHE_LEFT_LINKED)) {
		cache->version++;
		(*entry)->flags &= ~VY_CACHE_LEFT_LINKED;
		assert((*prev_entry)->flags & VY_CACHE_RIGHT_LINKED);
		(*prev_entry)->flags &= ~VY_CACHE_RIGHT_LINKED;
	}
	if (prev_entry) {
		cache->version++;
		(*prev_entry)->right_boundary_level = cache->key_def->part_count;
	}

	struct vy_cache_tree_iterator next = itr;
	vy_cache_tree_iterator_next(&cache->cache_tree, &next);
	struct vy_cache_entry **next_entry =
		vy_cache_tree_iterator_get_elem(&cache->cache_tree, &next);

	if (exact && entry && ((*entry)->flags & VY_CACHE_RIGHT_LINKED)) {
		cache->version++;
		(*entry)->flags &= ~VY_CACHE_RIGHT_LINKED;
		assert((*next_entry)->flags & VY_CACHE_LEFT_LINKED);
		(*next_entry)->flags &= ~VY_CACHE_LEFT_LINKED;
	}
	if (entry && !exact) {
		cache->version++;
		(*entry)->left_boundary_level = cache->key_def->part_count;
	}

	if (exact) {
		cache->version++;
		struct vy_cache_entry *to_delete = *entry;
		assert(vy_stmt_type(to_delete->stmt) == IPROTO_REPLACE);
		if (deleted != NULL) {
			*deleted = to_delete->stmt;
			tuple_ref(to_delete->stmt);
		}
		vy_cache_tree_delete(&cache->cache_tree, to_delete);
		vy_cache_entry_delete(cache->env, to_delete);
	}
}

/**
 * Get a stmt by current position
 */
static struct tuple *
vy_cache_iterator_curr_stmt(struct vy_cache_iterator *itr)
{
	struct vy_cache_tree *tree = &itr->cache->cache_tree;
	struct vy_cache_entry **entry =
		vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);
	return entry ? (*entry)->stmt : NULL;
}

/**
 * Determine whether the merge iterator must be stopped or not.
 * That is made by examining flags of a cache record.
 *
 * @param itr - the iterator
 * @param entry - current record of the cache
 * @param stop - flag to set
 */
static void
vy_cache_iterator_is_stop(struct vy_cache_iterator *itr,
			  struct vy_cache_entry *entry, bool *stop)
{
	*stop = false;
	uint8_t key_level = tuple_field_count(itr->key);
	/* select{} is actually an EQ iterator with part_count == 0 */
	bool iter_is_eq = itr->iterator_type == ITER_EQ || key_level == 0;
	if (iterator_direction(itr->iterator_type) > 0) {
		if (entry->flags & VY_CACHE_LEFT_LINKED)
			*stop = true;
		if (iter_is_eq && entry->left_boundary_level <= key_level)
			*stop = true;
	} else {
		if (entry->flags & VY_CACHE_RIGHT_LINKED)
			*stop = true;
		if (iter_is_eq && entry->right_boundary_level <= key_level)
			*stop = true;
	}
}

/**
 * Determine whether the merge iterator must be stopped or not in case when
 * there are no more values in the cache for given key.
 * That is made by examining flags of the previous cache record.
 *
 * @param itr - the iterator
 * @param last_entry - the last record from previous step of the iterator
 * @param stop - flag to set
 */
static void
vy_cache_iterator_is_end_stop(struct vy_cache_iterator *itr,
			      struct vy_cache_entry *last_entry, bool *stop)
{
	struct vy_cache_entry *entry = last_entry;
	uint8_t key_level = tuple_field_count(itr->key);
	/* select{} is actually an EQ iterator with part_count == 0 */
	bool iter_is_eq = itr->iterator_type == ITER_EQ || key_level == 0;
	if (iterator_direction(itr->iterator_type) > 0) {
		if (entry->flags & VY_CACHE_RIGHT_LINKED)
			*stop = true;
		if (iter_is_eq && entry->right_boundary_level <= key_level)
			*stop = true;
	} else {
		if (entry->flags & VY_CACHE_LEFT_LINKED)
			*stop = true;
		if (iter_is_eq && entry->left_boundary_level <= key_level)
			*stop = true;
	}
}

/**
 * Find next (lower, older) record with the same key as current
 *
 */
static void
vy_cache_iterator_start(struct vy_cache_iterator *itr, struct tuple **ret,
			bool *stop)
{
	assert(!itr->search_started);
	assert(itr->curr_stmt == NULL);
	itr->stat->lookup_count++;
	*ret = NULL;
	*stop = false;
	itr->search_started = true;
	itr->version = itr->cache->version;
	struct vy_cache_tree *tree = &itr->cache->cache_tree;
	const struct tuple *key = itr->key;

	if (tuple_field_count(itr->key) > 0) {
		bool exact;
		itr->curr_pos = itr->iterator_type == ITER_EQ ||
				itr->iterator_type == ITER_GE ||
				itr->iterator_type == ITER_LT ?
				vy_cache_tree_lower_bound(tree, key, &exact) :
				vy_cache_tree_upper_bound(tree, key, &exact);
		if (itr->iterator_type == ITER_EQ && !exact)
			return;
	} else if (itr->iterator_type == ITER_LE) {
		itr->curr_pos = vy_cache_tree_invalid_iterator();
	} else {
		assert(itr->iterator_type == ITER_GE);
		itr->curr_pos = vy_cache_tree_iterator_first(tree);
	}

	if (itr->iterator_type == ITER_LT || itr->iterator_type == ITER_LE)
		vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
	if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
		return;

	struct vy_cache_entry **entry =
		vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);
	vy_cache_iterator_is_stop(itr, *entry, stop);
	struct tuple *candidate = (*entry)->stmt;

	while (vy_stmt_lsn(candidate) > (**itr->read_view).vlsn) {
		/* The cache stores the latest tuple of the key,
		 * but there could be earlier tuples in runs */
		*stop = false;
		if (iterator_direction(itr->iterator_type) > 0)
			vy_cache_tree_iterator_next(tree, &itr->curr_pos);
		else
			vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
		if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
			return;
		entry = vy_cache_tree_iterator_get_elem(tree,
							&itr->curr_pos);
		candidate = (*entry)->stmt;
		if (itr->iterator_type == ITER_EQ &&
		    vy_stmt_compare(key, candidate, itr->cache->key_def))
			return;
	}
	itr->curr_stmt = candidate;
	tuple_ref(itr->curr_stmt);
	*ret = itr->curr_stmt;
	return;
}

static void
vy_cache_iterator_restore_pos(struct vy_cache_iterator *itr,
			      bool *step_was_made)
{
	*step_was_made = false;
	struct vy_cache_tree *tree = &itr->cache->cache_tree;

	if (itr->version == itr->cache->version)
		return;
	itr->version = itr->cache->version;

	const struct tuple *stmt = vy_cache_iterator_curr_stmt(itr);
	if (stmt == itr->curr_stmt)
		return;

	bool exact;
	itr->curr_pos =
		vy_cache_tree_lower_bound(tree, itr->curr_stmt, &exact);
	if (exact)
		return;
	*step_was_made = true;

	tuple_unref(itr->curr_stmt);
	itr->curr_stmt = NULL;
	if (iterator_direction(itr->iterator_type) < 0)
		vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
	if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
		return;
	itr->curr_stmt = vy_cache_iterator_curr_stmt(itr);
	tuple_ref(itr->curr_stmt);
}

NODISCARD int
vy_cache_iterator_next_key(struct vy_stmt_iterator *vitr,
			   struct tuple **ret, bool *stop)
{
	assert(vitr->iface->next_key == vy_cache_iterator_next_key);
	*ret = NULL;
	*stop = false;
	struct vy_cache_iterator *itr = (struct vy_cache_iterator *) vitr;

	/* disable cache for errinj test - let it try to read from disk */
	ERROR_INJECT(ERRINJ_VY_READ_PAGE,
		     { itr->search_started = true; return 0; });
	ERROR_INJECT(ERRINJ_VY_READ_PAGE_TIMEOUT,
		     { itr->search_started = true; return 0; });

	if (!itr->search_started) {
		vy_cache_iterator_start(itr, ret, stop);
		return 0;
	}
	if (!itr->curr_stmt) /* End of search. */
		return 0;
	itr->stat->step_count++;

	struct vy_cache_tree *tree = &itr->cache->cache_tree;
	int dir = iterator_direction(itr->iterator_type);
	const struct tuple *key = itr->key;

	bool step_was_made;
	vy_cache_iterator_restore_pos(itr, &step_was_made);
	if (!itr->curr_stmt) /* End of search. */
		return 0;
	if (!step_was_made) {
		struct vy_cache_entry **entry =
			vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);

		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
		if (dir > 0)
			vy_cache_tree_iterator_next(tree, &itr->curr_pos);
		else
			vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
		if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos)) {
			vy_cache_iterator_is_end_stop(itr, *entry, stop);
			return 0;
		}
		itr->curr_stmt = vy_cache_iterator_curr_stmt(itr);
		tuple_ref(itr->curr_stmt);
	}

	if (itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(itr->key, itr->curr_stmt, itr->cache->key_def)) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
		struct vy_cache_entry **entry =
			vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);
		vy_cache_iterator_is_end_stop(itr, *entry, stop);
		return 0;
	}
	struct vy_cache_entry **entry =
		vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);
	vy_cache_iterator_is_stop(itr, *entry, stop);

	while (vy_stmt_lsn(itr->curr_stmt) > (**itr->read_view).vlsn) {
		/* The cache stores the latest tuple of the key,
		 * but there could be earlier tuples in runs */
		*stop = false;
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
		if (dir > 0)
			vy_cache_tree_iterator_next(tree, &itr->curr_pos);
		else
			vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
		if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
			return 0;
		entry = vy_cache_tree_iterator_get_elem(tree,
							&itr->curr_pos);
		struct tuple *stmt = (*entry)->stmt;
		if (itr->iterator_type == ITER_EQ &&
		    vy_stmt_compare(key, stmt, itr->cache->key_def))
			return 0;
		itr->curr_stmt = stmt;
		tuple_ref(itr->curr_stmt);
	}
	*ret = itr->curr_stmt;
	return 0;
}

NODISCARD int
vy_cache_iterator_next_lsn(struct vy_stmt_iterator *vitr, struct tuple **ret)
{
	(void)vitr;
	assert(vitr->iface->next_lsn == vy_cache_iterator_next_lsn);
	/* next_key must return REPLACE, so there's no need of next_lsn */
	assert(!((struct vy_cache_iterator *) vitr)->search_started);
	*ret = NULL;
	return 0;
}

/**
 * Restore the current position (if necessary).
 * @sa struct vy_stmt_iterator comments.
 *
 * @param last_stmt the key the iterator was positioned on
 *
 * @retval 0 nothing changed
 * @retval 1 iterator position was changed
 */
int
vy_cache_iterator_restore(struct vy_stmt_iterator *vitr,
			  const struct tuple *last_stmt, struct tuple **ret,
			  bool *stop)
{
	assert(vitr->iface->restore == vy_cache_iterator_restore);
	*ret = NULL;
	struct vy_cache_iterator *itr = (struct vy_cache_iterator *) vitr;

	/* disable cache for errinj test - let it try to read from disk */
	ERROR_INJECT(ERRINJ_VY_READ_PAGE,
		     { itr->search_started = true; return 0; });
	ERROR_INJECT(ERRINJ_VY_READ_PAGE_TIMEOUT,
		     { itr->search_started = true; return 0; });

	struct key_def *def = itr->cache->key_def;
	struct vy_cache_tree *tree = &itr->cache->cache_tree;
	int dir = iterator_direction(itr->iterator_type);
	if (itr->curr_stmt != NULL) {
		bool step_was_made;
		vy_cache_iterator_restore_pos(itr, &step_was_made);
	}

	if (itr->search_started) {
		if (itr->curr_stmt == NULL)
			return 0;
		int rc = 0;
		struct vy_cache_tree_iterator pos = itr->curr_pos;
		if (last_stmt == NULL && itr->curr_stmt != NULL) {
			struct vy_cache_entry **entry =
				vy_cache_tree_iterator_get_elem(tree, &pos);
			assert(entry != NULL);
			vy_cache_iterator_is_stop(itr, *entry, stop);
			*ret = itr->curr_stmt;
			return 0;
		}
		if (itr->version == itr->cache->version) {
			*ret = itr->curr_stmt;
			return 0;
		}
		while (true) {
			if (dir > 0)
				vy_cache_tree_iterator_prev(tree, &pos);
			else
				vy_cache_tree_iterator_next(tree, &pos);
			if (vy_cache_tree_iterator_is_invalid(&pos))
				break;
			struct vy_cache_entry **entry =
				vy_cache_tree_iterator_get_elem(tree, &pos);
			assert(*entry && (*entry)->stmt);
			struct tuple *t = (*entry)->stmt;
			int cmp = dir * vy_stmt_compare(t, last_stmt, def);
			if (cmp < 0)
				break;
			if (vy_stmt_lsn(t) <= (**itr->read_view).vlsn) {
				if (itr->curr_stmt != NULL)
					tuple_unref(itr->curr_stmt);
				itr->curr_pos = pos;
				itr->curr_stmt = t;
				tuple_ref(itr->curr_stmt);
				rc = 1;
				vy_cache_iterator_is_stop(itr, *entry, stop);
			}
		}
		*ret = itr->curr_stmt;
		return rc;
	}

	assert(itr->curr_stmt == NULL);

	if (last_stmt == NULL) {
		vy_cache_iterator_start(itr, ret, stop);
		return *ret != NULL;
	}

	/*
	 * Restoration is very similar to first search so we'll use
	 * that.
	 */
	enum iterator_type save_type = itr->iterator_type;
	const struct tuple *save_key = itr->key;
	itr->iterator_type = dir > 0 ? ITER_GE : ITER_LE;
	itr->key = last_stmt;
	vy_cache_iterator_start(itr, ret, stop);
	itr->iterator_type = save_type;
	itr->key = save_key;
	if (*ret == NULL) /* Search ended. */
		return 0;
	if (vy_stmt_compare(itr->curr_stmt, last_stmt, def) == 0) {
		int rc = vy_cache_iterator_next_key(vitr, ret, stop);
		assert(rc == 0);
		(void)rc;
	} else if (itr->iterator_type == ITER_EQ &&
		   vy_stmt_compare(itr->key, itr->curr_stmt, def) != 0) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
	}
	*ret = itr->curr_stmt;
	return itr->curr_stmt != NULL;
}

/**
 * Close the iterator and free resources.
 */
void
vy_cache_iterator_close(struct vy_stmt_iterator *vitr)
{
	assert(vitr->iface->close == vy_cache_iterator_close);
	struct vy_cache_iterator *itr = (struct vy_cache_iterator *) vitr;
	if (itr->curr_stmt != NULL) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
	}
	TRASH(itr);
}

static struct vy_stmt_iterator_iface vy_cache_iterator_iface = {
	.next_key = vy_cache_iterator_next_key,
	.next_lsn = vy_cache_iterator_next_lsn,
	.restore = vy_cache_iterator_restore,
	.cleanup = NULL,
	.close = vy_cache_iterator_close,
};

void
vy_cache_iterator_open(struct vy_cache_iterator *itr,
		       struct vy_iterator_stat *stat, struct vy_cache *cache,
		       enum iterator_type iterator_type,
		       const struct tuple *key, const struct vy_read_view **rv)
{
	itr->base.iface = &vy_cache_iterator_iface;
	itr->stat = stat;

	itr->cache = cache;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;
	if (tuple_field_count(key) == 0) {
		/* NULL key. change itr->iterator_type for simplification */
		itr->iterator_type = iterator_type == ITER_LT ||
				     iterator_type == ITER_LE ?
				     ITER_LE : ITER_GE;
	}

	itr->curr_stmt = NULL;
	itr->curr_pos = vy_cache_tree_invalid_iterator();

	itr->version = 0;
	itr->search_started = false;
}
