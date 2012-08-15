#ifndef INCLUDES_TARANTOOL_ROPE_H
#define INCLUDES_TARANTOOL_ROPE_H
/*
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
#include <stddef.h>
#include <stdbool.h>

typedef unsigned int rsize_t;
typedef int rssize_t;

/** Tallest allowable tree, 1.44*log(2^32) */
enum { ROPE_HEIGHT_MAX = 46 };

struct rope_node {
	/** Node height, see rope_node_height(), used for AVL balance. */
	int height;
	/** Subtree size. */
	rsize_t tree_size;
        /* Substring size. */
	rsize_t leaf_size;
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
	void *(*seq_getn)(void *, size_t);
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

static inline rsize_t
rope_node_size(struct rope_node *node)
{
	return node ? node->tree_size : 0;
}

static inline rsize_t
rope_size(struct rope *rope)
{
	return rope_node_size(rope->root);
}

/** Create a new empty rope.
 * @param seq_getn  a function which returns
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

struct rope *
rope_new(void *(*seq_getn)(void *, size_t),
	 void *(*alloc_func)(void *, size_t),
	 void (*free_func)(void *, void *),
	 void *alloc_ctx);

/** Delete a rope. Doesn't delete rope entries. */
void
rope_delete(struct rope *rope);

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
rope_insert(struct rope *rope, rsize_t offset, void *data,
	    rsize_t size);

/** Make sure there is a rope node which has a substring
 * which starts at the given offset. Useful when
 * rope substrings carry additional information.
 *
 * @retval 0   success
 * @retval -1  failed to allocate memory for a new
 *             tree node
 */
struct rope_node *
rope_extract(struct rope *rope, rsize_t offset);

/**
 * Erase a single element from a rope at the given
 * offset.
 *
 * @pre offset < rope_size(rope)
 */
int
rope_erase(struct rope *rope, rsize_t offset);

/** Initialize an iterator. */
static inline void
rope_iter_init(struct rope_iter *it, struct rope *rope)
{
	it->rope = rope;
}

/** Create an iterator. */
static inline struct rope_iter *
rope_iter_new(struct rope *rope)
{
	struct rope_iter *it = rope->alloc(rope->alloc_ctx,
					     sizeof(struct rope_iter));

	if (it == NULL)
		return NULL;
	rope_iter_init(it, rope);
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

#endif /* INCLUDES_TARANTOOL_ROPE_H */
