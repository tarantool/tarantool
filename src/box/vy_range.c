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
#include "vy_range.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RB_COMPACT 1
#include <small/rb.h>
#include <small/rlist.h>

#include "diag.h"
#include "iterator_type.h"
#include "key_def.h"
#include "trivia/util.h"
#include "tuple.h"
#include "vy_run.h"
#include "vy_stat.h"
#include "vy_stmt.h"

int
vy_range_tree_cmp(struct vy_range *range_a, struct vy_range *range_b)
{
	if (range_a == range_b)
		return 0;

	/* Any key > -inf. */
	if (range_a->begin == NULL)
		return -1;
	if (range_b->begin == NULL)
		return 1;

	assert(range_a->cmp_def == range_b->cmp_def);
	return vy_key_compare(range_a->begin, range_b->begin,
			      range_a->cmp_def);
}

int
vy_range_tree_key_cmp(const struct tuple *stmt, struct vy_range *range)
{
	/* Any key > -inf. */
	if (range->begin == NULL)
		return 1;
	return vy_stmt_compare_with_key(stmt, range->begin, range->cmp_def);
}

struct vy_range *
vy_range_tree_find_by_key(vy_range_tree_t *tree,
			  enum iterator_type iterator_type,
			  const struct tuple *key)
{
	uint32_t key_field_count = tuple_field_count(key);
	if (key_field_count == 0) {
		switch (iterator_type) {
		case ITER_LT:
		case ITER_LE:
		case ITER_REQ:
			return vy_range_tree_last(tree);
		case ITER_GT:
		case ITER_GE:
		case ITER_EQ:
			return vy_range_tree_first(tree);
		default:
			unreachable();
			return NULL;
		}
	}
	struct vy_range *range;
	if (iterator_type == ITER_GE || iterator_type == ITER_GT ||
	    iterator_type == ITER_EQ) {
		/**
		 * Case 1. part_count == 1, looking for [10]. ranges:
		 * {1, 3, 5} {7, 8, 9} {10, 15 20} {22, 32, 42}
		 *                      ^looking for this
		 * Case 2. part_count == 1, looking for [10]. ranges:
		 * {1, 2, 4} {5, 6, 7, 8} {50, 100, 200}
		 *            ^looking for this
		 * Case 3. part_count == 2, looking for [10]. ranges:
		 * {[1, 2], [2, 3]} {[9, 1], [10, 1], [10 2], [11 3]} {[12,..}
		 *                   ^looking for this
		 * Case 4. part_count == 2, looking for [10]. ranges:
		 * {[1, 2], [10, 1]} {[10, 2] [10 3] [11 3]} {[12, 1]..}
		 *  ^looking for this
		 * Case 5. part_count does not matter, looking for [10].
		 * ranges:
		 * {100, 200}, {300, 400}
		 * ^looking for this
		 */
		/**
		 * vy_range_tree_psearch finds least range with begin == key
		 * or previous if equal was not found
		 */
		range = vy_range_tree_psearch(tree, key);
		/* switch to previous for case (4) */
		if (range != NULL && range->begin != NULL &&
		    key_field_count < range->cmp_def->part_count &&
		    vy_stmt_compare_with_key(key, range->begin,
					     range->cmp_def) == 0)
			range = vy_range_tree_prev(tree, range);
		/* for case 5 or subcase of case 4 */
		if (range == NULL)
			range = vy_range_tree_first(tree);
	} else {
		assert(iterator_type == ITER_LT || iterator_type == ITER_LE ||
		       iterator_type == ITER_REQ);
		/**
		 * Case 1. part_count == 1, looking for [10]. ranges:
		 * {1, 3, 5} {7, 8, 9} {10, 15 20} {22, 32, 42}
		 *                      ^looking for this
		 * Case 2. part_count == 1, looking for [10]. ranges:
		 * {1, 2, 4} {5, 6, 7, 8} {50, 100, 200}
		 *            ^looking for this
		 * Case 3. part_count == 2, looking for [10]. ranges:
		 * {[1, 2], [2, 3]} {[9, 1], [10, 1], [10 2], [11 3]} {[12,..}
		 *                   ^looking for this
		 * Case 4. part_count == 2, looking for [10]. ranges:
		 * {[1, 2], [10, 1]} {[10, 2] [10 3] [11 3]} {[12, 1]..}
		 *                    ^looking for this
		 * Case 5. part_count does not matter, looking for [10].
		 * ranges:
		 * {1, 2}, {3, 4, ..}
		 *          ^looking for this
		 */
		/**
		 * vy_range_tree_nsearch finds most range with begin == key
		 * or next if equal was not found
		 */
		range = vy_range_tree_nsearch(tree, key);
		if (range != NULL) {
			/* fix curr_range for cases 2 and 3 */
			if (range->begin != NULL &&
			    vy_stmt_compare_with_key(key, range->begin,
						     range->cmp_def) != 0) {
				struct vy_range *prev;
				prev = vy_range_tree_prev(tree, range);
				if (prev != NULL)
					range = prev;
			}
		} else {
			/* Case 5 */
			range = vy_range_tree_last(tree);
		}
	}
	return range;
}

struct vy_range *
vy_range_new(int64_t id, struct tuple *begin, struct tuple *end,
	     const struct key_def *cmp_def)
{
	struct vy_range *range = calloc(1, sizeof(*range));
	if (range == NULL) {
		diag_set(OutOfMemory, sizeof(*range),
			 "malloc", "struct vy_range");
		return NULL;
	}
	range->id = id;
	if (begin != NULL) {
		tuple_ref(begin);
		range->begin = begin;
	}
	if (end != NULL) {
		tuple_ref(end);
		range->end = end;
	}
	range->cmp_def = cmp_def;
	rlist_create(&range->slices);
	range->heap_node.pos = UINT32_MAX;
	return range;
}

void
vy_range_delete(struct vy_range *range)
{
	if (range->begin != NULL)
		tuple_unref(range->begin);
	if (range->end != NULL)
		tuple_unref(range->end);

	struct vy_slice *slice, *next_slice;
	rlist_foreach_entry_safe(slice, &range->slices, in_range, next_slice)
		vy_slice_delete(slice);

	TRASH(range);
	free(range);
}

int
vy_range_snprint(char *buf, int size, const struct vy_range *range)
{
	int total = 0;
	SNPRINT(total, snprintf, buf, size, "(");
	if (range->begin != NULL)
		SNPRINT(total, tuple_snprint, buf, size, range->begin);
	else
		SNPRINT(total, snprintf, buf, size, "-inf");
	SNPRINT(total, snprintf, buf, size, "..");
	if (range->end != NULL)
		SNPRINT(total, tuple_snprint, buf, size, range->end);
	else
		SNPRINT(total, snprintf, buf, size, "inf");
	SNPRINT(total, snprintf, buf, size, ")");
	return total;
}

void
vy_range_add_slice(struct vy_range *range, struct vy_slice *slice)
{
	rlist_add_entry(&range->slices, slice, in_range);
	range->slice_count++;
	vy_disk_stmt_counter_add(&range->count, &slice->count);
}

void
vy_range_add_slice_before(struct vy_range *range, struct vy_slice *slice,
			  struct vy_slice *next_slice)
{
	rlist_add_tail(&next_slice->in_range, &slice->in_range);
	range->slice_count++;
	vy_disk_stmt_counter_add(&range->count, &slice->count);
}

void
vy_range_remove_slice(struct vy_range *range, struct vy_slice *slice)
{
	assert(range->slice_count > 0);
	assert(!rlist_empty(&range->slices));
	rlist_del_entry(slice, in_range);
	range->slice_count--;
	vy_disk_stmt_counter_sub(&range->count, &slice->count);
}

/**
 * To reduce write amplification caused by compaction, we follow
 * the LSM tree design. Runs in each range are divided into groups
 * called levels:
 *
 *   level 1: runs 1 .. L_1
 *   level 2: runs L_1 + 1 .. L_2
 *   ...
 *   level N: runs L_{N-1} .. L_N
 *
 * where L_N is the total number of runs, N is the total number of
 * levels, older runs have greater numbers. Runs at each subsequent
 * are run_size_ratio times larger than on the previous one. When
 * the number of runs at a level exceeds run_count_per_level, we
 * compact all its runs along with all runs from the upper levels
 * and in-memory indexes.  Including  previous levels into
 * compaction is relatively cheap, because of the level size
 * ratio.
 *
 * Given a range, this function computes the maximal level that needs
 * to be compacted and sets @compact_priority to the number of runs in
 * this level and all preceding levels.
 */
void
vy_range_update_compact_priority(struct vy_range *range,
				 const struct index_opts *opts)
{
	assert(opts->run_count_per_level > 0);
	assert(opts->run_size_ratio > 1);

	range->compact_priority = 0;

	/* Total number of checked runs. */
	uint32_t total_run_count = 0;
	/* The total size of runs checked so far. */
	uint64_t total_size = 0;
	/* Estimated size of a compacted run, if compaction is scheduled. */
	uint64_t est_new_run_size = 0;
	/* The number of runs at the current level. */
	uint32_t level_run_count = 0;
	/*
	 * The target (perfect) size of a run at the current level.
	 * For the first level, it's the size of the newest run.
	 * For lower levels it's computed as first level run size
	 * times run_size_ratio.
	 */
	uint64_t target_run_size = 0;

	struct vy_slice *slice;
	rlist_foreach_entry(slice, &range->slices, in_range) {
		uint64_t size = slice->count.bytes_compressed;
		/*
		 * The size of the first level is defined by
		 * the size of the most recent run.
		 */
		if (target_run_size == 0)
			target_run_size = size;
		total_size += size;
		level_run_count++;
		total_run_count++;
		while (size > target_run_size) {
			/*
			 * The run size exceeds the threshold
			 * set for the current level. Move this
			 * run down to a lower level. Switch the
			 * current level and reset the level run
			 * count.
			 */
			level_run_count = 1;
			/*
			 * If we have already scheduled
			 * a compaction of an upper level, and
			 * estimated compacted run will end up at
			 * this level, include the new run into
			 * this level right away to avoid
			 * a cascading compaction.
			 */
			if (est_new_run_size > target_run_size)
				level_run_count++;
			/*
			 * Calculate the target run size for this
			 * level.
			 */
			target_run_size *= opts->run_size_ratio;
			/*
			 * Keep pushing the run down until
			 * we find an appropriate level for it.
			 */
		}
		if (level_run_count > opts->run_count_per_level) {
			/*
			 * The number of runs at the current level
			 * exceeds the configured maximum. Arrange
			 * for compaction. We compact all runs at
			 * this level and upper levels.
			 */
			range->compact_priority = total_run_count;
			est_new_run_size = total_size;
		}
	}
}

/**
 * Return true and set split_key accordingly if the range needs to be
 * split in two.
 *
 * - We should never split a range until it was merged at least once
 *   (actually, it should be a function of run_count_per_level/number
 *   of runs used for the merge: with low run_count_per_level it's more
 *   than once, with high run_count_per_level it's once).
 * - We should use the last run size as the size of the range.
 * - We should split around the last run middle key.
 * - We should only split if the last run size is greater than
 *   4/3 * range_size.
 */
bool
vy_range_needs_split(struct vy_range *range, const struct index_opts *opts,
		     const char **p_split_key)
{
	struct vy_slice *slice;

	/* The range hasn't been merged yet - too early to split it. */
	if (range->n_compactions < 1)
		return false;

	/* Find the oldest run. */
	assert(!rlist_empty(&range->slices));
	slice = rlist_last_entry(&range->slices, struct vy_slice, in_range);

	/* The range is too small to be split. */
	if (slice->count.bytes_compressed < opts->range_size * 4 / 3)
		return false;

	/* Find the median key in the oldest run (approximately). */
	struct vy_page_info *mid_page;
	mid_page = vy_run_page_info(slice->run, slice->first_page_no +
				    (slice->last_page_no -
				     slice->first_page_no) / 2);

	struct vy_page_info *first_page = vy_run_page_info(slice->run,
						slice->first_page_no);

	/* No point in splitting if a new range is going to be empty. */
	if (key_compare(first_page->min_key, mid_page->min_key,
			range->cmp_def) == 0)
		return false;
	/*
	 * In extreme cases the median key can be < the beginning
	 * of the slice, e.g.
	 *
	 * RUN:
	 * ... |---- page N ----|-- page N + 1 --|-- page N + 2 --
	 *     | min_key = [10] | min_key = [50] | min_key = [100]
	 *
	 * SLICE:
	 * begin = [30], end = [70]
	 * first_page_no = N, last_page_no = N + 1
	 *
	 * which makes mid_page_no = N and mid_page->min_key = [10].
	 *
	 * In such cases there's no point in splitting the range.
	 */
	if (slice->begin != NULL && key_compare(mid_page->min_key,
			tuple_data(slice->begin), range->cmp_def) <= 0)
		return false;
	/*
	 * The median key can't be >= the end of the slice as we
	 * take the min key of a page for the median key.
	 */
	assert(slice->end == NULL || key_compare(mid_page->min_key,
			tuple_data(slice->end), range->cmp_def) < 0);

	*p_split_key = mid_page->min_key;
	return true;
}

/**
 * Check if a range should be coalesced with one or more its neighbors.
 * If it should, return true and set @p_first and @p_last to the first
 * and last ranges to coalesce, otherwise return false.
 *
 * We coalesce ranges together when they become too small, less than
 * half the target range size to avoid split-coalesce oscillations.
 */
bool
vy_range_needs_coalesce(struct vy_range *range, vy_range_tree_t *tree,
			const struct index_opts *opts,
			struct vy_range **p_first, struct vy_range **p_last)
{
	struct vy_range *it;

	/* Size of the coalesced range. */
	uint64_t total_size = range->count.bytes_compressed;
	/* Coalesce ranges until total_size > max_size. */
	uint64_t max_size = opts->range_size / 2;

	/*
	 * We can't coalesce a range that was scheduled for dump
	 * or compaction, because it is about to be processed by
	 * a worker thread.
	 */
	assert(!vy_range_is_scheduled(range));

	*p_first = *p_last = range;
	for (it = vy_range_tree_next(tree, range);
	     it != NULL && !vy_range_is_scheduled(it);
	     it = vy_range_tree_next(tree, it)) {
		uint64_t size = it->count.bytes_compressed;
		if (total_size + size > max_size)
			break;
		total_size += size;
		*p_last = it;
	}
	for (it = vy_range_tree_prev(tree, range);
	     it != NULL && !vy_range_is_scheduled(it);
	     it = vy_range_tree_prev(tree, it)) {
		uint64_t size = it->count.bytes_compressed;
		if (total_size + size > max_size)
			break;
		total_size += size;
		*p_first = it;
	}
	return *p_first != *p_last;
}
