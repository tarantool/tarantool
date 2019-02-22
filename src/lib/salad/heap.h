/*
 * *No header guard*: the header is allowed to be included twice
 * with different sets of defines.
 */
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *		copyright notice, this list of conditions and the
 *		following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *		copyright notice, this list of conditions and the following
 *		disclaimer in the documentation and/or other materials
 *		provided with the distribution.
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
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <memory.h>

/**
 * Additional user defined name that appended to prefix 'heap'
 *	for all names of structs and functions in this header file.
 * All names use pattern: <HEAP_NAME>heap_<name of func/struct>
 * May be empty, but still have to be defined (just #define HEAP_NAME)
 * Example:
 * #define HEAP_NAME test_
 * ...
 * test_heap_create(&some_heap);
 * test_heap_destroy(&some_heap);
 */

/* For predefinition of structures and type non specific functions just make:
 * #define HEAP_FORWARD_DECLARATION
 * #inlude "heap.h"
 */
#ifndef HEAP_FORWARD_DECLARATION

#ifndef HEAP_NAME
#error "HEAP_NAME must be defined"
#endif /* HEAP_NAME */


/**
 * Data comparing function. Takes 3 parameters - heap, node1, node2,
 * where heap is pointer onto heap_t structure and node1, node2
 * are two pointers on nodes in your structure.
 * For example you have such type:
 *	 struct my_type {
 *	 	int value;
 *	 	struct heap_node vnode;
 *	 };
 * Then node1 and node2 will be pointers on field vnode of two
 * my_type instances.
 * The function below is example of valid comparator by value:
 *
 * bool test_type_less(const heap_t *heap, const struct heap_node *a,
 *		       const struct heap_node *b) {
 *
 *	const struct my_type *left = (struct my_type *)((char *)a -
 *					offsetof(struct my_type, vnode));
 *	const struct my_type *right = (struct my_type *)((char *)b -
 *					offsetof(struct my_type, vnode));
 *	return left->value < right->value;
 * }
 *
 * HEAP_LESS is less function that is important!
 */

#ifndef HEAP_LESS
#error "HEAP_LESS must be defined"
#endif

/** Structure, stored in the heap. */
#ifndef heap_value_t
#error "heap_value_t must be defined"
#endif

/** Name of heap_node attribute in heap_value_t. */
#ifndef heap_value_attr
#error "heap_value_attr must be defined"
#endif

/**
 * Tools for name substitution:
 */
#ifndef CONCAT3
#define CONCAT3_R(a, b, c) a##b##c
#define CONCAT3(a, b, c) CONCAT3_R(a, b, c)
#endif

#ifdef _
#error '_' must be undefinded!
#endif
#ifndef HEAP
#define HEAP(name) CONCAT3(HEAP_NAME, _, name)
#endif

#endif /* HEAP_FORWARD_DECLARATION */

/* Structures. */

#ifndef HEAP_STRUCTURES /* Include guard for structures */

#define HEAP_STRUCTURES

enum {
	HEAP_INITIAL_CAPACITY = 8,
	HEAP_NODE_STRAY_POS = UINT32_MAX,
};

typedef uint32_t heap_off_t;

/**
 * Main structure for holding heap.
 */
struct heap_core_structure {
	heap_off_t size;
	heap_off_t capacity;
	struct heap_node **harr; /* array of heap node pointers */
};

typedef struct heap_core_structure heap_t;

/**
 * Heap entry structure.
 */
struct heap_node {
	heap_off_t pos;
};

/** Initialize a heap node with default values. */
static inline void
heap_node_create(struct heap_node *node)
{
	node->pos = HEAP_NODE_STRAY_POS;
}

/** Check if a heap node does not belong to any heap. */
static inline bool
heap_node_is_stray(const struct heap_node *node)
{
	return node->pos == HEAP_NODE_STRAY_POS;
}

/**
 * Heap iterator structure.
 */
struct heap_iterator {
	heap_t *heap;
	heap_off_t curr_pos;
};

#endif /* HEAP_STRUCTURES */

#ifndef HEAP_FORWARD_DECLARATION

#ifndef container_of
#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member  ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member)  );})
#endif

#define node_to_value(n) container_of(n, heap_value_t, heap_value_attr)
#define value_to_node(v) (&(v)->heap_value_attr)

/* Extern API that is the most usefull part. */

/**
 * Initialize the heap.
 */
static inline void
HEAP(create)(heap_t *heap);

/**
 * Destroy current heap.
 */
static inline void
HEAP(destroy)(heap_t *heap);

/**
 * Return min value.
 */
static inline heap_value_t *
HEAP(top)(heap_t *heap);

/**
 * Erase min value.
 */
static inline heap_value_t *
HEAP(pop)(heap_t *heap);

/**
 * Insert value.
 */
static inline int
HEAP(insert)(heap_t *heap, heap_value_t *value);

/**
 * Delete node from heap.
 */
static inline void
HEAP(delete)(heap_t *heap, heap_value_t *value);

/**
 * Heapify tree after update of value.
 */
static inline void
HEAP(update)(heap_t *heap, heap_value_t *value);

/**
 * Heapify tree after updating all values.
 */
static inline void
HEAP(update_all)(heap_t *heap);

/**
 * Heap iterator init.
 */
static inline void
HEAP(iterator_init)(heap_t *heap, struct heap_iterator *it);

/**
 * Heap iterator next.
 */
static inline heap_value_t *
HEAP(iterator_next)(struct heap_iterator *it);

/* Routines. Functions below are useless for ordinary user. */

/**
 * Heapify tree after update of value having @a node attribute.
 */
static inline void
HEAP(update_node)(heap_t *heap, struct heap_node *node);

/*
 * Update backlink in the give heap_node structure.
 */
static inline void
HEAP(update_link)(heap_t *heap, heap_off_t pos);

/**
 * Sift up current node.
 */
static inline void
HEAP(sift_up)(heap_t *heap, struct heap_node *node);

/**
 * Sift down current node.
 */
static inline void
HEAP(sift_down)(heap_t *heap, struct heap_node *node);

/* Debug functions */

/**
 * Check that heap inveriants is holded.
 */
static inline int /* inline for suppress warning */
HEAP(check)(heap_t *heap);


/* Function definitions. */

/**
 * Init heap.
 */
static inline void
HEAP(create)(heap_t *heap)
{
	heap->size = 0;
	heap->capacity = 0;
	heap->harr = NULL;
}

/**
 * Destroy current heap.
 */
static inline void
HEAP(destroy)(heap_t *heap)
{
	free(heap->harr);
}

static inline void
HEAP(update_node)(heap_t *heap, struct heap_node *node)
{
	HEAP(sift_down)(heap, node);
	HEAP(sift_up)(heap, node);
}

/*
 * Update backlink in the give heap_node structure.
 */
static inline void
HEAP(update_link)(heap_t *heap, heap_off_t pos)
{
	heap->harr[pos]->pos = pos;
}

/**
 * Sift up current node.
 */
static inline void
HEAP(sift_up)(heap_t *heap, struct heap_node *node)
{
	heap_off_t curr_pos = node->pos, parent = (curr_pos - 1) / 2;

	while (curr_pos > 0 && HEAP_LESS(heap, node_to_value(node),
					 node_to_value(heap->harr[parent]))) {

		node = heap->harr[curr_pos];
		heap->harr[curr_pos] = heap->harr[parent];
		HEAP(update_link)(heap, curr_pos);
		heap->harr[parent] = node;
		HEAP(update_link)(heap, parent);

		curr_pos = parent;
		parent = (curr_pos - 1) / 2;
		/* here overflow can occure, but that won't affect */
	}
}

/**
 * Sift down current node.
 */
static inline void
HEAP(sift_down)(heap_t *heap, struct heap_node *node)
{
	heap_off_t curr_pos = node->pos, left, right;
	heap_off_t min_child;

	while (true) {
		left = 2 * curr_pos + 1;
		right = 2 * curr_pos + 2;
		min_child = left;
		if (right < heap->size &&
		    HEAP_LESS(heap, node_to_value(heap->harr[right]),
			      node_to_value(heap->harr[left])))
			min_child = right;

		if (left >= heap->size ||
		    HEAP_LESS(heap, node_to_value(heap->harr[curr_pos]),
			      node_to_value(heap->harr[min_child])))
			return;

		node = heap->harr[curr_pos];
		heap->harr[curr_pos] = heap->harr[min_child];
		heap->harr[min_child] = node;
		HEAP(update_link)(heap, curr_pos);
		HEAP(update_link)(heap, min_child);

		curr_pos = min_child;
	}
}

/**
 * Increase capacity.
 */
static inline int
HEAP(reserve)(heap_t *heap)
{
	if (heap->capacity > heap->size)
		return 0;
	heap_off_t capacity = heap->capacity == 0 ? HEAP_INITIAL_CAPACITY :
		heap->capacity << 1;
	void *harr = realloc(heap->harr, sizeof(struct heap_node *) * capacity);
	if (harr == NULL)
		return -1;
	heap->harr = harr;
	heap->capacity = capacity;
	return 0;
}

/**
 * Insert value.
 */
static inline int
HEAP(insert)(heap_t *heap, heap_value_t *value)
{
	if (HEAP(reserve)(heap))
		return -1;
	struct heap_node *node = value_to_node(value);
	heap->harr[heap->size] = node;
	HEAP(update_link)(heap, heap->size++);
	HEAP(sift_up)(heap, node); /* heapify */

	return 0;
}

/**
 * Return min value without removing it from heap.
 * If heap is empty, return NULL.
 */
static inline heap_value_t *
HEAP(top)(heap_t *heap)
{
	if (heap->size == 0)
		return NULL;
	return node_to_value(heap->harr[0]);
}

/**
 * Erase min value. Returns delete value.
 */
static inline heap_value_t *
HEAP(pop)(heap_t *heap)
{
	if (heap->size == 0)
		return NULL;
	heap_value_t *res = node_to_value(heap->harr[0]);
	HEAP(delete)(heap, res);
	return res;
}

/*
 * Delete node from heap.
 */
static inline void
HEAP(delete)(heap_t *heap, heap_value_t *value)
{
	if (heap->size == 0)
		return;

	struct heap_node *node = value_to_node(value);
	assert(! heap_node_is_stray(node));
	heap->size--;
	heap_off_t curr_pos = node->pos;
	heap_node_create(node);

	if (curr_pos == heap->size)
		return;

	heap->harr[curr_pos] = heap->harr[heap->size];
	HEAP(update_link)(heap, curr_pos);
	HEAP(update_node)(heap, heap->harr[curr_pos]);
}

static inline void
HEAP(update)(heap_t *heap, heap_value_t *value)
{
	HEAP(update_node)(heap, value_to_node(value));
}

/**
 * Heapify tree after updating all values.
 */
static inline void
HEAP(update_all)(heap_t *heap)
{
	if (heap->size <= 1)
		return;

	/* Find the parent of the last element. */
	heap_off_t curr_pos = (heap->size - 2) / 2;

	do {
		HEAP(sift_down)(heap, heap->harr[curr_pos]);
	} while (curr_pos-- > 0);
}

/**
 * Heap iterator init.
 */
static inline void
HEAP(iterator_init)(heap_t *heap, struct heap_iterator *it)
{
	it->curr_pos = 0;
	it->heap = heap;
}

/**
 * Heap iterator next.
 */
static inline heap_value_t *
HEAP(iterator_next)(struct heap_iterator *it)
{
	if (it->curr_pos == it->heap->size)
		return NULL;
	return node_to_value(it->heap->harr[it->curr_pos++]);
}

/**
 * Check that heap inveriants is holded.
 */
static inline int
HEAP(check)(heap_t *heap)
{
	heap_off_t left, right, min_child;
	for (heap_off_t curr_pos = 0;
	     2 * curr_pos + 1 < heap->size;
	     ++curr_pos) {

		left = 2 * curr_pos + 1;
		right = 2 * curr_pos + 2;
		min_child = left;
		if (right < heap->size &&
		    HEAP_LESS(heap, node_to_value(heap->harr[right]),
			      node_to_value(heap->harr[left])))
			min_child = right;

		if (HEAP_LESS(heap, node_to_value(heap->harr[min_child]),
			      node_to_value(heap->harr[curr_pos])))
			return -1;
	}

	return 0;
}

#undef node_to_value
#undef value_to_node
#undef heap_value_t
#undef heap_value_attr

#endif /* HEAP_FORWARD_DECLARATION */

#undef HEAP_FORWARD_DECLARATION
#undef HEAP_NAME
#undef HEAP_LESS
