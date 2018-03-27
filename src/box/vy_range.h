#ifndef INCLUDES_TARANTOOL_BOX_VY_RANGE_H
#define INCLUDES_TARANTOOL_BOX_VY_RANGE_H
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

#include <stdbool.h>
#include <stdint.h>

#define RB_COMPACT 1
#include <small/rb.h>
#include <small/rlist.h>

#include "iterator_type.h"
#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"
#include "trivia/util.h"
#include "vy_stat.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct index_opts;
struct key_def;
struct tuple;
struct vy_slice;

/**
 * Range of keys in an LSM tree stored on disk.
 */
struct vy_range {
	/** Unique ID of this range. */
	int64_t id;
	/**
	 * Range lower bound. NULL if range is leftmost.
	 * Both 'begin' and 'end' statements have SELECT type with
	 * the full idexed key.
	 */
	struct tuple *begin;
	/** Range upper bound. NULL if range is rightmost. */
	struct tuple *end;
	/** Key definition for comparing range boundaries.
	 * Contains secondary and primary key parts for secondary
	 * keys, to ensure an always distinct result for
	 * non-unique keys.
	 */
	const struct key_def *cmp_def;
	/** An estimate of the number of statements in this range. */
	struct vy_disk_stmt_counter count;
	/**
	 * List of run slices in this range, linked by vy_slice->in_range.
	 * The newer a slice, the closer it to the list head.
	 */
	struct rlist slices;
	/** Number of entries in the ->slices list. */
	int slice_count;
	/**
	 * The goal of compaction is to reduce read amplification.
	 * All ranges for which the LSM tree has more runs per
	 * level than run_count_per_level or run size larger than
	 * one defined by run_size_ratio of this level are candidates
	 * for compaction.
	 * Unlike other LSM implementations, Vinyl can have many
	 * sorted runs in a single level, and is able to compact
	 * runs from any number of adjacent levels. Moreover,
	 * higher levels are always taken in when compacting
	 * a lower level - i.e. L1 is always included when
	 * compacting L2, and both L1 and L2 are always included
	 * when compacting L3.
	 *
	 * This variable contains the number of runs the next
	 * compaction of this range will include.
	 *
	 * The lower the level is scheduled for compaction,
	 * the bigger it tends to be because upper levels are
	 * taken in.
	 * @sa vy_range_update_compact_priority() to see
	 * how we  decide how many runs to compact next time.
	 */
	int compact_priority;
	/** Number of times the range was compacted. */
	int n_compactions;
	/** Link in vy_lsm->tree. */
	rb_node(struct vy_range) tree_node;
	/** Link in vy_lsm->range_heap. */
	struct heap_node heap_node;
	/**
	 * Incremented whenever a run is added to or deleted
	 * from this range. Used invalidate read iterators.
	 */
	uint32_t version;
};

/**
 * Heap of all ranges of the same LSM tree, prioritized by
 * vy_range->compact_priority.
 */
#define HEAP_NAME vy_range_heap
static inline bool
vy_range_heap_less(struct heap_node *a, struct heap_node *b)
{
	struct vy_range *r1 = container_of(a, struct vy_range, heap_node);
	struct vy_range *r2 = container_of(b, struct vy_range, heap_node);
	return r1->compact_priority > r2->compact_priority;
}
#define HEAP_LESS(h, l, r) vy_range_heap_less(l, r)
#include "salad/heap.h"
#undef HEAP_LESS
#undef HEAP_NAME

/** Return true if a task is scheduled for a given range. */
static inline bool
vy_range_is_scheduled(struct vy_range *range)
{
	return range->heap_node.pos == UINT32_MAX;
}

/**
 * Search tree of all ranges of the same LSM tree, sorted by
 * vy_range->begin. Ranges in a tree are supposed to span
 * all possible keys without overlaps.
 */
int
vy_range_tree_cmp(struct vy_range *range_a, struct vy_range *range_b);
int
vy_range_tree_key_cmp(const struct tuple *stmt, struct vy_range *range);

typedef rb_tree(struct vy_range) vy_range_tree_t;
rb_gen_ext_key(MAYBE_UNUSED static inline, vy_range_tree_, vy_range_tree_t,
	       struct vy_range, tree_node, vy_range_tree_cmp,
	       const struct tuple *, vy_range_tree_key_cmp);

/**
 * Find the first range in which a given key should be looked up.
 *
 * @param tree          Range tree to search.
 * @param iterator_type Iterator type.
 * @param key           Key to look up.
 *
 * @retval              The first range to look up the key in.
 */
struct vy_range *
vy_range_tree_find_by_key(vy_range_tree_t *tree,
			  enum iterator_type iterator_type,
			  const struct tuple *key);

/**
 * Allocate and initialize a range (either a new one or for
 * restore from disk).
 *
 * @param id        Range id.
 * @param begin     Range begin (inclusive) or NULL for -inf.
 * @param end       Range end (exclusive) or NULL for +inf.
 * @param cmp_def   Key definition for comparing range boundaries.
 *
 * @retval not NULL The new range.
 * @retval NULL     Out of memory.
 */
struct vy_range *
vy_range_new(int64_t id, struct tuple *begin, struct tuple *end,
	     const struct key_def *cmp_def);

/**
 * Free a range and all its slices.
 *
 * @param range     Range to free.
 */
void
vy_range_delete(struct vy_range *range);

/** An snprint-style function to print boundaries of a range. */
int
vy_range_snprint(char *buf, int size, const struct vy_range *range);

static inline const char *
vy_range_str(struct vy_range *range)
{
	char *buf = tt_static_buf();
	vy_range_snprint(buf, TT_STATIC_BUF_LEN, range);
	return buf;
}

/** Add a run slice to the head of a range's list. */
void
vy_range_add_slice(struct vy_range *range, struct vy_slice *slice);

/** Add a run slice to a range's list before @next_slice. */
void
vy_range_add_slice_before(struct vy_range *range, struct vy_slice *slice,
			  struct vy_slice *next_slice);

/** Remove a run slice from a range's list. */
void
vy_range_remove_slice(struct vy_range *range, struct vy_slice *slice);

/**
 * Update compaction priority of a range.
 *
 * @param range     The range.
 * @param opts      Index options.
 */
void
vy_range_update_compact_priority(struct vy_range *range,
				 const struct index_opts *opts);

/**
 * Check if a range needs to be split in two.
 *
 * @param range             The range.
 * @param opts              Index options.
 * @param[out] p_split_key  Key to split the range by.
 *
 * @retval true             If the range needs to be split.
 */
bool
vy_range_needs_split(struct vy_range *range, const struct index_opts *opts,
		     const char **p_split_key);

/**
 * Check if a range needs to be coalesced with adjacent
 * ranges in a range tree.
 *
 * @param range         The range.
 * @param tree          The range tree.
 * @param opts          Index options.
 * @param[out] p_first  The first range in the tree to coalesce.
 * @param[out] p_last   The last range in the tree to coalesce.
 *
 * @retval true         If the range needs to be coalesced.
 */
bool
vy_range_needs_coalesce(struct vy_range *range, vy_range_tree_t *tree,
			const struct index_opts *opts,
			struct vy_range **p_first, struct vy_range **p_last);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_RANGE_H */
