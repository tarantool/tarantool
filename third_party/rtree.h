/*
 * Guttman's R-Tree
 * Copyright (C) 2014 Mail.RU
 */

#ifndef __RTREE_H__
#define __RTREE_H__

#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <float.h>

#define MAX_HEIGHT 16
#define DIMENSIONS 2

typedef double  coord_t;
typedef double  area_t;
typedef void*   record_t;

#define AREA_MAX DBL_MAX
#define AREA_MIN DBL_MIN

enum {
	RTREE_DIMENSION = 2,
	RTREE_PAGE_SIZE = 1024 /* R-Tree use linear search within element on the page,
				  so larger page cause worse performance */
};

class R_tree;
class R_page;
class R_tree_iterator;

class rectangle_t
{
public:
	coord_t boundary[RTREE_DIMENSION*2];

	// Squarer of distance
	area_t distance2(coord_t const* point) const
	{
		area_t d = 0;
		for (int i = 0; i < RTREE_DIMENSION; i++) {
			if (point[i] < boundary[i]) {
				d += (boundary[i] - point[i]) * (boundary[i] - point[i]);
			} else if (point[i] > boundary[RTREE_DIMENSION + i]) {
				d += (boundary[RTREE_DIMENSION + i] - point[i])
				   * (boundary[RTREE_DIMENSION + i] - point[i]);
			}
		}
		return d;
	}


	friend area_t area(rectangle_t const& r) {
		area_t area = 1;
		for (int i = RTREE_DIMENSION;
		     --i >= 0;
		     area *= r.boundary[i+RTREE_DIMENSION] - r.boundary[i]);
		return area;
	}

	void operator +=(rectangle_t const& r) {
		int i = RTREE_DIMENSION;
		while (--i >= 0) {
			boundary[i] = (boundary[i] <= r.boundary[i])
				? boundary[i] : r.boundary[i];
			boundary[i+RTREE_DIMENSION] =
				(boundary[i+RTREE_DIMENSION] >= r.boundary[i+RTREE_DIMENSION])
				? boundary[i+RTREE_DIMENSION] : r.boundary[i+RTREE_DIMENSION];
		}
	}
	rectangle_t operator + (rectangle_t const& r) const {
		rectangle_t res;
		int i = RTREE_DIMENSION;
		while (--i >= 0) {
			res.boundary[i] = (boundary[i] <= r.boundary[i])
				? boundary[i] : r.boundary[i];
			res.boundary[i+RTREE_DIMENSION] =
				(boundary[i+RTREE_DIMENSION] >= r.boundary[i+RTREE_DIMENSION])
				? boundary[i+RTREE_DIMENSION] : r.boundary[i+RTREE_DIMENSION];
		}
		return res;
	}
	bool operator& (rectangle_t const& r) const {
		int i = RTREE_DIMENSION;
		while (--i >= 0) {
			if (boundary[i] > r.boundary[i+RTREE_DIMENSION] ||
			    r.boundary[i] > boundary[i+RTREE_DIMENSION])
			{
				return false;
			}
		}
		return true;
	}
	bool operator <= (rectangle_t const& r) const {
		int i = RTREE_DIMENSION;
		while (--i >= 0) {
			if (boundary[i] < r.boundary[i] ||
			    boundary[i+RTREE_DIMENSION] > r.boundary[i+RTREE_DIMENSION])
			{
				return false;
			}
		}
		return true;
	}
	bool operator < (rectangle_t const& r) const {
		return *this <= r && *this != r;
	}

	bool operator >= (rectangle_t const& r) const {
		return r <= *this;
	}
	bool operator > (rectangle_t const& r) const {
		return r <= *this && *this != r;
	}

	bool operator == (rectangle_t const& r) const {
		int i = RTREE_DIMENSION*2;
		while (--i >= 0) {
			if (boundary[i] != r.boundary[i]) {
				return false;
			}
		}
		return true;
	}
	bool operator != (rectangle_t const& r) const {
		return !(*this == r);
	}
	bool operator_true(rectangle_t const&) const {
		return true;
	}
};

enum Spatial_search_op
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

class R_tree_iterator
{
	friend class R_tree;
	struct {
		R_page* page;
		int     pos;
	} stack[MAX_HEIGHT];

	struct Neighbor {
		void*     child;
		Neighbor* next;
		int       level;
		area_t    distance;
	};

	enum {
		N_ELEMS = (RTREE_PAGE_SIZE-sizeof(Neighbor*))/ sizeof(Neighbor)
	};

	struct Neighbor_page {
		Neighbor_page* next;
		Neighbor buf[N_ELEMS];
	};

	Neighbor* allocate_neighbour();


	typedef bool (rectangle_t::*comparator_t)(rectangle_t const& r) const;

	rectangle_t r;
	Spatial_search_op op;
	R_tree* tree;
	Neighbor* list;
	Neighbor* free;
	bool eof;
	int update_count;
	Neighbor_page* page_list;
	int page_pos;

	comparator_t intr_cmp;
	comparator_t leaf_cmp;

	bool goto_first(int sp, R_page* pg);
	bool goto_next(int sp);
	bool init(R_tree const* tree, rectangle_t const& r, Spatial_search_op op);
	void insert(Neighbor* node);

	Neighbor* new_neighbor(void* child, area_t distance, int level);
	void free_neighbor(Neighbor* n);
public:
	void reset();
	record_t next();

	R_tree_iterator();
	~R_tree_iterator();
};

class R_tree
{
	friend class R_tree_iterator;
	friend class R_page;

	typedef void* (*page_alloc_t)();
	typedef void (*page_free_t)(void*);

public:
	size_t used_size() const {
		return n_pages * RTREE_PAGE_SIZE;
	}

	unsigned number_of_records() const {
		return n_records;
	}
	bool search(rectangle_t const& r, Spatial_search_op op, R_tree_iterator& iterator) const;
	void insert(rectangle_t const& r, record_t obj);
	bool remove(rectangle_t const& r, record_t obj);
	void purge();
	R_tree(page_alloc_t page_alloc, page_free_t page_free);
	~R_tree();

protected:
	unsigned n_records;
	unsigned height;
	R_page*  root;
	int update_count;
	int n_pages;
	page_alloc_t page_alloc;
	page_free_t page_free;
};

#endif



