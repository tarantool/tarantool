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

#include "memtx_read_set.h"
#include "index.h"
#include "key_def.h"

int
memtx_read_interval_cmpl(const struct memtx_read_interval *a,
		      const struct memtx_read_interval *b)
{
	assert(a->index == b->index);
	assert(a->left.key != NULL);
	assert(a->right.key != NULL);
	assert(b->left.key != NULL);
	assert(b->right.key != NULL);

	struct key_def *cmp_def = a->index->def->cmp_def;
	int cmp = key_compare_ext(a->left.key, a->left.hint,
				  b->left.key, b->left.hint, cmp_def);
	if (cmp != 0)
		return cmp;
	if (a->left_belongs && !b->left_belongs)
		return -1;
	if (!a->left_belongs && b->left_belongs)
		return 1;
	if (is_inf(a->left.key) || is_inf(b->left.key))
		return 0;

	const char* a_key = a->left.key;
	uint32_t a_parts = mp_decode_array(&a_key);
	const char* b_key = b->left.key;
	uint32_t b_parts = mp_decode_array(&b_key);
	if (a->left_belongs)
		return a_parts < b_parts ? -1 : a_parts > b_parts;
	else
		return a_parts > b_parts ? -1 : a_parts < b_parts;
}

int
memtx_read_interval_cmpr(const struct memtx_read_interval *a,
		      const struct memtx_read_interval *b)
{
	assert(a->index == b->index);
	assert(a->left.key != NULL);
	assert(a->right.key != NULL);
	assert(b->left.key != NULL);
	assert(b->right.key != NULL);

	struct key_def *cmp_def = a->index->def->cmp_def;
	int cmp = key_compare_ext(a->right.key, a->right.hint,
				  b->right.key, b->right.hint, cmp_def);
	if (cmp != 0)
		return cmp;
	if (a->right_belongs && !b->right_belongs)
		return 1;
	if (!a->right_belongs && b->right_belongs)
		return -1;
	if (is_inf(a->right.key) || is_inf(b->right.key))
		return 0;

	const char* a_key = a->right.key;
	uint32_t a_parts = mp_decode_array(&a_key);
	const char* b_key = b->right.key;
	uint32_t b_parts = mp_decode_array(&b_key);
	if (a->right_belongs)
		return a_parts > b_parts ? -1 : a_parts < b_parts;
	else
		return a_parts < b_parts ? -1 : a_parts > b_parts;
}

bool
memtx_read_interval_should_merge(const struct memtx_read_interval *l,
			      const struct memtx_read_interval *r)
{
	assert(l->index == r->index);
	assert(l->left.key != NULL);
	assert(l->right.key != NULL);
	assert(r->left.key != NULL);
	assert(r->right.key != NULL);
	assert(memtx_read_interval_cmpl(l, r) <= 0);

	struct key_def *cmp_def = l->index->def->cmp_def;
	int cmp = key_compare_ext(l->right.key, l->right.hint,
				  r->left.key, r->left.hint, cmp_def);
	if (cmp > 0)
		return true;
	if (cmp < 0)
		return false;
	if (l->right_belongs && r->left_belongs)
		return true;
	if (!l->right_belongs && !r->left_belongs)
		return false;
	if (is_inf(l->right.key) || is_inf(r->left.key))
		return true;

	const char* left_key = l->right.key;
	uint32_t l_parts = mp_decode_array(&left_key);
	const char* right_key = r->left.key;
	uint32_t r_parts = mp_decode_array(&right_key);
	if (l->right_belongs)
		return l_parts <= r_parts;
	else
		return l_parts >= r_parts;
}

int
memtx_entry_compare(struct memtx_entry left, struct memtx_entry right,
	struct key_def *key_def)
{
	return key_compare_ext(left.key, left.hint, right.key, right.hint,
			       key_def);
}

static inline bool
memtx_entry_is_equal(struct memtx_entry a, struct memtx_entry b)
{
	return a.key == b.key && a.hint == b.hint;
}

struct txn *
memtx_tx_conflict_iterator_next(struct memtx_tx_conflict_iterator *it)
{
	struct memtx_read_interval *curr, *left, *right;
	while ((curr = memtx_index_read_set_walk_next(&it->tree_walk, it->tree_dir,
						 &left, &right)) != NULL) {
		struct key_def *cmp_def = curr->index->def->cmp_def;
		const struct memtx_read_interval *last = curr->subtree_last;

		assert(left == NULL || left->index == curr->index);
		assert(right == NULL || right->index == curr->index);

		int cmp_right = memtx_entry_compare(it->key, last->right,
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
		if (memtx_entry_is_equal(curr->left, last->right)) {
			/* Optimize comparison out. */
			cmp_left = cmp_right;
		} else {
			cmp_left = memtx_entry_compare(it->key, curr->left,
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
		if (memtx_entry_is_equal(curr->left, curr->right)) {
			/* Optimize comparison out. */
			cmp_right = cmp_left;
		} else if (curr != last) {
			cmp_right = memtx_entry_compare(it->key, curr->right,
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
