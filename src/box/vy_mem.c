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
#include "vy_mem.h"

#include <stdlib.h>

#include <trivia/util.h>
#include <small/lsregion.h>
#include <small/slab_arena.h>
#include <small/quota.h>

#include "diag.h"
#include "tuple.h"
#include "vy_history.h"

/** {{{ vy_mem_env */

enum {
	/** Slab size for tuple arena. */
	SLAB_SIZE = 16 * 1024 * 1024
};

void
vy_mem_env_create(struct vy_mem_env *env, size_t memory)
{
	/* Vinyl memory is limited by vy_quota. */
	quota_init(&env->quota, QUOTA_MAX);
	tuple_arena_create(&env->arena, &env->quota, memory,
			   SLAB_SIZE, false, "vinyl");
	lsregion_create(&env->allocator, &env->arena);
	env->tree_extent_size = 0;
}

void
vy_mem_env_destroy(struct vy_mem_env *env)
{
	lsregion_destroy(&env->allocator);
	tuple_arena_destroy(&env->arena);
}

/* }}} vy_mem_env */

/** {{{ vy_mem */

static void *
vy_mem_tree_extent_alloc(void *ctx)
{
	struct vy_mem *mem = (struct vy_mem *) ctx;
	struct vy_mem_env *env = mem->env;
	void *ret = lsregion_aligned_alloc(&env->allocator,
					   VY_MEM_TREE_EXTENT_SIZE,
					   alignof(void *), mem->generation);
	if (ret == NULL) {
		diag_set(OutOfMemory, VY_MEM_TREE_EXTENT_SIZE,
			 "lsregion_aligned_alloc", "ret");
		return NULL;
	}
	mem->tree_extent_size += VY_MEM_TREE_EXTENT_SIZE;
	env->tree_extent_size += VY_MEM_TREE_EXTENT_SIZE;
	return ret;
}

static void
vy_mem_tree_extent_free(void *ctx, void *p)
{
	/* Can't free part of region allocated memory. */
	(void)ctx;
	(void)p;
}

struct vy_mem *
vy_mem_new(struct vy_mem_env *env, struct key_def *cmp_def,
	   struct tuple_format *format, int64_t generation,
	   uint32_t space_cache_version)
{
	struct vy_mem *index = calloc(1, sizeof(*index));
	if (!index) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct vy_mem");
		return NULL;
	}
	index->env = env;
	index->dump_lsn = -1;
	index->cmp_def = cmp_def;
	index->generation = generation;
	index->space_cache_version = space_cache_version;
	index->format = format;
	tuple_format_ref(format);
	vy_mem_tree_create(&index->tree, cmp_def,
			   vy_mem_tree_extent_alloc,
			   vy_mem_tree_extent_free, index);
	rlist_create(&index->in_sealed);
	fiber_cond_create(&index->pin_cond);
	return index;
}

void
vy_mem_delete(struct vy_mem *index)
{
	index->env->tree_extent_size -= index->tree_extent_size;
	tuple_format_unref(index->format);
	fiber_cond_destroy(&index->pin_cond);
	TRASH(index);
	free(index);
}

struct vy_entry
vy_mem_older_lsn(struct vy_mem *mem, struct vy_entry entry)
{
	struct vy_mem_tree_key tree_key;
	tree_key.entry = entry;
	tree_key.lsn = vy_stmt_lsn(entry.stmt) - 1;
	bool exact = false;
	struct vy_mem_tree_iterator itr =
		vy_mem_tree_lower_bound(&mem->tree, &tree_key, &exact);

	if (vy_mem_tree_iterator_is_invalid(&itr))
		return vy_entry_none();

	struct vy_entry result;
	result = *vy_mem_tree_iterator_get_elem(&mem->tree, &itr);
	if (vy_entry_compare(result, entry, mem->cmp_def) != 0)
		return vy_entry_none();
	return result;
}

int
vy_mem_insert_upsert(struct vy_mem *mem, struct vy_entry entry)
{
	assert(vy_stmt_type(entry.stmt) == IPROTO_UPSERT);
	/* Check if the statement can be inserted in the vy_mem. */
	assert(entry.stmt->format_id == tuple_format_id(mem->format));
	/* The statement must be from a lsregion. */
	assert(!vy_stmt_is_refable(entry.stmt));
	size_t size = tuple_size(entry.stmt);
	struct vy_entry replaced = vy_entry_none();
	struct vy_mem_tree_iterator inserted;
	if (vy_mem_tree_insert_get_iterator(&mem->tree, entry, &replaced,
					    &inserted) != 0)
		return -1;
	assert(! vy_mem_tree_iterator_is_invalid(&inserted));
	assert(vy_entry_is_equal(entry,
		*vy_mem_tree_iterator_get_elem(&mem->tree, &inserted)));
	if (replaced.stmt == NULL)
		mem->count.rows++;
	mem->count.bytes += size;
	/*
	 * All iterators begin to see the new statement, and
	 * will be aborted in case of rollback.
	 */
	mem->version++;
	/*
	 * Update n_upserts if needed. Get the previous statement
	 * from the inserted one and if it has the same key, then
	 * increment n_upserts of the new statement until the
	 * predefined limit:
	 *
	 * UPSERT, n = 0
	 * UPSERT, n = 1,
	 *         ...
	 * UPSERT, n = threshold,
	 * UPSERT, n = threshold + 1,
	 * UPSERT, n = threshold + 1, all following ones have
	 *         ...                threshold + 1.
	 * These values are used by vy_lsm_commit_upsert to squash
	 * UPSERTs subsequence.
	 */
	vy_mem_tree_iterator_next(&mem->tree, &inserted);
	struct vy_entry *older = vy_mem_tree_iterator_get_elem(&mem->tree,
							       &inserted);
	if (older == NULL || vy_stmt_type(older->stmt) != IPROTO_UPSERT ||
	    vy_entry_compare(entry, *older, mem->cmp_def) != 0)
		return 0;
	uint8_t n_upserts = vy_stmt_n_upserts(older->stmt);
	/*
	 * Stop increment if the threshold is reached to avoid
	 * creation of multiple squashing tasks.
	 */
	if (n_upserts <= VY_UPSERT_THRESHOLD)
		n_upserts++;
	else
		assert(n_upserts == VY_UPSERT_INF);
	vy_stmt_set_n_upserts(entry.stmt, n_upserts);
	return 0;
}

int
vy_mem_insert(struct vy_mem *mem, struct vy_entry entry)
{
	assert(vy_stmt_type(entry.stmt) != IPROTO_UPSERT);
	/* Check if the statement can be inserted in the vy_mem. */
	assert(vy_stmt_is_key(entry.stmt) ||
	       entry.stmt->format_id == tuple_format_id(mem->format));
	/* The statement must be from a lsregion. */
	assert(!vy_stmt_is_refable(entry.stmt));
	size_t size = tuple_size(entry.stmt);
	struct vy_entry replaced = vy_entry_none();
	if (vy_mem_tree_insert(&mem->tree, entry, &replaced, NULL))
		return -1;
	if (replaced.stmt == NULL)
		mem->count.rows++;
	mem->count.bytes += size;
	/*
	 * All iterators begin to see the new statement, and
	 * will be aborted in case of rollback.
	 */
	mem->version++;
	return 0;
}

void
vy_mem_commit_stmt(struct vy_mem *mem, struct vy_entry entry)
{
	/* The statement must be from a lsregion. */
	assert(!vy_stmt_is_refable(entry.stmt));
	int64_t lsn = vy_stmt_lsn(entry.stmt);
	/*
	 * Normally statement LSN grows monotonically,
	 * but not in case of building an index on an
	 * existing non-empty space. Hence use of MAX
	 * here.
         */
	mem->dump_lsn = MAX(mem->dump_lsn, lsn);
	/*
	 * If we don't bump mem version after assigning LSN to
	 * a mem statement, a read iterator which uses
	 * committed_read_view and yields might not see it after
	 * yield finishes and return a stale tuple.
	 */
	mem->version++;
}

void
vy_mem_rollback_stmt(struct vy_mem *mem, struct vy_entry entry)
{
	/* This is the statement we've inserted before. */
	assert(!vy_stmt_is_refable(entry.stmt));
	int rc = vy_mem_tree_delete(&mem->tree, entry);
	assert(rc == 0);
	(void) rc;
	/* We can't free memory in case of rollback. */
	mem->count.rows--;
	mem->version++;
}

/* }}} vy_mem */

/* {{{ vy_mem_iterator support functions */

/**
 * Make a step in the iterator direction.
 * @retval 0 success
 * @retval 1 EOF
 */
static int
vy_mem_iterator_step(struct vy_mem_iterator *itr)
{
	if (itr->iterator_type == ITER_LE || itr->iterator_type == ITER_LT)
		vy_mem_tree_iterator_prev(&itr->mem->tree, &itr->curr_pos);
	else
		vy_mem_tree_iterator_next(&itr->mem->tree, &itr->curr_pos);
	if (vy_mem_tree_iterator_is_invalid(&itr->curr_pos))
		return 1;
	itr->curr = *vy_mem_tree_iterator_get_elem(&itr->mem->tree,
						   &itr->curr_pos);
	return 0;
}

/**
 * Find next record with lsn <= itr->lsn record.
 * Current position must be at the beginning of serie of records with the
 * same key it terms of direction of iterator (i.e. left for GE, right for LE)
 *
 * @retval 0 Found
 * @retval 1 Not found
 */
static int
vy_mem_iterator_find_lsn(struct vy_mem_iterator *itr)
{
	/* Skip to the first statement visible in the read view. */
	assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
	assert(vy_entry_is_equal(itr->curr,
		*vy_mem_tree_iterator_get_elem(&itr->mem->tree,
					       &itr->curr_pos)));
	struct key_def *cmp_def = itr->mem->cmp_def;
	while (vy_stmt_lsn(itr->curr.stmt) > (**itr->read_view).vlsn ||
	       vy_stmt_flags(itr->curr.stmt) & VY_STMT_SKIP_READ) {
		if (vy_mem_iterator_step(itr) != 0 ||
		    (itr->iterator_type == ITER_EQ &&
		     vy_entry_compare(itr->key, itr->curr, cmp_def))) {
			itr->curr = vy_entry_none();
			return 1;
		}
	}
	if (iterator_direction(itr->iterator_type) > 0)
		return 0;
	/*
	 * Since statements are sorted by LSN in descending order,
	 * for LE/LT iterator we must skip to the statement with
	 * max LSN visible in the read view.
	 */
	struct vy_mem_tree_iterator prev_pos = itr->curr_pos;
	vy_mem_tree_iterator_prev(&itr->mem->tree, &prev_pos);
	if (vy_mem_tree_iterator_is_invalid(&prev_pos)) {
		/* No more statements. */
		return 0;
	}
	struct vy_entry prev;
	prev = *vy_mem_tree_iterator_get_elem(&itr->mem->tree, &prev_pos);
	if (vy_stmt_lsn(prev.stmt) > (**itr->read_view).vlsn ||
	    vy_entry_compare(itr->curr, prev, cmp_def) != 0) {
		/*
		 * The next statement is either invisible in
		 * the read view or for another key.
		 */
		return 0;
	}
	/*
	 * We could iterate linearly until a statement invisible
	 * in the read view is found, but there's a good chance
	 * that this key is frequently updated and so the iteration
	 * is going to take long. So instead we look it up - it's
	 * pretty cheap anyway.
	 */
	struct vy_mem_tree_key tree_key;
	tree_key.entry = itr->curr;
	tree_key.lsn = (**itr->read_view).vlsn;
	itr->curr_pos = vy_mem_tree_lower_bound(&itr->mem->tree,
						&tree_key, NULL);
	assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
	itr->curr = *vy_mem_tree_iterator_get_elem(&itr->mem->tree,
						   &itr->curr_pos);

	/* Skip VY_STMT_SKIP_READ statements, if any. */
	while (vy_stmt_flags(itr->curr.stmt) & VY_STMT_SKIP_READ) {
		vy_mem_tree_iterator_next(&itr->mem->tree, &itr->curr_pos);
		assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
		itr->curr = *vy_mem_tree_iterator_get_elem(&itr->mem->tree,
							   &itr->curr_pos);
	}
	return 0;
}

/**
 * Position the iterator to the first statement satisfying the
 * iterator search criteria and following the given key (pass
 * NULL to start iteration).
 *
 * @retval 0 Found
 * @retval 1 Not found
 */
static int
vy_mem_iterator_seek(struct vy_mem_iterator *itr, struct vy_entry last)
{
	itr->stat->lookup++;
	itr->search_started = true;
	itr->version = itr->mem->version;
	itr->curr = vy_entry_none();

	struct vy_entry key = itr->key;
	enum iterator_type iterator_type = itr->iterator_type;
	if (last.stmt != NULL) {
		key = last;
		iterator_type = iterator_direction(itr->iterator_type) > 0 ?
				ITER_GT : ITER_LT;
	}

	bool exact = false;
	struct vy_mem_tree_key tree_key;
	tree_key.entry = key;
	/* (lsn == INT64_MAX - 1) means that lsn is ignored in comparison */
	tree_key.lsn = INT64_MAX - 1;
	if (!vy_stmt_is_empty_key(key.stmt)) {
		if (iterator_type == ITER_LE || iterator_type == ITER_GT) {
			itr->curr_pos =
				vy_mem_tree_upper_bound(&itr->mem->tree,
							&tree_key, &exact);
		} else {
			assert(iterator_type == ITER_EQ ||
			       iterator_type == ITER_GE ||
			       iterator_type == ITER_LT);
			itr->curr_pos =
				vy_mem_tree_lower_bound(&itr->mem->tree,
							&tree_key, &exact);
		}
	} else if (iterator_type == ITER_LE) {
		itr->curr_pos = vy_mem_tree_invalid_iterator();
	} else {
		assert(iterator_type == ITER_GE);
		itr->curr_pos = vy_mem_tree_first(&itr->mem->tree);
	}

	if (iterator_type == ITER_LT || iterator_type == ITER_LE)
		vy_mem_tree_iterator_prev(&itr->mem->tree, &itr->curr_pos);
	if (vy_mem_tree_iterator_is_invalid(&itr->curr_pos))
		return 1;
	itr->curr = *vy_mem_tree_iterator_get_elem(&itr->mem->tree,
						   &itr->curr_pos);
	if (itr->iterator_type == ITER_EQ &&
	    ((last.stmt == NULL && !exact) ||
	     (last.stmt != NULL &&
	      vy_entry_compare(itr->key, itr->curr, itr->mem->cmp_def) != 0))) {
		itr->curr = vy_entry_none();
		return 1;
	}
	return vy_mem_iterator_find_lsn(itr);
}

/* }}} vy_mem_iterator support functions */

/* {{{ vy_mem_iterator API implementation */

void
vy_mem_iterator_open(struct vy_mem_iterator *itr, struct vy_mem_iterator_stat *stat,
		     struct vy_mem *mem, enum iterator_type iterator_type,
		     struct vy_entry key, const struct vy_read_view **rv)
{
	itr->stat = stat;

	assert(key.stmt != NULL);
	itr->mem = mem;

	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;

	itr->curr_pos = vy_mem_tree_invalid_iterator();
	itr->curr = vy_entry_none();

	itr->search_started = false;
}

/*
 * Find the next record with different key as current and visible lsn.
 * @retval 0 Found
 * @retval 1 Not found
 */
static NODISCARD int
vy_mem_iterator_next_key(struct vy_mem_iterator *itr)
{
	if (!itr->search_started)
		return vy_mem_iterator_seek(itr, vy_entry_none());
	if (itr->curr.stmt == NULL) /* End of search. */
		return 1;
	assert(itr->mem->version == itr->version);
	assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
	assert(vy_entry_is_equal(itr->curr,
		*vy_mem_tree_iterator_get_elem(&itr->mem->tree,
					       &itr->curr_pos)));
	struct key_def *cmp_def = itr->mem->cmp_def;

	struct vy_entry prev = itr->curr;
	if (vy_mem_iterator_step(itr) != 0) {
		itr->curr = vy_entry_none();
		return 1;
	}
	/*
	 * If we are still on the same key after making a step,
	 * there's a good chance there's a lot of statements
	 * for this key so instead of iterating further we simply
	 * look up the next key - it's pretty cheap anyway.
	 */
	if (vy_entry_compare(prev, itr->curr, cmp_def) == 0)
		return vy_mem_iterator_seek(itr, itr->curr);

	if (itr->iterator_type == ITER_EQ &&
	    vy_entry_compare(itr->key, itr->curr, cmp_def) != 0) {
		itr->curr = vy_entry_none();
		return 1;
	}
	return vy_mem_iterator_find_lsn(itr);
}

/*
 * Find next (lower, older) record with the same key as current
 * @retval 0 Found
 * @retval 1 Not found
 */
static NODISCARD int
vy_mem_iterator_next_lsn(struct vy_mem_iterator *itr)
{
	assert(itr->search_started);
	if (itr->curr.stmt == NULL) /* End of search. */
		return 1;
	assert(itr->mem->version == itr->version);
	assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
	assert(vy_entry_is_equal(itr->curr,
		*vy_mem_tree_iterator_get_elem(&itr->mem->tree,
					       &itr->curr_pos)));
	struct key_def *cmp_def = itr->mem->cmp_def;

	struct vy_mem_tree_iterator next_pos = itr->curr_pos;
next:
	vy_mem_tree_iterator_next(&itr->mem->tree, &next_pos);
	if (vy_mem_tree_iterator_is_invalid(&next_pos))
		return 1; /* EOF */

	struct vy_entry next;
	next = *vy_mem_tree_iterator_get_elem(&itr->mem->tree, &next_pos);
	if (vy_entry_compare(itr->curr, next, cmp_def) != 0)
		return 1;

	itr->curr_pos = next_pos;
	itr->curr = next;
	if (vy_stmt_flags(itr->curr.stmt) & VY_STMT_SKIP_READ)
		goto next;
	return 0;
}

/**
 * Append statements for the current key to a statement history
 * until a terminal statement is found. Returns 0 on success, -1
 * on memory allocation error.
 */
static NODISCARD int
vy_mem_iterator_get_history(struct vy_mem_iterator *itr,
			    struct vy_history *history)
{
	do {
		vy_stmt_counter_acct_tuple(&itr->stat->get, itr->curr.stmt);
		if (vy_history_append_stmt(history, itr->curr) != 0)
			return -1;
		if (vy_history_is_terminal(history))
			break;
	} while (vy_mem_iterator_next_lsn(itr) == 0);
	return 0;
}

NODISCARD int
vy_mem_iterator_next(struct vy_mem_iterator *itr,
		     struct vy_history *history)
{
	vy_history_cleanup(history);
	if (vy_mem_iterator_next_key(itr) == 0)
		return vy_mem_iterator_get_history(itr, history);
	return 0;
}

NODISCARD int
vy_mem_iterator_skip(struct vy_mem_iterator *itr, struct vy_entry last,
		     struct vy_history *history)
{
	assert(!itr->search_started || itr->version == itr->mem->version);

	/*
	 * Check if the iterator is already positioned
	 * at the statement following last.
	 */
	if (itr->search_started &&
	    (itr->curr.stmt == NULL || last.stmt == NULL ||
	     iterator_direction(itr->iterator_type) *
	     vy_entry_compare(itr->curr, last, itr->mem->cmp_def) > 0))
		return 0;

	vy_history_cleanup(history);
	if (vy_mem_iterator_seek(itr, last) == 0)
		return vy_mem_iterator_get_history(itr, history);
	return 0;
}

NODISCARD int
vy_mem_iterator_restore(struct vy_mem_iterator *itr, struct vy_entry last,
			struct vy_history *history)
{
	if (!itr->search_started || itr->version == itr->mem->version)
		return 0;

	vy_mem_iterator_seek(itr, last);

	vy_history_cleanup(history);
	if (itr->curr.stmt != NULL &&
	    vy_mem_iterator_get_history(itr, history) != 0)
		return -1;
	return 1;
}

void
vy_mem_iterator_close(struct vy_mem_iterator *itr)
{
	TRASH(itr);
}

static NODISCARD int
vy_mem_stream_next(struct vy_stmt_stream *virt_stream, struct vy_entry *ret)
{
	assert(virt_stream->iface->next == vy_mem_stream_next);
	struct vy_mem_stream *stream = (struct vy_mem_stream *)virt_stream;

	struct vy_entry *res =
		vy_mem_tree_iterator_get_elem(&stream->mem->tree,
					      &stream->curr_pos);
	if (res == NULL) {
		*ret = vy_entry_none();
	} else {
		*ret = *res;
		vy_mem_tree_iterator_next(&stream->mem->tree,
					  &stream->curr_pos);
	}
	return 0;
}

static const struct vy_stmt_stream_iface vy_mem_stream_iface = {
	.start = NULL,
	.next = vy_mem_stream_next,
	.stop = NULL,
	.close = NULL
};

void
vy_mem_stream_open(struct vy_mem_stream *stream, struct vy_mem *mem)
{
	stream->base.iface = &vy_mem_stream_iface;
	stream->mem = mem;
	stream->curr_pos = vy_mem_tree_first(&mem->tree);
}

/* }}} vy_mem_iterator API implementation */
