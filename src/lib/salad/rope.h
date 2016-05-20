#ifndef INCLUDES_TARANTOOL_ROPE_H
#define INCLUDES_TARANTOOL_ROPE_H
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
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

typedef uint32_t rope_size_t;
typedef int32_t rope_ssize_t;
typedef void *(*rope_split_func)(void *, void *, size_t, size_t);
typedef void *(*rope_alloc_func)(void *, size_t);
typedef void (*rope_free_func)(void *, void *);

/** Tallest allowable tree, 1.44*log(2^32) */
enum { ROPE_HEIGHT_MAX = 46 };

struct rope_node {
	/** Node height, see rope_node_height(), used for AVL balance. */
	int height;
	/** Subtree size. */
	rope_size_t tree_size;
	/* Substring size. */
	rope_size_t leaf_size;
	/* Substring. */
	void *data;
	/* Left (0) and right (1) links */
	struct rope_node *link[2];
};

struct rope {
	/** Top of the tree */
	struct rope_node *root;
	/** Memory management context. */
	void *alloc_ctx;
	/** Get a sequence tail, given offset. */
	rope_split_func split;
	/** Split function context. */
	void *split_ctx;
	/** Allocate memory (context, size). */
	void *(*alloc)(void *, size_t);
	/** Free memory (context, pointer) */
	void (*free)(void *, void *);
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

static inline void *
rope_leaf_data(struct rope_node *node)
{
	return node->data;
}

static inline rope_size_t
rope_size(struct rope *rope)
{
	return rope_node_size(rope->root);
}

/** Initialize an empty rope. */
static inline void
rope_create(struct rope *rope, rope_split_func split_func, void *split_ctx,
	    rope_alloc_func alloc_func, rope_free_func free_func,
	    void *alloc_ctx)
{
	rope->root = NULL;
	rope->split = split_func;
	rope->split_ctx = split_ctx;
	rope->alloc = alloc_func;
	rope->free = free_func;
	rope->alloc_ctx = alloc_ctx;
}

/** Create a new empty rope.
 * @param split_func a function which returns
 *                  a pointer to substring
 *                  given an offset. Used
 *                  to split substrings when
 *                  inserting into a rope.
 * @param alloc_func used to allocate memory
 * @param free_func  used to free memory
 * @param alloc_ctx  allocator context
 *
 * @return  an empty rope, or NULL if failed
 *          to allocate memory
 */
static inline struct rope *
rope_new(rope_split_func split_func, void *split_ctx,
	 rope_alloc_func alloc_func, rope_free_func free_func, void *alloc_ctx)
{
	struct rope *rope= (struct rope *) alloc_func(alloc_ctx,
						      sizeof(struct rope));
	if (rope == NULL)
		return NULL;
	rope_create(rope, split_func, split_ctx, alloc_func, free_func, alloc_ctx);
	return rope;
}


/** Delete rope contents. Can also be used
 * to free a rope which is allocated on stack.
 * Doesn't delete rope substrings, only
 * rope nodes.
 */
void
rope_clear(struct rope *rope);

/** Delete a rope allocated with rope_new() */
static inline void
rope_delete(struct rope *rope)
{
	rope_clear(rope);
	rope->free(rope->alloc_ctx, rope);
}

/** Insert a substring into a rope at the given
 * offset.
 * If offset is greater than rope size, insertion
 * happens at the end.
 *
 * @retval 0   success
 * @retval -1  failed to allocate memory for a new
 *             tree node
 */
int
rope_insert(struct rope *rope, rope_size_t offset, void *data,
	    rope_size_t size);

/** Append a substring at rope tail. */
static inline int
rope_append(struct rope *rope, void *data, size_t size)
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
struct rope_node *
rope_extract_node(struct rope *rope, rope_size_t offset);

static inline void *
rope_extract(struct rope *rope, rope_size_t offset)
{
	return rope_leaf_data(rope_extract_node(rope, offset));
}

/**
 * Erase a single element from a rope at the given
 * offset.
 *
 * @pre offset < rope_size(rope)
 */
int
rope_erase(struct rope *rope, rope_size_t offset);

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
	struct rope_iter *it = (struct rope_iter *)
			rope->alloc(rope->alloc_ctx, sizeof(struct rope_iter));

	if (it == NULL)
		return NULL;
	rope_iter_create(it, rope);
	return it;
}

/**
 * Begin iteration.
 * @retval NULL the rope is empty
 */
struct rope_node *
rope_iter_start(struct rope_iter *it);

/**
 * Advance to the next rope node.
 *
 * @return  node, or NULL if iterator
 *          has advanced beyond the last
 *          node.
 */
struct rope_node *
rope_iter_next(struct rope_iter *it);

/** Free iterator. */
static inline void
rope_iter_delete(struct rope_iter *it)
{
	it->rope->free(it->rope->alloc_ctx, it);
}

/** Apply visit_leaf function to every rope leaf. */
void
rope_traverse(struct rope *rope, void (*visit_leaf)(void *, size_t));

/** Check AVL tree consistency. */
void
rope_check(struct rope *rope);

/** Pretty print a rope. */
void
rope_pretty_print(struct rope *rope, void (*print_leaf)(void *, size_t));

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_ROPE_H */
