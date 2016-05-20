/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
/*
 * This is a rope implementation which uses AVL tree
 * balancing algorithm for rope tree balance.
 */
#include "rope.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static inline int
rope_node_height(struct rope_node *node)
{
	return node ? node->height : 0;
}

#if !defined(MAX)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif /* MAX */

static inline void
rope_relink(struct rope_node *node)
{
	node->tree_size = (rope_node_size(node->link[0]) +
			   rope_node_size(node->link[1]) +
			   node->leaf_size);
	node->height = MAX(rope_node_height(node->link[0]),
			   rope_node_height(node->link[1])) + 1;
}

static inline struct rope_node *
rope_node_new(struct rope *rope, void *data, rope_size_t size)
{
	struct rope_node *node =
		(struct rope_node *) rope->alloc(rope->alloc_ctx,
						 sizeof(struct rope_node));

	if (node == NULL)
		return NULL;

	node->height = 1;
	node->tree_size = node->leaf_size = size;
	node->data = data;
	node->link[0] = node->link[1] = NULL;

	return node;
}

void
rope_clear(struct rope *rope)
{
	struct rope_node *it = rope->root;
	struct rope_node *save;

	/* Destruction by rotation */
	while (it != NULL) {
		if (it->link[0] == NULL) {
			/* Remove node */
			save = it->link[1];
			rope->free(rope->alloc_ctx, it);
		} else {
			/* Rotate right */
			save = it->link[0];
			it->link[0] = save->link[1];
			save->link[1] = it;
		}
		it = save;
	}
	rope->root = NULL;
}

static struct rope_node *
rope_node_split(struct rope *rope, struct rope_node *node, rope_size_t offset)
{
	rope_size_t old_size = node->leaf_size;
	node->leaf_size = offset;

	void *data = rope->split(rope->split_ctx, node->data, old_size, offset);
	return rope_node_new(rope, data, old_size - offset);
}

static inline struct rope_node *
avl_rotate_single(struct rope_node *parent, int direction)
{
	struct rope_node *save = parent->link[!direction];

	parent->link[!direction] = save->link[direction];
	save->link[direction] = parent;

	/* First relink the parent, since it's now a child. */
	rope_relink(parent);
	rope_relink(save);

	return save;
}

static inline struct rope_node *
avl_rotate_double(struct rope_node *parent, int direction)
{
	parent->link[!direction] =
		avl_rotate_single(parent->link[!direction], !direction);
	return avl_rotate_single(parent, direction);
}

/** Rebalance the tree. */
static inline void
avl_rebalance_after_insert(struct rope_node ***path,
			   struct rope_node ***p_end, int insert_height)
{
	while (p_end > path) {
		struct rope_node *left = **p_end--;
		struct rope_node *parent = **p_end;
		/*
		 * To use the same rotation functions, set mirror
		 * to 1 if left is right and right is left.
		 */
		int mirror = left != parent->link[0];
		struct rope_node *right = parent->link[!mirror];

		int left_height = rope_node_height(left);
		int right_height = rope_node_height(right);
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
			struct rope_node *l_left = left->link[mirror];
			struct rope_node *l_right = left->link[!mirror];
			int l_left_height = rope_node_height(l_left);
			int l_right_height = rope_node_height(l_right);
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

/* This is a copy-cat of the previous loop,
 * with the exception that the heuristic to break
 * the loop is different.
 */
static inline void
avl_rebalance_after_delete(struct rope_node ***path,
			   struct rope_node ***p_end)
{
	while (p_end > path) {
		struct rope_node *left = **p_end--;
		struct rope_node *parent = **p_end;

		int mirror = left != parent->link[0];

		struct rope_node *right = parent->link[!mirror];

		int left_height = rope_node_height(left);
		int right_height = rope_node_height(right);

		parent->height = MAX(left_height, right_height) + 1;
		/*
		 * Right was taller, and we deleted from the left.
		 * We can break the loop since there can be no
		 * changes in height up in the route.
		 */
		if (left_height - right_height == -1)
			break;

		if (left_height - right_height <= -2) {
			struct rope_node *r_left = right->link[mirror];
			struct rope_node *r_right = right->link[!mirror];
			int r_left_height = rope_node_height(r_left);
			int r_right_height = rope_node_height(r_right);

			if (r_left_height <= r_right_height)
				**p_end = avl_rotate_single(parent, mirror);
			else
				**p_end = avl_rotate_double(parent, mirror);
		}
	}
}

/**
 * Find a rope node which contains the substring at offset,
 * adjusting tree size with adjust_size and saving the path
 * in path.
 *
 * @return the end of the route.
 */
static inline struct rope_node ***
avl_route_to_offset(struct rope_node ***path, rope_size_t *p_offset,
		    ssize_t adjust_size)
{
	rope_size_t offset = *p_offset;
	while (**path) {
		struct rope_node *node = **path;

		node->tree_size += adjust_size;

		rope_size_t left_size = rope_node_size(node->link[0]);

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

/**
 * Route to successor or predecessor node of the node
 * in **path. It's either the rightmost leaf of the left child
 * (previous node) or leftmost leaf of the right child.
 */
static inline struct rope_node ***
avl_route_to_next(struct rope_node ***path, int dir, rope_ssize_t adjust_size)
{
	struct rope_node *node = **path;
	*++path = &node->link[dir];
	while (**path) {
		node = **path;
		node->tree_size += adjust_size;
		*++path = &node->link[!dir];
	}
	return path;
}

/**
 * A new node is always inserted at a leaf position.
 * If insertion unbalances the tree, the rebalancing
 * procedure may put the node into an intermediate position.
 *
 * While traversing the tree, we simultaneously update
 * tree sizes of all intermediate nodes, taking into account
 * the size of the new node.
 *
 * When insertion offset falls at the middle of an existing node,
 * we truncate this node and attach its tail to the left leaf
 * of the new node. This trim operation doesn't decrease the old
 * subtree height, and, while it does change subtree size
 * temporarily, as long as we attach the new node to the right
 * subtree of the truncated node, truncation has no effect on the
 * tree size either.
 *
 * Rebalancing, when it occurs, will correctly update subtree
 * heights and sizes of all modified nodes.
 */
int
rope_insert(struct rope *rope, rope_size_t offset, void *data, rope_size_t size)
{
	if (offset > rope_size(rope))
		offset = rope_size(rope);

	assert(size);

	struct rope_node *new_node = rope_node_new(rope, data, size);

	if (new_node == NULL)
		return -1;

	struct rope_node **path[ROPE_HEIGHT_MAX];
	path[0] = &rope->root;

	struct rope_node ***p_end = avl_route_to_offset(path, &offset, size);
	if (**p_end != NULL) {
		/*
		 * The offset is inside an existing
		 * substring in the rope. If offset is 0,
		 * then insert the new node at the rightmost leaf
		 * of the left child. Otherwise, cut the tail of
		 * the substring, make it a prefix of the inserted
		 * string, and insert the result at the leftmost
		 * leaf of the right child.
		 */
		if (offset != 0) {
			struct rope_node *split_node;
			split_node = rope_node_split(rope, **p_end, offset);
			if (split_node == NULL)
				return -1;
			split_node->link[0] = new_node;
			split_node->height++;
			split_node->tree_size += new_node->tree_size;
			new_node = split_node;
		}
		p_end = avl_route_to_next(p_end, offset != 0,
					  new_node->tree_size);
	}
	**p_end = new_node;
	avl_rebalance_after_insert(path, p_end, new_node->height);
	return 0;
}

/** Make sure there is a rope node at the given offset. */
struct rope_node *
rope_extract_node(struct rope *rope, rope_size_t offset)
{
	assert(offset < rope_size(rope));

	struct rope_node **path[ROPE_HEIGHT_MAX];
	path[0] = &rope->root;

	struct rope_node ***p_end = avl_route_to_offset(path, &offset, 0);
	if (offset == 0)
		return **p_end;
	struct rope_node *new_node = rope_node_split(rope, **p_end, offset);
	if (new_node == NULL)
		return NULL;
	p_end = avl_route_to_next(p_end, 1, new_node->tree_size);
	**p_end = new_node;
	avl_rebalance_after_insert(path, p_end, new_node->height);
	return new_node;
}

/**
 * Erase a single element from the rope.
 * This is a straightforward implementation for a single-element
 * deletion from a rope. A generic cut from a rope involves
 * 2 tree splits and one merge.
 *
 * When deleting a single element, 3 cases are possible:
 * - offset falls at a node with a single element. In this
 *   case we perform a normal AVL tree delete.
 * - offset falls at the end or the beginning of an existing node
 *   with leaf_size > 1. In that case we trim the existing node
 *   and return.
 * - offset falls inside an existing node. In that case
 *   we split the existing node at offset, and insert the tail.
 *
 * The implementation is a copycat of rope_insert(). If you're
 * trying to understand the code, it's recommended to start
 * from rope_insert().
 */
int
rope_erase(struct rope *rope, rope_size_t offset)
{
	assert(offset < rope_size(rope));

	struct rope_node **path[ROPE_HEIGHT_MAX];
	path[0] = &rope->root;

	struct rope_node ***p_end = avl_route_to_offset(path, &offset, -1);

	struct rope_node *node = **p_end;

	if (node->leaf_size > 1) {
		/* Check if we can simply trim the node. */
		if (offset == 0) {
			/* Cut the head. */
			node->data = rope->split(rope->split_ctx, node->data,
						 node->leaf_size, 1);
			node->leaf_size -= 1;
			return 0;
		}
		rope_size_t size = node->leaf_size;
		/* Cut the tail */
		void *next = rope->split(rope->split_ctx, node->data,
					 node->leaf_size, offset);
		node->leaf_size = offset;
		if (offset == size - 1)
			return 0; /* Trimmed the tail, nothing else to do */
		/*
		 * Offset falls inside a substring. Erase the
		 * first field and insert the tail.
		 */
		next = rope->split(rope->split_ctx, next, size - offset, 1);
		struct rope_node *new_node =
			rope_node_new(rope, next, size - offset - 1);
		if (new_node == NULL)
			return -1;
		/* Trim the old node. */
		p_end = avl_route_to_next(p_end, 1, new_node->tree_size);
		**p_end = new_node;
		avl_rebalance_after_insert(path, p_end, new_node->height);
		return 0;
	}
	/* We need to delete the node. */
	assert(offset == 0);
	int direction;
	if (node->link[0] != NULL && node->link[1] != NULL) {
		/*
		 * The node has two non-NULL leaves. We can't
		 * simply delete the node since in that case we
		 * won't know what to do with one of the leaves.
		 * Instead of deleting the node, store in it data
		 * from the rightmost node in the left subtree, or
		 * the leftmost node in the right subtree,
		 * (depending on which subtree is taller), and
		 * delete this leftmost/rightmost node instead.
		 */
		struct rope_node *save = node;
		direction = node->link[1]->height > node->link[0]->height;
		p_end = avl_route_to_next(p_end, direction, 0) - 1;
		node = **p_end;
		/* Move the data pointers. */
		save->data = node->data;
		save->leaf_size = node->leaf_size;
		/*
		 * Now follow the path again and update tree_size
		 * in the parents of the moved child.
	         */
		save = save->link[direction];
		while (save != node) {
			save->tree_size -= node->leaf_size;
			save = save->link[!direction];
		}
	} else {
		/*
		 * Left or right subtree are NULL, so we
		 * can simply put the non-NULL leaf in place
		 * of the parent.
		 */
		direction = node->link[0] == NULL;
	}
	**p_end = node->link[direction];
	rope->free(rope, node);
	avl_rebalance_after_delete(path, p_end);
	return 0;
}

/**
 * Traverse left until the left subtree is NULL,
 * save the path in iter->path.
 * @pre iter->path[iter->top] is not NULL
 * @post iter->path[iter->top] is not NULL and points to the last
 * not-NULL node.
 */
static inline void
rope_iter_down_to_leaf(struct rope_iter *it)
{
	while (it->top[0]->link[0] != NULL) {
		it->top[1] = it->top[0]->link[0];
		it->top++;
	}
}

struct rope_node *
rope_iter_start(struct rope_iter *it)
{
	it->top = it->path;
	it->top[0] = it->rope->root;

	if (it->top[0] != NULL)
		rope_iter_down_to_leaf(it);
	return it->top[0];
}

struct rope_node *
rope_iter_next(struct rope_iter *it)
{
	if (it->top[0]->link[1] != NULL) {
		it->top[1] = it->top[0]->link[1];
		it->top++;
		rope_iter_down_to_leaf(it);
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

/** Apply visit_leaf function to every rope leaf. */
void
rope_traverse(struct rope *rope, void (*visit_leaf)(void *, size_t))
{
	struct rope_iter iter;
	rope_iter_create(&iter, rope);

	struct rope_node *leaf;

	for (leaf = rope_iter_start(&iter);
	     leaf != NULL;
	     leaf = rope_iter_next(&iter)) {

		visit_leaf(leaf->data, leaf->leaf_size);
	}
}

void
rope_check(struct rope *rope)
{
	struct rope_iter iter;
	rope_iter_create(&iter, rope);

	struct rope_node *node;

	for (node = rope_iter_start(&iter);
	     node != NULL;
	     node = rope_iter_next(&iter)) {

		assert(node->leaf_size != 0);

		assert(node->tree_size == rope_node_size(node->link[0])
		       + rope_node_size(node->link[1]) + node->leaf_size);

		assert(node->height == (MAX(rope_node_height(node->link[0]),
					    rope_node_height(node->link[1])) + 1));
		if (node->leaf_size == 0 ||
		    node->tree_size != (rope_node_size(node->link[0]) +
					rope_node_size(node->link[1]) +
					node->leaf_size) ||
		    node->height != MAX(rope_node_height(node->link[0]),
					rope_node_height(node->link[1])) + 1)
			abort();
	}
}

static void
rope_node_print(struct rope_node *node,
		void (*print)(void *, size_t),
		const char *prefix, int dir)
{
	const char *conn[] = { "┌──", "└──" };

	const char *padding[] = { "│   ", "   " };

	rope_size_t child_prefix_len = strlen(prefix) + strlen(padding[0]) + 1;
	char *child_prefix = malloc(child_prefix_len);

	if (node && (node->link[0] || node->link[1])) {
		snprintf(child_prefix, child_prefix_len - 1,
			 "%s%s", prefix, padding[!dir]);
		rope_node_print(node->link[0], print, child_prefix, 0);
	}

	snprintf(child_prefix, child_prefix_len - 1, "%s%s",
		 prefix, padding[dir]);
	printf("%s%s", prefix, conn[dir]);

	if (node == NULL) {
		printf("nil\n");
	} else {

		printf("{ len = %zu, height = %d, data = '",
		       (size_t) node->leaf_size, node->height);
		print(node->data, node->leaf_size);
		printf("'}\n");

		if (node && (node->link[0] || node->link[1]))
			rope_node_print(node->link[1], print, child_prefix, 1);
	}

	free(child_prefix);
}

void
rope_pretty_print(struct rope *rope, void (*print_leaf)(void *, size_t))
{
	printf("size = %zu\nstring = '", (size_t) rope_size(rope));
	rope_traverse(rope, print_leaf);
	printf("'\n");
	rope_node_print(rope->root, print_leaf, "", true);
	printf("\n");
}
