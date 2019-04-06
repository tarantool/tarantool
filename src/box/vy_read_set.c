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
#include "vy_read_set.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "trivia/util.h"
#include "tuple.h"
#include "vy_lsm.h"
#include "vy_stmt.h"

int
vy_read_interval_cmpl(const struct vy_read_interval *a,
		      const struct vy_read_interval *b)
{
	assert(a->lsm == b->lsm);
	struct key_def *cmp_def = a->lsm->cmp_def;
	int cmp = vy_entry_compare(a->left, b->left, cmp_def);
	if (cmp != 0)
		return cmp;
	if (a->left_belongs && !b->left_belongs)
		return -1;
	if (!a->left_belongs && b->left_belongs)
		return 1;
	uint32_t a_parts = vy_stmt_key_part_count(a->left.stmt, cmp_def);
	uint32_t b_parts = vy_stmt_key_part_count(b->left.stmt, cmp_def);
	if (a->left_belongs)
		return a_parts < b_parts ? -1 : a_parts > b_parts;
	else
		return a_parts > b_parts ? -1 : a_parts < b_parts;
}

int
vy_read_interval_cmpr(const struct vy_read_interval *a,
		      const struct vy_read_interval *b)
{
	assert(a->lsm == b->lsm);
	struct key_def *cmp_def = a->lsm->cmp_def;
	int cmp = vy_entry_compare(a->right, b->right, cmp_def);
	if (cmp != 0)
		return cmp;
	if (a->right_belongs && !b->right_belongs)
		return 1;
	if (!a->right_belongs && b->right_belongs)
		return -1;
	uint32_t a_parts = vy_stmt_key_part_count(a->right.stmt, cmp_def);
	uint32_t b_parts = vy_stmt_key_part_count(b->right.stmt, cmp_def);
	if (a->right_belongs)
		return a_parts > b_parts ? -1 : a_parts < b_parts;
	else
		return a_parts < b_parts ? -1 : a_parts > b_parts;
}

bool
vy_read_interval_should_merge(const struct vy_read_interval *l,
			      const struct vy_read_interval *r)
{
	assert(l->lsm == r->lsm);
	assert(vy_read_interval_cmpl(l, r) <= 0);
	struct key_def *cmp_def = l->lsm->cmp_def;
	int cmp = vy_entry_compare(l->right, r->left, cmp_def);
	if (cmp > 0)
		return true;
	if (cmp < 0)
		return false;
	if (l->right_belongs && r->left_belongs)
		return true;
	if (!l->right_belongs && !r->left_belongs)
		return false;
	uint32_t l_parts = vy_stmt_key_part_count(l->right.stmt, cmp_def);
	uint32_t r_parts = vy_stmt_key_part_count(r->left.stmt, cmp_def);
	if (l->right_belongs)
		return l_parts <= r_parts;
	else
		return l_parts >= r_parts;
}

struct vy_tx *
vy_tx_conflict_iterator_next(struct vy_tx_conflict_iterator *it)
{
	struct vy_read_interval *curr, *left, *right;
	while ((curr = vy_lsm_read_set_walk_next(&it->tree_walk, it->tree_dir,
						 &left, &right)) != NULL) {
		struct key_def *cmp_def = curr->lsm->cmp_def;
		const struct vy_read_interval *last = curr->subtree_last;

		assert(left == NULL || left->lsm == curr->lsm);
		assert(right == NULL || right->lsm == curr->lsm);

		int cmp_right = vy_entry_compare(it->key, last->right,
						 cmp_def);
		if (cmp_right == 0 && !last->right_belongs)
			cmp_right = 1;

		if (cmp_right > 0) {
			/*
			 * The point is to the right of the rightmost
			 * interval in the subtree so there cannot be
			 * any conflicts in this subtree.
			 */
			it->tree_dir = 0;
			continue;
		}

		int cmp_left;
		if (vy_entry_is_equal(curr->left, last->right)) {
			/* Optimize comparison out. */
			cmp_left = cmp_right;
		} else {
			cmp_left = vy_entry_compare(it->key, curr->left,
						    cmp_def);
			if (cmp_left == 0 && !curr->left_belongs)
				cmp_left = -1;
		}

		if (cmp_left < 0) {
			/*
			 * The point is to the left of the current interval
			 * so an intersection can only be found in the left
			 * subtree.
			 */
			it->tree_dir = RB_WALK_LEFT;
		} else {
			/*
			 * Both subtrees can have intervals that contain the
			 * given point.
			 */
			it->tree_dir = RB_WALK_LEFT | RB_WALK_RIGHT;
		}

		/*
		 * Check if the point is within the current interval.
		 */
		if (vy_entry_is_equal(curr->left, curr->right)) {
			/* Optimize comparison out. */
			cmp_right = cmp_left;
		} else if (curr != last) {
			cmp_right = vy_entry_compare(it->key, curr->right,
						     cmp_def);
			if (cmp_right == 0 && !curr->right_belongs)
				cmp_right = 1;
		}

		if (cmp_left >= 0 && cmp_right <= 0) {
			/*
			 * The point is within the current interval.
			 * Return the conflicting transaction before
			 * continuing tree traversal.
			 */
			break;
		}
	}
	return curr != NULL ? curr->tx : NULL;
}
