/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Copyright (c) 1993-1994 by Xerox Corporation.  All rights reserved.
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose,  provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 *
 * Author: Hans-J. Boehm (boehm@parc.xerox.com)
 */

/**
 * This macro helps to implement some common rope functions, not
 * depending on a specific rope, only once, in a single object
 * file.
 */
#define ROPE_SRC
#define rope_data_t void *
#define rope_ctx_t void *
#include "rope.h"

#include <stdlib.h>

static inline int
avl_node_height(struct avl_node *node)
{
	return node ? node->height : 0;
}

static inline void
avl_node_relink(struct avl_node *node)
{
	node->tree_size = (avl_node_size(node->link[0]) +
			   avl_node_size(node->link[1]) +
			   node->leaf_size);
	node->height = MAX(avl_node_height(node->link[0]),
			   avl_node_height(node->link[1])) + 1;
}

static inline struct avl_node *
avl_rotate_single(struct avl_node *parent, int direction)
{
	struct avl_node *save = parent->link[!direction];

	parent->link[!direction] = save->link[direction];
	save->link[direction] = parent;

	/* First relink the parent, since it's now a child. */
	avl_node_relink(parent);
	avl_node_relink(save);

	return save;
}

static inline struct avl_node *
avl_rotate_double(struct avl_node *parent, int direction)
{
	parent->link[!direction] =
		avl_rotate_single(parent->link[!direction], !direction);
	return avl_rotate_single(parent, direction);
}

void
avl_rebalance_after_insert(struct avl_node ***path, struct avl_node ***p_end,
			   int insert_height)
{
	while (p_end > path) {
		struct avl_node *left = **p_end--;
		struct avl_node *parent = **p_end;
		/*
		 * To use the same rotation functions, set mirror
		 * to 1 if left is right and right is left.
		 */
		int mirror = left != parent->link[0];
		struct avl_node *right = parent->link[!mirror];

		int left_height = avl_node_height(left);
		int right_height = avl_node_height(right);
		parent->height = MAX(left_height, right_height) + 1;
		/*
		 * Rotations flattened the tree, so there is no
		 * further changes in height up the insertion
		 * path.
		 */
		if (left_height == right_height)
			break;
		/*
		 * We've been adding a new child (children) to the
		 * 'left' subtree, so it couldn't get shorter.
		 * The old difference between subtrees was in the
		 * range -1..1. So the new difference can only be
		 * in the range -1..1 + height(new_node).
		 */
		if (left_height - right_height >= 2) {
			struct avl_node *l_left = left->link[mirror];
			struct avl_node *l_right = left->link[!mirror];
			int l_left_height = avl_node_height(l_left);
			int l_right_height = avl_node_height(l_right);
			/*
			 * Rotate in the direction, opposite to
			 * the skew. E.g. if we have two left-left
			 * nodes hanging off the tree, rotate the
			 * parent clockwise. If we have a left
			 * node with a right child, rotate the
			 * child counterclockwise, and then the whole
			 * thing clockwise.
			 */
			if (l_left_height >= l_right_height)
				**p_end = avl_rotate_single(parent,
							    !mirror);
			else
				**p_end = avl_rotate_double(parent,
							    !mirror);
			/*
			 * If we inserted only one node, no more
			 * than 1 rotation is required (see
			 * D. Knuth, Introduction to Algorithms,
			 * vol. 3.). For 2 nodes, its max
			 * 2 rotations.
			 */
			if (l_left_height != l_right_height &&
			    --insert_height == 0)
				break;
		}
	}
}

void
avl_rebalance_after_delete(struct avl_node ***path,
			   struct avl_node ***p_end)
{
	while (p_end > path) {
		struct avl_node *left = **p_end--;
		struct avl_node *parent = **p_end;

		int mirror = left != parent->link[0];

		struct avl_node *right = parent->link[!mirror];

		int left_height = avl_node_height(left);
		int right_height = avl_node_height(right);

		parent->height = MAX(left_height, right_height) + 1;
		/*
		 * Right was taller, and we deleted from the left.
		 * We can break the loop since there can be no
		 * changes in height up in the route.
		 */
		if (left_height - right_height == -1)
			break;

		if (left_height - right_height <= -2) {
			struct avl_node *r_left = right->link[mirror];
			struct avl_node *r_right = right->link[!mirror];
			int r_left_height = avl_node_height(r_left);
			int r_right_height = avl_node_height(r_right);

			if (r_left_height <= r_right_height)
				**p_end = avl_rotate_single(parent, mirror);
			else
				**p_end = avl_rotate_double(parent, mirror);
		}
	}
}

struct avl_node ***
avl_route_to_offset(struct avl_node ***path, uint32_t *p_offset,
		    ssize_t adjust_size)
{
	uint32_t offset = *p_offset;
	while (**path) {
		struct avl_node *node = **path;

		node->tree_size += adjust_size;

		uint32_t left_size = avl_node_size(node->link[0]);

		if (offset < left_size) {
			/* The offset lays in  the left subtree. */
			*++path = &node->link[0];
		} else {
			/* Make the new offset relative to the parent. */
			offset -= left_size;

			if (offset < node->leaf_size) {
				/* Found. */
				break;
			} else {
				/*
				 * Make the offset relative to the
				 * leftmost node in the right subtree.
				 */
				offset -= node->leaf_size;
			}
			*++path = &node->link[1];
		}
	}
	*p_offset = offset;
	return path;
}

struct avl_node ***
avl_route_to_next(struct avl_node ***path, int dir, int32_t adjust_size)
{
	struct avl_node *node = **path;
	*++path = &node->link[dir];
	while (**path) {
		node = **path;
		node->tree_size += adjust_size;
		*++path = &node->link[!dir];
	}
	return path;
}

/**
 * Traverse left until the left subtree is NULL,
 * save the path in iter->path.
 * @pre iter->path[iter->top] is not NULL
 * @post iter->path[iter->top] is not NULL and points to the last
 * not-NULL node.
 */
static inline void
avl_iter_down_to_leaf(struct avl_iter *it)
{
	while (it->top[0]->link[0] != NULL) {
		it->top[1] = it->top[0]->link[0];
		it->top++;
	}
}

struct avl_node *
avl_iter_start(struct avl_iter *it)
{
	it->top = it->path;
	it->top[0] = it->rope->root;

	if (it->top[0] != NULL)
		avl_iter_down_to_leaf(it);
	return it->top[0];
}

struct avl_node *
avl_iter_next(struct avl_iter *it)
{
	if (it->top[0]->link[1] != NULL) {
		it->top[1] = it->top[0]->link[1];
		it->top++;
		avl_iter_down_to_leaf(it);
	} else {
		/*
		 * Right subtree is NULL. Left subtree is fully
		 * traversed (guaranteed by the order in which we
		 * iterate). Pop up the path until the current
		 * node points to a link we haven't visited
		 * yet: this is the case when we return to the
		 * parent from its left child.
		 */
		do {
			/*
			 * Returned to the root from the right
			 * subtree: the tree is fully traversed.
			 */
			if (it->top == it->path) {
				/*
				 * Crash, rather than infinite loop
				 * if next() is called beyond last.
				 */
				it->top[0] = NULL;
				return NULL;
			}
			it->top--;
		} while (it->top[1] == it->top[0]->link[1]);
	}
	return *it->top;
}

void
avl_iter_check(struct avl_iter *iter)
{
	for (struct avl_node *node = avl_iter_start(iter); node != NULL;
	     node = avl_iter_next(iter)) {

		assert(node->leaf_size != 0);

		assert(node->tree_size == avl_node_size(node->link[0])
		       + avl_node_size(node->link[1]) + node->leaf_size);

		assert(node->height == (MAX(avl_node_height(node->link[0]),
					    avl_node_height(node->link[1])) + 1));
		if (node->leaf_size == 0 ||
		    node->tree_size != (avl_node_size(node->link[0]) +
					avl_node_size(node->link[1]) +
					node->leaf_size) ||
		    node->height != MAX(avl_node_height(node->link[0]),
					avl_node_height(node->link[1])) + 1)
			abort();
	}
}
