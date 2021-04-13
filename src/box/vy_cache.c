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
	/* Flag in cache node that means that there are no values in DB
	 * that less than the current and greater than the previous */
	VY_CACHE_LEFT_LINKED = 1,
	/* Flag in cache node that means that there are no values in DB
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
	mempool_create(&e->cache_node_mempool, slab_cache,
		       sizeof(struct vy_cache_node));
}

void
vy_cache_env_destroy(struct vy_cache_env *e)
{
	mempool_destroy(&e->cache_node_mempool);
}

static inline size_t
vy_cache_node_size(const struct vy_cache_node *node)
{
	size_t size = sizeof(*node);
	/*
	 * Tuples are shared between primary and secondary index
	 * cache so to avoid double accounting, we account only
	 * primary index tuples.
	 */
	if (node->cache->is_primary)
		size += tuple_size(node->entry.stmt);
	return size;
}

static struct vy_cache_node *
vy_cache_node_new(struct vy_cache_env *env, struct vy_cache *cache,
		  struct vy_entry entry)
{
	struct vy_cache_node *node = mempool_alloc(&env->cache_node_mempool);
	if (node == NULL)
		return NULL;
	tuple_ref(entry.stmt);
	node->cache = cache;
	node->entry = entry;
	node->flags = 0;
	node->left_boundary_level = cache->cmp_def->part_count;
	node->right_boundary_level = cache->cmp_def->part_count;
	rlist_add(&env->cache_lru, &node->in_lru);
	env->mem_used += vy_cache_node_size(node);
	vy_stmt_counter_acct_tuple(&cache->stat.count, entry.stmt);
	return node;
}

static void
vy_cache_node_delete(struct vy_cache_env *env, struct vy_cache_node *node)
{
	vy_stmt_counter_unacct_tuple(&node->cache->stat.count,
				     node->entry.stmt);
	assert(env->mem_used >= vy_cache_node_size(node));
	env->mem_used -= vy_cache_node_size(node);
	tuple_unref(node->entry.stmt);
	rlist_del(&node->in_lru);
	TRASH(node);
	mempool_free(&env->cache_node_mempool, node);
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
		struct key_def *cmp_def, bool is_primary)
{
	cache->env = env;
	cache->cmp_def = cmp_def;
	cache->is_primary = is_primary;
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
		struct vy_cache_node **node =
			vy_cache_tree_iterator_get_elem(&cache->cache_tree,
							&itr);
		assert(node != NULL && *node != NULL);
		vy_cache_node_delete(cache->env, *node);
		vy_cache_tree_iterator_next(&cache->cache_tree, &itr);
	}
	vy_cache_tree_destroy(&cache->cache_tree);
}

static void
vy_cache_gc_step(struct vy_cache_env *env)
{
	struct rlist *lru = &env->cache_lru;
	struct vy_cache_node *node =
		rlist_last_entry(lru, struct vy_cache_node, in_lru);
	struct vy_cache *cache = node->cache;
	struct vy_cache_tree *tree = &cache->cache_tree;
	if (node->flags & (VY_CACHE_LEFT_LINKED | VY_CACHE_RIGHT_LINKED)) {
		bool exact;
		struct vy_cache_tree_iterator itr =
			vy_cache_tree_lower_bound(tree, node->entry, &exact);
		assert(exact);
		if (node->flags & VY_CACHE_LEFT_LINKED) {
			struct vy_cache_tree_iterator prev = itr;
			vy_cache_tree_iterator_prev(tree, &prev);
			struct vy_cache_node **prev_node =
				vy_cache_tree_iterator_get_elem(tree, &prev);
			assert((*prev_node)->flags & VY_CACHE_RIGHT_LINKED);
			(*prev_node)->flags &= ~VY_CACHE_RIGHT_LINKED;
		}
		if (node->flags & VY_CACHE_RIGHT_LINKED) {
			struct vy_cache_tree_iterator next = itr;
			vy_cache_tree_iterator_next(&cache->cache_tree,
						    &next);
			struct vy_cache_node **next_node =
				vy_cache_tree_iterator_get_elem(tree, &next);
			assert((*next_node)->flags & VY_CACHE_LEFT_LINKED);
			(*next_node)->flags &= ~VY_CACHE_LEFT_LINKED;
		}
	}
	cache->version++;
	vy_stmt_counter_acct_tuple(&cache->stat.evict, node->entry.stmt);
	vy_cache_tree_delete(&cache->cache_tree, node);
	vy_cache_node_delete(cache->env, node);
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
vy_cache_add(struct vy_cache *cache, struct vy_entry curr,
	     struct vy_entry prev, struct vy_entry key,
	     enum iterator_type order)
{
	if (cache->env->mem_quota == 0) {
		/* Cache is disabled. */
		return;
	}

	/* Delete some entries if quota overused */
	vy_cache_gc(cache->env);

	if (curr.stmt != NULL && vy_stmt_lsn(curr.stmt) == INT64_MAX) {
		/* Do not store a statement from write set of a tx */
		return;
	}

	/* The case of the first or the last result in key+order query */
	bool is_boundary = (curr.stmt != NULL) != (prev.stmt != NULL);

	if (prev.stmt != NULL && vy_stmt_lsn(prev.stmt) == INT64_MAX) {
		/* Previous statement is from tx write set, can't store it */
		prev = vy_entry_none();
	}

	if (prev.stmt == NULL && curr.stmt == NULL) {
		/* Do not store empty ranges */
		return;
	}

	int direction = iterator_direction(order);
	/**
	 * Let's determine boundary_level (left/right) of the new record
	 * in cache to be inserted.
	 */
	uint8_t boundary_level = cache->cmp_def->part_count;
	if (curr.stmt != NULL) {
		if (is_boundary) {
			/**
			 * That means that the curr is the first in a result.
			 * Regardless of order, the statement is the first in
			 * sequence of statements that is equal to the key.
			 */
			boundary_level = vy_stmt_key_part_count(key.stmt,
							cache->cmp_def);
		}
	} else {
		assert(prev.stmt != NULL);
		if (order == ITER_EQ || order == ITER_REQ) {
			/* that is the last statement that is equal to key */
			boundary_level = vy_stmt_key_part_count(key.stmt,
							cache->cmp_def);
		} else {
			/* that is the last statement */
			boundary_level = 0;
		}
		/**
		 * That means that the search was ended, and prev was
		 * the last statement of the result. It is equivalent to
		 * first found statement with a reverse order. Let's transform
		 * to the equivalent case in order of further simplification.
		 */
		direction = -direction;
		curr = prev;
		prev = vy_entry_none();
	}
	TRASH(&order);

	assert(vy_stmt_type(curr.stmt) == IPROTO_INSERT ||
	       vy_stmt_type(curr.stmt) == IPROTO_REPLACE);
	assert(prev.stmt == NULL ||
	       vy_stmt_type(prev.stmt) == IPROTO_INSERT ||
	       vy_stmt_type(prev.stmt) == IPROTO_REPLACE);
	cache->version++;

	/* Insert/replace new node to the tree */
	struct vy_cache_node *node =
		vy_cache_node_new(cache->env, cache, curr);
	if (node == NULL) {
		/* memory error, let's live without a cache */
		return;
	}
	struct vy_cache_node *replaced = NULL;
	struct vy_cache_tree_iterator inserted;
	if (vy_cache_tree_insert_get_iterator(&cache->cache_tree, node,
					      &replaced, &inserted) != 0) {
		/* memory error, let's live without a cache */
		vy_cache_node_delete(cache->env, node);
		return;
	}
	assert(!vy_cache_tree_iterator_is_invalid(&inserted));
	if (replaced != NULL) {
		node->flags = replaced->flags;
		node->left_boundary_level = replaced->left_boundary_level;
		node->right_boundary_level = replaced->right_boundary_level;
		vy_cache_node_delete(cache->env, replaced);
	}
	if (direction > 0 && boundary_level < node->left_boundary_level)
		node->left_boundary_level = boundary_level;
	else if (direction < 0 && boundary_level < node->right_boundary_level)
		node->right_boundary_level = boundary_level;

	vy_stmt_counter_acct_tuple(&cache->stat.put, curr.stmt);

	/* Done if it's not a chain */
	if (prev.stmt == NULL)
		return;

	/* The flag that must be set in the inserted chain node */
	uint32_t flag = direction > 0 ? VY_CACHE_LEFT_LINKED :
			VY_CACHE_RIGHT_LINKED;

#ifndef NDEBUG
	/**
	 * Usually prev is already in the cache but there are cases
	 * when it's not (see below).
	 * There must be no entries between (prev, curr) interval in
	 * any case. (1)
	 * Farther, if the curr node is already linked (in certain direction),
	 * it must be linked with prev (in that direction). (2)
	 * Let't check (1) and (2) for debug reasons.
	 *
	 * There are two cases in which prev statement is absent
	 * in the cache:
	 * 1) The statement was in prepared state and then it was
	 *  committed or rollbacked.
	 * 2) The node was popped out by vy_cache_gc.
	 *
	 * Note that case when the prev is owerwritten by other TX
	 * is impossible because this TX would be sent to read view and
	 * wouldn't be able to add anything to the cache.
	 */
	if (direction > 0)
		vy_cache_tree_iterator_prev(&cache->cache_tree, &inserted);
	else
		vy_cache_tree_iterator_next(&cache->cache_tree, &inserted);

	if (!vy_cache_tree_iterator_is_invalid(&inserted)) {
		struct vy_cache_node **prev_check_node =
			vy_cache_tree_iterator_get_elem(&cache->cache_tree,
							&inserted);
		assert(*prev_check_node != NULL);
		struct vy_entry prev_check = (*prev_check_node)->entry;
		int cmp = vy_entry_compare(prev, prev_check, cache->cmp_def);

		if (node->flags & flag) {
			/* The found node must be exactly prev. (2) */
			assert(cmp == 0);
		} else {
			/*
			 * The found node must be exactly prev or lay
			 * farther than prev. (1)
			 */
			assert(cmp * direction >= 0);
		}
	} else {
		/* Cannot be in chain (2) */
		assert(!(node->flags & flag));
	}
#endif

	if (node->flags & flag)
		return;

	/* Insert/replace node with previous statement */
	struct vy_cache_node *prev_node =
		vy_cache_node_new(cache->env, cache, prev);
	if (prev_node == NULL) {
		/* memory error, let's live without a chain */
		return;
	}
	replaced = NULL;
	if (vy_cache_tree_insert(&cache->cache_tree, prev_node, &replaced, NULL)) {
		/* memory error, let's live without a chain */
		vy_cache_node_delete(cache->env, prev_node);
		return;
	}
	if (replaced != NULL) {
		prev_node->flags = replaced->flags;
		prev_node->left_boundary_level = replaced->left_boundary_level;
		prev_node->right_boundary_level = replaced->right_boundary_level;
		vy_cache_node_delete(cache->env, replaced);
	}

	/* Set proper flags */
	node->flags |= flag;
	/* Set inverted flag in the previous node */
	prev_node->flags |= (VY_CACHE_LEFT_LINKED |
			     VY_CACHE_RIGHT_LINKED) ^ flag;
}

struct vy_entry
vy_cache_get(struct vy_cache *cache, struct vy_entry key)
{
	struct vy_cache_node **node =
		vy_cache_tree_find(&cache->cache_tree, key);
	if (node == NULL)
		return vy_entry_none();
	return (*node)->entry;
}

void
vy_cache_on_write(struct vy_cache *cache, struct vy_entry entry,
		  struct vy_entry *deleted)
{
	vy_cache_gc(cache->env);
	bool exact = false;
	struct vy_cache_tree_iterator itr;
	itr = vy_cache_tree_lower_bound(&cache->cache_tree, entry, &exact);
	struct vy_cache_node **node =
		vy_cache_tree_iterator_get_elem(&cache->cache_tree, &itr);
	assert(!exact || node != NULL);
	/*
	 * There are three cases possible
	 * (1) there's a value in cache that is equal to entry.
	 *   ('exact' == true, 'node' points the equal value in cache)
	 * (2) there's no value in cache that is equal to entry, and lower_bound
	 *   returned the next record.
	 *   ('exact' == false, 'node' points to the equal value in cache)
	 * (3) there's no value in cache that is equal to entry, and lower_bound
	 *   returned invalid iterator, so there's no bigger value.
	 *   ('exact' == false, 'node' == NULL)
	 */

	if (vy_stmt_type(entry.stmt) == IPROTO_DELETE && !exact) {
		/* there was nothing and there is nothing now */
		return;
	}

	struct vy_cache_tree_iterator prev = itr;
	vy_cache_tree_iterator_prev(&cache->cache_tree, &prev);
	struct vy_cache_node **prev_node =
		vy_cache_tree_iterator_get_elem(&cache->cache_tree, &prev);

	if (node != NULL && ((*node)->flags & VY_CACHE_LEFT_LINKED)) {
		cache->version++;
		(*node)->flags &= ~VY_CACHE_LEFT_LINKED;
		assert((*prev_node)->flags & VY_CACHE_RIGHT_LINKED);
		(*prev_node)->flags &= ~VY_CACHE_RIGHT_LINKED;
	}
	if (prev_node != NULL) {
		cache->version++;
		(*prev_node)->right_boundary_level = cache->cmp_def->part_count;
	}

	struct vy_cache_tree_iterator next = itr;
	vy_cache_tree_iterator_next(&cache->cache_tree, &next);
	struct vy_cache_node **next_node =
		vy_cache_tree_iterator_get_elem(&cache->cache_tree, &next);

	if (exact && ((*node)->flags & VY_CACHE_RIGHT_LINKED)) {
		cache->version++;
		(*node)->flags &= ~VY_CACHE_RIGHT_LINKED;
		assert((*next_node)->flags & VY_CACHE_LEFT_LINKED);
		(*next_node)->flags &= ~VY_CACHE_LEFT_LINKED;
	}
	if (node && !exact) {
		cache->version++;
		(*node)->left_boundary_level = cache->cmp_def->part_count;
	}

	if (exact) {
		assert(node != NULL);
		cache->version++;
		struct vy_cache_node *to_delete = *node;
		assert(vy_stmt_type(to_delete->entry.stmt) == IPROTO_INSERT ||
		       vy_stmt_type(to_delete->entry.stmt) == IPROTO_REPLACE);
		if (deleted != NULL) {
			*deleted = to_delete->entry;
			tuple_ref(to_delete->entry.stmt);
		}
		vy_stmt_counter_acct_tuple(&cache->stat.invalidate,
					   to_delete->entry.stmt);
		vy_cache_tree_delete(&cache->cache_tree, to_delete);
		vy_cache_node_delete(cache->env, to_delete);
	}
}

/**
 * Get a stmt by current position
 */
static struct vy_entry
vy_cache_iterator_curr(struct vy_cache_iterator *itr)
{
	struct vy_cache_tree *tree = &itr->cache->cache_tree;
	struct vy_cache_node **node =
		vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);
	return node ? (*node)->entry : vy_entry_none();
}

/**
 * Determine whether the merge iterator must be stopped or not.
 * That is made by examining flags of a cache record.
 *
 * @param itr - the iterator
 * @param node - current record of the cache
 */
static inline bool
vy_cache_iterator_is_stop(struct vy_cache_iterator *itr,
			  struct vy_cache_node *node)
{
	uint8_t key_level = vy_stmt_key_part_count(itr->key.stmt,
						   itr->cache->cmp_def);
	/* select{} is actually an EQ iterator with part_count == 0 */
	bool iter_is_eq = itr->iterator_type == ITER_EQ || key_level == 0;
	if (iterator_direction(itr->iterator_type) > 0) {
		if (node->flags & VY_CACHE_LEFT_LINKED)
			return true;
		if (iter_is_eq && node->left_boundary_level <= key_level)
			return true;
	} else {
		if (node->flags & VY_CACHE_RIGHT_LINKED)
			return true;
		if (iter_is_eq && node->right_boundary_level <= key_level)
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
 * @param last_node - the last record from previous step of the iterator
 */
static inline bool
vy_cache_iterator_is_end_stop(struct vy_cache_iterator *itr,
			      struct vy_cache_node *last_node)
{
	uint8_t key_level = vy_stmt_key_part_count(itr->key.stmt,
						   itr->cache->cmp_def);
	/* select{} is actually an EQ iterator with part_count == 0 */
	bool iter_is_eq = itr->iterator_type == ITER_EQ || key_level == 0;
	if (iterator_direction(itr->iterator_type) > 0) {
		if (last_node->flags & VY_CACHE_RIGHT_LINKED)
			return true;
		if (iter_is_eq && last_node->right_boundary_level <= key_level)
			return true;
	} else {
		if (last_node->flags & VY_CACHE_LEFT_LINKED)
			return true;
		if (iter_is_eq && last_node->left_boundary_level <= key_level)
			return true;
	}
	return false;
}

/**
 * Make one tree's iterator step from the current position.
 * Direction of the step depends on the iterator type.
 * @param itr Iterator to make step.
 *
 * @retval Must a read iterator stop on the cached statement?
 * The function is implicitly used by vy_read_iterator_next and
 * return value is used to determine if the read iterator can
 * return the cached statement without lookups in mems and runs.
 * It is possible when the cached statement is a part of a
 * continuous cached tuples chain. In such a case mems or runs can
 * not contain more suitable tuples.
 */
static inline bool
vy_cache_iterator_step(struct vy_cache_iterator *itr)
{
	if (itr->curr.stmt != NULL) {
		tuple_unref(itr->curr.stmt);
		itr->curr = vy_entry_none();
	}
	struct vy_cache_tree *tree = &itr->cache->cache_tree;
	struct vy_cache_node *prev_node =
		*vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);
	if (iterator_direction(itr->iterator_type) > 0)
		vy_cache_tree_iterator_next(tree, &itr->curr_pos);
	else
		vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
	if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
		return vy_cache_iterator_is_end_stop(itr, prev_node);
	struct vy_cache_node *node =
		*vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);

	if (itr->iterator_type == ITER_EQ &&
	    vy_entry_compare(itr->key, node->entry, itr->cache->cmp_def)) {
		return vy_cache_iterator_is_end_stop(itr, prev_node);
	}
	itr->curr = node->entry;
	tuple_ref(itr->curr.stmt);
	return vy_cache_iterator_is_stop(itr, node);
}

/**
 * Skip all statements that are invisible in the read view
 * associated with the iterator.
 */
static void
vy_cache_iterator_skip_to_read_view(struct vy_cache_iterator *itr, bool *stop)
{
	while (itr->curr.stmt != NULL &&
	       vy_stmt_lsn(itr->curr.stmt) > (**itr->read_view).vlsn) {
		/*
		 * The cache stores the latest tuple of the key,
		 * but there could be older tuples in runs.
		 */
		*stop = false;
		vy_cache_iterator_step(itr);
	}
}

/**
 * Position the iterator to the first cache node satisfying
 * the iterator search criteria and following the given key
 * (pass NULL to start iteration).
 *
 * Like vy_cache_iterator_step(), this functions returns true
 * if the cached statement is a part of a continuous tuple chain
 * and hence the caller doesn't need to scan mems and runs.
 */
static bool
vy_cache_iterator_seek(struct vy_cache_iterator *itr, struct vy_entry last)
{
	struct vy_cache_tree *tree = &itr->cache->cache_tree;

	if (itr->curr.stmt != NULL) {
		tuple_unref(itr->curr.stmt);
		itr->curr = vy_entry_none();
	}
	itr->cache->stat.lookup++;

	struct vy_entry key = itr->key;
	enum iterator_type iterator_type = itr->iterator_type;
	if (last.stmt != NULL) {
		key = last;
		iterator_type = iterator_direction(itr->iterator_type) > 0 ?
				ITER_GT : ITER_LT;
	}

	bool exact = false;
	if (!vy_stmt_is_empty_key(key.stmt)) {
		itr->curr_pos = iterator_type == ITER_EQ ||
				iterator_type == ITER_GE ||
				iterator_type == ITER_LT ?
				vy_cache_tree_lower_bound(tree, key, &exact) :
				vy_cache_tree_upper_bound(tree, key, &exact);
	} else if (iterator_type == ITER_LE) {
		itr->curr_pos = vy_cache_tree_invalid_iterator();
	} else {
		assert(iterator_type == ITER_GE);
		itr->curr_pos = vy_cache_tree_iterator_first(tree);
	}

	if (iterator_type == ITER_LT || iterator_type == ITER_LE)
		vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
	if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
		return false;

	struct vy_cache_node *node;
	node = *vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);

	if (itr->iterator_type == ITER_EQ &&
	    ((last.stmt == NULL && !exact) ||
	     (last.stmt != NULL && vy_entry_compare(itr->key, node->entry,
						    itr->cache->cmp_def) != 0)))
		return false;

	itr->curr = node->entry;
	tuple_ref(itr->curr.stmt);
	return vy_cache_iterator_is_stop(itr, node);
}

NODISCARD int
vy_cache_iterator_next(struct vy_cache_iterator *itr,
		       struct vy_history *history, bool *stop)
{
	vy_history_cleanup(history);

	if (!itr->search_started) {
		assert(itr->curr.stmt == NULL);
		itr->search_started = true;
		itr->version = itr->cache->version;
		*stop = vy_cache_iterator_seek(itr, vy_entry_none());
	} else {
		assert(itr->version == itr->cache->version);
		if (itr->curr.stmt == NULL)
			return 0;
		*stop = vy_cache_iterator_step(itr);
	}

	vy_cache_iterator_skip_to_read_view(itr, stop);
	if (itr->curr.stmt != NULL) {
		vy_stmt_counter_acct_tuple(&itr->cache->stat.get,
					   itr->curr.stmt);
		return vy_history_append_stmt(history, itr->curr);
	}
	return 0;
}

NODISCARD int
vy_cache_iterator_skip(struct vy_cache_iterator *itr, struct vy_entry last,
		       struct vy_history *history, bool *stop)
{
	assert(!itr->search_started || itr->version == itr->cache->version);

	/*
	 * Check if the iterator is already positioned
	 * at the statement following last.
	 */
	if (itr->search_started &&
	    (itr->curr.stmt == NULL || last.stmt == NULL ||
	     iterator_direction(itr->iterator_type) *
	     vy_entry_compare(itr->curr, last, itr->cache->cmp_def) > 0))
		return 0;

	vy_history_cleanup(history);

	itr->search_started = true;
	itr->version = itr->cache->version;
	*stop = vy_cache_iterator_seek(itr, last);
	vy_cache_iterator_skip_to_read_view(itr, stop);

	if (itr->curr.stmt != NULL) {
		vy_stmt_counter_acct_tuple(&itr->cache->stat.get,
					   itr->curr.stmt);
		return vy_history_append_stmt(history, itr->curr);
	}
	return 0;
}

NODISCARD int
vy_cache_iterator_restore(struct vy_cache_iterator *itr, struct vy_entry last,
			  struct vy_history *history, bool *stop)
{
	if (!itr->search_started || itr->version == itr->cache->version)
		return 0;

	bool pos_changed = false;
	itr->version = itr->cache->version;
	if ((itr->curr.stmt == NULL && itr->iterator_type == ITER_EQ) ||
	    (itr->curr.stmt != NULL &&
	     !vy_entry_is_equal(itr->curr, vy_cache_iterator_curr(itr)))) {
		/*
		 * EQ search ended or the iterator was invalidated.
		 * In either case the best we can do is restart the
		 * search.
		 */
		*stop = vy_cache_iterator_seek(itr, last);
		vy_cache_iterator_skip_to_read_view(itr, stop);
		pos_changed = true;
	} else {
		/*
		 * The iterator position is still valid, but new
		 * statements may have appeared between last
		 * and the current statement. Reposition to the
		 * statement closiest to last.
		 */
		bool key_belongs = false;
		struct vy_entry key = last;
		if (key.stmt == NULL) {
			key = itr->key;
			key_belongs = (itr->iterator_type == ITER_EQ ||
				       itr->iterator_type == ITER_GE ||
				       itr->iterator_type == ITER_LE);
		}
		int dir = iterator_direction(itr->iterator_type);
		struct key_def *def = itr->cache->cmp_def;
		struct vy_cache_tree *tree = &itr->cache->cache_tree;
		struct vy_cache_tree_iterator pos = itr->curr_pos;
		if (itr->curr.stmt == NULL)
			pos = vy_cache_tree_invalid_iterator();
		while (true) {
			if (dir > 0)
				vy_cache_tree_iterator_prev(tree, &pos);
			else
				vy_cache_tree_iterator_next(tree, &pos);
			if (vy_cache_tree_iterator_is_invalid(&pos))
				break;
			struct vy_cache_node *node =
				*vy_cache_tree_iterator_get_elem(tree, &pos);
			int cmp = dir * vy_entry_compare(node->entry, key, def);
			if (cmp < 0 || (cmp == 0 && !key_belongs))
				break;
			if (vy_stmt_lsn(node->entry.stmt) <=
					(**itr->read_view).vlsn) {
				itr->curr_pos = pos;
				if (itr->curr.stmt != NULL)
					tuple_unref(itr->curr.stmt);
				itr->curr = node->entry;
				tuple_ref(itr->curr.stmt);
				*stop = vy_cache_iterator_is_stop(itr, node);
				pos_changed = true;
			}
			if (cmp == 0)
				break;
		}
	}
	if (!pos_changed)
		return 0;

	vy_history_cleanup(history);
	if (itr->curr.stmt != NULL) {
		vy_stmt_counter_acct_tuple(&itr->cache->stat.get,
					   itr->curr.stmt);
		if (vy_history_append_stmt(history, itr->curr) != 0)
			return -1;
	}
	return 1;
}

void
vy_cache_iterator_close(struct vy_cache_iterator *itr)
{
	if (itr->curr.stmt != NULL)
		tuple_unref(itr->curr.stmt);
	TRASH(itr);
}

void
vy_cache_iterator_open(struct vy_cache_iterator *itr, struct vy_cache *cache,
		       enum iterator_type iterator_type, struct vy_entry key,
		       const struct vy_read_view **rv)
{
	itr->cache = cache;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;

	itr->curr = vy_entry_none();
	itr->curr_pos = vy_cache_tree_invalid_iterator();

	itr->version = 0;
	itr->search_started = false;
}
