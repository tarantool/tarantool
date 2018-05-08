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
#include "fiber.h"
#include "schema_def.h"
#include "vy_history.h"

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
vy_cache_env_create(struct vy_cache_env *e, struct slab_cache *slab_cache)
{
	rlist_create(&e->cache_lru);
	e->mem_used = 0;
	e->mem_quota = 0;
	mempool_create(&e->cache_entry_mempool, slab_cache,
		       sizeof(struct vy_cache_entry));
}

void
vy_cache_env_destroy(struct vy_cache_env *e)
{
	mempool_destroy(&e->cache_entry_mempool);
}

static inline size_t
vy_cache_entry_size(const struct vy_cache_entry *entry)
{
	return sizeof(*entry) + tuple_size(entry->stmt);
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
	entry->left_boundary_level = cache->cmp_def->part_count;
	entry->right_boundary_level = cache->cmp_def->part_count;
	rlist_add(&env->cache_lru, &entry->in_lru);
	env->mem_used += vy_cache_entry_size(entry);
	vy_stmt_counter_acct_tuple(&cache->stat.count, stmt);
	return entry;
}

static void
vy_cache_entry_delete(struct vy_cache_env *env, struct vy_cache_entry *entry)
{
	vy_stmt_counter_unacct_tuple(&entry->cache->stat.count, entry->stmt);
	assert(env->mem_used >= vy_cache_entry_size(entry));
	env->mem_used -= vy_cache_entry_size(entry);
	tuple_unref(entry->stmt);
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
		struct key_def *cmp_def)
{
	cache->env = env;
	cache->cmp_def = cmp_def;
	cache->version = 1;
	vy_cache_tree_create(&cache->cache_tree, cmp_def,
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
	vy_stmt_counter_acct_tuple(&cache->stat.evict, entry->stmt);
	vy_cache_tree_delete(&cache->cache_tree, entry);
	vy_cache_entry_delete(cache->env, entry);
}

static void
vy_cache_gc(struct vy_cache_env *env)
{
	for (uint32_t i = 0;
	     env->mem_used > env->mem_quota && i < VY_CACHE_CLEANUP_MAX_STEPS;
	     i++) {
		vy_cache_gc_step(env);
	}
}

void
vy_cache_env_set_quota(struct vy_cache_env *env, size_t quota)
{
	env->mem_quota = quota;
	while (env->mem_used > env->mem_quota) {
		vy_cache_gc(env);
		/*
		 * Make sure we don't block other tx fibers
		 * for too long.
		 */
		fiber_sleep(0);
	}
}

void
vy_cache_add(struct vy_cache *cache, struct tuple *stmt,
	     struct tuple *prev_stmt, const struct tuple *key,
	     enum iterator_type order)
{
	if (cache->env->mem_quota == 0) {
		/* Cache is disabled. */
		return;
	}

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
	uint8_t boundary_level = cache->cmp_def->part_count;
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
		if (order == ITER_EQ || order == ITER_REQ) {
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

	assert(vy_stmt_type(stmt) == IPROTO_INSERT ||
	       vy_stmt_type(stmt) == IPROTO_REPLACE);
	assert(prev_stmt == NULL ||
	       vy_stmt_type(prev_stmt) == IPROTO_INSERT ||
	       vy_stmt_type(prev_stmt) == IPROTO_REPLACE);
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
	assert(!vy_cache_tree_iterator_is_invalid(&inserted));
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

	vy_stmt_counter_acct_tuple(&cache->stat.put, stmt);

	/* Done if it's not a chain */
	if (prev_stmt == NULL)
		return;

	/* The flag that must be set in the inserted chain entry */
	uint32_t flag = direction > 0 ? VY_CACHE_LEFT_LINKED :
			VY_CACHE_RIGHT_LINKED;

#ifndef NDEBUG
	/**
	 * Usually prev_stmt is already in the cache but there are cases
	 * when it's not (see below).
	 * There must be no entries between (prev_stmt, stmt) interval in
	 * any case. (1)
	 * Farther, if the stmt entry is already linked (in certain direction),
	 * it must be linked with prev_stmt (in that direction). (2)
	 * Let't check (1) and (2) for debug reasons.
	 *
	 * There are two cases in which prev_stmt statement is absent
	 * in the cache:
	 * 1) The statement was in prepared state and then it was
	 *  committed or rollbacked.
	 * 2) The entry was popped out by vy_cache_gc.
	 *
	 * Note that case when the prev_stmt is owerwritten by other TX
	 * is impossible because this TX would be sent to read view and
	 * wouldn't be able to add anything to the cache.
	 */
	if (direction > 0)
		vy_cache_tree_iterator_prev(&cache->cache_tree, &inserted);
	else
		vy_cache_tree_iterator_next(&cache->cache_tree, &inserted);

	if (!vy_cache_tree_iterator_is_invalid(&inserted)) {
		struct vy_cache_entry **prev_check_entry =
			vy_cache_tree_iterator_get_elem(&cache->cache_tree,
							&inserted);
		assert(*prev_check_entry != NULL);
		struct tuple *prev_check_stmt = (*prev_check_entry)->stmt;
		int cmp = vy_tuple_compare(prev_stmt, prev_check_stmt,
					   cache->cmp_def);

		if (entry->flags & flag) {
			/* The found entry must be exactly prev_stmt. (2) */
			assert(cmp == 0);
		} else {
			/*
			 * The found entry must be exactly prev_stmt or lay
			 * farther than prev_stmt. (1)
			 */
			assert(cmp * direction >= 0);
		}
	} else {
		/* Cannot be in chain (2) */
		assert(!(entry->flags & flag));
	}
#endif

	if (entry->flags & flag)
		return;

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

struct tuple *
vy_cache_get(struct vy_cache *cache, const struct tuple *key)
{
	struct vy_cache_entry **entry =
		vy_cache_tree_find(&cache->cache_tree, key);
	if (entry == NULL)
		return NULL;
	return (*entry)->stmt;
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
	assert(!exact || entry != NULL);
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

	if (entry != NULL && ((*entry)->flags & VY_CACHE_LEFT_LINKED)) {
		cache->version++;
		(*entry)->flags &= ~VY_CACHE_LEFT_LINKED;
		assert((*prev_entry)->flags & VY_CACHE_RIGHT_LINKED);
		(*prev_entry)->flags &= ~VY_CACHE_RIGHT_LINKED;
	}
	if (prev_entry != NULL) {
		cache->version++;
		(*prev_entry)->right_boundary_level = cache->cmp_def->part_count;
	}

	struct vy_cache_tree_iterator next = itr;
	vy_cache_tree_iterator_next(&cache->cache_tree, &next);
	struct vy_cache_entry **next_entry =
		vy_cache_tree_iterator_get_elem(&cache->cache_tree, &next);

	if (exact && ((*entry)->flags & VY_CACHE_RIGHT_LINKED)) {
		cache->version++;
		(*entry)->flags &= ~VY_CACHE_RIGHT_LINKED;
		assert((*next_entry)->flags & VY_CACHE_LEFT_LINKED);
		(*next_entry)->flags &= ~VY_CACHE_LEFT_LINKED;
	}
	if (entry && !exact) {
		cache->version++;
		(*entry)->left_boundary_level = cache->cmp_def->part_count;
	}

	if (exact) {
		assert(entry != NULL);
		cache->version++;
		struct vy_cache_entry *to_delete = *entry;
		assert(vy_stmt_type(to_delete->stmt) == IPROTO_INSERT ||
		       vy_stmt_type(to_delete->stmt) == IPROTO_REPLACE);
		if (deleted != NULL) {
			*deleted = to_delete->stmt;
			tuple_ref(to_delete->stmt);
		}
		vy_stmt_counter_acct_tuple(&cache->stat.invalidate,
					   to_delete->stmt);
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
 */
static inline bool
vy_cache_iterator_is_stop(struct vy_cache_iterator *itr,
			  struct vy_cache_entry *entry)
{
	uint8_t key_level = tuple_field_count(itr->key);
	/* select{} is actually an EQ iterator with part_count == 0 */
	bool iter_is_eq = itr->iterator_type == ITER_EQ || key_level == 0;
	if (iterator_direction(itr->iterator_type) > 0) {
		if (entry->flags & VY_CACHE_LEFT_LINKED)
			return true;
		if (iter_is_eq && entry->left_boundary_level <= key_level)
			return true;
	} else {
		if (entry->flags & VY_CACHE_RIGHT_LINKED)
			return true;
		if (iter_is_eq && entry->right_boundary_level <= key_level)
			return true;
	}
	return false;
}

/**
 * Determine whether the merge iterator must be stopped or not in case when
 * there are no more values in the cache for given key.
 * That is made by examining flags of the previous cache record.
 *
 * @param itr - the iterator
 * @param last_entry - the last record from previous step of the iterator
 */
static inline bool
vy_cache_iterator_is_end_stop(struct vy_cache_iterator *itr,
			      struct vy_cache_entry *last_entry)
{
	uint8_t key_level = tuple_field_count(itr->key);
	/* select{} is actually an EQ iterator with part_count == 0 */
	bool iter_is_eq = itr->iterator_type == ITER_EQ || key_level == 0;
	if (iterator_direction(itr->iterator_type) > 0) {
		if (last_entry->flags & VY_CACHE_RIGHT_LINKED)
			return true;
		if (iter_is_eq && last_entry->right_boundary_level <= key_level)
			return true;
	} else {
		if (last_entry->flags & VY_CACHE_LEFT_LINKED)
			return true;
		if (iter_is_eq && last_entry->left_boundary_level <= key_level)
			return true;
	}
	return false;
}

/**
 * Make one tree's iterator step from the current position.
 * Direction of the step depends on the iterator type.
 * @param itr Iterator to make step.
 * @param[out] ret Result tuple.
 *
 * @retval Must a merge_iterator stop on @a ret?
 * The function is implicitly used by merge_iterator_next_key and
 * return value is used to determine if the merge_iterator can
 * return @a ret to a read_iterator immediately, without lookups
 * in mems and runs. It is possible, when @a ret is a part of
 * continuous cached tuples chain. In such a case mems or runs can
 * not contain more suitable tuples.
 */
static inline bool
vy_cache_iterator_step(struct vy_cache_iterator *itr, struct tuple **ret)
{
	*ret = NULL;
	struct vy_cache_tree *tree = &itr->cache->cache_tree;
	struct vy_cache_entry *prev_entry =
		*vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);
	if (iterator_direction(itr->iterator_type) > 0)
		vy_cache_tree_iterator_next(tree, &itr->curr_pos);
	else
		vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
	if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
		return vy_cache_iterator_is_end_stop(itr, prev_entry);
	struct vy_cache_entry *entry =
		*vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);

	if (itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(itr->key, entry->stmt, itr->cache->cmp_def)) {
		return vy_cache_iterator_is_end_stop(itr, prev_entry);
	}
	*ret = entry->stmt;
	return vy_cache_iterator_is_stop(itr, entry);
}

/**
 * Skip all statements that are invisible in the read view
 * associated with the iterator.
 */
static void
vy_cache_iterator_skip_to_read_view(struct vy_cache_iterator *itr, bool *stop)
{
	while (itr->curr_stmt != NULL &&
	       vy_stmt_lsn(itr->curr_stmt) > (**itr->read_view).vlsn) {
		/*
		 * The cache stores the latest tuple of the key,
		 * but there could be older tuples in runs.
		 */
		*stop = false;
		vy_cache_iterator_step(itr, &itr->curr_stmt);
	}
}

/**
 * Position the iterator to the first cache entry satisfying
 * the search criteria for a given key and direction.
 */
static void
vy_cache_iterator_seek(struct vy_cache_iterator *itr,
		       enum iterator_type iterator_type,
		       const struct tuple *key,
		       struct vy_cache_entry **entry)
{
	struct vy_cache_tree *tree = &itr->cache->cache_tree;

	*entry = NULL;
	itr->cache->stat.lookup++;

	if (tuple_field_count(key) > 0) {
		bool exact;
		itr->curr_pos = iterator_type == ITER_EQ ||
				iterator_type == ITER_GE ||
				iterator_type == ITER_LT ?
				vy_cache_tree_lower_bound(tree, key, &exact) :
				vy_cache_tree_upper_bound(tree, key, &exact);
		if (iterator_type == ITER_EQ && !exact)
			return;
	} else if (iterator_type == ITER_LE) {
		itr->curr_pos = vy_cache_tree_invalid_iterator();
	} else {
		assert(iterator_type == ITER_GE);
		itr->curr_pos = vy_cache_tree_iterator_first(tree);
	}

	if (iterator_type == ITER_LT || iterator_type == ITER_LE)
		vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
	if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
		return;

	*entry = *vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);
}

NODISCARD int
vy_cache_iterator_next(struct vy_cache_iterator *itr,
		       struct vy_history *history, bool *stop)
{
	*stop = false;
	vy_history_cleanup(history);

	if (!itr->search_started) {
		assert(itr->curr_stmt == NULL);
		itr->search_started = true;
		itr->version = itr->cache->version;
		struct vy_cache_entry *entry;
		vy_cache_iterator_seek(itr, itr->iterator_type,
				       itr->key, &entry);
		if (entry == NULL)
			return 0;
		itr->curr_stmt = entry->stmt;
		*stop = vy_cache_iterator_is_stop(itr, entry);
	} else {
		assert(itr->version == itr->cache->version);
		if (itr->curr_stmt == NULL)
			return 0;
		tuple_unref(itr->curr_stmt);
		*stop = vy_cache_iterator_step(itr, &itr->curr_stmt);
	}

	vy_cache_iterator_skip_to_read_view(itr, stop);
	if (itr->curr_stmt != NULL) {
		tuple_ref(itr->curr_stmt);
		vy_stmt_counter_acct_tuple(&itr->cache->stat.get,
					   itr->curr_stmt);
		return vy_history_append_stmt(history, itr->curr_stmt);
	}
	return 0;
}

NODISCARD int
vy_cache_iterator_skip(struct vy_cache_iterator *itr,
		       const struct tuple *last_stmt,
		       struct vy_history *history, bool *stop)
{
	*stop = false;
	vy_history_cleanup(history);

	assert(!itr->search_started || itr->version == itr->cache->version);

	itr->search_started = true;
	itr->version = itr->cache->version;
	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = NULL;

	const struct tuple *key = itr->key;
	enum iterator_type iterator_type = itr->iterator_type;
	if (last_stmt != NULL) {
		key = last_stmt;
		iterator_type = iterator_direction(iterator_type) > 0 ?
				ITER_GT : ITER_LT;
	}

	struct vy_cache_entry *entry;
	vy_cache_iterator_seek(itr, iterator_type, key, &entry);

	if (itr->iterator_type == ITER_EQ && last_stmt != NULL &&
	    entry != NULL && vy_stmt_compare(itr->key, entry->stmt,
					     itr->cache->cmp_def) != 0)
		entry = NULL;

	if (entry != NULL) {
		*stop = vy_cache_iterator_is_stop(itr, entry);
		itr->curr_stmt = entry->stmt;
	}

	vy_cache_iterator_skip_to_read_view(itr, stop);
	if (itr->curr_stmt != NULL) {
		tuple_ref(itr->curr_stmt);
		vy_stmt_counter_acct_tuple(&itr->cache->stat.get,
					   itr->curr_stmt);
		return vy_history_append_stmt(history, itr->curr_stmt);
	}
	return 0;
}

NODISCARD int
vy_cache_iterator_restore(struct vy_cache_iterator *itr,
			  const struct tuple *last_stmt,
			  struct vy_history *history, bool *stop)
{
	struct key_def *def = itr->cache->cmp_def;
	int dir = iterator_direction(itr->iterator_type);

	if (!itr->search_started || itr->version == itr->cache->version)
		return 0;

	itr->version = itr->cache->version;
	struct tuple *prev_stmt = itr->curr_stmt;
	if (prev_stmt != NULL)
		tuple_unref(prev_stmt);

	const struct tuple *key = itr->key;
	enum iterator_type iterator_type = itr->iterator_type;
	if (last_stmt != NULL) {
		key = last_stmt;
		iterator_type = dir > 0 ? ITER_GT : ITER_LT;
	}

	if ((prev_stmt == NULL && itr->iterator_type == ITER_EQ) ||
	    (prev_stmt != NULL &&
	     prev_stmt != vy_cache_iterator_curr_stmt(itr))) {
		/*
		 * EQ search ended or the iterator was invalidated.
		 * In either case the best we can do is restart the
		 * search.
		 */
		struct vy_cache_entry *entry;
		vy_cache_iterator_seek(itr, iterator_type, key, &entry);

		itr->curr_stmt = NULL;
		if (entry != NULL && itr->iterator_type == ITER_EQ &&
		    vy_stmt_compare(itr->key, entry->stmt, def) != 0)
			entry = NULL;

		if (entry != NULL) {
			*stop = vy_cache_iterator_is_stop(itr, entry);
			itr->curr_stmt = entry->stmt;
		}
		vy_cache_iterator_skip_to_read_view(itr, stop);
	} else {
		/*
		 * The iterator position is still valid, but new
		 * statements may have appeared between last_stmt
		 * and the current statement. Reposition to the
		 * statement closiest to last_stmt.
		 */
		struct vy_cache_tree *tree = &itr->cache->cache_tree;
		struct vy_cache_tree_iterator pos = itr->curr_pos;
		bool key_belongs = (iterator_type == ITER_EQ ||
				    iterator_type == ITER_GE ||
				    iterator_type == ITER_LE);
		if (prev_stmt == NULL)
			pos = vy_cache_tree_invalid_iterator();
		while (true) {
			if (dir > 0)
				vy_cache_tree_iterator_prev(tree, &pos);
			else
				vy_cache_tree_iterator_next(tree, &pos);
			if (vy_cache_tree_iterator_is_invalid(&pos))
				break;
			struct vy_cache_entry *entry =
				*vy_cache_tree_iterator_get_elem(tree, &pos);
			int cmp = dir * vy_stmt_compare(entry->stmt, key, def);
			if (cmp < 0 || (cmp == 0 && !key_belongs))
				break;
			if (vy_stmt_lsn(entry->stmt) <= (**itr->read_view).vlsn) {
				itr->curr_pos = pos;
				itr->curr_stmt = entry->stmt;
				*stop = vy_cache_iterator_is_stop(itr, entry);
			}
			if (cmp == 0)
				break;
		}
	}

	vy_history_cleanup(history);
	if (itr->curr_stmt != NULL) {
		tuple_ref(itr->curr_stmt);
		vy_stmt_counter_acct_tuple(&itr->cache->stat.get,
					   itr->curr_stmt);
		if (vy_history_append_stmt(history, itr->curr_stmt) != 0)
			return -1;
		return prev_stmt != itr->curr_stmt;
	}
	return 0;
}

void
vy_cache_iterator_close(struct vy_cache_iterator *itr)
{
	if (itr->curr_stmt != NULL) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
	}
	TRASH(itr);
}

void
vy_cache_iterator_open(struct vy_cache_iterator *itr, struct vy_cache *cache,
		       enum iterator_type iterator_type,
		       const struct tuple *key, const struct vy_read_view **rv)
{
	itr->cache = cache;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;

	itr->curr_stmt = NULL;
	itr->curr_pos = vy_cache_tree_invalid_iterator();

	itr->version = 0;
	itr->search_started = false;
}
