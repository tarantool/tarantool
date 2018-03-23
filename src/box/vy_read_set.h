#ifndef INCLUDES_TARANTOOL_BOX_VY_READ_SET_H
#define INCLUDES_TARANTOOL_BOX_VY_READ_SET_H
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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RB_COMPACT 1
#include <small/rb.h>

#include "salad/stailq.h"
#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;
struct vy_tx;
struct vy_lsm;

/**
 * A tuple interval read by a transaction.
 */
struct vy_read_interval {
	/** Transaction. */
	struct vy_tx *tx;
	/** LSM tree that the transaction read from. */
	struct vy_lsm *lsm;
	/** Left boundary of the interval. */
	struct tuple *left;
	/** Right boundary of the interval. */
	struct tuple *right;
	/** Set if the left boundary belongs to the interval. */
	bool left_belongs;
	/** Set if the right boundary belongs to the interval. */
	bool right_belongs;
	/**
	 * The interval with the max right boundary over
	 * all nodes in the subtree rooted at this node.
	 */
	const struct vy_read_interval *subtree_last;
	/** Link in vy_tx->read_set. */
	rb_node(struct vy_read_interval) in_tx;
	/** Link in vy_lsm->read_set. */
	rb_node(struct vy_read_interval) in_lsm;
	/**
	 * Auxiliary list node. Used by vy_tx_track() to
	 * link intervals to be merged.
	 */
	struct stailq_entry in_merge;
};

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
vy_read_interval_cmpl(const struct vy_read_interval *a,
		      const struct vy_read_interval *b);

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
vy_read_interval_cmpr(const struct vy_read_interval *a,
		      const struct vy_read_interval *b);

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
vy_read_interval_should_merge(const struct vy_read_interval *l,
			      const struct vy_read_interval *r);

/**
 * Tree that contains tuple intervals read by a transactions.
 * Linked by vy_read_interval->in_tx. Sorted by vy_lsm, then
 * by vy_read_interval->left. Intervals stored in this tree
 * must not intersect.
 */
typedef rb_tree(struct vy_read_interval) vy_tx_read_set_t;

static inline int
vy_tx_read_set_cmp(const struct vy_read_interval *a,
		   const struct vy_read_interval *b)
{
	assert(a->tx == b->tx);
	int rc = a->lsm < b->lsm ? -1 : a->lsm > b->lsm;
	if (rc == 0)
		rc = vy_read_interval_cmpl(a, b);
	return rc;
}

rb_gen(MAYBE_UNUSED static inline, vy_tx_read_set_, vy_tx_read_set_t,
       struct vy_read_interval, in_tx, vy_tx_read_set_cmp);

/**
 * Interval tree used for tracking reads done from an LSM tree by
 * all active transactions. Linked by vy_read_interval->in_lsm.
 * Sorted by vy_read_interval->left, then by vy_tx. Intervals that
 * belong to different transactions may intersect.
 */
typedef rb_tree(struct vy_read_interval) vy_lsm_read_set_t;

static inline int
vy_lsm_read_set_cmp(const struct vy_read_interval *a,
		    const struct vy_read_interval *b)
{
	assert(a->lsm == b->lsm);
	int rc = vy_read_interval_cmpl(a, b);
	if (rc == 0)
		rc = a->tx < b->tx ? -1 : a->tx > b->tx;
	return rc;
}

static inline void
vy_lsm_read_set_aug(struct vy_read_interval *node,
		    const struct vy_read_interval *left,
		    const struct vy_read_interval *right)
{
	node->subtree_last = node;
	if (left != NULL &&
	    vy_read_interval_cmpr(left->subtree_last, node->subtree_last) > 0)
		node->subtree_last = left->subtree_last;
	if (right != NULL &&
	    vy_read_interval_cmpr(right->subtree_last, node->subtree_last) > 0)
		node->subtree_last = right->subtree_last;
}

rb_gen_aug(MAYBE_UNUSED static inline, vy_lsm_read_set_, vy_lsm_read_set_t,
	   struct vy_read_interval, in_lsm, vy_lsm_read_set_cmp,
	   vy_lsm_read_set_aug);

/**
 * Iterator over transactions that conflict with a statement.
 */
struct vy_tx_conflict_iterator {
	/** The statement. */
	const struct tuple *stmt;
	/**
	 * Iterator over the interval tree checked
	 * for intersections with the statement.
	 */
	struct vy_lsm_read_set_walk tree_walk;
	/**
	 * Direction of tree traversal to be used on the
	 * next iteration.
	 */
	int tree_dir;
};

static inline void
vy_tx_conflict_iterator_init(struct vy_tx_conflict_iterator *it,
			     vy_lsm_read_set_t *read_set,
			     const struct tuple *stmt)
{
	vy_lsm_read_set_walk_init(&it->tree_walk, read_set);
	it->tree_dir = 0;
	it->stmt = stmt;
}

/**
 * Return the next conflicting transaction or NULL.
 * Note, the same transaction may be returned more than once.
 */
struct vy_tx *
vy_tx_conflict_iterator_next(struct vy_tx_conflict_iterator *it);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_READ_SET_H */
