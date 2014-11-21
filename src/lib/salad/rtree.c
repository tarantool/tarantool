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
#include "rtree.h"
#include <string.h>
#include <assert.h>

/*------------------------------------------------------------------------- */
/* R-tree internal structures definition */
/*------------------------------------------------------------------------- */

struct rtree_page_branch {
	struct rtree_rect rect;
	union {
		struct rtree_page *page;
		record_t record;
	} data;
};

enum {
	/* maximal number of branches at page */
	RTREE_MAX_FILL = (RTREE_PAGE_SIZE - sizeof(int)) /
		sizeof(struct rtree_page_branch),
	/* minimal number of branches at non-root page */
	RTREE_MIN_FILL = RTREE_MAX_FILL / 2
};

struct rtree_page {
	/* number of branches at page */
	int n;
	/* branches */
	struct rtree_page_branch b[RTREE_MAX_FILL];
};

struct rtree_neighbor {
	void *child;
	struct rtree_neighbor *next;
	int level;
	sq_coord_t distance;
};

enum {
	RTREE_NEIGHBORS_IN_PAGE = (RTREE_PAGE_SIZE - sizeof(void*)) /
		sizeof(struct rtree_neighbor)
};

struct rtree_neighbor_page {
	struct rtree_neighbor_page* next;
	struct rtree_neighbor buf[RTREE_NEIGHBORS_IN_PAGE];
};

struct rtree_reinsert_list {
	struct rtree_page *chain;
	int level;
};

/*------------------------------------------------------------------------- */
/* R-tree rectangle methods */
/*------------------------------------------------------------------------- */

void
rtree_rect_normalize(struct rtree_rect *rect)
{
	for (int i = RTREE_DIMENSION; --i >= 0; ) {
		if (rect->lower_point.coords[i] <= rect->upper_point.coords[i])
			continue;
		coord_t tmp = rect->lower_point.coords[i];
		rect->lower_point.coords[i] = rect->upper_point.coords[i];
		rect->upper_point.coords[i] = tmp;
	}
}

void
rtree_set2d(struct rtree_rect *rect,
	    coord_t left, coord_t bottom, coord_t right, coord_t top)
{
	assert(RTREE_DIMENSION == 2);
	rect->lower_point.coords[0] = left;
	rect->lower_point.coords[1] = bottom;
	rect->upper_point.coords[0] = right;
	rect->upper_point.coords[1] = top;
}

static sq_coord_t
rtree_rect_point_distance2(const struct rtree_rect *rect,
			   const struct rtree_point *point)
{
	sq_coord_t result = 0;
	for (int i = RTREE_DIMENSION; --i >= 0; ) {
		if (point->coords[i] < rect->lower_point.coords[i]) {
			sq_coord_t diff = (sq_coord_t)(point->coords[i] -
				rect->lower_point.coords[i]);
			result += diff * diff;
		} else if (point->coords[i] > rect->upper_point.coords[i]) {
			sq_coord_t diff = (sq_coord_t)(point->coords[i] -
				rect->upper_point.coords[i]);
			result += diff * diff;
		}
	}
	return result;
}

static area_t
rtree_rect_area(const struct rtree_rect *rect)
{
	area_t area = 1;
	for (int i = RTREE_DIMENSION; --i >= 0; ) {
		area *= rect->upper_point.coords[i] -
			rect->lower_point.coords[i];
	}
	return area;
}

static void
rtree_rect_add(struct rtree_rect *to, const struct rtree_rect *item)
{
	for (int i = RTREE_DIMENSION; --i >= 0; ) {
		if (to->lower_point.coords[i] > item->lower_point.coords[i])
			to->lower_point.coords[i] = item->lower_point.coords[i];
		if (to->upper_point.coords[i] < item->upper_point.coords[i])
			to->upper_point.coords[i] = item->upper_point.coords[i];
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

static struct rtree_rect
rtree_rect_cover(const struct rtree_rect *item1,
		 const struct rtree_rect *item2)
{
	struct rtree_rect res;
	for (int i = RTREE_DIMENSION; --i >= 0; ) {
		res.lower_point.coords[i] =
			rtree_min(item1->lower_point.coords[i],
				  item2->lower_point.coords[i]);
		res.upper_point.coords[i] =
			rtree_max(item1->upper_point.coords[i],
				  item2->upper_point.coords[i]);
	}
	return res;
}

static bool
rtree_rect_intersects_rect(const struct rtree_rect *rt1,
			   const struct rtree_rect *rt2)
{
	for (int i = RTREE_DIMENSION; --i >= 0; )
		if (rt1->lower_point.coords[i] > rt2->upper_point.coords[i] ||
		    rt1->upper_point.coords[i] < rt2->lower_point.coords[i])
			return false;
	return true;
}

static bool
rtree_rect_in_rect(const struct rtree_rect *rt1,
		   const struct rtree_rect *rt2)
{
	for (int i = RTREE_DIMENSION; --i >= 0; )
		if (rt1->lower_point.coords[i] < rt2->lower_point.coords[i] ||
		    rt1->upper_point.coords[i] > rt2->upper_point.coords[i])
			return false;
	return true;
}

static bool
rtree_rect_strict_in_rect(const struct rtree_rect *rt1,
			  const struct rtree_rect *rt2)
{
	for (int i = RTREE_DIMENSION; --i >= 0; )
		if (rt1->lower_point.coords[i] <= rt2->lower_point.coords[i] ||
		    rt1->upper_point.coords[i] >= rt2->upper_point.coords[i])
			return false;
	return true;
}

static bool
rtree_rect_holds_rect(const struct rtree_rect *rt1,
		      const struct rtree_rect *rt2)
{
	return rtree_rect_in_rect(rt2, rt1);
}

static bool
rtree_rect_strict_holds_rect(const struct rtree_rect *rt1,
			     const struct rtree_rect *rt2)
{
	return rtree_rect_strict_in_rect(rt2, rt1);
}

static bool
rtree_rect_equal_to_rect(const struct rtree_rect *rt1,
			 const struct rtree_rect *rt2)
{
	for (int i = RTREE_DIMENSION; --i >= 0; )
		if (rt1->lower_point.coords[i] != rt2->lower_point.coords[i] ||
		    rt1->upper_point.coords[i] != rt2->upper_point.coords[i])
			return false;
	return true;
}

static bool
rtree_always_true(const struct rtree_rect *rt1,
		  const struct rtree_rect *rt2)
{
	(void)rt1;
	(void)rt2;
	return true;
}

/*------------------------------------------------------------------------- */
/* R-tree page methods */
/*------------------------------------------------------------------------- */

static struct rtree_page *
rtree_alloc_page(struct rtree *tree)
{
	return (struct rtree_page *)tree->page_alloc();
}

static void
rtree_free_page(struct rtree *tree, struct rtree_page *page)
{
	tree->page_free(page);
}

static void
set_next_reinsert_page(struct rtree_page *page, struct rtree_page *next_page)
{
	/* The page must be MIN_FILLed, so last branch is unused */
	page->b[RTREE_MAX_FILL - 1].data.page = next_page;
}

struct rtree_page *
get_next_reinsert_page(const struct rtree_page *page)
{
	return page->b[RTREE_MAX_FILL - 1].data.page;
}

/* Calculate cover of all rectangles at page */
static struct rtree_rect
rtree_page_cover(const struct rtree_page *page)
{
	struct rtree_rect res = page->b[0].rect;
	for (int i = 1; i < page->n; i++)
		rtree_rect_add(&res, &page->b[i].rect);
	return res;
}

/* Create root page by first inserting record */
static void
rtree_page_init_with_record(struct rtree_page *page,
			    struct rtree_rect *rect, record_t obj)
{
	page->n = 1;
	page->b[0].rect = *rect;
	page->b[0].data.record = obj;
}

/* Create root page by branch */
static void
rtree_page_init_with_branch(struct rtree_page *page,
			    const struct rtree_page_branch *br)
{
	page->n = 1;
	page->b[0] = *br;
}

/* Create new root page (root splitting) */
static void
rtree_page_init_with_pages(struct rtree_page *page,
			   struct rtree_page *page1,
			   struct rtree_page *page2)
{
	page->n = 2;
	page->b[0].rect = rtree_page_cover(page1);
	page->b[0].data.page = page1;
	page->b[1].rect = rtree_page_cover(page2);
	page->b[1].data.page = page2;
}

static struct rtree_page *
rtree_split_page(struct rtree *tree, struct rtree_page *page,
		 const struct rtree_page_branch *br)
{
	area_t rect_area[RTREE_MAX_FILL + 1];
	rect_area[0] = rtree_rect_area(&br->rect);
	for (int i = 0; i < RTREE_MAX_FILL; i++)
		rect_area[i + 1] = rtree_rect_area(&page->b[i].rect);

	/*
	 * As the seeds for the two groups, find two rectangles
	 * which waste the most area if covered by a single
	 * rectangle.
	 */
	int seed[2] = {-1, -1};
	coord_t worst_waste = 0;
	bool worst_waste_set = false;

	const struct rtree_page_branch *bp = br;
	for (int i = 0; i < RTREE_MAX_FILL; i++) {
		for (int j = i + 1; j <= RTREE_MAX_FILL; j++) {
			struct rtree_rect cover =
				rtree_rect_cover(&bp->rect,
						 &page->b[j - 1].rect);
			coord_t waste = rtree_rect_area(&cover) -
				rect_area[i] - rect_area[j];
			if (!worst_waste_set) {
				worst_waste_set = true;
				worst_waste = waste;
				seed[0] = i;
				seed[1] = j;
			}else if (waste > worst_waste) {
				worst_waste = waste;
				seed[0] = i;
				seed[1] = j;
			}
		}
		bp = page->b + i;
	}
	assert(seed[0] >= 0);

	char taken[RTREE_MAX_FILL];
	memset(taken, 0, sizeof(taken));
	struct rtree_rect group_rect[2];
	struct rtree_page *p = rtree_alloc_page(tree);
	tree->n_pages++;

	taken[seed[1] - 1] = 2;
	group_rect[1] = page->b[seed[1] - 1].rect;

	if (seed[0] == 0) {
		group_rect[0] = br->rect;
		rtree_page_init_with_branch(p, br);
	} else {
		group_rect[0] = page->b[seed[0] - 1].rect;
		rtree_page_init_with_branch(p, &page->b[seed[0] - 1]);
		page->b[seed[0] - 1] = *br;
	}
	area_t group_area[2] = {rect_area[seed[0]], rect_area[seed[1]]};
	int group_card[2] = {1, 1};

	/*
	 * Split remaining rectangles between two groups.
	 * The one chosen is the one with the greatest difference in area
	 * expansion depending on which group - the rect most strongly
	 * attracted to one group and repelled from the other.
	 */
	while (group_card[0] + group_card[1] < RTREE_MAX_FILL + 1
	       && group_card[0] < RTREE_MAX_FILL + 1 - RTREE_MIN_FILL
	       && group_card[1] < RTREE_MAX_FILL + 1 - RTREE_MIN_FILL)
	{
		int better_group = -1, chosen = -1;
		area_t biggest_diff = -1;
		for (int i = 0; i < RTREE_MAX_FILL; i++) {
			if (taken[i])
				continue;
			struct rtree_rect cover0 =
				rtree_rect_cover(&group_rect[0],
						 &page->b[i].rect);
			struct rtree_rect cover1 =
				rtree_rect_cover(&group_rect[1],
						 &page->b[i].rect);
			area_t diff = rtree_rect_area(&cover0) - group_area[0]
				- (rtree_rect_area(&cover1) - group_area[1]);
			if (diff > biggest_diff || -diff > biggest_diff) {
				chosen = i;
				if (diff < 0) {
					better_group = 0;
					biggest_diff = -diff;
				} else {
					better_group = 1;
					biggest_diff = diff;
				}
			}
		}
		assert(chosen >= 0);
		group_card[better_group]++;
		rtree_rect_add(&group_rect[better_group],
			       &page->b[chosen].rect);
		group_area[better_group] =
			rtree_rect_area(&group_rect[better_group]);
		taken[chosen] = better_group + 1;
		if (better_group == 0)
			p->b[group_card[0] - 1] = page->b[chosen];
	}
	/*
	 * If one group gets too full, then remaining rectangle
	 * are split between two groups in such way to balance
	 * CARDs of two groups.
	 */
	if (group_card[0] + group_card[1] < RTREE_MAX_FILL + 1) {
		for (int i = 0; i < RTREE_MAX_FILL; i++) {
			if (taken[i])
				continue;
			if (group_card[0] >= group_card[1]) {
				taken[i] = 2;
				group_card[1] += 1;
			} else {
				taken[i] = 1;
				p->b[group_card[0]++] = page->b[i];
			}
		}
	}
	p->n = group_card[0];
	page->n = group_card[1];
	for (int i = 0, j = 0; i < page->n; j++) {
		if (taken[j] == 2)
			page->b[i++] = page->b[j];
	}
	return p;
}

static struct rtree_page*
rtree_page_add_branch(struct rtree *tree, struct rtree_page *page,
		      const struct rtree_page_branch *br)
{
	if (page->n < RTREE_MAX_FILL) {
		page->b[page->n++] = *br;
		return NULL;
	} else {
		return rtree_split_page(tree, page, br);
	}
}

static void
rtree_page_remove_branch(struct rtree_page *page, int i)
{
	page->n -= 1;
	memmove(page->b + i, page->b + i + 1,
		(page->n - i) * sizeof(struct rtree_page_branch));
}

static struct rtree_page *
rtree_page_insert(struct rtree *tree, struct rtree_page *page,
		  const struct rtree_rect *rect, record_t obj, int level)
{
	struct rtree_page_branch br;
	if (--level != 0) {
		/* not a leaf page */
		int mini = -1;
		area_t min_incr, best_area;
		for (int i = 0; i < page->n; i++) {
			area_t r_area = rtree_rect_area(&page->b[i].rect);
			struct rtree_rect cover =
				rtree_rect_cover(&page->b[i].rect, rect);
			area_t incr = rtree_rect_area(&cover) - r_area;
			assert(incr >= 0);
			if (i == 0) {
				best_area = r_area;
				min_incr = incr;
				mini = i;
			} else if (incr < min_incr) {
				best_area = r_area;
				min_incr = incr;
				mini = i;
			} else if (incr == min_incr && r_area < best_area) {
				best_area = r_area;
				mini = i;
			}
		}
		assert(mini >= 0);
		struct rtree_page *p = page->b[mini].data.page;
		struct rtree_page *q = rtree_page_insert(tree, p,
							 rect, obj, level);
		if (q == NULL) {
			/* child was not split */
			rtree_rect_add(&page->b[mini].rect, rect);
			return NULL;
		} else {
			/* child was split */
			page->b[mini].rect = rtree_page_cover(p);
			br.data.page = q;
			br.rect = rtree_page_cover(q);
			return rtree_page_add_branch(tree, page, &br);
		}
	} else {
		br.data.record = obj;
		br.rect = *rect;
		return rtree_page_add_branch(tree, page, &br);
	}
}

static bool
rtree_page_remove(struct rtree *tree, struct rtree_page *page,
		  const struct rtree_rect *rect, record_t obj,
		  int level, struct rtree_reinsert_list *rlist)
{
	if (--level != 0) {
		for (int i = 0; i < page->n; i++) {
			if (!rtree_rect_intersects_rect(&page->b[i].rect, rect))
				continue;
			struct rtree_page *next_page = page->b[i].data.page;
			if (!rtree_page_remove(tree, next_page, rect,
					       obj, level, rlist))
				continue;
			if (next_page->n >= RTREE_MIN_FILL) {
				page->b[i].rect =
					rtree_page_cover(next_page);
			} else {
				/* not enough entries in child */
				set_next_reinsert_page(next_page, rlist->chain);
				rlist->chain = next_page;
				rlist->level = level - 1;
				rtree_page_remove_branch(page, i);
			}
			return true;
		}
	} else {
		for (int i = 0; i < page->n; i++) {
			if (page->b[i].data.page == obj) {
				rtree_page_remove_branch(page, i);
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
		for (int i = 0; i < page->n; i++)
			rtree_page_purge(tree, page->b[i].data.page, level);
	}
	tree->page_free(page);
}

/*------------------------------------------------------------------------- */
/* R-tree iterator methods */
/*------------------------------------------------------------------------- */

static bool
rtree_iterator_goto_first(struct rtree_iterator *itr, int sp, struct rtree_page* pg)
{
	if (sp + 1 == itr->tree->height) {
		for (int i = 0, n = pg->n; i < n; i++) {
			if (itr->leaf_cmp(&itr->rect, &pg->b[i].rect)) {
				itr->stack[sp].page = pg;
				itr->stack[sp].pos = i;
				return true;
			}
		}
	} else {
		for (int i = 0, n = pg->n; i < n; i++) {
			if (itr->intr_cmp(&itr->rect, &pg->b[i].rect)
			    && rtree_iterator_goto_first(itr, sp + 1,
							 pg->b[i].data.page))
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
rtree_iterator_goto_next(struct rtree_iterator *itr, int sp)
{
	struct rtree_page *pg = itr->stack[sp].page;
	if (sp + 1 == itr->tree->height) {
		for (int i = itr->stack[sp].pos, n = pg->n; ++i < n;) {
			if (itr->leaf_cmp(&itr->rect, &pg->b[i].rect)) {
				itr->stack[sp].pos = i;
				return true;
			}
		}
	} else {
		for (int i = itr->stack[sp].pos, n = pg->n; ++i < n;) {
			if (itr->intr_cmp(&itr->rect, &pg->b[i].rect)
			    && rtree_iterator_goto_first(itr, sp + 1,
							 pg->b[i].data.page))
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
		itr->tree->page_free(curr);
	}
	itr->page_list = NULL;
	itr->page_pos = RTREE_NEIGHBORS_IN_PAGE;
}

static void
rtree_iterator_reset(struct rtree_iterator *itr)
{
	if (itr->neigh_list != NULL) {
		struct rtree_neighbor **npp = &itr->neigh_free_list;
		while (*npp != NULL) {
			npp = &(*npp)->next;
		}
		*npp = itr->neigh_list;
		itr->neigh_list = NULL;
	}
}

static struct rtree_neighbor *
rtree_iterator_allocate_neighbour(struct rtree_iterator *itr)
{
	if (itr->page_pos >= RTREE_NEIGHBORS_IN_PAGE) {
		struct rtree_neighbor_page *new_page =
			(struct rtree_neighbor_page *)itr->tree->page_alloc();
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
	n->next = NULL;
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
	itr->neigh_list = NULL;
	itr->neigh_free_list = NULL;
	itr->page_list = NULL;
	itr->page_pos = RTREE_NEIGHBORS_IN_PAGE;
}

static void
rtree_iterator_insert_neighbor(struct rtree_iterator *itr,
			       struct rtree_neighbor *node)
{
	struct rtree_neighbor *prev = NULL, *next = itr->neigh_list;
	sq_coord_t distance = node->distance;
	while (next != NULL && next->distance < distance) {
		prev = next;
		next = prev->next;
	}
	node->next = next;
	if (prev == NULL)
		itr->neigh_list = node;
	else
		prev->next = node;
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
			struct rtree_neighbor *neighbor = itr->neigh_list;
			if (neighbor == NULL)
				return NULL;
			void *child = neighbor->child;
			int level = neighbor->level;
			itr->neigh_list = neighbor->next;
			rtree_iterator_free_neighbor(itr, neighbor);
			if (level == 0)
				return (record_t)child;
			struct rtree_page *pg = (struct rtree_page *)child;
			for (int i = 0, n = pg->n; i < n; i++) {
				struct rtree_page *pg =
					(struct rtree_page *)child;
				coord_t distance =
					rtree_rect_point_distance2(&pg->b[i].rect,
								   &itr->rect.lower_point);
				struct rtree_neighbor *neigh =
					rtree_iterator_new_neighbor(itr, pg->b[i].data.page,
								    distance, level - 1);
				rtree_iterator_insert_neighbor(itr, neigh);
			}
		}
	}
	int sp = itr->tree->height - 1;
	if (!itr->eof && rtree_iterator_goto_next(itr, sp))
		return itr->stack[sp].page->b[itr->stack[sp].pos].data.record;
	itr->eof = true;
	return NULL;
}

/*------------------------------------------------------------------------- */
/* R-tree methods */
/*------------------------------------------------------------------------- */

void
rtree_init(struct rtree *tree,
	   rtree_page_alloc_t page_alloc, rtree_page_free_t page_free)
{
	tree->n_records = 0;
	tree->height = 0;
	tree->root = NULL;
	tree->version = 0;
	tree->n_pages = 0;
	tree->page_alloc = page_alloc;
	tree->page_free = page_free;
}

void
rtree_destroy(struct rtree *tree)
{
	rtree_purge(tree);
}

void
rtree_insert(struct rtree *tree, struct rtree_rect *rect, record_t obj)
{
	if (tree->root == NULL) {
		tree->root = rtree_alloc_page(tree);
		rtree_page_init_with_record(tree->root, rect, obj);
		tree->height = 1;
		tree->n_pages++;
	} else {
		struct rtree_page *p =
			rtree_page_insert(tree, tree->root, rect, obj, tree->height);
		if (p != NULL) {
			/* root splitted */
			struct rtree_page *new_root = rtree_alloc_page(tree);
			rtree_page_init_with_pages(new_root, tree->root, p);
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
			struct rtree_page *p =
				rtree_page_insert(tree, tree->root,
						  &pg->b[i].rect,
						  pg->b[i].data.record,
						  tree->height - level);
			if (p != NULL) {
				/* root splitted */
				struct rtree_page *new_root
					= rtree_alloc_page(tree);
				rtree_page_init_with_pages(new_root,
							   tree->root, p);
				tree->root = new_root;
				tree->height++;
				tree->n_pages++;
			}
		}
		level--;
		struct rtree_page *next = get_next_reinsert_page(pg);
		rtree_free_page(tree, pg);
		tree->n_pages--;
		pg = next;
	}
	if (tree->root->n == 1 && tree->height > 1) {
		struct rtree_page *new_root = tree->root->b[0].data.page;
		rtree_free_page(tree, tree->root);
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
	itr->tree = tree;
	itr->version = tree->version;
	itr->rect = *rect;
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
			struct rtree_rect cover = rtree_page_cover(tree->root);
			sq_coord_t distance =
				rtree_rect_point_distance2(&cover,
							   &rect->lower_point);
			itr->neigh_list =
				rtree_iterator_new_neighbor(itr, tree->root,
							    distance,
							    tree->height);
			return true;
		} else {
			itr->neigh_list = NULL;
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
	return tree->n_pages * RTREE_PAGE_SIZE;
}

unsigned
rtree_number_of_records(const struct rtree *tree) {
	return tree->n_records;
}

#if 0
void
rtree_debug_print_page(const struct rtree_page *page, unsigned level, unsigned path)
{
	printf("%d:", path);
	if (--level) {
		for (int i = 0; i < page->n; i++) {
			printf(" [");
			for (int j = 0; j < RTREE_DIMENSION; j++)
				printf("%lg ", (double)page->b[i].rect.lower_point.coords[j]);
			for (int j = 0; j < RTREE_DIMENSION; j++)
				printf("%lg ", (double)page->b[i].rect.upper_point.coords[j]);
			printf("]");
		}
		printf("\n");
		for (int i = 0; i < page->n; i++)
			rtree_debug_print_page(page->b[i].data.page, level, path * 100 + i);
	} else {
		for (int i = 0; i < page->n; i++) {
			printf(" [");
			for (int j = 0; j < RTREE_DIMENSION; j++)
				printf("%lg ", (double)page->b[i].rect.lower_point.coords[j]);
			for (int j = 0; j < RTREE_DIMENSION; j++)
				printf("%lg ", (double)page->b[i].rect.upper_point.coords[j]);
			printf(": %p]", (void *)page->b[i].data.record);
		}
		printf("\n");
	}
}

void
rtree_debug_print(const struct rtree *tree)
{
	if (tree->root)
		rtree_debug_print_page(tree->root, tree->height, 0);
}
#endif

