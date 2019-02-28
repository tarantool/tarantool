#ifndef TARANTOOL_LIB_SALAD_RTREE_H_INCLUDED
#define TARANTOOL_LIB_SALAD_RTREE_H_INCLUDED
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
#include <stddef.h>
#include <stdbool.h>
#include "small/matras.h"

#define RB_COMPACT 1
#include "small/rb.h"

/**
 * In-memory Guttman's R-tree
 */

/* Type of payload data */
typedef void *record_t;
/* Type of coordinate */
typedef double coord_t;
/* Type of square coordinate */
typedef double sq_coord_t;
/* Type of area (volume) of rectangle (box) */
typedef double area_t;

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct rtree_neighbor {
	rb_node(struct rtree_neighbor) link;
	struct rtree_neighbor *next;
	void *child;
	int level;
	sq_coord_t distance;
};

typedef rb_tree(struct rtree_neighbor) rtnt_t;

enum {
	/** Maximal possible R-tree height */
	RTREE_MAX_HEIGHT = 16,
	/** Maximal possible R-tree height */
	RTREE_MAX_DIMENSION = 20
};

/**
 * Rtree search operations. Used for searching and iterations.
 * All operations except SOP_ALL reqires a rectangle to be set,
 * and treat it in different ways
 */
enum spatial_search_op
{
	/* Find and itearate all records */
	SOP_ALL,
	/* Find and itearate records with the same rectangle */
	SOP_EQUALS,
	/* Find and itearate records that contain given rectangle */
	SOP_CONTAINS,
	/* Find and itearate records that strictly contain given rectangle */
	SOP_STRICT_CONTAINS,
	/* Find and itearate records that overlaps with given rectangle */
	SOP_OVERLAPS,
	/* Find and itearate records that belongs to given rectangle */
	SOP_BELONGS,
	/* Find and itearate records that strictly belongs to given rectangle */
	SOP_STRICT_BELONGS,
	/* Find and itearate nearest records from a given point (the point is
	 * acluattly lowest_point of given rectangle). Records are iterated in
	 * order of distance to given point. Yes, it is KNN iterator */
	SOP_NEIGHBOR
};

/* pointers to page allocation and deallocations functions */
typedef void *(*rtree_extent_alloc_t)(void *ctx);
typedef void (*rtree_extent_free_t)(void *ctx, void *extent);

/* A box in RTREE_DIMENSION space */
struct rtree_rect
{
	/* coords: { low X, upper X, low Y, upper Y, etc } */
	coord_t coords[RTREE_MAX_DIMENSION * 2];
};

/* Type of function, comparing two rectangles */
typedef bool (*rtree_comparator_t)(const struct rtree_rect *rt1,
				   const struct rtree_rect *rt2,
				   unsigned dimension);

/* Type distance comparison */
enum rtree_distance_type {
	RTREE_EUCLID = 0, /* Euclid distance, sqrt(dx*dx + dy*dy) */
	RTREE_MANHATTAN = 1 /* Manhattan distance, fabs(dx) + fabs(dy) */
};

/* Main rtree struct */
struct rtree
{
	/* Root node (page) */
	struct rtree_page *root;
	/* R-tree dimension */
	unsigned dimension;
	/* Minimal number of branches in tree page */
	unsigned page_min_fill;
	/* Maximal number of branches in tree page */
	unsigned page_max_fill;
	/* Page size in bytes */
	unsigned page_size;
	/* Page branch size in bytes */
	unsigned page_branch_size;
	/* For iterator usage, pages are splitted into structs neighbours
	 * Here is number of neighbours fit into one page */
	unsigned neighbours_in_page;
	/* Number of records in entire tree */
	unsigned n_records;
	/* Height of a tree */
	unsigned height;
	/* Unique version that increments on every tree modification */
	unsigned version;
	/* Number of allocated (used) pages */
	unsigned n_pages;
	/* Matras for allocating new page */
	struct matras mtab;
	/* List of free pages */
	void *free_pages;
	/* Distance type */
	enum rtree_distance_type distance_type;
};

/* Struct for iteration and retrieving rtree values */
struct rtree_iterator
{
	/* Pointer to rtree */
	const struct rtree *tree;
	/* Rectangle of current iteration operation */
	struct rtree_rect rect;
	/* Type of current iteration operation */
	enum spatial_search_op op;
	/* Flag that means that no more values left */
	bool eof;
	/* A verion of a tree when the iterator was created */
	unsigned version;

	/* Special rb tree of closest neqighbors
	 * Used only for iteration with op = SOP_NEIGHBOR
	 * For allocating list entries, page allocator of tree is used.
	 * Allocated page is much bigger than list entry and thus
	 * provides several list entries.
	 */
	rtnt_t neigh_tree;
	/* List of unused (deleted) list entries */
	struct rtree_neighbor *neigh_free_list;
	/* List of tree pages, allocated for list entries */
	struct rtree_neighbor_page *page_list;
	/* Position of ready-to-use list entry in allocated page */
	unsigned page_pos;

	/* Comparators for comparison rectagnle of the iterator with
	 * rectangles of tree nodes. If the comparator returns true,
	 * the node is accepted; if false - skipped.
	 */
	/* Comparator for interanal (not leaf) nodes of the tree */
	rtree_comparator_t intr_cmp;
	/* Comparator for leaf nodes of the tree */
	rtree_comparator_t leaf_cmp;

	/* Current path of search in tree */
	struct {
		struct rtree_page *page;
		int pos;
	} stack[RTREE_MAX_HEIGHT];
};

/**
 * @brief Rectangle normalization. Makes lower_point member to be vertex
 * with minimal coordinates, and upper_point - with maximal coordinates.
 * Useful when the rectangle is initialized with two diagonal vertexes that
 * could be not lowest and highest correspondingly.
 * @param rect - pointer to a rectangle
 */
void
rtree_rect_normalize(struct rtree_rect *rect, unsigned dimension);

/**
 * @brief Set up 2D rectangle by 4 coordinates
 * @param rect - pointer to a rectangle
 * @params left, bottom, right, top - corresponding coordinates
 */
void
rtree_set2d(struct rtree_rect *rect,
	    coord_t left, coord_t bottom, coord_t right, coord_t top);

/**
 * @brief Set up 2D rectangle by 2 coordinates (set to point)
 * @param rect - pointer to a rectangle
 * @params x, y - corresponding coordinates
 */
void
rtree_set2dp(struct rtree_rect *rect, coord_t x, coord_t y);

/**
 * @brief Initialize a tree
 * @param tree - pointer to a tree
 * @param extent_size - size of extents allocated by extent_alloc (see next)
 * @param extent_alloc - extent allocation function
 * @param extent_free - extent deallocation function
 * @param alloc_ctx - argument passed to extent allocator
 * @return 0 on success, -1 on error
 */
int
rtree_init(struct rtree *tree, unsigned dimension, uint32_t extent_size,
	   rtree_extent_alloc_t extent_alloc, rtree_extent_free_t extent_free,
	   void *alloc_ctx, enum rtree_distance_type distance_type);

/**
 * @brief Destroy a tree
 * @param tree - pointer to a tree
 */
void
rtree_destroy(struct rtree *tree);

/**
 * @brief Delete all data from a tree, i.e. make it empty
 * @param tree - pointer to a tree
 */
void
rtree_purge(struct rtree *tree);

/**
 * @brief Find a record in a tree
 * @return true if at least one record found (false otherwise)
 * @param tree - pointer to a tree
 * @param rect - rectangle to find (the meaning depends on op argument)
 * @param op - type of search, see enum spatial_search_op for details
 * @param itr - pointer to iterator (must be initialized earlier),
 *  iterator itr should be used for accessing found record
 */
bool
rtree_search(const struct rtree *tree, const struct rtree_rect *rect,
	     enum spatial_search_op op, struct rtree_iterator *itr);

/**
 * @brief Insert a record to the tree
 * @param tree - pointer to a tree
 * @param rect - rectangle to insert
 * @param obj - record to insert
 */
void
rtree_insert(struct rtree *tree, struct rtree_rect *rect, record_t obj);

/**
 * @brief Remove the record from a tree
 * @return true if the record deleted (false otherwise)
 * @param tree - pointer to a tree
 * @param rect - rectangle of the record to delete
 * @param obj - record to delete
 */
bool
rtree_remove(struct rtree *tree, const struct rtree_rect *rect, record_t obj);

/**
 * @brief Size of memory used by tree
 * @param tree - pointer to a tree
 **/
size_t
rtree_used_size(const struct rtree *tree);

/**
 * @brief Number of records in the tree
 * @param tree - pointer to a tree
 **/
unsigned
rtree_number_of_records(const struct rtree *tree);

#if 0
/**
 * @brief Print a tree to stdout. Debug function, thus disabled.
 * Needs <stdio.h> to be included before
 * @param tree - pointer to a tree
 **/
void
rtree_debug_print(const struct rtree *tree);
#endif

/**
 * @brief Initialize an iterator for rtree
 * Every iterator must be initialized before any usage
 * @param itr - pointer to a iterator
 **/
void
rtree_iterator_init(struct rtree_iterator *itr);

/**
 * @brief Destroy an iterator
 * Every iterator must be destroyed
 * @param itr - pointer to a iterator
 **/
void
rtree_iterator_destroy(struct rtree_iterator *itr);

/**
 * @brief Retrieve a record from the iterator and iterate it to the next record
 * @return a record or NULL if no more records
 * @param itr - pointer to a iterator
 **/
record_t
rtree_iterator_next(struct rtree_iterator *itr);

#if defined(__cplusplus)
} /* extern "C" { */
#endif /* defined(__cplusplus) */

#endif /* #ifndef TARANTOOL_LIB_SALAD_RTREE_H_INCLUDED */
