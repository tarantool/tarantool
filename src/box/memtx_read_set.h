#ifndef INCLUDES_TARANTOOL_BOX_MEMTX_READ_SET_H
#define INCLUDES_TARANTOOL_BOX_MEMTX_READ_SET_H
/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RB_COMPACT 1
#include <small/rb.h>

#include "salad/stailq.h"
#include "trivia/util.h"
#include "tuple_compare.h"
#include "tuple.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct memtx_entry {
    const char *key;
    hint_t hint;
};

static inline struct memtx_entry
memtx_entry_empty()
{
	struct memtx_entry e = {NULL, HINT_NONE};
	return e;
}

static inline struct memtx_entry
memtx_entry_from_tuple(struct tuple *tuple)
{
	struct memtx_entry e = {tuple_data(tuple), HINT_NONE};
	return e;
}

/**
 * A tuple interval read by a transaction.
 */
struct memtx_read_interval {
    /** Transaction. */
    struct txn *tx;
    /** index tree that the transaction read from. */
    struct index *index;
    /** Left boundary of the interval. */
    struct memtx_entry left;
    /** Right boundary of the interval. */
    struct memtx_entry right;
    /** Set if the left boundary belongs to the interval. */
    bool left_belongs;
    /** Set if the right boundary belongs to the interval. */
    bool right_belongs;
    /**
     * The interval with the max right boundary over
     * all nodes in the subtree rooted at this node.
     */
    const struct memtx_read_interval *subtree_last;
    /** Link in memtx_tx->read_set. */
    rb_node(struct memtx_read_interval) in_tx;
    /** Link in memtx_index->read_set. */
    rb_node(struct memtx_read_interval) in_index;
    /**
     * Auxiliary list node. Used by memtx_tx_track() to
     * link intervals to be merged.
     */
    struct stailq_entry in_merge;
};

struct memtx_read_interval *
memtx_read_interval_new(struct txn *tx, struct index *index,
			struct memtx_entry left, bool left_belongs,
			struct memtx_entry right, bool right_belongs);

void
memtx_read_interval_delete(struct memtx_read_interval *interval);

/**
 * Compare left boundaries of two intervals.
 *
 * Let 'A' and 'B' be the intervals of keys from the left boundary
 * of 'a' and 'b' to plus infinity, respectively. Assume that
 *
 * - a > b iff A is spanned by B
 * - a = b iff A equals B
 * - a < b iff A spans B
 */
int
memtx_read_interval_cmpl(const struct memtx_read_interval *a,
		      const struct memtx_read_interval *b);

/**
 * Compare right boundaries of two intervals.
 *
 * Let 'A' and 'B' be the intervals of keys from minus infinity to
 * the right boundary of 'a' and 'b', respectively. Assume that
 *
 * - a > b iff A spans B
 * - a = b iff A equals B
 * - a < b iff A is spanned by B
 */
int
memtx_read_interval_cmpr(const struct memtx_read_interval *a,
		      const struct memtx_read_interval *b);

/**
 * Return true if two intervals should be merged.
 * Interval 'l' must start before interval 'r'.
 * Note, if this function returns true, it does not
 * necessarily mean that the intervals intersect -
 * they might complement each other, e.g.
 *
 *   (10, 12] and (12, 20]
 */
bool
memtx_read_interval_should_merge(const struct memtx_read_interval *l,
			      const struct memtx_read_interval *r);

/**
 * Tree that contains tuple intervals read by a transactions.
 * Linked by memtx_read_interval->in_tx. Sorted by memtx_index, then
 * by memtx_read_interval->left. Intervals stored in this tree
 * must not intersect.
 */
typedef rb_tree(struct memtx_read_interval) memtx_tx_read_set_t;

static inline int
memtx_tx_read_set_cmp(const struct memtx_read_interval *a,
		   const struct memtx_read_interval *b)
{
	assert(a->tx == b->tx);
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0)
		rc = memtx_read_interval_cmpl(a, b);
	return rc;
}

rb_gen(MAYBE_UNUSED static inline, memtx_tx_read_set_, memtx_tx_read_set_t,
       struct memtx_read_interval, in_tx, memtx_tx_read_set_cmp);

/**
 * Interval tree used for tracking reads done from an index tree by
 * all active transactions. Linked by memtx_read_interval->in_index.
 * Sorted by memtx_read_interval->left, then by tx. Intervals that
 * belong to different transactions may intersect.
 */
typedef rb_tree(struct memtx_read_interval) memtx_index_read_set_t;

static inline int
memtx_index_read_set_cmp(const struct memtx_read_interval *a,
		    const struct memtx_read_interval *b)
{
	assert(a->index == b->index);
	int rc = memtx_read_interval_cmpl(a, b);
	if (rc == 0)
		rc = a->tx < b->tx ? -1 : a->tx > b->tx;
	return rc;
}

static inline void
memtx_index_read_set_aug(struct memtx_read_interval *node,
		    const struct memtx_read_interval *left,
		    const struct memtx_read_interval *right)
{
	node->subtree_last = node;
	if (left != NULL &&
	    memtx_read_interval_cmpr(left->subtree_last, node->subtree_last) > 0)
		node->subtree_last = left->subtree_last;
	if (right != NULL &&
	    memtx_read_interval_cmpr(right->subtree_last, node->subtree_last) > 0)
		node->subtree_last = right->subtree_last;
}

rb_gen_aug(MAYBE_UNUSED static inline, memtx_index_read_set_, memtx_index_read_set_t,
	   struct memtx_read_interval, in_index, memtx_index_read_set_cmp,
	   memtx_index_read_set_aug);

/**
 * Iterator over transactions that conflict with a statement.
 */
struct memtx_tx_conflict_iterator {
    /** The statement. */
    struct memtx_entry key;
    /**
     * Iterator over the interval tree checked
     * for intersections with the statement.
     */
    struct memtx_index_read_set_walk tree_walk;
    /**
     * Direction of tree traversal to be used on the
     * next iteration.
     */
    int tree_dir;
};

static inline void
memtx_tx_conflict_iterator_init(struct memtx_tx_conflict_iterator *it,
			     memtx_index_read_set_t *read_set, struct memtx_entry key)
{
	memtx_index_read_set_walk_init(&it->tree_walk, read_set);
	it->tree_dir = 0;
	it->key = key;
}

/**
 * Return the next conflicting transaction or NULL.
 * Note, the same transaction may be returned more than once.
 */
struct txn *
memtx_tx_conflict_iterator_next(struct memtx_tx_conflict_iterator *it);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_MEMTX_READ_SET_H */
