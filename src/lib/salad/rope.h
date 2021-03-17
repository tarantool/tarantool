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
/*
 * This is a rope implementation which uses AVL tree
 * balancing algorithm for rope tree balance.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#ifndef MAX
#define NEED_UNDEF_MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif /* MAX */

/** {{{ AVL private API */

struct avl_node;
struct avl_iter;

void
avl_rebalance_after_insert(struct avl_node ***path,
			   struct avl_node ***p_end, int insert_height);

void
avl_rebalance_after_delete(struct avl_node ***path,
			   struct avl_node ***p_end);

/**
 * Find a rope node which contains the substring at @a p_offset,
 * adjusting tree size with @a adjust_size and saving the path in
 * @a path.
 * @return The end of the route.
 */
struct avl_node ***
avl_route_to_offset(struct avl_node ***path, uint32_t *p_offset,
		    ssize_t adjust_size);

/**
 * Route to successor or predecessor node of the node in @a path.
 * It's either the rightmost leaf of the left child (previous
 * node) or leftmost leaf of the right child.
 */
struct avl_node ***
avl_route_to_next(struct avl_node ***path, int dir,
		  int32_t adjust_size);

struct avl_node *
avl_iter_start(struct avl_iter *it);

struct avl_node *
avl_iter_next(struct avl_iter *it);

void
avl_iter_check(struct avl_iter *iter);

/** }}} AVL private API */

/**
 * Needed definitions:
 *
 * rope_name, optional
 * rope_data_t
 * rope_ctx_t
 * ROPE_SPLIT_F
 * ROPE_ALLOC_F
 * ROPE_FREE_F, optional
 */

#define CONCAT_R(a, b) a##b
#define CONCAT(a, b) CONCAT_R(a, b)
#define CONCAT3_R(a, b, c) a##b##c
#define CONCAT3(a, b, c) CONCAT3_R(a, b, c)

#if defined(ROPE_SRC)
	#define rope_api(x) CONCAT(avl_, x)
#elif defined(rope_name)
	#define rope_api(x) CONCAT3(rope_name, _rope_, x)
	#define rope CONCAT(rope_name, _rope)
#else
	#define rope_api(x) CONCAT(rope_, x)
#endif

#ifndef ROPE_FREE_F
	#define ROPE_FREE(a, b) do { (void) a; (void) b; } while(0)
#else
	#define ROPE_FREE(a, b) ROPE_FREE_F(a, b)
#endif

#define rope_node rope_api(node)
#define rope_node_size rope_api(node_size)
#define rope_leaf_size rope_api(leaf_size)
#define rope_leaf_data rope_api(leaf_data)
#define rope_iter rope_api(iter)
#define rope_create rope_api(create)
#define rope_new rope_api(new)
#define rope_size rope_api(size)
#define rope_clear rope_api(clear)
#define rope_delete rope_api(delete)
#define rope_node_new rope_api(node_new)
#define rope_node_split rope_api(node_split)
#define rope_insert rope_api(insert)
#define rope_append rope_api(append)
#define rope_extract_node rope_api(extract_node)
#define rope_extract rope_api(extract)
#define rope_erase rope_api(erase)
#define rope_iter_create rope_api(iter_create)
#define rope_iter_new rope_api(iter_new)
#define rope_iter_start rope_api(iter_start)
#define rope_iter_next rope_api(iter_next)
#define rope_iter_delete rope_api(iter_delete)
#define rope_traverse rope_api(traverse)
#define rope_check rope_api(check)
#define rope_node_print rope_api(node_print)
#define rope_pretty_print rope_api(pretty_print)

typedef uint32_t rope_size_t;
typedef int32_t rope_ssize_t;

/** Tallest allowable tree, 1.44*log(2^32) */
#define ROPE_HEIGHT_MAX 46

struct rope_node {
	/**
	 * Node height, see avl_node_height(), used for AVL
	 * balance.
	 */
	int height;
	/** Subtree size. */
	rope_size_t tree_size;
	/* Substring size. */
	rope_size_t leaf_size;
	/* Left (0) and right (1) links */
	struct rope_node *link[2];
	/* Substring. */
	rope_data_t data;
};

struct rope {
	/** Top of the tree */
	struct rope_node *root;
	/** Rope context. */
	rope_ctx_t ctx;
};

struct rope_iter {
	/** rope->free is used to free the iterator. */
	struct rope *rope;
	/** End of the traversal path. */
	struct rope_node **top;
	/** Traversal path */
	struct rope_node *path[ROPE_HEIGHT_MAX];
};

static inline rope_size_t
rope_node_size(struct rope_node *node)
{
	return node ? node->tree_size : 0;
}

static inline rope_size_t
rope_leaf_size(struct rope_node *node)
{
	return node->leaf_size;
}

static inline rope_data_t
rope_leaf_data(struct rope_node *node)
{
	return node->data;
}

static inline rope_size_t
rope_size(struct rope *rope)
{
	return rope_node_size(rope->root);
}

/**
 * Everything below is not needed in the rope's own source file,
 * so it is macrosed out.
 */
#ifndef ROPE_SRC

/** Initialize an empty rope. */
static inline void
rope_create(struct rope *rope, rope_ctx_t ctx)
{
	rope->root = NULL;
	rope->ctx = ctx;
}

/** Create a new empty rope.
 * @param alloc_ctx  allocator context
 *
 * @return  an empty rope, or NULL if failed
 *          to allocate memory
 */
static inline struct rope *
rope_new(rope_ctx_t ctx)
{
	struct rope *rope = (struct rope *) ROPE_ALLOC_F(ctx, sizeof(*rope));
	if (rope != NULL)
		rope_create(rope, ctx);
	return rope;
}

/** Delete rope contents. Can also be used
 * to free a rope which is allocated on stack.
 * Doesn't delete rope substrings, only
 * rope nodes.
 */
static void
rope_clear(struct rope *rope)
{
#ifdef ROPE_FREE_F
	struct rope_node *it = rope->root;
	struct rope_node *save;

	/* Destruction by rotation */
	while (it != NULL) {
		if (it->link[0] == NULL) {
			/* Remove node */
			save = it->link[1];
			ROPE_FREE(rope->ctx, it);
		} else {
			/* Rotate right */
			save = it->link[0];
			it->link[0] = save->link[1];
			save->link[1] = it;
		}
		it = save;
	}
#endif /* ROPE_FREE_F */
	rope->root = NULL;
}

/** Delete a rope allocated with rope_new() */
static inline void
rope_delete(struct rope *rope)
{
	rope_clear(rope);
	ROPE_FREE(rope->ctx, rope);
}

static inline struct rope_node *
rope_node_new(struct rope *rope, rope_data_t data, rope_size_t size)
{
	struct rope_node *node =
		(struct rope_node *) ROPE_ALLOC_F(rope->ctx, sizeof(*node));
	if (node == NULL)
		return NULL;
	node->height = 1;
	node->tree_size = node->leaf_size = size;
	node->data = data;
	node->link[0] = node->link[1] = NULL;

	return node;
}

static inline struct rope_node *
rope_node_split(struct rope *rope, struct rope_node *node, rope_size_t offset)
{
	rope_size_t old_size = node->leaf_size;
	node->leaf_size = offset;

	rope_data_t data = ROPE_SPLIT_F(rope->ctx, node->data, old_size,
					offset);

	return rope_node_new(rope, data, old_size - offset);
}

/**
 * Insert a substring into a rope at the given offset. If offset
 * is greater than rope size, insertion happens at the end. A new
 * node is always inserted at a leaf position. If insertion
 * unbalances the tree, the rebalancing procedure may put the node
 * into an intermediate position.
 *
 * While traversing the tree, we simultaneously update tree sizes
 * of all intermediate nodes, taking into account the size of the
 * new node.
 *
 * When insertion offset falls at the middle of an existing node,
 * we truncate this node and attach its tail to the left leaf of
 * the new node. This trim operation doesn't decrease the old
 * subtree height, and, while it does change subtree size
 * temporarily, as long as we attach the new node to the right
 * subtree of the truncated node, truncation has no effect on the
 * tree size either.
 *
 * Rebalancing, when it occurs, will correctly update subtree
 * heights and sizes of all modified nodes.
 *
 * @retval 0 Success.
 * @retval -1 Memory error.
 */
static int
rope_insert(struct rope *rope, rope_size_t offset, rope_data_t data,
	    rope_size_t size)
{
	if (offset > rope_size(rope))
		offset = rope_size(rope);

	assert(size);

	struct rope_node *new_node = rope_node_new(rope, data, size);

	if (new_node == NULL)
		return -1;

	struct rope_node **path[ROPE_HEIGHT_MAX];
	path[0] = &rope->root;

	struct rope_node ***p_end = (struct rope_node ***)
		avl_route_to_offset((struct avl_node ***) path, &offset, size);
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
			if (split_node == NULL) {
			        ROPE_FREE(rope->ctx, new_node);
			        return -1;
			}
			split_node->link[0] = new_node;
			split_node->height++;
			split_node->tree_size += new_node->tree_size;
			new_node = split_node;
		}
		p_end = (struct rope_node ***)
			avl_route_to_next((struct avl_node ***) p_end,
					   offset != 0, new_node->tree_size);
	}
	**p_end = new_node;
	avl_rebalance_after_insert((struct avl_node ***) path,
				   (struct avl_node ***) p_end,
				   new_node->height);
	return 0;
}

/** Append a substring at rope tail. */
static inline int
rope_append(struct rope *rope, rope_data_t data, size_t size)
{
	return rope_insert(rope, rope_size(rope), data, size);
}

/** Make sure there is a rope node which has a substring
 * which starts at the given offset. Useful when
 * rope substrings carry additional information.
 *
 * @retval NULL failed to allocate memory for a new
 *              tree node
 */
static struct rope_node *
rope_extract_node(struct rope *rope, rope_size_t offset)
{
	assert(offset < rope_size(rope));

	struct rope_node **path[ROPE_HEIGHT_MAX];
	path[0] = &rope->root;

	struct rope_node ***p_end = (struct rope_node ***)
		avl_route_to_offset((struct avl_node ***) path, &offset, 0);
	if (offset == 0)
		return **p_end;
	struct rope_node *new_node = rope_node_split(rope, **p_end, offset);
	if (new_node == NULL)
		return NULL;
	p_end = (struct rope_node ***)
		avl_route_to_next((struct avl_node ***) p_end, 1,
				  new_node->tree_size);
	**p_end = new_node;
	avl_rebalance_after_insert((struct avl_node ***) path,
				   (struct avl_node ***) p_end,
				   new_node->height);
	return new_node;
}

static inline rope_data_t
rope_extract(struct rope *rope, rope_size_t offset)
{
	return rope_leaf_data(rope_extract_node(rope, offset));
}

/**
 * Erase a single element from the rope. This is a straightforward
 * implementation for a single-element deletion from a rope. A
 * generic cut from a rope involves 2 tree splits and one merge.
 *
 * When deleting a single element, 3 cases are possible:
 * - offset falls at a node with a single element. In this case we
 *   perform a normal AVL tree delete.
 * - offset falls at the end or the beginning of an existing node
 *   with leaf_size > 1. In that case we trim the existing node
 *   and return.
 * - offset falls inside an existing node. In that case we split
 *   the existing node at offset, and insert the tail.
 *
 * The implementation is a copycat of rope_insert(). If you're
 * trying to understand the code, it's recommended to start from
 * rope_insert().
 */
static inline int
rope_erase(struct rope *rope, rope_size_t offset)
{
	assert(offset < rope_size(rope));

	struct rope_node **path[ROPE_HEIGHT_MAX];
	path[0] = &rope->root;

	struct rope_node ***p_end = (struct rope_node ***)
		avl_route_to_offset((struct avl_node ***) path, &offset, -1);

	struct rope_node *node = **p_end;

	if (node->leaf_size > 1) {
		/* Check if we can simply trim the node. */
		if (offset == 0) {
			/* Cut the head. */
			node->data = ROPE_SPLIT_F(rope->ctx, node->data,
						  node->leaf_size, 1);
			node->leaf_size -= 1;
			return 0;
		}
		rope_size_t size = node->leaf_size;
		/* Cut the tail */
		rope_data_t next = ROPE_SPLIT_F(rope->ctx, node->data,
						node->leaf_size, offset);
		node->leaf_size = offset;
		if (offset == size - 1)
			return 0; /* Trimmed the tail, nothing else to do */
		/*
		 * Offset falls inside a substring. Erase the
		 * first field and insert the tail.
		 */
		next = ROPE_SPLIT_F(rope->ctx, next, size - offset, 1);
		struct rope_node *new_node =
			rope_node_new(rope, next, size - offset - 1);
		if (new_node == NULL)
			return -1;
		/* Trim the old node. */
		p_end = (struct rope_node ***)
			avl_route_to_next((struct avl_node ***) p_end, 1,
					  new_node->tree_size);
		**p_end = new_node;
		avl_rebalance_after_insert((struct avl_node ***) path,
					   (struct avl_node ***) p_end,
					   new_node->height);
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
		p_end = (struct rope_node ***)
			avl_route_to_next((struct avl_node ***) p_end,
					  direction, 0) - 1;
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
	ROPE_FREE(rope->ctx, node);
	avl_rebalance_after_delete((struct avl_node ***) path,
				   (struct avl_node ***) p_end);
	return 0;
}

/** Initialize an iterator. */
static inline void
rope_iter_create(struct rope_iter *it, struct rope *rope)
{
	it->rope = rope;
}

/** Create an iterator. */
static inline struct rope_iter *
rope_iter_new(struct rope *rope)
{
	struct rope_iter *it =
		(struct rope_iter *) ROPE_ALLOC_F(rope->ctx, sizeof(*it));
	if (it != NULL)
		rope_iter_create(it, rope);
	return it;
}

/**
 * Begin iteration.
 * @retval NULL the rope is empty
 */
static inline struct rope_node *
rope_iter_start(struct rope_iter *it)
{
	return (struct rope_node *) avl_iter_start((struct avl_iter *) it);
}

/**
 * Advance to the next rope node.
 *
 * @return  node, or NULL if iterator
 *          has advanced beyond the last
 *          node.
 */
static struct rope_node *
rope_iter_next(struct rope_iter *it)
{
	return (struct rope_node *) avl_iter_next((struct avl_iter *) it);
}

/** Free iterator. */
static inline void
rope_iter_delete(struct rope_iter *it)
{
	ROPE_FREE(it->rope->ctx, it);
}

/** Apply visit_leaf function to every rope leaf. */
static void
rope_traverse(struct rope *rope, void (*visit_leaf)(rope_data_t, size_t))
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

/** Check AVL tree consistency. */
static inline void
rope_check(struct rope *rope)
{
	struct rope_iter iter;
	rope_iter_create(&iter, rope);
	avl_iter_check((struct avl_iter *) &iter);
}

static void
rope_node_print(struct rope_node *node, void (*print)(rope_data_t, size_t),
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

		if (node->link[0] || node->link[1])
			rope_node_print(node->link[1], print, child_prefix, 1);
	}

	free(child_prefix);
}

/** Pretty print a rope. */
static inline void
rope_pretty_print(struct rope *rope, void (*print_leaf)(rope_data_t, size_t))
{
	printf("size = %zu\nstring = '", (size_t) rope_size(rope));
	rope_traverse(rope, print_leaf);
	printf("'\n");
	rope_node_print(rope->root, print_leaf, "", true);
	printf("\n");
}

#ifdef NEED_UNDEF_MAX
#undef NEED_UNDEF_MAX
#undef MAX
#endif

#endif /* ROPE_SRC */

#undef rope_node
#undef rope_node_size
#undef rope_leaf_size
#undef rope_leaf_data
#undef rope_iter
#undef rope_create
#undef rope_new
#undef rope_size
#undef rope_clear
#undef rope_delete
#undef rope_node_new
#undef rope_node_split
#undef rope_insert
#undef rope_append
#undef rope_extract_node
#undef rope_extract
#undef rope_erase
#undef rope_iter_create
#undef rope_iter_new
#undef rope_iter_start
#undef rope_iter_next
#undef rope_iter_delete
#undef rope_traverse
#undef rope_check
#undef rope_node_print
#undef rope_pretty_print

#undef rope
#undef rope_api
#undef rope_name
#undef ROPE_HEIGHT_MAX
#undef rope_ctx_t
#undef rope_data_t
#undef ROPE_SPLIT_F
#undef ROPE_ALLOC_F
#undef ROPE_FREE_F
#undef ROPE_FREE

#undef CONCAT_R
#undef CONCAT
#undef CONCAT3_R
#undef CONCAT3

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
