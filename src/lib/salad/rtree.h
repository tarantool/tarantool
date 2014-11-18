#ifndef TARANTOOL_RTREE_H_INCLUDED
#define TARANTOOL_RTREE_H_INCLUDED
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

enum {
	/* Number of dimensions of R-tree geometry */
	RTREE_DIMENSION = 2,
	/* Maximal possible R-tree height */
	RTREE_MAX_HEIGHT = 16,
	/* R-Tree use linear search within element on the page,
	   so larger page cause worse performance */
	RTREE_PAGE_SIZE = 1024
};

enum spatial_search_op
{
	SOP_ALL,
	SOP_EQUALS,
	SOP_CONTAINS,
	SOP_STRICT_CONTAINS,
	SOP_OVERLAPS,
	SOP_BELONGS,
	SOP_STRICT_BELONGS,
	SOP_NEIGHBOR
};

/* pointers to page allocation and deallocations functions */
typedef void* (*rtree_page_alloc_t)();
typedef void (*rtree_page_free_t)(void*);

/* A point in RTREE_DIMENSION space */
struct rtree_point
{
	/* coordinates of the point */
	coord_t coords[RTREE_DIMENSION];
};

/* A box in RTREE_DIMENSION space */
struct rtree_rect
{
	/* vertex with minimal coordinates */
	struct rtree_point lower_point;
	/* vertex with maximal coordinates (diagonal to lower_point) */
	struct rtree_point upper_point;
};

/* Type of function, comparing two rectangles */
typedef bool (*rtree_comparator_t)(const struct rtree_rect *rt1,
				   const struct rtree_rect *rt2);

/* Main rtree struct */
struct rtree
{
	/* Root node (page) */
	struct rtree_page *root;
	/* Number of records in entire tree */
	unsigned n_records;
	/* Height of a tree */
	unsigned height;
	/* Unique version that increments on every tree modification */
	unsigned version;
	/* Number of allocated (used) pages */
	unsigned n_pages;
	/* Function for allocation new pages */
	rtree_page_alloc_t page_alloc;
	/* Function for deallocation new pages */
	rtree_page_free_t page_free;
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
	int version;

	/* Special single-linked list of closest neqighbors
	 * Used only for iteration with op = SOP_NEIGHBOR
	 * For allocating list entries, page allocator of tree is used.
	 * Allocated page is much bigger than list entry and thus
	 * provides several list entries.
	 */
	struct rtree_neighbor *neigh_list;
	/* List of unused (deleted) list entries */
	struct rtree_neighbor *neigh_free_list;
	/* List of tree pages, allocated for list entries */
	struct rtree_neighbor_page *page_list;
	/* Position of ready-to-use list entry in allocated page */
	int page_pos;

	rtree_comparator_t intr_cmp;
	rtree_comparator_t leaf_cmp;

	struct {
		struct rtree_page *page;
		int pos;
	} stack[RTREE_MAX_HEIGHT];
};

void
rtree_rect_normalize(struct rtree_rect *rect);

void
rtree_set2d(struct rtree_rect *rect,
	    coord_t left, coord_t bottom, coord_t right, coord_t top);

void
rtree_init(struct rtree *tree, rtree_page_alloc_t page_alloc, rtree_page_free_t page_free);

void
rtree_destroy(struct rtree *tree);

void
rtree_purge(struct rtree *tree);

bool
rtree_search(const struct rtree *tree, const struct rtree_rect *rect,
	     enum spatial_search_op op, struct rtree_iterator *itr);

void
rtree_insert(struct rtree *tree, struct rtree_rect *rect, record_t obj);

bool
rtree_remove(struct rtree *tree, const struct rtree_rect *rect, record_t obj);

size_t
rtree_used_size(const struct rtree *tree);

unsigned
rtree_number_of_records(const struct rtree *tree);

#if 0
void
rtree_debug_print(const struct rtree *tree);
#endif

void
rtree_iterator_init(struct rtree_iterator *itr);

void
rtree_iterator_destroy(struct rtree_iterator *itr);

record_t
rtree_iterator_next(struct rtree_iterator *itr);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* #ifndef TARANTOOL_RTREE_H_INCLUDED */
