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
#include "diag.h"

/** {{{ vy_mem */

static void *
vy_mem_tree_extent_alloc(void *ctx)
{
	struct vy_mem *mem = (struct vy_mem *) ctx;
	void *ret = lsregion_alloc(mem->allocator, VY_MEM_TREE_EXTENT_SIZE,
				   mem->generation);
	if (ret == NULL)
		diag_set(OutOfMemory, VY_MEM_TREE_EXTENT_SIZE, "lsregion_alloc",
			 "ret");
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
vy_mem_new(struct lsregion *allocator, int64_t generation,
	   const struct key_def *key_def, struct tuple_format *format,
	   struct tuple_format *format_with_colmask,
	   struct tuple_format *upsert_format, uint32_t schema_version)
{
	struct vy_mem *index = calloc(1, sizeof(*index));
	if (!index) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct vy_mem");
		return NULL;
	}
	index->min_lsn = INT64_MAX;
	index->max_lsn = -1;
	index->key_def = key_def;
	index->generation = generation;
	index->schema_version = schema_version;
	index->allocator = allocator;
	index->format = format;
	tuple_format_ref(format, 1);
	index->format_with_colmask = format_with_colmask;
	tuple_format_ref(format_with_colmask, 1);
	index->upsert_format = upsert_format;
	tuple_format_ref(upsert_format, 1);
	vy_mem_tree_create(&index->tree, key_def,
			   vy_mem_tree_extent_alloc,
			   vy_mem_tree_extent_free, index);
	rlist_create(&index->in_sealed);
	rlist_create(&index->in_dump_fifo);
	ipc_cond_create(&index->pin_cond);
	return index;
}

void
vy_mem_update_formats(struct vy_mem *mem, struct tuple_format *new_format,
		      struct tuple_format *new_format_with_colmask,
		      struct tuple_format *new_upsert_format)
{
	assert(mem->count.rows == 0);
	tuple_format_ref(mem->format, -1);
	tuple_format_ref(mem->format_with_colmask, -1);
	tuple_format_ref(mem->upsert_format, -1);
	mem->format = new_format;
	mem->format_with_colmask = new_format_with_colmask;
	mem->upsert_format = new_upsert_format;
	tuple_format_ref(mem->format, 1);
	tuple_format_ref(mem->format_with_colmask, 1);
	tuple_format_ref(mem->upsert_format, 1);
}

void
vy_mem_delete(struct vy_mem *index)
{
	tuple_format_ref(index->format, -1);
	tuple_format_ref(index->format_with_colmask, -1);
	tuple_format_ref(index->upsert_format, -1);
	ipc_cond_destroy(&index->pin_cond);
	TRASH(index);
	free(index);
}

const struct tuple *
vy_mem_older_lsn(struct vy_mem *mem, const struct tuple *stmt)
{
	struct tree_mem_key tree_key;
	tree_key.stmt = stmt;
	tree_key.lsn = vy_stmt_lsn(stmt) - 1;
	bool exact = false;
	struct vy_mem_tree_iterator itr =
		vy_mem_tree_lower_bound(&mem->tree, &tree_key, &exact);

	if (vy_mem_tree_iterator_is_invalid(&itr))
		return NULL;

	const struct tuple *result;
	result = *vy_mem_tree_iterator_get_elem(&mem->tree, &itr);
	if (vy_stmt_compare(result, stmt, mem->key_def) != 0)
		return NULL;
	return result;
}

int
vy_mem_insert_upsert(struct vy_mem *mem, const struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) == IPROTO_UPSERT);
	/* Check if the statement can be inserted in the vy_mem. */
	assert(stmt->format_id == tuple_format_id(mem->format_with_colmask) ||
	       stmt->format_id == tuple_format_id(mem->format) ||
	       stmt->format_id == tuple_format_id(mem->upsert_format));
	/* The statement must be from a lsregion. */
	assert(vy_stmt_is_region_allocated(stmt));
	size_t size = tuple_size(stmt);
	const struct tuple *replaced_stmt = NULL;
	struct vy_mem_tree_iterator inserted;
	if (vy_mem_tree_insert_get_iterator(&mem->tree, stmt, &replaced_stmt,
					    &inserted) != 0)
		return -1;
	assert(! vy_mem_tree_iterator_is_invalid(&inserted));
	assert(*vy_mem_tree_iterator_get_elem(&mem->tree, &inserted) == stmt);
	if (replaced_stmt == NULL)
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
	 * These values are used by vy_index_commit to squash
	 * UPSERTs subsequence.
	 */
	vy_mem_tree_iterator_next(&mem->tree, &inserted);
	const struct tuple **older = vy_mem_tree_iterator_get_elem(&mem->tree,
								   &inserted);
	if (older == NULL || vy_stmt_type(*older) != IPROTO_UPSERT ||
	    vy_tuple_compare(stmt, *older, mem->key_def) != 0)
		return 0;
	uint8_t n_upserts = vy_stmt_n_upserts(*older);
	/*
	 * Stop increment if the threshold is reached to avoid
	 * creation of multiple squashing tasks.
	 */
	if (n_upserts <= VY_UPSERT_THRESHOLD)
		n_upserts++;
	else
		assert(n_upserts == VY_UPSERT_INF);
	vy_stmt_set_n_upserts((struct tuple *)stmt, n_upserts);
	return 0;
}

int
vy_mem_insert(struct vy_mem *mem, const struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) != IPROTO_UPSERT);
	/* Check if the statement can be inserted in the vy_mem. */
	assert(stmt->format_id == tuple_format_id(mem->format_with_colmask) ||
	       stmt->format_id == tuple_format_id(mem->format) ||
	       stmt->format_id == tuple_format_id(mem->upsert_format));
	/* The statement must be from a lsregion. */
	assert(vy_stmt_is_region_allocated(stmt));
	size_t size = tuple_size(stmt);
	const struct tuple *replaced_stmt = NULL;
	if (vy_mem_tree_insert(&mem->tree, stmt, &replaced_stmt))
		return -1;
	if (replaced_stmt == NULL)
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
vy_mem_commit_stmt(struct vy_mem *mem, const struct tuple *stmt)
{
	/* The statement must be from a lsregion. */
	assert(vy_stmt_is_region_allocated(stmt));
	int64_t lsn = vy_stmt_lsn(stmt);
	if (mem->min_lsn == INT64_MAX)
		mem->min_lsn = lsn;
	assert(mem->min_lsn <= lsn);
	if (mem->max_lsn < lsn)
		mem->max_lsn = lsn;
}

void
vy_mem_rollback_stmt(struct vy_mem *mem, const struct tuple *stmt)
{
	/* This is the statement we've inserted before. */
	assert(vy_stmt_is_region_allocated(stmt));
	int rc = vy_mem_tree_delete(&mem->tree, stmt);
	assert(rc == 0);
	(void) rc;
	/* We can't free memory in case of rollback. */
	mem->count.rows--;
	mem->version++;
}

/* }}} vy_mem */

/* {{{ vy_mem_iterator support functions */

/**
 * Copy current statement into the out parameter. It is necessary
 * because vy_mem stores its tuples in the lsregion allocated
 * area, and lsregion tuples can't be referenced or unreferenced.
 */
static int
vy_mem_iterator_copy_to(struct vy_mem_iterator *itr, struct tuple **ret)
{
	assert(itr->curr_stmt != NULL);
	if (itr->last_stmt)
		tuple_unref(itr->last_stmt);
	itr->last_stmt = vy_stmt_dup(itr->curr_stmt, tuple_format(itr->curr_stmt));
	*ret = itr->last_stmt;
	if (itr->last_stmt != NULL) {
		vy_stmt_counter_acct_tuple(&itr->stat->get, *ret);
		return 0;
	}
	return -1;
}

/**
 * Get a stmt by current position
 */
static const struct tuple *
vy_mem_iterator_curr_stmt(struct vy_mem_iterator *itr)
{
	return *vy_mem_tree_iterator_get_elem(&itr->mem->tree, &itr->curr_pos);
}

/**
 * Make a step in directions defined by @iterator_type.
 * @retval 0 success
 * @retval 1 EOF
 */
static int
vy_mem_iterator_step(struct vy_mem_iterator *itr,
		     enum iterator_type iterator_type)
{
	if (iterator_type == ITER_LE || iterator_type == ITER_LT)
		vy_mem_tree_iterator_prev(&itr->mem->tree, &itr->curr_pos);
	else
		vy_mem_tree_iterator_next(&itr->mem->tree, &itr->curr_pos);
	if (vy_mem_tree_iterator_is_invalid(&itr->curr_pos))
		return 1;
	itr->curr_stmt = vy_mem_iterator_curr_stmt(itr);
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
vy_mem_iterator_find_lsn(struct vy_mem_iterator *itr,
			 enum iterator_type iterator_type,
			 const struct tuple *key)
{
	assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
	assert(itr->curr_stmt == vy_mem_iterator_curr_stmt(itr));
	const struct key_def *key_def = itr->mem->key_def;
	while (vy_stmt_lsn(itr->curr_stmt) > (**itr->read_view).vlsn) {
		if (vy_mem_iterator_step(itr, iterator_type) != 0 ||
		    (iterator_type == ITER_EQ &&
		     vy_stmt_compare(key, itr->curr_stmt, key_def))) {
			itr->curr_stmt = NULL;
			return 1;
		}
	}
	if (iterator_type == ITER_LE || iterator_type == ITER_LT) {
		struct vy_mem_tree_iterator prev_pos = itr->curr_pos;
		vy_mem_tree_iterator_prev(&itr->mem->tree, &prev_pos);

		while (!vy_mem_tree_iterator_is_invalid(&prev_pos)) {
			const struct tuple *prev_stmt =
				*vy_mem_tree_iterator_get_elem(&itr->mem->tree,
							       &prev_pos);
			if (vy_stmt_lsn(prev_stmt) > (**itr->read_view).vlsn ||
			    vy_stmt_compare(itr->curr_stmt, prev_stmt,
					    key_def) != 0)
				break;
			itr->curr_pos = prev_pos;
			itr->curr_stmt = prev_stmt;
			vy_mem_tree_iterator_prev(&itr->mem->tree, &prev_pos);
		}
	}
	assert(itr->curr_stmt != NULL);
	return 0;
}

/**
 * Position the iterator to the first entry in the memory tree
 * satisfying the search criteria for a given key and direction.
 *
 * @retval 0 Found
 * @retval 1 Not found
 */
static int
vy_mem_iterator_start_from(struct vy_mem_iterator *itr,
			   enum iterator_type iterator_type,
			   const struct tuple *key)
{
	assert(! itr->search_started);
	itr->stat->lookup++;
	itr->version = itr->mem->version;
	itr->search_started = true;

	struct tree_mem_key tree_key;
	tree_key.stmt = key;
	/* (lsn == INT64_MAX - 1) means that lsn is ignored in comparison */
	tree_key.lsn = INT64_MAX - 1;
	if (tuple_field_count(key) > 0) {
		if (iterator_type == ITER_EQ) {
			bool exact;
			itr->curr_pos =
				vy_mem_tree_lower_bound(&itr->mem->tree,
							&tree_key, &exact);
			if (!exact)
				return 1;
		} else if (iterator_type == ITER_LE ||
			   iterator_type == ITER_GT) {
			itr->curr_pos =
				vy_mem_tree_upper_bound(&itr->mem->tree,
							&tree_key, NULL);
		} else {
			assert(iterator_type == ITER_GE ||
			       iterator_type == ITER_LT);
			itr->curr_pos =
				vy_mem_tree_lower_bound(&itr->mem->tree,
							&tree_key, NULL);
		}
	} else if (iterator_type == ITER_LE) {
		itr->curr_pos = vy_mem_tree_invalid_iterator();
	} else {
		assert(iterator_type == ITER_GE);
		itr->curr_pos = vy_mem_tree_iterator_first(&itr->mem->tree);
	}

	if (iterator_type == ITER_LT || iterator_type == ITER_LE)
		vy_mem_tree_iterator_prev(&itr->mem->tree, &itr->curr_pos);
	if (vy_mem_tree_iterator_is_invalid(&itr->curr_pos))
		return 1;
	itr->curr_stmt = vy_mem_iterator_curr_stmt(itr);
	return vy_mem_iterator_find_lsn(itr, iterator_type, key);
}

/**
 * Start iteration from the key following @before_first.
 *
 * @retval 0 Found
 * @retval 1 Not found
 */
static int
vy_mem_iterator_start(struct vy_mem_iterator *itr)
{
	assert(!itr->search_started);

	if (itr->before_first == NULL)
		return vy_mem_iterator_start_from(itr, itr->iterator_type,
						  itr->key);

	enum iterator_type iterator_type = itr->iterator_type;
	if (iterator_type == ITER_GE || iterator_type == ITER_EQ)
		iterator_type = ITER_GT;
	else if (iterator_type == ITER_LE)
		iterator_type = ITER_LT;
	int rc = vy_mem_iterator_start_from(itr, iterator_type,
					    itr->before_first);
	if (rc == 0 && itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(itr->key, itr->curr_stmt,
			    itr->mem->key_def) != 0) {
		itr->curr_stmt = NULL;
		rc = 1;
	}
	return rc;
}

/**
 * Restores iterator if the mem have been changed
 */
static void
vy_mem_iterator_check_version(struct vy_mem_iterator *itr)
{
	assert(itr->curr_stmt != NULL);
	if (itr->version == itr->mem->version)
		return;
	itr->version = itr->mem->version;
	const struct tuple * const *record;
	record = vy_mem_tree_iterator_get_elem(&itr->mem->tree, &itr->curr_pos);
	if (record != NULL && *record == itr->curr_stmt)
		return;
	struct tree_mem_key tree_key;
	tree_key.stmt = itr->curr_stmt;
	tree_key.lsn = vy_stmt_lsn(itr->curr_stmt);
	bool exact;
	itr->curr_pos = vy_mem_tree_lower_bound(&itr->mem->tree,
						&tree_key, &exact);
	assert(exact);
}

/* }}} vy_mem_iterator support functions */

/* {{{ vy_mem_iterator API implementation */

/* Declared below */
static const struct vy_stmt_iterator_iface vy_mem_iterator_iface;

void
vy_mem_iterator_open(struct vy_mem_iterator *itr, struct vy_mem_iterator_stat *stat,
		     struct vy_mem *mem, enum iterator_type iterator_type,
		     const struct tuple *key, const struct vy_read_view **rv,
		     struct tuple *before_first)
{
	itr->base.iface = &vy_mem_iterator_iface;
	itr->stat = stat;

	assert(key != NULL);
	itr->mem = mem;

	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;
	if (tuple_field_count(key) == 0) {
		/* NULL key. change itr->iterator_type for simplification */
		itr->iterator_type = iterator_type == ITER_LT ||
				     iterator_type == ITER_LE ?
				     ITER_LE : ITER_GE;
	}
	itr->before_first = before_first;
	if (before_first != NULL)
		tuple_ref(before_first);

	itr->curr_pos = vy_mem_tree_invalid_iterator();
	itr->curr_stmt = NULL;
	itr->last_stmt = NULL;

	itr->search_started = false;
}

/*
 * Find the next record with different key as current and visible lsn.
 * @retval 0 Found
 * @retval 1 Not found
 */
static NODISCARD int
vy_mem_iterator_next_key_impl(struct vy_mem_iterator *itr)
{
	if (!itr->search_started)
		return vy_mem_iterator_start(itr);
	if (!itr->curr_stmt) /* End of search. */
		return 1;
	assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
	vy_mem_iterator_check_version(itr);
	assert(itr->curr_stmt == vy_mem_iterator_curr_stmt(itr));
	const struct key_def *key_def = itr->mem->key_def;

	const struct tuple *prev_stmt = itr->curr_stmt;
	do {
		if (vy_mem_iterator_step(itr, itr->iterator_type) != 0) {
			itr->curr_stmt = NULL;
			return 1;
		}
	} while (vy_stmt_compare(prev_stmt, itr->curr_stmt, key_def) == 0);

	if (itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(itr->key, itr->curr_stmt, key_def) != 0) {
		itr->curr_stmt = NULL;
		return 1;
	}
	return vy_mem_iterator_find_lsn(itr, itr->iterator_type, itr->key);
}

/**
 * Find the next record with different key as current and visible lsn.
 * @retval 0 success or EOF (*ret == NULL)
 */
static NODISCARD int
vy_mem_iterator_next_key(struct vy_stmt_iterator *vitr, struct tuple **ret,
			 bool *stop)
{
	(void)stop;
	assert(vitr->iface->next_key == vy_mem_iterator_next_key);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *) vitr;
	*ret = NULL;

	if (vy_mem_iterator_next_key_impl(itr) == 0)
		return vy_mem_iterator_copy_to(itr, ret);
	return 0;
}

/*
 * Find next (lower, older) record with the same key as current
 * @retval 0 Found
 * @retval 1 Not found
 */
static NODISCARD int
vy_mem_iterator_next_lsn_impl(struct vy_mem_iterator *itr)
{
	if (!itr->search_started)
		return vy_mem_iterator_start(itr);
	if (!itr->curr_stmt) /* End of search. */
		return 1;
	assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
	vy_mem_iterator_check_version(itr);
	assert(itr->curr_stmt == vy_mem_iterator_curr_stmt(itr));
	const struct key_def *key_def = itr->mem->key_def;

	struct vy_mem_tree_iterator next_pos = itr->curr_pos;
	vy_mem_tree_iterator_next(&itr->mem->tree, &next_pos);
	if (vy_mem_tree_iterator_is_invalid(&next_pos))
		return 1; /* EOF */

	const struct tuple *next_stmt;
	next_stmt = *vy_mem_tree_iterator_get_elem(&itr->mem->tree, &next_pos);
	if (vy_stmt_compare(itr->curr_stmt, next_stmt, key_def) == 0) {
		itr->curr_pos = next_pos;
		itr->curr_stmt = next_stmt;
		return 0;
	}
	return 1;
}

/**
 * Find next (lower, older) record with the same key as current
 * @retval 0 success or EOF (*ret == NULL)
 */
static NODISCARD int
vy_mem_iterator_next_lsn(struct vy_stmt_iterator *vitr, struct tuple **ret)
{
	assert(vitr->iface->next_lsn == vy_mem_iterator_next_lsn);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *) vitr;
	*ret = NULL;
	if (vy_mem_iterator_next_lsn_impl(itr) == 0)
		return vy_mem_iterator_copy_to(itr, ret);
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
static NODISCARD int
vy_mem_iterator_restore(struct vy_stmt_iterator *vitr,
			const struct tuple *last_stmt, struct tuple **ret,
			bool *stop)
{
	(void)stop;
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *) vitr;
	const struct key_def *def = itr->mem->key_def;
	int rc;
	int64_t last_stmt_lsn = 0;
	if (last_stmt != NULL)
		last_stmt_lsn = vy_stmt_lsn(last_stmt);
	*ret = NULL;
	/*
	 * Last_stmt has lsn > iterator vlsn. It is
	 * possible, when we restore on prepared, but not commited
	 * statement from transaction placed in read view.
	 */
	bool last_stmt_is_prepared = last_stmt != NULL &&
				     last_stmt_lsn > (**itr->read_view).vlsn;
	assert(last_stmt == NULL || itr->curr_stmt == NULL ||
	       iterator_direction(itr->iterator_type) *
	       vy_tuple_compare(itr->curr_stmt, last_stmt, def) > 0 ||
	       (vy_tuple_compare(itr->curr_stmt, last_stmt, def) == 0 &&
		vy_stmt_lsn(itr->curr_stmt) >= last_stmt_lsn));

	if (!itr->search_started) {
		if (last_stmt == NULL) {
			if (vy_mem_iterator_start(itr) == 0)
				return vy_mem_iterator_copy_to(itr, ret);
			return 0;
		}

		/*
		 * Restoration is very similar to first search so we'll use
		 * that.
		 */
		enum iterator_type iterator_type = itr->iterator_type;
		if (iterator_type == ITER_GT || iterator_type == ITER_EQ)
			iterator_type = ITER_GE;
		else if (iterator_type == ITER_LT)
			iterator_type = ITER_LE;
		rc = vy_mem_iterator_start_from(itr, iterator_type, last_stmt);
		if (rc > 0) /* Search ended. */
			return 0;
		bool position_changed = true;
		if (vy_stmt_compare(itr->curr_stmt, last_stmt, def) == 0) {
			if (last_stmt_is_prepared) {
				rc = vy_mem_iterator_next_key_impl(itr);
				assert(rc >= 0);
			} else if (vy_stmt_lsn(itr->curr_stmt) >=
				   last_stmt_lsn) {
				position_changed = false;
				/*
				 * Skip the same stmt to next stmt or older
				 * version.
				 */
				do {
					rc = vy_mem_iterator_next_lsn_impl(itr);
					if (rc == 0) /* Move further. */
						continue;
					assert(rc > 0);
					rc = vy_mem_iterator_next_key_impl(itr);
					assert(rc >= 0);
					break;
				} while (vy_stmt_lsn(itr->curr_stmt) >=
					 last_stmt_lsn);
				if (itr->curr_stmt != NULL)
					position_changed = true;
			}
		} else if (itr->iterator_type == ITER_EQ &&
			   vy_stmt_compare(itr->key, itr->curr_stmt,
					   def) != 0) {
			return 1;
		}
		if (itr->curr_stmt != NULL &&
		    vy_mem_iterator_copy_to(itr, ret) < 0)
			return -1;
		/* remove this */
		assert(last_stmt != NULL);
		if (itr->curr_stmt) {
			int dir = iterator_direction(itr->iterator_type);
			assert(dir * vy_tuple_compare(*ret, last_stmt, def) >= 0);
			(void)dir;
		}
		return position_changed;
	}

	if (itr->version == itr->mem->version) {
		if (itr->curr_stmt)
			return vy_mem_iterator_copy_to(itr, ret);
		return 0;
	}

	if (last_stmt == NULL || itr->curr_stmt == NULL) {
		itr->version = itr->mem->version;
		const struct tuple *was_stmt = itr->curr_stmt;
		itr->search_started = false;
		itr->curr_stmt = NULL;
		if (last_stmt == NULL) {
			vy_mem_iterator_start(itr);
		} else {
			enum iterator_type iterator_type = itr->iterator_type;
			if (iterator_type == ITER_GT ||
			    iterator_type == ITER_EQ)
				iterator_type = ITER_GE;
			else if (iterator_type == ITER_LT)
				iterator_type = ITER_LE;
			vy_mem_iterator_start_from(itr, iterator_type,
						   last_stmt);
		}
		if (itr->curr_stmt != NULL &&
		    vy_mem_iterator_copy_to(itr, ret) < 0)
			return -1;
		return was_stmt != itr->curr_stmt;
	}

	vy_mem_iterator_check_version(itr);
	if (itr->iterator_type == ITER_GE || itr->iterator_type == ITER_GT ||
	    itr->iterator_type == ITER_EQ) {
		/* we need to find min(key) and (if there're several) max(lsn)
		 * with the following requirements:
		 * 1) <= curr_stmt
		 * 2) >= last_stmt
		 * 3) <= vlsn
		 * 4) < last_stmt.lsn (only if it is == last_stmt) */
		/* Search to the left from itr->curr_stmt up to last_stmt
		 * for something that fits lsn requirements.
		 * The last found is the result */
		rc = 0;
		struct vy_mem_tree_iterator pos = itr->curr_pos; /* (1) */
		while (true) {
			vy_mem_tree_iterator_prev(&itr->mem->tree, &pos);
			if (vy_mem_tree_iterator_is_invalid(&pos))
				break;
			const struct tuple *t =
				*vy_mem_tree_iterator_get_elem(&itr->mem->tree,
							       &pos);
			int cmp = vy_stmt_compare(t, last_stmt, def);
			if (cmp < 0 || (cmp == 0 &&
			    vy_stmt_lsn(t) >= last_stmt_lsn))
				break; /* (2) and (4) */
			if (vy_stmt_lsn(t) <= (**itr->read_view).vlsn) { /*(3)*/
				/* saving the last that fits the requirements */
				itr->curr_pos = pos;
				itr->curr_stmt = t;
				rc = 1;
			}
		}
		/* Check some requirements */
		assert(vy_stmt_lsn(itr->curr_stmt) <= (**itr->read_view).vlsn);
		assert(vy_stmt_compare(itr->curr_stmt, last_stmt, def) >= 0);
		assert(vy_stmt_compare(itr->curr_stmt, last_stmt, def) != 0 ||
		       vy_stmt_lsn(itr->curr_stmt) < last_stmt_lsn ||
		       last_stmt_is_prepared);
		if (vy_mem_iterator_copy_to(itr, ret) < 0)
			return -1;
		return rc;
	}
	assert(itr->iterator_type == ITER_LE || itr->iterator_type == ITER_LT);
	/* we need to find max(key) and (if there're several) max(lsn)
	 * with the following requirements:
	 * 1) >= curr_stmt
	 * 2) <= last_stmt
	 * 3) <= vlsn
	 * 4) < last_stmt.lsn (only if it is == last_stmt) */
	/* A) there might be some tuples == curr_stmt with lsn > curr_lsn,
	 * we have to try to find that one (with max(lsn))
	 * B) for every key > curr_stmt (but <= last_stmt) we must look for
	 * tuple that fits requirements (and with max(lsn) for that key)
	 * After A and B the last tuple we met is the result */
	rc = 0;
	/* A */
	bool curr_last = vy_stmt_compare(itr->curr_stmt, last_stmt, def) == 0;
	struct vy_mem_tree_iterator pos = itr->curr_pos; /* (1) */
	while (true) {
		vy_mem_tree_iterator_prev(&itr->mem->tree, &pos);
		if (vy_mem_tree_iterator_is_invalid(&pos))
			break;
		const struct tuple *t =
			*vy_mem_tree_iterator_get_elem(&itr->mem->tree, &pos);
		int cmp = vy_stmt_compare(t, itr->curr_stmt, def);
		assert(cmp <= 0); /* (2) is automatic */
		if (cmp < 0 || vy_stmt_lsn(t) > (**itr->read_view).vlsn)
			break; /* (1) and (3) */
		if (curr_last && vy_stmt_lsn(t) >= last_stmt_lsn)
			break; /* 4 */
		/* saving max(lsn) tuple within this key */
		itr->curr_pos = pos;
		itr->curr_stmt = t;
		rc = 1;
	}
	if (curr_last) {
		/* Don't need (B) part */
		goto done;
	}
	/* B */
	/* forward to the next key */
	pos = itr->curr_pos; /* (1) */
	const struct tuple *this_key = NULL;
	while (true) {
		vy_mem_tree_iterator_next(&itr->mem->tree, &pos);
		if (vy_mem_tree_iterator_is_invalid(&pos)) {
			/* nothing there */
			goto done;
		}
		this_key = *vy_mem_tree_iterator_get_elem(&itr->mem->tree, &pos);
		int cmp = vy_stmt_compare(this_key, itr->curr_stmt, def);
		assert(cmp >= 0);
		if (cmp > 0)
			break;
	}
	/* scan key by key */
	assert(!curr_last);
	bool is_last  = false;
	bool found = false; /* found within this key */
	while (true) {
		if (!found &&
		    vy_stmt_lsn(this_key) <= (**itr->read_view).vlsn && /*(3)*/
		    (!is_last ||
		     vy_stmt_lsn(this_key) < last_stmt_lsn)) /* 4*/ {
			/* saving max(lsn) tuple within this key */
			found = true;
			itr->curr_pos = pos;
			itr->curr_stmt = this_key;
			rc = 1;
		}
		vy_mem_tree_iterator_next(&itr->mem->tree, &pos);
		if (vy_mem_tree_iterator_is_invalid(&pos))
			goto done;
		const struct tuple *t =
			*vy_mem_tree_iterator_get_elem(&itr->mem->tree,
						       &pos);
		int cmp = vy_stmt_compare(t, this_key, def);
		assert(cmp >= 0);
		if (cmp > 0) {
			if (is_last)
				goto done; /* (2) */
			cmp = vy_stmt_compare(t, last_stmt, def);
			if (cmp > 0)
				goto done; /* (2) */
			if (cmp == 0)
				is_last = true;
			found = false;
		}
		this_key = t;
	}
done:
	/* Check some requirements */
	assert(vy_stmt_lsn(itr->curr_stmt) <= (**itr->read_view).vlsn);
	assert(vy_stmt_compare(itr->curr_stmt, last_stmt, def) <= 0);
	assert(vy_stmt_compare(itr->curr_stmt, last_stmt, def) != 0 ||
	       vy_stmt_lsn(itr->curr_stmt) < last_stmt_lsn ||
	       last_stmt_is_prepared);
	if (vy_mem_iterator_copy_to(itr, ret) < 0)
		return -1;
	return rc;
}

/**
 * Free all resources allocated in a worker thread.
 */
static void
vy_mem_iterator_cleanup(struct vy_stmt_iterator *vitr)
{
	assert(vitr->iface->cleanup == vy_mem_iterator_cleanup);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *) vitr;
	if (itr->last_stmt != NULL)
		tuple_unref(itr->last_stmt);
	itr->last_stmt = NULL;
}

/**
 * Close the iterator and free resources.
 * Can be called only after cleanup().
 */
static void
vy_mem_iterator_close(struct vy_stmt_iterator *vitr)
{
	assert(vitr->iface->close == vy_mem_iterator_close);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *) vitr;
	assert(itr->last_stmt == NULL);
	if (itr->before_first != NULL)
		tuple_unref(itr->before_first);
	TRASH(itr);
	(void) itr;
}

static const struct vy_stmt_iterator_iface vy_mem_iterator_iface = {
	.next_key = vy_mem_iterator_next_key,
	.next_lsn = vy_mem_iterator_next_lsn,
	.restore = vy_mem_iterator_restore,
	.cleanup = vy_mem_iterator_cleanup,
	.close = vy_mem_iterator_close
};

static NODISCARD int
vy_mem_stream_next(struct vy_stmt_stream *virt_stream, struct tuple **ret)
{
	assert(virt_stream->iface->next == vy_mem_stream_next);
	struct vy_mem_stream *stream = (struct vy_mem_stream *)virt_stream;

	struct tuple **res = (struct tuple **)
		vy_mem_tree_iterator_get_elem(&stream->mem->tree,
					      &stream->curr_pos);
	if (res == NULL) {
		*ret = NULL;
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
	stream->curr_pos = vy_mem_tree_iterator_first(&mem->tree);
}

/* }}} vy_mem_iterator API implementation */
