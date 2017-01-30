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
	vy_quota_init(&e->quota, mem_quota, NULL, NULL);
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
	return malloc(VY_CACHE_TREE_EXTENT_SIZE);
}

static void
vy_cache_tree_page_free(void *ctx, void *p)
{
	struct vy_env *env = (struct vy_env *)ctx;
	(void)env;
	free(p);
}

struct vy_cache *
vy_cache_new(struct vy_cache_env *env, struct key_def *key_def)
{
	struct vy_cache *cache = (struct vy_cache *)
		malloc(sizeof(struct vy_cache));
	if (cache == NULL) {
		diag_set(OutOfMemory, sizeof(*cache),
			 "malloc", "struct vy_cache");
		return NULL;
	}

	cache->env = env;
	cache->key_def = key_def;
	cache->version = 1;
	vy_cache_tree_create(&cache->cache_tree, key_def,
			     vy_cache_tree_page_alloc,
			     vy_cache_tree_page_free, env);
	return cache;
}

void
vy_cache_delete(struct vy_cache *cache)
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
	free(cache);
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
	     struct tuple *prev_stmt, int direction)
{
	assert(vy_stmt_type(stmt) == IPROTO_REPLACE);
	assert(direction == 1 || direction == -1);

	/* Delete some entries if quota overused */
	vy_cache_gc(cache->env);
	cache->version++;

	/* Insert/replace new entry to the tree */
	struct vy_cache_entry *entry =
		vy_cache_entry_new(cache->env, cache, stmt);
	if (entry == NULL) {
		/* memory error, let's live without a cache */
		return;
	}
	struct vy_cache_entry *replaced = NULL;
	if (vy_cache_tree_insert(&cache->cache_tree, entry, &replaced)) {
		/* memory error, let's live without a cache */
		vy_cache_entry_delete(cache->env, entry);
		return;
	}
	if (replaced != NULL) {
		entry->flags = replaced->flags;
		vy_cache_entry_delete(cache->env, replaced);
	}

	/* Done if it's not a chain */
	if (prev_stmt == NULL)
		return;

	/* The flag that must be set in the inserted chain entry */
	uint32_t flag = direction > 0 ? VY_CACHE_LEFT_LINKED :
			VY_CACHE_RIGHT_LINKED;
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
		vy_cache_entry_delete(cache->env, replaced);
	}

	/* Set proper flags */
	entry->flags |= flag;
	/* Set inverted flag in the previous entry */
	prev_entry->flags |= (VY_CACHE_LEFT_LINKED |
			      VY_CACHE_RIGHT_LINKED) ^ flag;
}

void
vy_cache_on_write(struct vy_cache *cache, struct tuple *stmt)
{
	vy_cache_gc(cache->env);
	bool exact = false;
	struct vy_cache_tree_iterator itr;
	itr = vy_cache_tree_lower_bound(&cache->cache_tree, stmt, &exact);
	if (vy_cache_tree_iterator_is_invalid(&itr))
		return;
	struct vy_cache_entry *entry =
		*vy_cache_tree_iterator_get_elem(&cache->cache_tree, &itr);
	if (entry->flags & VY_CACHE_LEFT_LINKED) {
		cache->version++;
		entry->flags &= ~VY_CACHE_LEFT_LINKED;
		struct vy_cache_tree_iterator prev = itr;
		vy_cache_tree_iterator_prev(&cache->cache_tree, &prev);
		struct vy_cache_entry **prev_entry =
			vy_cache_tree_iterator_get_elem(&cache->cache_tree,
							&prev);
		assert((*prev_entry)->flags & VY_CACHE_RIGHT_LINKED);
		(*prev_entry)->flags &= ~VY_CACHE_RIGHT_LINKED;
	}
	if (exact && (entry->flags & VY_CACHE_RIGHT_LINKED)) {
		cache->version++;
		entry->flags &= ~VY_CACHE_RIGHT_LINKED;
		struct vy_cache_tree_iterator next = itr;
		vy_cache_tree_iterator_next(&cache->cache_tree, &next);
		struct vy_cache_entry **next_entry =
			vy_cache_tree_iterator_get_elem(&cache->cache_tree,
							&next);
		assert((*next_entry)->flags & VY_CACHE_LEFT_LINKED);
		(*next_entry)->flags &= ~VY_CACHE_LEFT_LINKED;
	}
	if (exact) {
		cache->version++;
		vy_cache_tree_delete(&cache->cache_tree, entry);
		vy_cache_entry_delete(cache->env, entry);
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
 * Find next (lower, older) record with the same key as current
 *
 * @retval 0
 */
static int
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
			return 1;
	} else if (itr->iterator_type == ITER_LE) {
		itr->curr_pos = vy_cache_tree_invalid_iterator();
	} else {
		assert(itr->iterator_type == ITER_GE);
		itr->curr_pos = vy_cache_tree_iterator_first(tree);
	}

	if (itr->iterator_type == ITER_LT || itr->iterator_type == ITER_LE)
		vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
	if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
		return 0;

	struct vy_cache_entry **entry =
		vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);
	struct tuple *candidate = (*entry)->stmt;
	int dir = iterator_direction(itr->iterator_type);
	if (dir > 0 && ((*entry)->flags & VY_CACHE_LEFT_LINKED))
		*stop = true;
	else if (dir < 0 && ((*entry)->flags & VY_CACHE_RIGHT_LINKED))
		*stop = true;

	while (vy_stmt_lsn(candidate) > *itr->vlsn) {
		if (iterator_direction(itr->iterator_type) > 0)
			vy_cache_tree_iterator_next(tree, &itr->curr_pos);
		else
			vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
		if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
			return 0;
		entry = vy_cache_tree_iterator_get_elem(tree,
							&itr->curr_pos);
		candidate = (*entry)->stmt;
		if (itr->iterator_type == ITER_EQ &&
		    vy_stmt_compare(key, candidate, itr->cache->key_def))
			return 0;
		if (dir > 0 && !((*entry)->flags & VY_CACHE_LEFT_LINKED))
			*stop = false;
		else if (dir < 0 && !((*entry)->flags & VY_CACHE_RIGHT_LINKED))
			*stop = false;
	}
	itr->curr_stmt = candidate;
	tuple_ref(itr->curr_stmt);
	*ret = itr->curr_stmt;
	return 0;
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
	if (!itr->search_started)
		return vy_cache_iterator_start(itr, ret, stop);
	if (!itr->curr_stmt) /* End of search. */
		return 0;
	itr->stat->step_count++;

	struct vy_cache_tree *tree = &itr->cache->cache_tree;
	const struct tuple *key = itr->key;

	bool step_was_made;
	vy_cache_iterator_restore_pos(itr, &step_was_made);
	if (!itr->curr_stmt) /* End of search. */
		return 0;
	if (!step_was_made) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
		if (iterator_direction(itr->iterator_type) > 0)
			vy_cache_tree_iterator_next(tree, &itr->curr_pos);
		else
			vy_cache_tree_iterator_prev(tree, &itr->curr_pos);
		if (vy_cache_tree_iterator_is_invalid(&itr->curr_pos))
			return 0;
		itr->curr_stmt = vy_cache_iterator_curr_stmt(itr);
		tuple_ref(itr->curr_stmt);
	}

	if (itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(itr->key, itr->curr_stmt, itr->cache->key_def)) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
		return 0;
	}
	struct vy_cache_entry **entry =
		vy_cache_tree_iterator_get_elem(tree, &itr->curr_pos);
	int dir = iterator_direction(itr->iterator_type);
	if (dir > 0 && ((*entry)->flags & VY_CACHE_LEFT_LINKED))
		*stop = true;
	else if (dir < 0 && ((*entry)->flags & VY_CACHE_RIGHT_LINKED))
		*stop = true;

	while (vy_stmt_lsn(itr->curr_stmt) > *itr->vlsn) {
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
		if (dir > 0 && !((*entry)->flags & VY_CACHE_LEFT_LINKED))
			*stop = false;
		else if (dir < 0 && !((*entry)->flags & VY_CACHE_RIGHT_LINKED))
			*stop = false;
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
	*ret = NULL;
	assert(vitr->iface->restore == vy_cache_iterator_restore);
	struct vy_cache_iterator *itr = (struct vy_cache_iterator *) vitr;

	struct key_def *def = itr->cache->key_def;
	struct vy_cache_tree *tree = &itr->cache->cache_tree;
	int dir = iterator_direction(itr->iterator_type);

	if (itr->search_started) {
		if (last_stmt == NULL)
			return 0;
		int rc = 0;
		struct vy_cache_tree_iterator pos = itr->curr_pos;
		while (true) {
			if (dir > 0)
				vy_cache_tree_iterator_prev(tree, &pos);
			else
				vy_cache_tree_iterator_next(tree, &pos);
			if (vy_cache_tree_iterator_is_invalid(&pos))
				break;
			struct vy_cache_entry **e;
			e = vy_cache_tree_iterator_get_elem(tree, &pos);
			struct tuple *t = (*e)->stmt;
			int cmp = vy_stmt_compare(t, last_stmt, def);
			if (cmp < 0)
				break;
			if (vy_stmt_lsn(t) <= *itr->vlsn) {
				if (itr->curr_stmt != NULL)
					tuple_unref(itr->curr_stmt);
				itr->curr_pos = pos;
				itr->curr_stmt = t;
				tuple_ref(itr->curr_stmt);
				rc = 1;
				if (dir > 0 && ((*e)->flags & VY_CACHE_LEFT_LINKED))
					*stop = true;
				else if (dir < 0 && ((*e)->flags & VY_CACHE_RIGHT_LINKED))
					*stop = true;
			}
		}
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

void
vy_cache_iterator_close(struct vy_stmt_iterator *vitr)
{
	assert(vitr->iface->close == vy_cache_iterator_close);
	struct vy_cache_iterator *itr = (struct vy_cache_iterator *) vitr;
	if (itr->curr_stmt != NULL) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
	}
}

static struct vy_stmt_iterator_iface vy_cache_iterator_iface = {
	.next_key = vy_cache_iterator_next_key,
	.next_lsn = vy_cache_iterator_next_lsn,
	.restore = vy_cache_iterator_restore,
	.close = vy_cache_iterator_close
};

void
vy_cache_iterator_open(struct vy_cache_iterator *itr,
		       struct vy_iterator_stat *stat, struct vy_cache *cache,
		       enum iterator_type iterator_type,
		       const struct tuple *key, const int64_t *vlsn)
{
	itr->base.iface = &vy_cache_iterator_iface;
	itr->stat = stat;

	itr->cache = cache;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->vlsn = vlsn;
	if (tuple_field_count(key) == 0) {
		/* NULL key. change itr->iterator_type for simplification */
		itr->iterator_type = iterator_type == ITER_LT ||
				     iterator_type == ITER_LE ?
				     ITER_LE : ITER_GE;
	}

	itr->curr_stmt = NULL;

	itr->version = 0;
	itr->search_started = false;
}
