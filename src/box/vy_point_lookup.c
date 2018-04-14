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
#include "vy_point_lookup.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <small/region.h>
#include <small/rlist.h>

#include "fiber.h"

#include "vy_lsm.h"
#include "vy_stmt.h"
#include "vy_tx.h"
#include "vy_mem.h"
#include "vy_run.h"
#include "vy_cache.h"
#include "vy_upsert.h"

/**
 * History of a key in vinyl is a continuous sequence of statements of the
 * same key in order of decreasing lsn. The history can be represented as a
 * list, the structure below describes one node of the list.
 */
struct vy_history_node {
	/** Link in a history list. */
	struct rlist link;
	/** History statement. Referenced if @is_refable is set. */
	struct tuple *stmt;
	/**
	 * Set if the statement stored in this node is refable,
	 * i.e. has a reference counter that can be incremented
	 * to pin the statement in memory. Refable statements are
	 * referenced by the history. It is a responsibility of
	 * the user of the history to track lifetime of unrefable
	 * statements.
	 *
	 * Note, we need to store this flag here, because by the
	 * time we clean up a history list, unrefable statements
	 * stored in it might have been deleted, thus making
	 * vy_stmt_is_refable() unusable.
	 */
	bool is_refable;
};

/**
 * Append an (older) statement to a history list.
 * Returns 0 on success, -1 on memory allocation error.
 */
static int
vy_history_append_stmt(struct rlist *history, struct tuple *stmt)
{
	struct region *region = &fiber()->gc;
	struct vy_history_node *node = region_alloc(region, sizeof(*node));
	if (node == NULL) {
		diag_set(OutOfMemory, sizeof(*node), "region",
			 "struct vy_history_node");
		return -1;
	}
	node->is_refable = vy_stmt_is_refable(stmt);
	if (node->is_refable)
		tuple_ref(stmt);
	node->stmt = stmt;
	rlist_add_tail_entry(history, node, link);
	return 0;
}

/**
 * Unref statement if necessary, remove node from history if it's there.
 */
static void
vy_history_cleanup(struct rlist *history, size_t region_svp)
{
	struct vy_history_node *node;
	rlist_foreach_entry(node, history, link)
		if (node->is_refable)
			tuple_unref(node->stmt);

	region_truncate(&fiber()->gc, region_svp);
}

/**
 * Return true if the history of a key contains terminal node in the end,
 * i.e. REPLACE of DELETE statement.
 */
static bool
vy_history_is_terminal(struct rlist *history)
{
	if (rlist_empty(history))
		return false;
	struct vy_history_node *node = rlist_last_entry(history,
					struct vy_history_node, link);
	assert(vy_stmt_type(node->stmt) == IPROTO_REPLACE ||
	       vy_stmt_type(node->stmt) == IPROTO_DELETE ||
	       vy_stmt_type(node->stmt) == IPROTO_INSERT ||
	       vy_stmt_type(node->stmt) == IPROTO_UPSERT);
       return vy_stmt_type(node->stmt) != IPROTO_UPSERT;
}

/**
 * Scan TX write set for given key.
 * Add one or no statement to the history list.
 */
static int
vy_point_lookup_scan_txw(struct vy_lsm *lsm, struct vy_tx *tx,
			 struct tuple *key, struct rlist *history)
{
	if (tx == NULL)
		return 0;
	lsm->stat.txw.iterator.lookup++;
	struct txv *txv =
		write_set_search_key(&tx->write_set, lsm, key);
	assert(txv == NULL || txv->lsm == lsm);
	if (txv == NULL)
		return 0;
	vy_stmt_counter_acct_tuple(&lsm->stat.txw.iterator.get,
				   txv->stmt);
	return vy_history_append_stmt(history, txv->stmt);
}

/**
 * Scan LSM tree cache for given key.
 * Add one or no statement to the history list.
 */
static int
vy_point_lookup_scan_cache(struct vy_lsm *lsm,
			   const struct vy_read_view **rv,
			   struct tuple *key, struct rlist *history)
{
	lsm->cache.stat.lookup++;
	struct tuple *stmt = vy_cache_get(&lsm->cache, key);

	if (stmt == NULL || vy_stmt_lsn(stmt) > (*rv)->vlsn)
		return 0;

	vy_stmt_counter_acct_tuple(&lsm->cache.stat.get, stmt);
	return vy_history_append_stmt(history, stmt);
}

/**
 * Scan one particular mem.
 * Add found statements to the history list up to terminal statement.
 */
static int
vy_point_lookup_scan_mem(struct vy_lsm *lsm, struct vy_mem *mem,
			 const struct vy_read_view **rv,
			 struct tuple *key, struct rlist *history)
{
	struct tree_mem_key tree_key;
	tree_key.stmt = key;
	tree_key.lsn = (*rv)->vlsn;
	bool exact;
	struct vy_mem_tree_iterator mem_itr =
		vy_mem_tree_lower_bound(&mem->tree, &tree_key, &exact);
	lsm->stat.memory.iterator.lookup++;
	const struct tuple *stmt = NULL;
	if (!vy_mem_tree_iterator_is_invalid(&mem_itr)) {
		stmt = *vy_mem_tree_iterator_get_elem(&mem->tree, &mem_itr);
		if (vy_stmt_compare(stmt, key, mem->cmp_def) != 0)
			stmt = NULL;
	}

	if (stmt == NULL)
		return 0;

	while (true) {
		if (vy_history_append_stmt(history, (struct tuple *)stmt) != 0)
			return -1;

		vy_stmt_counter_acct_tuple(&lsm->stat.memory.iterator.get,
					   stmt);

		if (vy_history_is_terminal(history))
			break;

		if (!vy_mem_tree_iterator_next(&mem->tree, &mem_itr))
			break;

		const struct tuple *prev_stmt = stmt;
		stmt = *vy_mem_tree_iterator_get_elem(&mem->tree, &mem_itr);
		if (vy_stmt_lsn(stmt) >= vy_stmt_lsn(prev_stmt))
			break;
		if (vy_stmt_compare(stmt, key, mem->cmp_def) != 0)
			break;
	}
	return 0;

}

/**
 * Scan all mems that belongs to the LSM tree.
 * Add found statements to the history list up to terminal statement.
 */
static int
vy_point_lookup_scan_mems(struct vy_lsm *lsm, const struct vy_read_view **rv,
			  struct tuple *key, struct rlist *history)
{
	assert(lsm->mem != NULL);
	int rc = vy_point_lookup_scan_mem(lsm, lsm->mem, rv, key, history);
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &lsm->sealed, in_sealed) {
		if (rc != 0 || vy_history_is_terminal(history))
			return rc;

		rc = vy_point_lookup_scan_mem(lsm, mem, rv, key, history);
	}
	return 0;
}

/**
 * Scan one particular slice.
 * Add found statements to the history list up to terminal statement.
 * Set *terminal_found to true if the terminal statement (DELETE or REPLACE)
 * was found.
 */
static int
vy_point_lookup_scan_slice(struct vy_lsm *lsm, struct vy_slice *slice,
			   const struct vy_read_view **rv, struct tuple *key,
			   struct rlist *history, bool *terminal_found)
{
	int rc = 0;
	/*
	 * The format of the statement must be exactly the space
	 * format with the same identifier to fully match the
	 * format in vy_mem.
	 */
	struct vy_run_iterator run_itr;
	vy_run_iterator_open(&run_itr, &lsm->stat.disk.iterator, slice,
			     ITER_EQ, key, rv, lsm->cmp_def, lsm->key_def,
			     lsm->disk_format, lsm->index_id == 0);
	struct tuple *stmt;
	rc = vy_run_iterator_next_key(&run_itr, &stmt);
	while (rc == 0 && stmt != NULL) {
		if (vy_history_append_stmt(history, stmt) != 0) {
			rc = -1;
			break;
		}
		if (vy_stmt_type(stmt) != IPROTO_UPSERT) {
			*terminal_found = true;
			break;
		}
		rc = vy_run_iterator_next_lsn(&run_itr, &stmt);
	}
	vy_run_iterator_close(&run_itr);
	return rc;
}

/**
 * Find a range and scan all slices that belongs to the range.
 * Add found statements to the history list up to terminal statement.
 * All slices are pinned before first slice scan, so it's guaranteed
 * that complete history from runs will be extracted.
 */
static int
vy_point_lookup_scan_slices(struct vy_lsm *lsm, const struct vy_read_view **rv,
			    struct tuple *key, struct rlist *history)
{
	struct vy_range *range = vy_range_tree_find_by_key(lsm->tree,
							   ITER_EQ, key);
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
			rc = vy_point_lookup_scan_slice(lsm, slices[i],
					rv, key, history, &terminal_found);
		vy_slice_unpin(slices[i]);
	}
	return rc;
}

/**
 * Get a resultant statement from collected history.
 */
static int
vy_history_apply(struct rlist *history, const struct key_def *cmp_def,
		 struct tuple_format *format, int *upserts_applied,
		 struct tuple **ret)
{
	*ret = NULL;
	*upserts_applied = 0;
	if (rlist_empty(history))
		return 0;

	struct tuple *curr_stmt = NULL;
	struct vy_history_node *node = rlist_last_entry(history,
					struct vy_history_node, link);
	if (vy_history_is_terminal(history)) {
		if (vy_stmt_type(node->stmt) == IPROTO_DELETE) {
			/* Ignore terminal delete */
		} else if (!node->is_refable) {
			curr_stmt = vy_stmt_dup(node->stmt);
		} else {
			curr_stmt = node->stmt;
			tuple_ref(curr_stmt);
		}
		node = rlist_prev_entry_safe(node, history, link);
	}
	while (node != NULL) {
		struct tuple *stmt = vy_apply_upsert(node->stmt, curr_stmt,
						     cmp_def, format, true);
		++*upserts_applied;
		if (stmt == NULL)
			return -1;
		if (curr_stmt != NULL)
			tuple_unref(curr_stmt);
		curr_stmt = stmt;
		node = rlist_prev_entry_safe(node, history, link);
	}
	*ret = curr_stmt;
	return 0;
}

int
vy_point_lookup(struct vy_lsm *lsm, struct vy_tx *tx,
		const struct vy_read_view **rv,
		struct tuple *key, struct tuple **ret)
{
	assert(tuple_field_count(key) >= lsm->cmp_def->part_count);

	*ret = NULL;
	size_t region_svp = region_used(&fiber()->gc);
	double start_time = ev_monotonic_now(loop());
	int rc = 0;

	lsm->stat.lookup++;
	/* History list */
	struct rlist history;
restart:
	rlist_create(&history);

	rc = vy_point_lookup_scan_txw(lsm, tx, key, &history);
	if (rc != 0 || vy_history_is_terminal(&history))
		goto done;

	rc = vy_point_lookup_scan_cache(lsm, rv, key, &history);
	if (rc != 0 || vy_history_is_terminal(&history))
		goto done;

	rc = vy_point_lookup_scan_mems(lsm, rv, key, &history);
	if (rc != 0 || vy_history_is_terminal(&history))
		goto done;

	/* Save version before yield */
	uint32_t mem_list_version = lsm->mem_list_version;

	rc = vy_point_lookup_scan_slices(lsm, rv, key, &history);
	if (rc != 0)
		goto done;

	ERROR_INJECT(ERRINJ_VY_POINT_ITER_WAIT, {
		while (mem_list_version == lsm->mem_list_version)
			fiber_sleep(0.01);
		/* Turn of the injection to avoid infinite loop */
		errinj(ERRINJ_VY_POINT_ITER_WAIT, ERRINJ_BOOL)->bparam = false;
	});

	if (mem_list_version != lsm->mem_list_version) {
		/*
		 * Mem list was changed during yield. This could be rotation
		 * or a dump. In case of dump the memory referenced by
		 * statement history is gone and we need to reread new history.
		 * This in unnecessary in case of rotation but since we
		 * cannot distinguish these two cases we always restart.
		 */
		vy_history_cleanup(&history, region_svp);
		goto restart;
	}

done:
	if (rc == 0) {
		int upserts_applied;
		rc = vy_history_apply(&history, lsm->cmp_def, lsm->mem_format,
				      &upserts_applied, ret);
		lsm->stat.upsert.applied += upserts_applied;
	}
	vy_history_cleanup(&history, region_svp);

	if (rc != 0)
		return -1;

	if (*ret != NULL) {
		vy_stmt_counter_acct_tuple(&lsm->stat.get, *ret);
		if ((*rv)->vlsn == INT64_MAX)
			vy_cache_add(&lsm->cache, *ret, NULL, key, ITER_EQ);
	}

	double latency = ev_monotonic_now(loop()) - start_time;
	latency_collect(&lsm->stat.latency, latency);

	if (latency > lsm->env->too_long_threshold) {
		say_warn("%s: get(%s) => %s took too long: %.3f sec",
			 vy_lsm_name(lsm), tuple_str(key),
			 vy_stmt_str(*ret), latency);
	}
	return 0;
}
