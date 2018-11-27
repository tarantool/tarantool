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
#include "rtree.h"
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <sys/types.h>

/*------------------------------------------------------------------------- */
/* R-tree internal structures definition */
/*------------------------------------------------------------------------- */
enum {
	/* rtree will try to determine optimal page size */
	RTREE_OPTIMAL_BRANCHES_IN_PAGE = 18,
	/* actual number of branches could be up to double of the previous
	 * constant */
	RTREE_MAXIMUM_BRANCHES_IN_PAGE = RTREE_OPTIMAL_BRANCHES_IN_PAGE * 2
};

struct rtree_page_branch {
	union {
		struct rtree_page *page;
		record_t record;
	} data;
	struct rtree_rect rect;
};

enum {
	RTREE_BRANCH_DATA_SIZE = offsetof(struct rtree_page_branch, rect)
};

struct rtree_page {
	/* number of branches at page */
	unsigned n;
	/* branches */
	struct rtree_page_branch data[];
};

struct rtree_neighbor_page {
	struct rtree_neighbor_page* next;
	struct rtree_neighbor buf[];
};

struct rtree_reinsert_list {
	struct rtree_page *chain;
	int level;
};

static int
neighbor_cmp(const struct rtree_neighbor *a, const struct rtree_neighbor *b)
{
	return a->distance < b->distance ? -1 :
	       a->distance > b->distance ? 1 :
	       a->level < b->level ? -1 :
	       a->level > b->level ? 1 :
	       a < b ? -1 : a > b ? 1 : 0;
	return 0;
}

rb_gen(, rtnt_, rtnt_t, struct rtree_neighbor, link, neighbor_cmp);

/*------------------------------------------------------------------------- */
/* R-tree rectangle methods */
/*------------------------------------------------------------------------- */

void
rtree_rect_normalize(struct rtree_rect *rect, unsigned dimension)
{
	for (int i = dimension; --i >= 0; ) {
		coord_t *coords = &rect->coords[2 * i];
		if (coords[0] <= coords[1])
			continue;
		coord_t tmp = coords[0];
		coords[0] = coords[1];
		coords[1] = tmp;
	}
}

static void
rtree_rect_copy(struct rtree_rect *to, const struct rtree_rect *from,
		unsigned dimension)
{
	for (int i = dimension * 2; --i >= 0; )
		to->coords[i] = from->coords[i];
}

void
rtree_set2d(struct rtree_rect *rect,
	    coord_t left, coord_t bottom, coord_t right, coord_t top)
{
	rect->coords[0] = left;
	rect->coords[1] = right;
	rect->coords[2] = bottom;
	rect->coords[3] = top;
}

void
rtree_set2dp(struct rtree_rect *rect, coord_t x, coord_t y)
{
	rect->coords[0] = x;
	rect->coords[1] = x;
	rect->coords[2] = y;
	rect->coords[3] = y;
}

/* Manhattan distance */
static sq_coord_t
rtree_rect_neigh_distance(const struct rtree_rect *rect,
			   const struct rtree_rect *neigh_rect,
			   unsigned dimension)
{
	sq_coord_t result = 0;
	for (int i = dimension; --i >= 0; ) {
		const coord_t *coords = &rect->coords[2 * i];
		coord_t neigh_coord = neigh_rect->coords[2 * i];
		if (neigh_coord < coords[0]) {
			sq_coord_t diff = (sq_coord_t)(neigh_coord - coords[0]);
			result += -diff;
		} else if (neigh_coord > coords[1]) {
			sq_coord_t diff = (sq_coord_t)(neigh_coord - coords[1]);
			result += diff;
		}
	}
	return result;
}

/* Euclid distance, squared */
static sq_coord_t
rtree_rect_neigh_distance2(const struct rtree_rect *rect,
			   const struct rtree_rect *neigh_rect,
			   unsigned dimension)
{
	sq_coord_t result = 0;
	for (int i = dimension; --i >= 0; ) {
		const coord_t *coords = &rect->coords[2 * i];
		coord_t neigh_coord = neigh_rect->coords[2 * i];
		if (neigh_coord < coords[0]) {
			sq_coord_t diff = (sq_coord_t)(neigh_coord - coords[0]);
			result += diff * diff;
		} else if (neigh_coord > coords[1]) {
			sq_coord_t diff = (sq_coord_t)(neigh_coord - coords[1]);
			result += diff * diff;
		}
	}
	return result;
}

static area_t
rtree_rect_area(const struct rtree_rect *rect, unsigned dimension)
{
	area_t area = 1;
	for (int i = dimension; --i >= 0; ) {
		const coord_t *coords = &rect->coords[2 * i];
		area *= coords[1] - coords[0];
	}
	return area;
}

static coord_t
rtree_rect_half_margin(const struct rtree_rect *rect, unsigned dimension)
{
	coord_t hm = 0;
	for (int i = dimension; --i >= 0; ) {
		const coord_t *coords = &rect->coords[2 * i];
		hm += coords[1] - coords[0];
	}
	return hm;
}

static void
rtree_rect_add(struct rtree_rect *to, const struct rtree_rect *item,
	       unsigned dimension)
{
	for (int i = dimension; --i >= 0; ) {
		coord_t *to_coords = &to->coords[2 * i];
		const coord_t *item_coords = &item->coords[2 * i];
		if (to_coords[0] > item_coords[0])
			to_coords[0] = item_coords[0];
		if (to_coords[1] < item_coords[1])
			to_coords[1] = item_coords[1];
	}
}

static coord_t
rtree_min(coord_t a, coord_t b)
{
	return a < b ? a : b;
}

static coord_t
rtree_max(coord_t a, coord_t b)
{
	return a > b ? a : b;
}

static void
rtree_rect_cover(const struct rtree_rect *item1,
		 const struct rtree_rect *item2,
		 struct rtree_rect *result,
		 unsigned dimension)
{
	for (int i = dimension; --i >= 0; ) {
		const coord_t *i1_coords = &item1->coords[2 * i];
		const coord_t *i2_coords = &item2->coords[2 * i];
		coord_t *r_coords = &result->coords[2 * i];
		r_coords[0] = rtree_min(i1_coords[0], i2_coords[0]);
		r_coords[1] = rtree_max(i1_coords[1], i2_coords[1]);
	}
}

static void
rtree_rect_intersection(const struct rtree_rect *item1,
			const struct rtree_rect *item2,
			struct rtree_rect *result,
			unsigned dimension)
{
	for (int i = dimension; --i >= 0; ) {
		const coord_t *i1_coords = &item1->coords[2 * i];
		const coord_t *i2_coords = &item2->coords[2 * i];
		coord_t *r_coords = &result->coords[2 * i];
		if (i1_coords[0] > i2_coords[1] || i1_coords[1] < i2_coords[0])
			r_coords[0] = r_coords[1] = 0;
		else {
			r_coords[0] = rtree_max(i1_coords[0], i2_coords[0]);
			r_coords[1] = rtree_min(i1_coords[1], i2_coords[1]);
		}
	}
}

static bool
rtree_rect_intersects_rect(const struct rtree_rect *rt1,
			   const struct rtree_rect *rt2,
			   unsigned dimension)
{
	for (int i = dimension; --i >= 0; ) {
		const coord_t *coords1 = &rt1->coords[2 * i];
		const coord_t *coords2 = &rt2->coords[2 * i];
		if (coords1[0] > coords2[1] || coords1[1] < coords2[0])
			return false;
	}
	return true;
}

static bool
rtree_rect_in_rect(const struct rtree_rect *rt1,
		   const struct rtree_rect *rt2,
		   unsigned dimension)
{
	for (int i = dimension; --i >= 0; ) {
		const coord_t *coords1 = &rt1->coords[2 * i];
		const coord_t *coords2 = &rt2->coords[2 * i];
		if (coords1[0] < coords2[0] || coords1[1] > coords2[1])
			return false;
	}
	return true;
}

static bool
rtree_rect_strict_in_rect(const struct rtree_rect *rt1,
			  const struct rtree_rect *rt2,
			  unsigned dimension)
{
	for (int i = dimension; --i >= 0; ) {
		const coord_t *coords1 = &rt1->coords[2 * i];
		const coord_t *coords2 = &rt2->coords[2 * i];
		if (coords1[0] <= coords2[0] || coords1[1] >= coords2[1])
			return false;
	}
	return true;
}

static bool
rtree_rect_holds_rect(const struct rtree_rect *rt1,
		      const struct rtree_rect *rt2,
		      unsigned dimension)
{
	return rtree_rect_in_rect(rt2, rt1, dimension);
}

static bool
rtree_rect_strict_holds_rect(const struct rtree_rect *rt1,
			     const struct rtree_rect *rt2,
			     unsigned dimension)
{
	return rtree_rect_strict_in_rect(rt2, rt1, dimension);
}

static bool
rtree_rect_equal_to_rect(const struct rtree_rect *rt1,
			 const struct rtree_rect *rt2,
			 unsigned dimension)
{
	for (int i = dimension * 2; --i >= 0; )
		if (rt1->coords[i] != rt2->coords[i])
			return false;
	return true;
}

static bool
rtree_always_true(const struct rtree_rect *rt1,
		  const struct rtree_rect *rt2,
		  unsigned dimension)
{
	(void) rt1;
	(void) rt2;
	(void) dimension;
	return true;
}

/*------------------------------------------------------------------------- */
/* R-tree page methods */
/*------------------------------------------------------------------------- */

static struct rtree_page *
rtree_page_alloc(struct rtree *tree)
{
	if (tree->free_pages) {
		struct rtree_page *result =
			(struct rtree_page *)tree->free_pages;
		tree->free_pages = *(void **)tree->free_pages;
		return result;
	} else {
		uint32_t unused_id;
		return (struct rtree_page *)
			matras_alloc(&tree->mtab, &unused_id);
	}
}

static void
rtree_page_free(struct rtree *tree, struct rtree_page *page)
{
	*(void **)page = tree->free_pages;
	tree->free_pages = (void *)page;
}

static struct rtree_page_branch *
rtree_branch_get(const struct rtree *tree, const struct rtree_page *page,
		 unsigned ind)
{
	return (struct rtree_page_branch *)
		((char *)page->data + ind * tree->page_branch_size);
}

static void
rtree_branch_copy(struct rtree_page_branch *to,
		  const struct rtree_page_branch *from, unsigned dimension)
{
	to->data = from->data;
	rtree_rect_copy(&to->rect, &from->rect, dimension);
}


static void
set_next_reinsert_page(const struct rtree *tree, struct rtree_page *page,
		       struct rtree_page *next_page)
{
	/* The page must be MIN_FILLed, so last branch is unused */
	struct rtree_page_branch *b = rtree_branch_get(tree, page,
						       tree->page_max_fill - 1);
	b->data.page = next_page;
}

struct rtree_page *
get_next_reinsert_page(const struct rtree *tree, const struct rtree_page *page)
{
	struct rtree_page_branch *b = rtree_branch_get(tree, page,
						       tree->page_max_fill - 1);
	return b->data.page;
}

/* Calculate cover of all rectangles at page */
static void
rtree_page_cover(const struct rtree *tree, const struct rtree_page *page,
		 struct rtree_rect *res)
{
	rtree_rect_copy(res, &rtree_branch_get(tree, page, 0)->rect,
			tree->dimension);
	for (unsigned i = 1; i < page->n; i++) {
		rtree_rect_add(res, &rtree_branch_get(tree, page, i)->rect,
			       tree->dimension);
	}
}

/* Create root page by first inserting record */
static void
rtree_page_init_with_record(const struct rtree *tree, struct rtree_page *page,
			    struct rtree_rect *rect, record_t obj)
{
	struct rtree_page_branch *b = rtree_branch_get(tree, page, 0);
	page->n = 1;
	rtree_rect_copy(&b->rect, rect, tree->dimension);
	b->data.record = obj;
}

/* Create new root page (root splitting) */
static void
rtree_page_init_with_pages(const struct rtree *tree, struct rtree_page *page,
			   struct rtree_page *page1, struct rtree_page *page2)
{
	page->n = 2;
	struct rtree_page_branch *b = rtree_branch_get(tree, page, 0);
	rtree_page_cover(tree, page1, &b->rect);
	b->data.page = page1;
	b = rtree_branch_get(tree, page, 1);
	rtree_page_cover(tree, page2, &b->rect);
	b->data.page = page2;
}

static struct rtree_page *
rtree_split_page(struct rtree *tree, struct rtree_page *page,
		 const struct rtree_page_branch *br)
{
	assert(page->n == tree->page_max_fill);
	const struct rtree_rect *rects[RTREE_MAXIMUM_BRANCHES_IN_PAGE + 1];
	unsigned ids[RTREE_MAXIMUM_BRANCHES_IN_PAGE + 1];
	rects[0] = &br->rect;
	ids[0] = 0;
	for (unsigned i = 0; i < page->n; i++) {
		struct rtree_page_branch *b = rtree_branch_get(tree, page, i);
		rects[i + 1] = &b->rect;
		ids[i + 1] = i + 1;
	}
	const unsigned n = page->n + 1;
	const unsigned k_max = n - 2 * tree->page_min_fill;
	unsigned d = tree->dimension;
	unsigned best_axis = 0;
	coord_t best_s = 0;
	for (unsigned a = 0; a < d; a++) {
		for (unsigned i = 0; i < n - 1; i++) {
			unsigned min_i = i;
			coord_t min_l = rects[ids[i]]->coords[2 * a];
			coord_t min_r = rects[ids[i]]->coords[2 * a + 1];
			for (unsigned j = i + 1; j < n; j++) {
				coord_t l = rects[ids[j]]->coords[2 * a];
				coord_t r = rects[ids[j]]->coords[2 * a + 1];
				if (l < min_l || (l == min_l && r < min_r)) {
					min_i = j;
					min_l = l;
					min_r = r;
				}
			}
			unsigned tmp = ids[i];
			ids[i] = ids[min_i];
			ids[min_i] = tmp;
		}
		struct rtree_rect test_rect;
		coord_t dir_hm[RTREE_MAXIMUM_BRANCHES_IN_PAGE + 1];
		coord_t rev_hm[RTREE_MAXIMUM_BRANCHES_IN_PAGE + 1];
		dir_hm[0] = 0;
		rtree_rect_copy(&test_rect, rects[ids[0]], d);
		dir_hm[1] = rtree_rect_half_margin(&test_rect, d);
		for (unsigned i = 1; i < n - tree->page_min_fill; i++) {
			rtree_rect_add(&test_rect, rects[ids[i]], d);
			dir_hm[i + 1] = rtree_rect_half_margin(&test_rect, d);
		}
		rev_hm[0] = 0;
		rtree_rect_copy(&test_rect, rects[ids[n - 1]], d);
		rev_hm[1] = rtree_rect_half_margin(&test_rect, d);
		for (unsigned i = 1; i < n - tree->page_min_fill; i++) {
			rtree_rect_add(&test_rect, rects[ids[n - i - 1]], d);
			rev_hm[i + 1] = rtree_rect_half_margin(&test_rect, d);
		}
		coord_t s = 0;
		for (unsigned k = 0; k < k_max; k++) {
			unsigned k1 = tree->page_min_fill + k;
			unsigned k2 = n - k1;
			s += dir_hm[k1] + rev_hm[k2];
		}
		if (a == 0 || s < best_s) {
			best_axis = a;
			best_s = s;
		}
	}
	unsigned a = best_axis;
	for (unsigned i = 0; i < n - 1; i++) {
		unsigned min_i = i;
		coord_t min_l = rects[ids[i]]->coords[2 * a];
		coord_t min_r = rects[ids[i]]->coords[2 * a + 1];
		for (unsigned j = i + 1; j < n; j++) {
			coord_t l = rects[ids[j]]->coords[2 * a];
			coord_t r = rects[ids[j]]->coords[2 * a + 1];
			if (l < min_l || (l == min_l && r < min_r)) {
				min_i = j;
				min_l = l;
				min_r = r;
			}
		}
		unsigned tmp = ids[i];
		ids[i] = ids[min_i];
		ids[min_i] = tmp;
	}
	area_t min_overlap = 0;
	area_t min_area = 0;
	unsigned min_k = 0;
	for (unsigned k = 0; k < k_max; k++) {
		unsigned k1 = tree->page_min_fill + k;
		/* unsigned k2 = n - k1; */
		struct rtree_rect rt1, rt2, over_rt;
		rtree_rect_copy(&rt1, rects[ids[0]], d);
		for (unsigned i = 1; i < k1; i++) {
			rtree_rect_add(&rt1, rects[ids[i]], d);
		}
		rtree_rect_copy(&rt2, rects[ids[k1]], d);
		for (unsigned i = k1 + 1; i < n; i++) {
			rtree_rect_add(&rt2, rects[ids[i]], d);
		}
		rtree_rect_intersection(&rt1, &rt2, &over_rt, d);
		area_t overlap = rtree_rect_area(&over_rt, d);
		area_t area = rtree_rect_area(&rt1, d) +
			rtree_rect_area(&rt2, d);
		if (k == 0 || overlap < min_overlap ||
			(overlap == min_overlap && area < min_area)) {
			min_k = k;
			min_overlap = overlap;
			min_area = area;
		}
	}
	unsigned k = min_k;
	unsigned k1 = tree->page_min_fill + k;
	unsigned k2 = n - k1;
	struct rtree_page *new_page = rtree_page_alloc(tree);
	tree->n_pages++;
	char taken[RTREE_MAXIMUM_BRANCHES_IN_PAGE];
	memset(taken, 0, sizeof(taken));
	for (unsigned i = 0; i < k1; i++) {
		struct rtree_page_branch *new_b =
			rtree_branch_get(tree, new_page, i);
		const struct rtree_page_branch *from_b = br;
		if (ids[i]) {
			from_b = rtree_branch_get(tree, page, ids[i] - 1);
			taken[ids[i] - 1] = 1;
		}
		rtree_branch_copy(new_b, from_b, d);
	}
	unsigned moved = 0;
	for (unsigned i = 0, j = 0; j < page->n; j++) {
		if (taken[j] == 0) {
			struct rtree_page_branch *to, *from;
			to = rtree_branch_get(tree, page, i++);
			from = rtree_branch_get(tree, page, j);
			rtree_branch_copy(to, from, tree->dimension);
			moved++;
		}
	}
	assert(moved == k2 || moved + 1 == k2);
	if (moved + 1 == k2) {
		struct rtree_page_branch *to;
		to = rtree_branch_get(tree, page, moved);
		rtree_branch_copy(to, br, tree->dimension);
	}
	new_page->n = k1;
	page->n = k2;
	return new_page;
}

static struct rtree_page*
rtree_page_add_branch(struct rtree *tree, struct rtree_page *page,
		      const struct rtree_page_branch *br)
{
	if (page->n < tree->page_max_fill) {
		struct rtree_page_branch *b;
		b = rtree_branch_get(tree, page, page->n++);
		rtree_branch_copy(b, br, tree->dimension);
		return NULL;
	} else {
		return rtree_split_page(tree, page, br);
	}
}

static void
rtree_page_remove_branch(struct rtree *tree, struct rtree_page *page, int i)
{
	page->n--;
	for (unsigned j = i; j < page->n; j++) {
		struct rtree_page_branch *to, *from;
		to = rtree_branch_get(tree, page, j);
		from = rtree_branch_get(tree, page, j + 1);
		rtree_branch_copy(to, from, tree->dimension);
	}
}

static struct rtree_page *
rtree_page_insert(struct rtree *tree, struct rtree_page *page,
		  const struct rtree_rect *rect, record_t obj, int level)
{
	struct rtree_page_branch br;
	if (--level != 0) {
		/* not a leaf page, minize area increase */
		unsigned mini = 0;
		char found = 0;
		area_t min_incr = 0, best_area = 0;
		for (unsigned i = 0; i < page->n; i++) {
			struct rtree_page_branch *b;
			b = rtree_branch_get(tree, page, i);
			area_t r_area = rtree_rect_area(&b->rect,
							tree->dimension);
			struct rtree_rect cover;
			rtree_rect_cover(&b->rect, rect,
					 &cover, tree->dimension);
			area_t incr = rtree_rect_area(&cover,
						      tree->dimension);
			incr -= r_area;
			assert(incr >= 0);
			if (i == 0 || incr < min_incr || (incr == min_incr && r_area < best_area)) {
				best_area = r_area;
				min_incr = incr;
				mini = i;
				found = 1;
			}
		}
		assert(found);
		(void) found;
		struct rtree_page_branch *b;
		b = rtree_branch_get(tree, page, mini);
		struct rtree_page *p = b->data.page;
		struct rtree_page *q = rtree_page_insert(tree, p,
							 rect, obj, level);
		if (q == NULL) {
			/* child was not split */
			rtree_rect_add(&b->rect, rect, tree->dimension);
			return NULL;
		} else {
			/* child was split */
			rtree_page_cover(tree, p, &b->rect);
			br.data.page = q;
			rtree_page_cover(tree, q, &br.rect);
			return rtree_page_add_branch(tree, page, &br);
		}
	} else {
		br.data.record = obj;
		rtree_rect_copy(&br.rect, rect, tree->dimension);
		return rtree_page_add_branch(tree, page, &br);
	}
}

static bool
rtree_page_remove(struct rtree *tree, struct rtree_page *page,
		  const struct rtree_rect *rect, record_t obj,
		  int level, struct rtree_reinsert_list *rlist)
{
	unsigned d = tree->dimension;
	if (--level != 0) {
		for (unsigned i = 0; i < page->n; i++) {
			struct rtree_page_branch *b;
			b = rtree_branch_get(tree, page, i);
			if (!rtree_rect_intersects_rect(&b->rect, rect, d))
				continue;
			struct rtree_page *next_page = b->data.page;
			if (!rtree_page_remove(tree, next_page, rect,
					       obj, level, rlist))
				continue;
			if (next_page->n >= tree->page_min_fill) {
				rtree_page_cover(tree, next_page, &b->rect);
			} else {
				/* not enough entries in child */
				set_next_reinsert_page(tree, next_page,
						       rlist->chain);
				rlist->chain = next_page;
				rlist->level = level - 1;
				rtree_page_remove_branch(tree, page, i);
			}
			return true;
		}
	} else {
		for (unsigned i = 0; i < page->n; i++) {
			struct rtree_page_branch *b;
			b = rtree_branch_get(tree, page, i);
			if (b->data.page == obj) {
				rtree_page_remove_branch(tree, page, i);
				return true;
			}
		}
	}
	return false;
}

static void
rtree_page_purge(struct rtree *tree, struct rtree_page *page, int level)
{
	if (--level != 0) { /* this is an internal node in the tree */
		for (unsigned i = 0; i < page->n; i++) {
			struct rtree_page_branch *b;
			b = rtree_branch_get(tree, page, i);
			rtree_page_purge(tree, b->data.page, level);
		}
	}
	rtree_page_free(tree, page);
}

/*------------------------------------------------------------------------- */
/* R-tree iterator methods */
/*------------------------------------------------------------------------- */

static bool
rtree_iterator_goto_first(struct rtree_iterator *itr, unsigned sp,
			  struct rtree_page* pg)
{
	unsigned d = itr->tree->dimension;
	if (sp + 1 == itr->tree->height) {
		for (unsigned i = 0, n = pg->n; i < n; i++) {
			struct rtree_page_branch *b;
			b = rtree_branch_get(itr->tree, pg, i);
			if (itr->leaf_cmp(&itr->rect, &b->rect, d)) {
				itr->stack[sp].page = pg;
				itr->stack[sp].pos = i;
				return true;
			}
		}
	} else {
		for (unsigned i = 0, n = pg->n; i < n; i++) {
			struct rtree_page_branch *b;
			b = rtree_branch_get(itr->tree, pg, i);
			if (itr->intr_cmp(&itr->rect, &b->rect, d)
			    && rtree_iterator_goto_first(itr, sp + 1,
							 b->data.page))
			{
				itr->stack[sp].page = pg;
				itr->stack[sp].pos = i;
				return true;
			}
		}
	}
	return false;
}


static bool
rtree_iterator_goto_next(struct rtree_iterator *itr, unsigned sp)
{
	unsigned d = itr->tree->dimension;
	struct rtree_page *pg = itr->stack[sp].page;
	if (sp + 1 == itr->tree->height) {
		for (unsigned i = itr->stack[sp].pos, n = pg->n; ++i < n;) {
			struct rtree_page_branch *b;
			b = rtree_branch_get(itr->tree, pg, i);
			if (itr->leaf_cmp(&itr->rect, &b->rect, d)) {
				itr->stack[sp].pos = i;
				return true;
			}
		}
	} else {
		for (int i = itr->stack[sp].pos, n = pg->n; ++i < n;) {
			struct rtree_page_branch *b;
			b = rtree_branch_get(itr->tree, pg, i);
			if (itr->intr_cmp(&itr->rect, &b->rect, d)
			    && rtree_iterator_goto_first(itr, sp + 1,
							 b->data.page))
			{
				itr->stack[sp].page = pg;
				itr->stack[sp].pos = i;
				return true;
			}
		}
	}
	return sp > 0 ? rtree_iterator_goto_next(itr, sp - 1) : false;
}

void
rtree_iterator_destroy(struct rtree_iterator *itr)
{
	struct rtree_neighbor_page *curr, *next;
	for (curr = itr->page_list; curr != NULL; curr = next) {
		next = curr->next;
		rtree_page_free((struct rtree *) itr->tree,
				(struct rtree_page *) curr);
	}
	itr->page_list = NULL;
	itr->page_pos = INT_MAX;
}

struct rtree_neighbor *
rtree_iterator_reset_cb(rtnt_t *t, struct rtree_neighbor *n, void *d)
{
	(void) t;
	struct rtree_iterator *itr = (struct rtree_iterator *)d;
	n->next = itr->neigh_free_list;
	itr->neigh_free_list = n;
	return 0;
}

static void
rtree_iterator_reset(struct rtree_iterator *itr)
{
	rtnt_iter(&itr->neigh_tree, 0, rtree_iterator_reset_cb, (void *)itr);
	rtnt_new(&itr->neigh_tree);
}

static struct rtree_neighbor *
rtree_iterator_allocate_neighbour(struct rtree_iterator *itr)
{
	if (itr->page_pos >= itr->tree->neighbours_in_page) {
		struct rtree_neighbor_page *new_page =
			(struct rtree_neighbor_page *)
			rtree_page_alloc((struct rtree*)itr->tree);
		new_page->next = itr->page_list;
		itr->page_list = new_page;
		itr->page_pos = 0;
	}
	return itr->page_list->buf + itr->page_pos++;
}

static struct rtree_neighbor *
rtree_iterator_new_neighbor(struct rtree_iterator *itr,
			    void *child, sq_coord_t distance, int level)
{
	struct rtree_neighbor *n = itr->neigh_free_list;
	if (n == NULL)
		n = rtree_iterator_allocate_neighbour(itr);
	else
		itr->neigh_free_list = n->next;
	n->child = child;
	n->distance = distance;
	n->level = level;
	return n;
}

static void
rtree_iterator_free_neighbor(struct rtree_iterator *itr,
			     struct rtree_neighbor *n)
{
	n->next = itr->neigh_free_list;
	itr->neigh_free_list = n;
}

void
rtree_iterator_init(struct rtree_iterator *itr)
{
	itr->tree = 0;
	rtnt_new(&itr->neigh_tree);
	itr->neigh_free_list = NULL;
	itr->page_list = NULL;
	itr->page_pos = INT_MAX;
}

static void
rtree_iterator_process_neigh(struct rtree_iterator *itr,
			     struct rtree_neighbor *neighbor)
{
	unsigned d = itr->tree->dimension;
	void *child = neighbor->child;
	struct rtree_page *pg = (struct rtree_page *)child;
	int level = neighbor->level;
	rtree_iterator_free_neighbor(itr, neighbor);
	for (int i = 0, n = pg->n; i < n; i++) {
		struct rtree_page_branch *b;
		b = rtree_branch_get(itr->tree, pg, i);
		coord_t distance;
		if (itr->tree->distance_type == RTREE_EUCLID)
			distance = rtree_rect_neigh_distance2(&b->rect,
							      &itr->rect, d);
		else
			distance = rtree_rect_neigh_distance(&b->rect,
							     &itr->rect, d);
		struct rtree_neighbor *neigh =
			rtree_iterator_new_neighbor(itr, b->data.page,
						    distance, level - 1);
		rtnt_insert(&itr->neigh_tree, neigh);
	}
}


record_t
rtree_iterator_next(struct rtree_iterator *itr)
{
	if (itr->version != itr->tree->version) {
		/* Index was updated since cursor initialziation */
		return NULL;
	}
	if (itr->op == SOP_NEIGHBOR) {
		/* To return element in order of increasing distance from
		 * specified point, we build sorted list of R-Tree items
		 * (ordered by distance from specified point) starting from
		 * root page.
		 * Algorithm is the following:
		 *
		 * insert root R-Tree page in the sorted list
		 * while sorted list is not empty:
		 *      get top element from the sorted list
		 *      if it is tree leaf (record) then return it as
		 *      current element
		 *      otherwise (R-Tree page)  get siblings of this R-Tree
		 *      page and insert them in sorted list
		*/
		while (true) {
			struct rtree_neighbor *neighbor =
				rtnt_first(&itr->neigh_tree);
			if (neighbor == NULL)
				return NULL;
			rtnt_remove(&itr->neigh_tree, neighbor);
			if (neighbor->level == 0) {
				void *child = neighbor->child;
				rtree_iterator_free_neighbor(itr, neighbor);
				return (record_t)child;
			} else {
				rtree_iterator_process_neigh(itr, neighbor);
			}
		}
	}
	int sp = itr->tree->height - 1;
	if (!itr->eof && rtree_iterator_goto_next(itr, sp)) {
		struct rtree_page_branch *b;
		b = rtree_branch_get(itr->tree,
				     itr->stack[sp].page, itr->stack[sp].pos);
		return b->data.record;
	}
	itr->eof = true;
	return NULL;
}

/*------------------------------------------------------------------------- */
/* R-tree methods */
/*------------------------------------------------------------------------- */

int
rtree_init(struct rtree *tree, unsigned dimension, uint32_t extent_size,
	   rtree_extent_alloc_t extent_alloc, rtree_extent_free_t extent_free,
	   void *alloc_ctx, enum rtree_distance_type distance_type)
{
	tree->n_records = 0;
	tree->height = 0;
	tree->root = NULL;
	tree->version = 0;
	tree->n_pages = 0;
	tree->free_pages = 0;

	tree->dimension = dimension;
	tree->distance_type = distance_type;
	tree->page_branch_size =
		(RTREE_BRANCH_DATA_SIZE + dimension * 2 * sizeof(coord_t));
	tree->page_size = RTREE_OPTIMAL_BRANCHES_IN_PAGE *
		tree->page_branch_size + sizeof(int);
	/* round up to closest power of 2 */
	int lz = __builtin_clz(tree->page_size - 1);
	tree->page_size = 1u << (sizeof(int) * CHAR_BIT - lz);
	assert(tree->page_size - sizeof(int) >=
	       tree->page_branch_size * RTREE_OPTIMAL_BRANCHES_IN_PAGE);
	tree->page_max_fill = (tree->page_size - sizeof(int)) /
		tree->page_branch_size;
	tree->page_min_fill = tree->page_max_fill * 2 / 5;
	tree->neighbours_in_page = (tree->page_size - sizeof(void *))
		/ sizeof(struct rtree_neighbor);

	matras_create(&tree->mtab, extent_size, tree->page_size,
		      extent_alloc, extent_free, alloc_ctx);
	return 0;
}

void
rtree_destroy(struct rtree *tree)
{
	rtree_purge(tree);
	matras_destroy(&tree->mtab);
}

void
rtree_insert(struct rtree *tree, struct rtree_rect *rect, record_t obj)
{
	if (tree->root == NULL) {
		tree->root = rtree_page_alloc(tree);
		rtree_page_init_with_record(tree, tree->root, rect, obj);
		tree->height = 1;
		tree->n_pages++;
	} else {
		struct rtree_page *p =
			rtree_page_insert(tree, tree->root, rect, obj, tree->height);
		if (p != NULL) {
			/* root splitted */
			struct rtree_page *new_root = rtree_page_alloc(tree);
			rtree_page_init_with_pages(tree, new_root,
						   tree->root, p);
			tree->root = new_root;
			tree->height++;
			tree->n_pages++;
		}
	}
	tree->version++;
	tree->n_records++;
}

bool
rtree_remove(struct rtree *tree, const struct rtree_rect *rect, record_t obj)
{
	struct rtree_reinsert_list rlist;
	rlist.chain = NULL;
	if (tree->height == 0)
		return false;
	if (!rtree_page_remove(tree, tree->root, rect, obj, tree->height, &rlist))
		return false;
	struct rtree_page *pg = rlist.chain;
	int level = rlist.level;
	while (pg != NULL) {
		for (int i = 0, n = pg->n; i < n; i++) {
			struct rtree_page_branch *b;
			b = rtree_branch_get(tree, pg, i);
			struct rtree_page *p =
				rtree_page_insert(tree, tree->root,
						  &b->rect, b->data.record,
						  tree->height - level);
			if (p != NULL) {
				/* root splitted */
				struct rtree_page *new_root
					= rtree_page_alloc(tree);
				rtree_page_init_with_pages(tree, new_root,
							   tree->root, p);
				tree->root = new_root;
				tree->height++;
				tree->n_pages++;
			}
		}
		level--;
		struct rtree_page *next = get_next_reinsert_page(tree, pg);
		rtree_page_free(tree, pg);
		tree->n_pages--;
		pg = next;
	}
	if (tree->root->n == 1 && tree->height > 1) {
		struct rtree_page_branch *b;
		b = rtree_branch_get(tree, tree->root, 0);
		struct rtree_page *new_root = b->data.page;
		rtree_page_free(tree, tree->root);
		tree->root = new_root;
		tree->height--;
		tree->n_pages--;
	}
	tree->n_records--;
	tree->version++;
	return true;
}

bool
rtree_search(const struct rtree *tree, const struct rtree_rect *rect,
	     enum spatial_search_op op, struct rtree_iterator *itr)
{
	rtree_iterator_reset(itr);
	assert(itr->tree == 0 || itr->tree == tree);
	itr->tree = tree;
	itr->version = tree->version;
	rtree_rect_copy(&itr->rect, rect, tree->dimension);
	itr->op = op;
	assert(tree->height <= RTREE_MAX_HEIGHT);
	switch (op) {
	case SOP_ALL:
		itr->intr_cmp = itr->leaf_cmp = rtree_always_true;
		break;
	case SOP_EQUALS:
		itr->intr_cmp = rtree_rect_in_rect;
		itr->leaf_cmp = rtree_rect_equal_to_rect;
		break;
	case SOP_CONTAINS:
		itr->intr_cmp = itr->leaf_cmp = rtree_rect_in_rect;
		break;
	case SOP_STRICT_CONTAINS:
		itr->intr_cmp = itr->leaf_cmp = rtree_rect_strict_in_rect;
		break;
	case SOP_OVERLAPS:
		itr->intr_cmp = itr->leaf_cmp = rtree_rect_intersects_rect;
		break;
	case SOP_BELONGS:
		itr->intr_cmp = rtree_rect_intersects_rect;
		itr->leaf_cmp = rtree_rect_holds_rect;
		break;
	case SOP_STRICT_BELONGS:
		itr->intr_cmp = rtree_rect_intersects_rect;
		itr->leaf_cmp = rtree_rect_strict_holds_rect;
		break;
	case SOP_NEIGHBOR:
		if (tree->root) {
			struct rtree_rect cover;
			rtree_page_cover(tree, tree->root, &cover);
			sq_coord_t distance;
			if (tree->distance_type == RTREE_EUCLID)
				distance =
				rtree_rect_neigh_distance2(&cover, rect,
							   tree->dimension);
			else
				distance =
				rtree_rect_neigh_distance(&cover, rect,
							  tree->dimension);
			struct rtree_neighbor *n =
				rtree_iterator_new_neighbor(itr, tree->root,
							    distance,
							    tree->height);
			rtnt_insert(&itr->neigh_tree, n);
			return true;
		} else {
			return false;
		}
	}
	if (tree->root && rtree_iterator_goto_first(itr, 0, tree->root)) {
		itr->stack[tree->height-1].pos -= 1;
		/* will be incremented by goto_next */
		itr->eof = false;
		return true;
	} else {
		itr->eof = true;
		return false;
	}
}

void
rtree_purge(struct rtree *tree)
{
	if (tree->root != NULL) {
		rtree_page_purge(tree, tree->root, tree->height);
		tree->root = NULL;
		tree->n_records = 0;
		tree->n_pages = 0;
		tree->height = 0;
	}
}

size_t
rtree_used_size(const struct rtree *tree)
{
	return tree->n_pages * tree->page_size;
}

unsigned
rtree_number_of_records(const struct rtree *tree) {
	return tree->n_records;
}

#if 0
#include <stdio.h>
void
rtree_debug_print_page(const struct rtree *tree, const struct rtree_page *page,
		       unsigned level, unsigned path)
{
	printf("%d:\n", path);
	unsigned d = tree->dimension;
	for (int i = 0; i < page->n; i++) {
		struct rtree_page_branch *b;
		b = rtree_branch_get(tree, page, i);
		double v = 1;
		for (unsigned j = 0; j < d; j++) {
			double d1 = b->rect.coords[j * 2];
			double d2 = b->rect.coords[j * 2 + 1];
			v *= (d2 - d1) / 100;
			printf("[%04.1lf-%04.1lf:%04.1lf]", d2, d1, d2 - d1);
		}
		printf("%d\n", (int)(v * 100));
	}
	if (--level > 1) {
		for (int i = 0; i < page->n; i++) {
			struct rtree_page_branch *b;
			b = rtree_branch_get(tree, page, i);
			rtree_debug_print_page(tree, b->data.page, level,
					       path * 100 + i + 1);
		}
	}
}

void
rtree_debug_print(const struct rtree *tree)
{
	if (tree->root)
		rtree_debug_print_page(tree, tree->root, tree->height, 1);
}
#endif

