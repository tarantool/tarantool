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
#include "vy_point_iterator.h"

#include "fiber.h"

#include "vy_index.h"
#include "vy_stmt.h"
#include "vy_tx.h"
#include "vy_mem.h"
#include "vy_run.h"
#include "vy_cache.h"
#include "vy_upsert.h"

void
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

void
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
	assert(txv == NULL || txv->index == itr->index);
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
		if (vy_stmt_compare(stmt, itr->key, mem->cmp_def) != 0)
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
		if (vy_stmt_compare(stmt, itr->key, mem->cmp_def) != 0)
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

		rc = vy_point_iterator_scan_mem(itr, mem, history);
	}
	return 0;
}

/**
 * Scan one particular slice.
 * Add found statements to the history list up to terminal statement.
 * Set *terminal_found to true if the terminal statement (DELETE or REPLACE)
 * was found.
 * @param itr - the iterator.
 * @param slice - a slice to scan.
 * @param history - history for adding statements.
 * @param terminal_found - is set to true if terminal stmt was found.
 * @return 0 on success, -1 otherwise.
 */
static int
vy_point_iterator_scan_slice(struct vy_point_iterator *itr,
			     struct vy_slice *slice, struct rlist *history,
			     bool *terminal_found)
{
	int rc = 0;
	/*
	 * The format of the statement must be exactly the space
	 * format with the same identifier to fully match the
	 * format in vy_mem.
	 */
	struct vy_index *index = itr->index;
	struct vy_run_iterator run_itr;
	vy_run_iterator_open(&run_itr, &index->stat.disk.iterator,
			     itr->run_env, slice, ITER_EQ, itr->key,
			     itr->p_read_view, index->cmp_def,
			     index->key_def, index->disk_format,
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
		if (vy_stmt_type(stmt) != IPROTO_UPSERT) {
			*terminal_found = true;
			break;
		}
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
	bool terminal_found = false;
	for (i = 0; i < slice_count; i++) {
		if (rc == 0 && !terminal_found)
			rc = vy_point_iterator_scan_slice(itr, slices[i],
							  history,
							  &terminal_found);
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
					itr->index->cmp_def,
					itr->index->mem_format,
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
int
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

	ERROR_INJECT(ERRINJ_VY_POINT_ITER_WAIT, {
		while (mem_list_version == itr->index->mem_list_version)
			fiber_sleep(0.01);
		/* Turn of the injection to avoid infinite loop */
		errinj(ERRINJ_VY_POINT_ITER_WAIT, ERRINJ_BOOL)->bparam = false;
	});

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
