/*
 * Guttman's R-Tree
 * Copyright (C) 2014 Mail.RU
 */

#ifndef TARANTOOL_RTREE_H_INCLUDED
#define TARANTOOL_RTREE_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>

/* Type of payload data */
typedef void *record_t;
/* Type of coordinate */
typedef double coord_t;
/* Minimal and maximal coordinate */
#include <float.h>
#define RTREE_COORD_MAX DBL_MAX
#define RTREE_COORD_MIN DBL_MIN

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

typedef void* (*rtree_page_alloc_t)();
typedef void (*rtree_page_free_t)(void*);

struct rtree_point
{
	coord_t coords[RTREE_DIMENSION];
};

struct rtree_rect
{
	struct rtree_point lower_point, upper_point;
};

typedef bool (*rtree_comparator_t)(const struct rtree_rect *rt1,
				   const struct rtree_rect *rt2);

struct rtree
{
	struct rtree_page *root;
	unsigned n_records;
	unsigned height;
	unsigned version;
	unsigned n_pages;
	rtree_page_alloc_t page_alloc;
	rtree_page_free_t page_free;
};

struct rtree_iterator
{
	const struct rtree *tree;
	struct rtree_rect rect;
	enum spatial_search_op op;
	bool eof;
	int version;

	struct rtree_neighbor *neigh_list;
	struct rtree_neighbor *neigh_free_list;
	struct rtree_neighbor_page *page_list;
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
