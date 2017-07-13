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
#include "vy_read_iterator.h"
#include "vy_run.h"
#include "vy_mem.h"
#include "vy_cache.h"
#include "vy_tx.h"
#include "fiber.h"
#include "vy_upsert.h"
#include "vy_index.h"
#include "vy_stat.h"

/* {{{ Merge iterator */

/**
 * Merge source, support structure for vy_merge_iterator
 * Contains source iterator, additional properties and merge state
 */
struct vy_merge_src {
	/** Source iterator */
	union {
		struct vy_run_iterator run_iterator;
		struct vy_mem_iterator mem_iterator;
		struct vy_txw_iterator txw_iterator;
		struct vy_cache_iterator cache_iterator;
		struct vy_stmt_iterator iterator;
	};
	/** Source can change during merge iteration */
	bool is_mutable;
	/** Source belongs to a range (@sa vy_merge_iterator comments). */
	bool belong_range;
	/**
	 * All sources with the same front_id as in struct
	 * vy_merge_iterator are on the same key of current output
	 * stmt (optimization)
	 */
	uint32_t front_id;
	struct tuple *stmt;
};

/**
 * Open the iterator.
 */
static void
vy_merge_iterator_open(struct vy_merge_iterator *itr,
		       enum iterator_type iterator_type,
		       const struct tuple *key,
		       const struct key_def *key_def,
		       struct tuple_format *format,
		       struct tuple_format *upsert_format,
		       bool is_primary)
{
	assert(key != NULL);
	itr->key_def = key_def;
	itr->format = format;
	itr->upsert_format = upsert_format;
	itr->is_primary = is_primary;
	itr->range_tree_version = 0;
	itr->p_mem_list_version = 0;
	itr->range_version = 0;
	itr->p_range_tree_version = NULL;
	itr->p_mem_list_version = NULL;
	itr->p_range_version = NULL;
	itr->key = key;
	itr->iterator_type = iterator_type;
	itr->src = NULL;
	itr->src_count = 0;
	itr->src_capacity = 0;
	itr->src = NULL;
	itr->curr_src = UINT32_MAX;
	itr->front_id = 1;
	itr->mutable_start = 0;
	itr->mutable_end = 0;
	itr->skipped_start = 0;
	itr->curr_stmt = NULL;
	itr->is_one_value = iterator_type == ITER_EQ &&
			    tuple_field_count(key) >= key_def->part_count;
	itr->unique_optimization =
		(iterator_type == ITER_EQ || iterator_type == ITER_GE ||
		 iterator_type == ITER_LE) &&
		tuple_field_count(key) >= key_def->part_count;
	itr->search_started = false;
	itr->range_ended = false;
}

/**
 * Free all resources allocated in a worker thread.
 */
static void
vy_merge_iterator_cleanup(struct vy_merge_iterator *itr)
{
	if (itr->curr_stmt != NULL) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
	}
	for (size_t i = 0; i < itr->src_count; i++) {
		vy_iterator_close_f cb =
			itr->src[i].iterator.iface->cleanup;
		if (cb != NULL)
			cb(&itr->src[i].iterator);
	}
	itr->src_capacity = 0;
	itr->range_version = 0;
	itr->range_tree_version = 0;
	itr->mem_list_version = 0;
	itr->p_range_version = NULL;
	itr->p_range_tree_version = NULL;
	itr->p_mem_list_version = NULL;
}

/**
 * Close the iterator and free resources.
 * Can be called only after cleanup().
 */
static void
vy_merge_iterator_close(struct vy_merge_iterator *itr)
{
	assert(cord_is_main());

	assert(itr->curr_stmt == NULL);
	for (size_t i = 0; i < itr->src_count; i++)
		itr->src[i].iterator.iface->close(&itr->src[i].iterator);
	free(itr->src);
	itr->src_count = 0;
	itr->src = NULL;
}

/**
 * Extend internal source array capacity to fit capacity sources.
 * Not necessary to call is but calling it allows to optimize internal memory
 * allocation
 */
static NODISCARD int
vy_merge_iterator_reserve(struct vy_merge_iterator *itr, uint32_t capacity)
{
	if (itr->src_capacity >= capacity)
		return 0;
	struct vy_merge_src *new_src = calloc(capacity, sizeof(*new_src));
	if (new_src == NULL) {
		diag_set(OutOfMemory, capacity * sizeof(*new_src),
			 "calloc", "new_src");
		return -1;
	}
	if (itr->src_count > 0) {
		memcpy(new_src, itr->src, itr->src_count * sizeof(*new_src));
		free(itr->src);
	}
	itr->src = new_src;
	itr->src_capacity = capacity;
	return 0;
}

/**
 * Add another source to merge iterator. Must be called before actual
 * iteration start and must not be called after.
 * @sa necessary order of adding requirements in struct vy_merge_iterator
 * comments.
 * The resulting vy_stmt_iterator must be properly initialized before merge
 * iteration start.
 * param is_mutable - Source can change during merge iteration
 * param belong_range - Source belongs to a range (see vy_merge_iterator comments)
 */
static struct vy_merge_src *
vy_merge_iterator_add(struct vy_merge_iterator *itr,
		      bool is_mutable, bool belong_range)
{
	assert(!itr->search_started);
	if (itr->src_count == itr->src_capacity) {
		if (vy_merge_iterator_reserve(itr, itr->src_count + 1) != 0)
			return NULL;
	}
	if (is_mutable) {
		if (itr->mutable_start == itr->mutable_end)
			itr->mutable_start = itr->src_count;
		itr->mutable_end = itr->src_count + 1;
	}
	itr->src[itr->src_count].front_id = 0;
	struct vy_merge_src *src = &itr->src[itr->src_count++];
	src->is_mutable = is_mutable;
	src->belong_range = belong_range;
	return src;
}

/*
 * Enable version checking.
 */
static void
vy_merge_iterator_set_version(struct vy_merge_iterator *itr,
			      const uint32_t *p_range_tree_version,
			      const uint32_t *p_mem_list_version,
			      const uint32_t *p_range_version)
{
	itr->p_range_tree_version = p_range_tree_version;
	if (itr->p_range_tree_version != NULL)
		itr->range_tree_version = *p_range_tree_version;
	itr->p_mem_list_version = p_mem_list_version;
	if (itr->p_mem_list_version != NULL)
		itr->mem_list_version = *p_mem_list_version;
	itr->p_range_version = p_range_version;
	if (itr->p_range_version != NULL)
		itr->range_version = *p_range_version;
}

/*
 * Try to restore position of merge iterator
 * @retval 0	if position did not change (iterator started)
 * @retval -2	iterator is no more valid
 */
static NODISCARD int
vy_merge_iterator_check_version(struct vy_merge_iterator *itr)
{
	if (itr->p_range_tree_version != NULL &&
	    *itr->p_range_tree_version != itr->range_tree_version)
		return -2;
	if (itr->p_mem_list_version != NULL &&
	    *itr->p_mem_list_version != itr->mem_list_version)
		return -2;
	if (itr->p_range_version != NULL &&
	    *itr->p_range_version != itr->range_version)
		return -2;
	return 0;
}

/**
 * Iterate to the next key
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read error
 * @retval -2 iterator is not valid anymore
 */
static NODISCARD int
vy_merge_iterator_next_key(struct vy_merge_iterator *itr, struct tuple **ret)
{
	*ret = NULL;
	if (itr->search_started && itr->is_one_value)
		return 0;
	itr->search_started = true;
	if (vy_merge_iterator_check_version(itr))
		return -2;
	const struct key_def *def = itr->key_def;
	int dir = iterator_direction(itr->iterator_type);
	uint32_t prev_front_id = itr->front_id;
	itr->front_id++;
	itr->curr_src = UINT32_MAX;
	struct tuple *min_stmt = NULL;
	itr->range_ended = true;
	int rc = 0;

	bool was_yield_possible = false;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		bool is_yield_possible = i >= itr->mutable_end;
		was_yield_possible = was_yield_possible || is_yield_possible;

		struct vy_merge_src *src = &itr->src[i];
		bool stop = false;
		if (src->front_id == prev_front_id) {
			assert(itr->curr_stmt != NULL);
			assert(i < itr->skipped_start);
			rc = src->iterator.iface->next_key(&src->iterator,
							   &src->stmt, &stop);
		} else if (i < itr->skipped_start || src->stmt == NULL) {
			/*
			 * Do not restore skipped unless it's the first round.
			 * Generally skipped srcs are handled below, but some
			 * iterators need to be restored before next_key call.
			 */
			rc = src->iterator.iface->restore(&src->iterator,
							  itr->curr_stmt,
							  &src->stmt, &stop);
			rc = rc > 0 ? 0 : rc;
		}
		if (vy_merge_iterator_check_version(itr))
			return -2;
		if (rc != 0)
			return rc;
		if (i >= itr->skipped_start && itr->curr_stmt != NULL) {
			while (src->stmt != NULL &&
			       dir * vy_tuple_compare(src->stmt, itr->curr_stmt,
						      def) <= 0) {
				rc = src->iterator.iface->next_key(&src->iterator,
								   &src->stmt,
								   &stop);
				if (vy_merge_iterator_check_version(itr))
					return -2;
				if (rc != 0)
					return rc;
			}
		}
		if (i >= itr->skipped_start)
			itr->skipped_start++;

		if (stop && src->stmt == NULL && min_stmt == NULL) {
			itr->front_id++;
			itr->curr_src = i;
			src->front_id = itr->front_id;
			itr->skipped_start = i + 1;
			break;
		}
		if (src->stmt == NULL)
			continue;

		itr->range_ended = itr->range_ended && !src->belong_range;

		if (itr->unique_optimization &&
		    vy_stmt_compare(src->stmt, itr->key, def) == 0)
			stop = true;

		int cmp = min_stmt == NULL ? -1 :
			  dir * vy_tuple_compare(src->stmt, min_stmt, def);
		if (cmp < 0) {
			itr->front_id++;
			if (min_stmt)
				tuple_unref(min_stmt);
			min_stmt = src->stmt;
			tuple_ref(min_stmt);
			itr->curr_src = i;
		}
		if (cmp <= 0)
			src->front_id = itr->front_id;

		if (stop) {
			itr->skipped_start = i + 1;
			break;
		}
	}
	if (itr->skipped_start < itr->src_count)
		itr->range_ended = false;

	if (itr->curr_stmt != NULL && min_stmt != NULL)
		assert(dir * vy_tuple_compare(min_stmt, itr->curr_stmt, def) >= 0);

	for (int i = MIN(itr->skipped_start, itr->mutable_end) - 1;
	     was_yield_possible && i >= (int) itr->mutable_start; i--) {
		struct vy_merge_src *src = &itr->src[i];
		bool stop;
		rc = src->iterator.iface->restore(&src->iterator,
						  itr->curr_stmt,
						  &src->stmt, &stop);
		if (vy_merge_iterator_check_version(itr))
			return -2;
		if (rc < 0)
			return rc;
		if (rc == 0)
			continue;

		int cmp = min_stmt == NULL ? -1 :
			  dir * vy_tuple_compare(src->stmt, min_stmt, def);
		if (cmp < 0) {
			itr->front_id++;
			if (min_stmt)
				tuple_unref(min_stmt);
			min_stmt = src->stmt;
			tuple_ref(min_stmt);
			itr->curr_src = i;
			src->front_id = itr->front_id;
		} else if (cmp == 0) {
			itr->curr_src = MIN(itr->curr_src, (uint32_t)i);
			src->front_id = itr->front_id;
		}
		if (itr->curr_stmt != NULL && min_stmt != NULL)
			assert(dir * vy_tuple_compare(min_stmt, itr->curr_stmt, def) >= 0);
	}

	if (itr->skipped_start < itr->src_count)
		itr->range_ended = false;

	itr->unique_optimization = false;

	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = min_stmt;
	*ret = itr->curr_stmt;

	return 0;
}

/**
 * Iterate to the next (elder) version of the same key
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read error
 * @retval -2 iterator is not valid anymore
 */
static NODISCARD int
vy_merge_iterator_next_lsn(struct vy_merge_iterator *itr, struct tuple **ret)
{
	if (!itr->search_started)
		return vy_merge_iterator_next_key(itr, ret);
	*ret = NULL;
	if (itr->curr_src == UINT32_MAX)
		return 0;
	assert(itr->curr_stmt != NULL);
	const struct key_def *def = itr->key_def;
	struct vy_merge_src *src = &itr->src[itr->curr_src];
	struct vy_stmt_iterator *sub_itr = &src->iterator;
	int rc = sub_itr->iface->next_lsn(sub_itr, &src->stmt);
	if (vy_merge_iterator_check_version(itr))
		return -2;
	if (rc != 0)
		return rc;
	if (src->stmt != NULL) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = src->stmt;
		tuple_ref(itr->curr_stmt);
		*ret = itr->curr_stmt;
		return 0;
	}
	for (uint32_t i = itr->curr_src + 1; i < itr->src_count; i++) {
		src = &itr->src[i];

		if (i >= itr->skipped_start) {
			itr->skipped_start++;
			bool stop = false;
			int cmp = -1;
			while (true) {
				rc = src->iterator.iface->next_key(&src->iterator,
								   &src->stmt,
								   &stop);
				if (vy_merge_iterator_check_version(itr))
					return -2;
				if (rc != 0)
					return rc;
				if (src->stmt == NULL)
					break;
				cmp = vy_tuple_compare(src->stmt, itr->curr_stmt,
						       def);
				if (cmp >= 0)
					break;
			}
			if (cmp == 0)
				itr->src[i].front_id = itr->front_id;
		}

		if (itr->src[i].front_id == itr->front_id) {
			itr->curr_src = i;
			tuple_unref(itr->curr_stmt);
			itr->curr_stmt = itr->src[i].stmt;
			tuple_ref(itr->curr_stmt);
			*ret = itr->curr_stmt;
			return 0;
		}
	}
	itr->curr_src = UINT32_MAX;
	return 0;
}

/**
 * Squash in the single statement all rest statements of current key
 * starting from the current statement.
 *
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 error
 * @retval -2 invalid iterator
 */
static NODISCARD int
vy_merge_iterator_squash_upsert(struct vy_merge_iterator *itr,
				struct tuple **ret, bool suppress_error,
				int64_t *upserts_applied)
{
	*ret = NULL;
	struct tuple *t = itr->curr_stmt;

	if (t == NULL)
		return 0;
	/* Upserts enabled only in the primary index. */
	assert(vy_stmt_type(t) != IPROTO_UPSERT || itr->is_primary);
	tuple_ref(t);
	while (vy_stmt_type(t) == IPROTO_UPSERT) {
		struct tuple *next;
		int rc = vy_merge_iterator_next_lsn(itr, &next);
		if (rc != 0) {
			tuple_unref(t);
			return rc;
		}
		if (next == NULL)
			break;
		struct tuple *applied;
		assert(itr->is_primary);
		applied = vy_apply_upsert(t, next, itr->key_def, itr->format,
					  itr->upsert_format, suppress_error);
		++*upserts_applied;
		tuple_unref(t);
		if (applied == NULL)
			return -1;
		t = applied;
	}
	*ret = t;
	return 0;
}

/**
 * Restore the position of merge iterator after the given key
 * and according to the initial retrieval order.
 */
static NODISCARD int
vy_merge_iterator_restore(struct vy_merge_iterator *itr,
			  const struct tuple *last_stmt)
{
	itr->unique_optimization = false;
	int result = 0;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		struct vy_stmt_iterator *sub_itr = &itr->src[i].iterator;
		bool stop;
		int rc = sub_itr->iface->restore(sub_itr, last_stmt,
						 &itr->src[i].stmt, &stop);
		if (rc < 0)
			return rc;
		if (vy_merge_iterator_check_version(itr) != 0)
			return -2;
		result = result || rc;
	}
	itr->skipped_start = itr->src_count;
	return result;
}

/* }}} Merge iterator */
/* {{{ Iterator over index */
#if 0

/**
 * ID of an iterator source type. Can be used in bitmaps.
 */
enum iterator_src_type {
	ITER_SRC_TXW = 1,
	ITER_SRC_CACHE = 2,
	ITER_SRC_MEM = 4,
	ITER_SRC_RUN = 8,
};

/**
 * History of a key in vinyl is a continuous sequence of statements of the
 * same key in order of decreasing lsn. The history can be represented as a
 * list, the structure below describes one node of the list.
 */
struct vy_stmt_history_node {
	/* Type of source that the history statement came from */
	enum iterator_src_type src_type;
	/* The history statement. Referenced for runs. */
	struct tuple *stmt;
	/* Link in the history list */
	struct rlist link;
};

/**
 * Point iterator is a special read iterator that is designed for
 * retrieving one value from index by a full key (all parts are present).
 *
 * Iterator collects necessary history of the given key from different sources
 * (txw, cache, mems, runs) that consists of some number of sequential upserts
 * and possibly one terminal statement (replace or delete). The iterator
 * sequentially scans txw, cache, mems and runs until a terminal statement is
 * met. After reading the slices the iterator checks that the list of mems
 * hasn't been changed and restarts if it is the case.
 * After the history is collected the iterator calculates resultant statement
 * and, if the result is the latest version of the key, adds it to cache.
 */
struct vy_point_iterator {
	/** Vinyl run environment. */
	struct vy_run_env *run_env;
	/* Search location and options */
	struct vy_index *index;
	struct vy_tx *tx;
	const struct vy_read_view **p_read_view;
	const struct tuple *key;

	/**
	 *  For compatibility reasons, the iterator references the
	 * resultant statement until own destruction.
	 */
	struct tuple *curr_stmt;
};

/**
 * Create an iterator by full key.
 */
static void
vy_point_iterator_open(struct vy_point_iterator *itr, struct vy_run_env *run_env,
		       struct vy_index *index, struct vy_tx *tx,
		       const struct vy_read_view **rv, const struct tuple *key)
{
	itr->run_env = run_env;
	vy_index_ref(index);
	itr->index = index;
	itr->tx = tx;
	itr->p_read_view = rv;
	itr->key = key;

	itr->curr_stmt = NULL;
}

/**
 * Allocate (region) new history node.
 * @return new node or NULL on memory error (diag is set).
 */
static struct vy_stmt_history_node *
vy_point_iterator_new_node()
{
	struct region *region = &fiber()->gc;
	struct vy_stmt_history_node *node = region_alloc(region, sizeof(*node));
	if (node == NULL)
		diag_set(OutOfMemory, sizeof(*node), "region",
			 "struct vy_stmt_history_node");
	return node;
}

/**
 * Unref statement if necessary, remove node from history if it's there.
 */
static void
vy_point_iterator_cleanup(struct rlist *history, size_t region_svp)
{
	struct vy_stmt_history_node *node;
	rlist_foreach_entry(node, history, link)
		if (node->src_type == ITER_SRC_RUN)
			tuple_unref(node->stmt);

	region_truncate(&fiber()->gc, region_svp);
}

/**
 * Free resources and close the iterator.
 */
static void
vy_point_iterator_close(struct vy_point_iterator *itr)
{
	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	vy_index_unref(itr->index);
	TRASH(itr);
}

/**
 * Return true if the history of a key contains terminal node in the end,
 * i.e. REPLACE of DELETE statement.
 */
static bool
vy_point_iterator_history_is_terminal(struct rlist *history)
{
	if (rlist_empty(history))
		return false;
	struct vy_stmt_history_node *node =
		rlist_last_entry(history, struct vy_stmt_history_node, link);
	assert(vy_stmt_type(node->stmt) == IPROTO_REPLACE ||
	       vy_stmt_type(node->stmt) == IPROTO_DELETE ||
	       vy_stmt_type(node->stmt) == IPROTO_UPSERT);
       return vy_stmt_type(node->stmt) != IPROTO_UPSERT;
}

/**
 * Scan TX write set for given key.
 * Add one or no statement to the history list.
 */
static int
vy_point_iterator_scan_txw(struct vy_point_iterator *itr, struct rlist *history)
{
	struct vy_tx *tx = itr->tx;
	if (tx == NULL)
		return 0;
	itr->index->stat.txw.iterator.lookup++;
	struct txv *txv =
		write_set_search_key(&tx->write_set, itr->index, itr->key);
	if (txv == NULL)
		return 0;
	vy_stmt_counter_acct_tuple(&itr->index->stat.txw.iterator.get,
				   txv->stmt);
	struct vy_stmt_history_node *node = vy_point_iterator_new_node();
	if (node == NULL)
		return -1;
	node->src_type = ITER_SRC_TXW;
	node->stmt = txv->stmt;
	rlist_add_tail(history, &node->link);
	return 0;
}

/**
 * Scan index cache for given key.
 * Add one or no statement to the history list.
 */
static int
vy_point_iterator_scan_cache(struct vy_point_iterator *itr,
			     struct rlist *history)
{
	itr->index->cache.stat.lookup++;
	struct tuple *stmt = vy_cache_get(&itr->index->cache, itr->key);

	if (stmt == NULL || vy_stmt_lsn(stmt) > (*itr->p_read_view)->vlsn)
		return 0;

	vy_stmt_counter_acct_tuple(&itr->index->cache.stat.get, stmt);
	struct vy_stmt_history_node *node = vy_point_iterator_new_node();
	if (node == NULL)
		return -1;

	node->src_type = ITER_SRC_CACHE;
	node->stmt = stmt;
	rlist_add_tail(history, &node->link);
	return 0;
}

/**
 * Scan one particular mem.
 * Add found statements to the history list up to terminal statement.
 */
static int
vy_point_iterator_scan_mem(struct vy_point_iterator *itr, struct vy_mem *mem,
			   struct rlist *history)
{
	struct tree_mem_key tree_key;
	tree_key.stmt = itr->key;
	tree_key.lsn = (*itr->p_read_view)->vlsn;
	bool exact;
	struct vy_mem_tree_iterator mem_itr =
		vy_mem_tree_lower_bound(&mem->tree, &tree_key, &exact);
	itr->index->stat.memory.iterator.lookup++;
	const struct tuple *stmt = NULL;
	if (!vy_mem_tree_iterator_is_invalid(&mem_itr)) {
		stmt = *vy_mem_tree_iterator_get_elem(&mem->tree, &mem_itr);
		if (vy_stmt_compare(stmt, itr->key, mem->key_def) != 0)
			stmt = NULL;
	}

	if (stmt == NULL)
		return 0;

	while (true) {
		struct vy_stmt_history_node *node = vy_point_iterator_new_node();
		if (node == NULL)
			return -1;

		vy_stmt_counter_acct_tuple(&itr->index->stat.memory.iterator.get,
					   stmt);

		node->src_type = ITER_SRC_MEM;
		node->stmt = (struct tuple *)stmt;
		rlist_add_tail(history, &node->link);
		if (vy_point_iterator_history_is_terminal(history))
			break;

		if (!vy_mem_tree_iterator_next(&mem->tree, &mem_itr))
			break;

		const struct tuple *prev_stmt = stmt;
		stmt = *vy_mem_tree_iterator_get_elem(&mem->tree, &mem_itr);
		if (vy_stmt_lsn(stmt) >= vy_stmt_lsn(prev_stmt))
			break;
		if (vy_stmt_compare(stmt, itr->key, mem->key_def) != 0)
			break;
	}
	return 0;

}

/**
 * Scan all mems that belongs to the index.
 * Add found statements to the history list up to terminal statement.
 */
static int
vy_point_iterator_scan_mems(struct vy_point_iterator *itr,
			    struct rlist *history)
{
	assert(itr->index->mem != NULL);
	int rc = vy_point_iterator_scan_mem(itr, itr->index->mem, history);
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &itr->index->sealed, in_sealed) {
		if (rc != 0 || vy_point_iterator_history_is_terminal(history))
			return rc;

		rc = vy_point_iterator_scan_mem(itr, itr->index->mem, history);
	}
	return 0;
}

/**
 * Scan one particular slice.
 * Add found statements to the history list up to terminal statement.
 */
static int
vy_point_iterator_scan_slice(struct vy_point_iterator *itr,
			     struct vy_slice *slice,
			     struct rlist *history)
{
	int rc = 0;
	/*
	 * The format of the statement must be exactly the space
	 * format with the same identifier to fully match the
	 * format in vy_mem.
	 */
	struct vy_index *index = itr->index;
	struct tuple_format *format = (index->space_index_count == 1 ?
				       index->space_format :
				       index->surrogate_format);
	struct vy_run_iterator run_itr;
	vy_run_iterator_open(&run_itr, &index->stat.disk.iterator,
			     itr->run_env, slice, ITER_EQ, itr->key,
			     itr->p_read_view, index->key_def,
			     index->user_key_def, format,
			     index->upsert_format, index->id == 0);
	while (true) {
		struct tuple *stmt;
		rc = run_itr.base.iface->next_lsn(&run_itr.base, &stmt);
		if (rc != 0)
			break;
		if (stmt == NULL)
			break;

		struct vy_stmt_history_node *node = vy_point_iterator_new_node();
		if (node == NULL) {
			rc = -1;
			break;
		}

		node->src_type = ITER_SRC_RUN;
		node->stmt = stmt;
		tuple_ref(stmt);
		rlist_add_tail(history, &node->link);
		if(vy_point_iterator_history_is_terminal(history))
			break;
	}
	run_itr.base.iface->cleanup(&run_itr.base);
	run_itr.base.iface->close(&run_itr.base);
	return rc;
}

/**
 * Find a range and scan all slices that belongs to the range.
 * Add found statements to the history list up to terminal statement.
 * All slices are pinned before first slice scan, so it's guaranteed
 * that complete history from runs will be extracted.
 */
static int
vy_point_iterator_scan_slices(struct vy_point_iterator *itr,
			      struct rlist *history)
{
	struct vy_range *range = vy_range_tree_find_by_key(itr->index->tree,
							   ITER_EQ, itr->key);
	assert(range != NULL);
	int slice_count = range->slice_count;
	struct vy_slice **slices = (struct vy_slice **)
		region_alloc(&fiber()->gc, slice_count * sizeof(*slices));
	if (slices == NULL) {
		diag_set(OutOfMemory, slice_count * sizeof(*slices),
			 "region", "slices array");
		return -1;
	}
	int i = 0;
	struct vy_slice *slice;
	rlist_foreach_entry(slice, &range->slices, in_range) {
		vy_slice_pin(slice);
		slices[i++] = slice;
	}
	assert(i == slice_count);
	int rc = 0;
	for (i = 0; i < slice_count; i++) {
		if (rc == 0 && !vy_point_iterator_history_is_terminal(history))
			rc = vy_point_iterator_scan_slice(itr, slices[i],
							  history);
		vy_slice_unpin(slices[i]);
	}
	return rc;
}

/**
 * Get a resultant statement from collected history. Add to cache if possible.
 */
static int
vy_point_iterator_apply_history(struct vy_point_iterator *itr,
				struct rlist *history)
{
	assert(itr->curr_stmt == NULL);
	if (rlist_empty(history))
		return 0;

	int64_t vlsn = (*itr->p_read_view)->vlsn;

	struct vy_stmt_history_node *node =
		rlist_last_entry(history, struct vy_stmt_history_node, link);
	if (vy_point_iterator_history_is_terminal(history)) {
		if (vy_stmt_type(node->stmt) == IPROTO_DELETE) {
			/* Ignore terminal delete */
		} else if (node->src_type == ITER_SRC_MEM) {
			itr->curr_stmt = vy_stmt_dup(node->stmt,
						     tuple_format(node->stmt));
		} else {
			itr->curr_stmt = node->stmt;
			tuple_ref(itr->curr_stmt);
		}
		node = rlist_prev_entry_safe(node, history, link);
	}
	while (node != NULL) {
		assert(vy_stmt_type(node->stmt) == IPROTO_UPSERT);
		if (vy_stmt_lsn(node->stmt) > vlsn) {
			/* We were sent to read view, skip the statement */
			node = rlist_prev_entry_safe(node, history, link);
			continue;
		}

		struct tuple *stmt =
			vy_apply_upsert(node->stmt, itr->curr_stmt,
					itr->index->key_def,
					itr->index->space_format,
					itr->index->upsert_format, true);
		itr->index->stat.upsert.applied++;
		if (stmt == NULL)
			return -1;
		if (itr->curr_stmt)
			tuple_unref(itr->curr_stmt);
		itr->curr_stmt = stmt;
		node = rlist_prev_entry_safe(node, history, link);
	}
	if (itr->curr_stmt) {
		vy_stmt_counter_acct_tuple(&itr->index->stat.get,
					   itr->curr_stmt);
	}
	/**
	 * Add a statement to the cache
	 */
	if ((**itr->p_read_view).vlsn == INT64_MAX) /* Do not store non-latest data */
		vy_cache_add(&itr->index->cache, itr->curr_stmt, NULL,
			     itr->key, ITER_EQ);
	return 0;
}

/*
 * Get a resultant tuple from the iterator. Actually do not change
 * iterator state thus second call will return the same statement
 * (unlike all other iterators that would return NULL on the second call)
 */
static int
vy_point_iterator_get(struct vy_point_iterator *itr, struct tuple **result)
{
	*result = NULL;
	size_t region_svp = region_used(&fiber()->gc);
	int rc = 0;

	itr->index->stat.lookup++;
	/* History list */
	struct rlist history;
restart:
	rlist_create(&history);

	rc = vy_point_iterator_scan_txw(itr, &history);
	if (rc != 0 || vy_point_iterator_history_is_terminal(&history))
		goto done;

	vy_point_iterator_scan_cache(itr, &history);
	if (rc != 0 || vy_point_iterator_history_is_terminal(&history))
		goto done;

	rc = vy_point_iterator_scan_mems(itr, &history);
	if (rc != 0 || vy_point_iterator_history_is_terminal(&history))
		goto done;

	/*
	 * From this moment we have to notify TX manager that we
	 * are about to read the key and if a new statement with the same
	 * key arrives we will be sent to read view.
	 */
	if (itr->tx != NULL) {
		rc = vy_tx_track(itr->tx, itr->index,
				 (struct tuple *) itr->key, false);
	}
	/* Save version before yield */
	uint32_t mem_list_version = itr->index->mem_list_version;

	rc = vy_point_iterator_scan_slices(itr, &history);
	if (rc != 0)
		goto done;

	if (mem_list_version != itr->index->mem_list_version) {
		/*
		 * Mem list was changed during yield. This could be rotation
		 * or a dump. In case of dump the memory referenced by
		 * statement history is gone and we need to reread new history.
		 * This in unnecessary in case of rotation but since we
		 * cannot distinguish these two cases we always restart.
		 */
		vy_point_iterator_cleanup(&history, region_svp);
		goto restart;
	}

done:
	if (rc == 0)
		rc = vy_point_iterator_apply_history(itr, &history);
	*result = itr->curr_stmt;
	vy_point_iterator_cleanup(&history, region_svp);
	return rc;
}
#endif

static void
vy_read_iterator_add_tx(struct vy_read_iterator *itr)
{
	assert(itr->tx != NULL);
	struct vy_txw_iterator_stat *stat = &itr->index->stat.txw.iterator;
	struct vy_merge_src *sub_src =
		vy_merge_iterator_add(&itr->merge_iterator, true, false);
	vy_txw_iterator_open(&sub_src->txw_iterator, stat, itr->tx, itr->index,
			     itr->iterator_type, itr->key);
	int rc = sub_src->iterator.iface->restore(&sub_src->iterator,
						  itr->curr_stmt, &sub_src->stmt, NULL);
	(void)rc;
}

static void
vy_read_iterator_add_cache(struct vy_read_iterator *itr)
{
	struct vy_merge_src *sub_src =
		vy_merge_iterator_add(&itr->merge_iterator, true, false);
	vy_cache_iterator_open(&sub_src->cache_iterator,
			       &itr->index->cache, itr->iterator_type,
			       itr->key, itr->read_view);
	if (itr->curr_stmt != NULL) {
		/*
		 * In order not to loose stop flag, do not restore cache
		 * iterator in general case (itr->curr_stmt)
		 */
		bool stop = false;
		int rc = sub_src->iterator.iface->restore(&sub_src->iterator,
							  itr->curr_stmt,
							  &sub_src->stmt, &stop);
		(void)rc;
	}
}

static void
vy_read_iterator_add_mem(struct vy_read_iterator *itr)
{
	struct vy_index *index = itr->index;
	struct vy_merge_src *sub_src;

	/* Add the active in-memory index. */
	assert(index->mem != NULL);
	sub_src = vy_merge_iterator_add(&itr->merge_iterator, true, false);
	vy_mem_iterator_open(&sub_src->mem_iterator,
			     &index->stat.memory.iterator,
			     index->mem, itr->iterator_type, itr->key,
			     itr->read_view, itr->curr_stmt);
	/* Add sealed in-memory indexes. */
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &index->sealed, in_sealed) {
		sub_src = vy_merge_iterator_add(&itr->merge_iterator,
						false, false);
		vy_mem_iterator_open(&sub_src->mem_iterator,
				     &index->stat.memory.iterator,
				     mem, itr->iterator_type, itr->key,
				     itr->read_view, itr->curr_stmt);
	}
}

static void
vy_read_iterator_add_disk(struct vy_read_iterator *itr)
{
	assert(itr->curr_range != NULL);
	struct vy_index *index = itr->index;
	struct tuple_format *format;
	struct vy_slice *slice;
	/*
	 * The format of the statement must be exactly the space
	 * format with the same identifier to fully match the
	 * format in vy_mem.
	 */
	format = (index->space_index_count == 1 ?
		  index->space_format : index->surrogate_format);
	rlist_foreach_entry(slice, &itr->curr_range->slices, in_range) {
		/*
		 * vy_task_dump_complete() may yield after adding
		 * a new run slice to a range and before removing
		 * dumped in-memory trees. We must not add both
		 * the slice and the trees in this case, because
		 * merge iterator can't deal with duplicates.
		 * Since index->dump_lsn is bumped after deletion
		 * of dumped in-memory trees, we can filter out
		 * the run slice containing duplicates by LSN.
		 */
		if (slice->run->info.min_lsn > index->dump_lsn)
			continue;
		assert(slice->run->info.max_lsn <= index->dump_lsn);
		struct vy_merge_src *sub_src = vy_merge_iterator_add(
			&itr->merge_iterator, false, true);
		vy_run_iterator_open(&sub_src->run_iterator,
				     &index->stat.disk.iterator,
				     itr->run_env, slice,
				     itr->iterator_type, itr->key,
				     itr->read_view, index->key_def,
				     index->user_key_def, format,
				     index->upsert_format, index->id == 0);
	}
}

/**
 * Set up merge iterator for the current range.
 */
static void
vy_read_iterator_use_range(struct vy_read_iterator *itr)
{
	if (itr->tx != NULL)
		vy_read_iterator_add_tx(itr);

	vy_read_iterator_add_cache(itr);
	vy_read_iterator_add_mem(itr);

	if (itr->curr_range != NULL)
		vy_read_iterator_add_disk(itr);

	/* Enable range and range index version checks */
	vy_merge_iterator_set_version(&itr->merge_iterator,
				      &itr->index->range_tree_version,
				      &itr->index->mem_list_version,
				      itr->curr_range != NULL ?
				      &itr->curr_range->version : NULL);
}

/**
 * Open the iterator.
 */
void
vy_read_iterator_open(struct vy_read_iterator *itr, struct vy_run_env *run_env,
		      struct vy_index *index, struct vy_tx *tx,
		      enum iterator_type iterator_type, const struct tuple *key,
		      const struct vy_read_view **rv)
{
	itr->run_env = run_env;
	itr->index = index;
	itr->tx = tx;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;
	itr->search_started = false;
	itr->curr_stmt = NULL;
	itr->curr_range = NULL;
}

/**
 * Start lazy search
 */
static void
vy_read_iterator_start(struct vy_read_iterator *itr)
{
	assert(!itr->search_started);
	assert(itr->curr_stmt == NULL);
	assert(itr->curr_range == NULL);
	itr->search_started = true;

	vy_range_iterator_open(&itr->range_iterator, itr->index->tree,
			       itr->iterator_type, itr->key);
	vy_range_iterator_next(&itr->range_iterator, &itr->curr_range);
	vy_merge_iterator_open(&itr->merge_iterator, itr->iterator_type,
			       itr->key, itr->index->key_def,
			       itr->index->space_format,
			       itr->index->upsert_format, itr->index->id == 0);
	vy_read_iterator_use_range(itr);

	itr->index->stat.lookup++;
}

/**
 * Check versions of index and current range and restores position if
 * something was changed
 */
static NODISCARD int
vy_read_iterator_restore(struct vy_read_iterator *itr)
{
	int rc;
restart:
	vy_range_iterator_restore(&itr->range_iterator, itr->curr_stmt,
				  &itr->curr_range);
	/* Re-create merge iterator */
	vy_merge_iterator_cleanup(&itr->merge_iterator);
	vy_merge_iterator_close(&itr->merge_iterator);
	vy_merge_iterator_open(&itr->merge_iterator, itr->iterator_type,
			       itr->key, itr->index->key_def,
			       itr->index->space_format,
			       itr->index->upsert_format, itr->index->id == 0);
	vy_read_iterator_use_range(itr);
	rc = vy_merge_iterator_restore(&itr->merge_iterator, itr->curr_stmt);
	if (rc == -1)
		return -1;
	if (rc == -2)
		goto restart;
	return rc;
}

/**
 * Conventional wrapper around vy_merge_iterator_next_key() to automatically
 * re-create the merge iterator on vy_index/vy_range/vy_run changes.
 */
static NODISCARD int
vy_read_iterator_merge_next_key(struct vy_read_iterator *itr,
				struct tuple **ret)
{
	int rc;
	struct vy_merge_iterator *mi = &itr->merge_iterator;
retry:
	*ret = NULL;
	while ((rc = vy_merge_iterator_next_key(mi, ret)) == -2)
		if (vy_read_iterator_restore(itr) < 0)
			return -1;
	/*
	 * If the iterator after next_key is on the same key then
	 * go to the next.
	 */
	if (*ret != NULL && itr->curr_stmt != NULL &&
	    vy_tuple_compare(itr->curr_stmt, *ret, itr->index->key_def) == 0)
		goto retry;
	return rc;
}

/**
 * Goto next range according to order
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
static NODISCARD int
vy_read_iterator_next_range(struct vy_read_iterator *itr, struct tuple **ret)
{
	assert(itr->curr_range != NULL);
	*ret = NULL;
	struct tuple *stmt = NULL;
	int rc = 0;
	struct vy_index *index = itr->index;
restart:
	vy_merge_iterator_cleanup(&itr->merge_iterator);
	vy_merge_iterator_close(&itr->merge_iterator);
	vy_merge_iterator_open(&itr->merge_iterator, itr->iterator_type,
			       itr->key, index->key_def, index->space_format,
			       index->upsert_format, index->id == 0);
	vy_range_iterator_next(&itr->range_iterator, &itr->curr_range);
	vy_read_iterator_use_range(itr);
	rc = vy_read_iterator_merge_next_key(itr, &stmt);
	if (rc < 0)
		return -1;
	assert(rc >= 0);
	if (!stmt && itr->merge_iterator.range_ended && itr->curr_range != NULL)
		goto restart;

	if (stmt != NULL && itr->curr_range != NULL) {
		/** Check if the statement is out of the range. */
		int dir = iterator_direction(itr->iterator_type);
		if (dir >= 0 && itr->curr_range->end != NULL &&
		    vy_tuple_compare_with_key(stmt, itr->curr_range->end,
					      index->key_def) >= 0) {
			goto restart;
		}
		if (dir < 0 && itr->curr_range->begin != NULL &&
		    vy_tuple_compare_with_key(stmt, itr->curr_range->begin,
					      index->key_def) < 0) {
			goto restart;
		}
	}

	*ret = stmt;
	return rc;
}

NODISCARD int
vy_read_iterator_next(struct vy_read_iterator *itr, struct tuple **result)
{
	ev_tstamp start_time = ev_now(loop());

	/* The key might be set to NULL during previous call, that means
	 * that there's no more data */
	if (itr->key == NULL) {
		*result = NULL;
		return 0;
	}
#if 0
	bool one_value = false;
	if (itr->iterator_type == ITER_EQ) {
		if (itr->index->opts.is_unique)
			one_value = tuple_field_count(itr->key) >=
				    itr->index->user_key_def->part_count;
		else
			one_value = tuple_field_count(itr->key) >=
				    itr->index->key_def->part_count;
	}
	/* Run a special iterator for a special case */
	if (one_value) {
		struct vy_point_iterator one;
		vy_point_iterator_open(&one, itr->run_env, itr->index,
				       itr->tx, itr->read_view, itr->key);
		int rc = vy_point_iterator_get(&one, result);
		if (*result) {
			tuple_ref(*result);
			itr->curr_stmt = *result;
		}
		vy_point_iterator_close(&one);
		itr->key = NULL;
		return rc;
	}
#endif

	*result = NULL;

	if (!itr->search_started)
		vy_read_iterator_start(itr);

	struct tuple *prev_key = itr->curr_stmt;
	if (prev_key != NULL)
		tuple_ref(prev_key);

	struct tuple *t = NULL;
	struct vy_merge_iterator *mi = &itr->merge_iterator;
	struct vy_index *index = itr->index;
	int rc = 0;
	while (true) {
		if (vy_read_iterator_merge_next_key(itr, &t)) {
			rc = -1;
			goto clear;
		}
restart:
		if (mi->range_ended && itr->curr_range != NULL &&
		    vy_read_iterator_next_range(itr, &t)) {
			rc = -1;
			goto clear;
		}
		if (t == NULL) {
			if (itr->curr_stmt != NULL)
				tuple_unref(itr->curr_stmt);
			itr->curr_stmt = NULL;
			rc = 0; /* No more data. */
			break;
		}
		rc = vy_merge_iterator_squash_upsert(mi, &t, true,
						     &index->stat.upsert.applied);
		if (rc != 0) {
			if (rc == -1)
				goto clear;
			do {
				if (vy_read_iterator_restore(itr) < 0) {
					rc = -1;
					goto clear;
				}
				rc = vy_merge_iterator_next_lsn(mi, &t);
			} while (rc == -2);
			if (rc != 0)
				goto clear;
			goto restart;
		}
		assert(t != NULL);
		if (vy_stmt_type(t) != IPROTO_DELETE) {
			if (vy_stmt_type(t) == IPROTO_UPSERT) {
				struct tuple *applied;
				assert(index->id == 0);
				applied = vy_apply_upsert(t, NULL,
							  index->key_def,
							  mi->format,
							  mi->upsert_format,
							  true);
				index->stat.upsert.applied++;
				tuple_unref(t);
				t = applied;
				assert(vy_stmt_type(t) == IPROTO_REPLACE);
			}
			if (itr->curr_stmt != NULL)
				tuple_unref(itr->curr_stmt);
			itr->curr_stmt = t;
			break;
		} else {
			tuple_unref(t);
		}
	}

	*result = itr->curr_stmt;
	assert(*result == NULL || vy_stmt_type(*result) == IPROTO_REPLACE);
	if (*result != NULL)
		vy_stmt_counter_acct_tuple(&index->stat.get, *result);

	/**
	 * Add a statement to the cache
	 */
	if ((**itr->read_view).vlsn == INT64_MAX) /* Do not store non-latest data */
		vy_cache_add(&itr->index->cache, *result, prev_key,
			     itr->key, itr->iterator_type);

clear:
	if (prev_key != NULL) {
		if (itr->curr_stmt != NULL)
			/*
			 * It is impossible to return fully equal
			 * statements in sequence. At least they
			 * must have different primary keys.
			 * (index->key_def includes primary
			 * parts).
			 */
			assert(vy_tuple_compare(prev_key, itr->curr_stmt,
						index->key_def) != 0);
		tuple_unref(prev_key);
	}

	latency_collect(&index->stat.latency, ev_now(loop()) - start_time);
	return rc;
}

/**
 * Close the iterator and free resources
 */
void
vy_read_iterator_close(struct vy_read_iterator *itr)
{
	assert(cord_is_main());
	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = NULL;
	if (itr->search_started)
		vy_merge_iterator_cleanup(&itr->merge_iterator);

	if (itr->search_started)
		vy_merge_iterator_close(&itr->merge_iterator);
}

/* }}} Iterator over index */
