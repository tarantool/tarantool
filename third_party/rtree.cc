#include <string.h>
#include <assert.h>
#include "rtree.h"

inline void* operator new(size_t, void* at)
{
	return at;
}

class R_page {
public:
	struct branch {
		rectangle_t r;
		R_page*     p;
	};

	enum {
		card = (RTREE_PAGE_SIZE-4)/sizeof(branch), // maximal number of branches at page
		min_fill = card/2        // minimal number of branches at non-root page
	};

	struct reinsert_list {
		R_page* chain;
		int     level;
		reinsert_list() { chain = NULL; }
	};

	R_page* insert(R_tree* tree, rectangle_t const& r, record_t obj, int level);

	bool remove(R_tree* tree, rectangle_t const& r, record_t obj, int level, reinsert_list& rlist);

	rectangle_t cover() const;

	R_page* split_page(R_tree* tree, branch const& br);

	R_page* add_branch(R_tree* tree, branch const& br) {
		if (n < card) {
			b[n++] = br;
			return NULL;
		} else {
			return split_page(tree, br);
		}
	}
	void remove_branch(int i);

	void purge(R_tree* tree, int level);

	R_page* next_reinsert_page() const { return (R_page*)b[card-1].p; }

	R_page(rectangle_t const& rect, record_t obj);
	R_page(R_page* old_root, R_page* new_page);

	int    n; // number of branches at page
	branch b[card];
};

R_tree::R_tree(FixedSizeAllocator::Factory* factory)
{
	n_records = 0;
	height = 0;
	root = NULL;
	update_count = 0;
	page_allocator = factory->create(sizeof(R_page));
	neighbor_allocator = factory->create(sizeof(R_tree_iterator::Neighbor));
	allocator_factory = factory;
}

R_tree::~R_tree()
{
	purge();
	allocator_factory->destroy(page_allocator);
	allocator_factory->destroy(neighbor_allocator);
}

void R_tree::insert(rectangle_t const& r, record_t obj)
{
	if (root == NULL) {
		root = new (page_allocator->alloc()) R_page(r, obj);
		height = 1;
	} else {
		R_page* p = root->insert(this, r, obj, height);
		if (p != NULL) {
			// root splitted
			root = new (page_allocator->alloc()) R_page(root, p);
			height += 1;
		}
	}
	update_count += 1;
	n_records += 1;
}


bool R_tree::remove(rectangle_t const& r, record_t obj)
{
	if (height != 0) {
		R_page::reinsert_list rlist;
		if (root->remove(this, r, obj, height, rlist)) {
			R_page* pg = rlist.chain;
			int level = rlist.level;
			while (pg != NULL) {
				for (int i = 0, n = pg->n; i < n; i++) {
					R_page* p = root->insert(this, pg->b[i].r,
								 pg->b[i].p, height-level);
					if (p != NULL) {
						// root splitted
						root = new (page_allocator->alloc()) R_page(root, p);
						height += 1;
					}
				}
				level -= 1;
				R_page* next = pg->next_reinsert_page();
				page_allocator->free(pg);
				pg = next;
			}
			if (root->n == 1 && height > 1) {
				R_page* new_root = root->b[0].p;
				page_allocator->free(root);
				root = new_root;
				height -= 1;
			}
			n_records -= 1;
			update_count += 1;
			return true;
		}
	}
	return false;
}

bool R_tree_iterator::goto_first(int sp, R_page* pg)
{
	if (sp+1 == tree->height) {
		for (int i = 0, n = pg->n; i < n; i++) {
			if ((r.*leaf_cmp)(pg->b[i].r)) {
				stack[sp].page = pg;
				stack[sp].pos = i;
				return true;
			}
		}
	} else {
		for (int i = 0, n = pg->n; i < n; i++) {
			if ((r.*intr_cmp)(pg->b[i].r) && goto_first(sp+1, pg->b[i].p)) {
				stack[sp].page = pg;
				stack[sp].pos = i;
				return true;
			}
		}
	}
	return false;
}


bool R_tree_iterator::goto_next(int sp)
{
	R_page* pg = stack[sp].page;
	if (sp+1 == tree->height) {
		for (int i = stack[sp].pos, n = pg->n; ++i < n;) {
			if ((r.*leaf_cmp)(pg->b[i].r)) {
				stack[sp].pos = i;
				return true;
			}
		}
	} else {
		for (int i = stack[sp].pos, n = pg->n; ++i < n;) {
			if ((r.*intr_cmp)(pg->b[i].r) && goto_first(sp+1, pg->b[i].p)) {
				stack[sp].page = pg;
				stack[sp].pos = i;
				return true;
			}
		}
	}
	return sp > 0 ? goto_next(sp-1) : false;
}

R_tree_iterator::R_tree_iterator()
{
	list = NULL;
	free = NULL;
	tree = NULL;
}

R_tree_iterator::~R_tree_iterator()
{
	Neighbor *curr, *next;
	reset();
	for (curr = free; curr != NULL; curr = next) {
		next = curr->next;
		tree->neighbor_allocator->free(curr);
	}
}

void R_tree_iterator::reset()
{
	if (list != NULL) {
		Neighbor** npp = &free;
		while (*npp != NULL) {
			npp = &(*npp)->next;
		}
		*npp = list;
		list = NULL;
	}
}


bool R_tree_iterator::init(R_tree const* tree, rectangle_t const& r, Spatial_search_op op)
{
	reset();
	this->tree = tree;
	this->update_count = tree->update_count;
	this->r = r;
	this->op = op;
	assert(tree->height <= MAX_HEIGHT);
	switch (op) {
	case SOP_ALL:
		intr_cmp = leaf_cmp = &rectangle_t::operator_true;
		break;
	case SOP_EQUALS:
		intr_cmp = &rectangle_t::operator <=;
		leaf_cmp = &rectangle_t::operator ==;
		break;
	case SOP_CONTAINS:
		intr_cmp = leaf_cmp = &rectangle_t::operator <=;
		break;
	case SOP_STRICT_CONTAINS:
		intr_cmp = leaf_cmp = &rectangle_t::operator <;
		break;
	case SOP_OVERLAPS:
		intr_cmp = leaf_cmp = &rectangle_t::operator &;
		break;
	case SOP_BELONGS:
		intr_cmp = &rectangle_t::operator &;
		leaf_cmp = &rectangle_t::operator >=;
		break;
	case SOP_STRICT_BELONGS:
		intr_cmp = &rectangle_t::operator &;
		leaf_cmp = &rectangle_t::operator >;
		break;
	case SOP_NEIGHBOR:
		if (tree->root) {
			list = new_neighbor(tree->root, tree->root->cover().distance2(r.boundary), tree->height);
			return true;
		} else {
			list = NULL;
			return false;
		}
	}
	if (tree->root && goto_first(0, tree->root)) {
		stack[tree->height-1].pos -= 1; // will be incremented by goto_next
		eof = false;
		return true;
	} else {
		eof = true;
		return false;
	}
}

void R_tree_iterator::insert(Neighbor* node)
{
	Neighbor *prev = NULL, *next = list;
	area_t distance = node->distance;
	while (next != NULL && next->distance < distance) {
		prev = next;
		next = prev->next;
	}
	node->next = next;
	if (prev == NULL) {
		list = node;
	} else {
		prev->next = node;
	}
}

R_tree_iterator::Neighbor* R_tree_iterator::new_neighbor(void* child, area_t distance, int level)
{
	Neighbor* n = free;
	if (n == NULL) {
		n = new (tree->neighbor_allocator->alloc()) Neighbor();
	} else {
		free = n->next;
	}
	n->child = child;
	n->distance = distance;
	n->level = level;
	n->next = NULL;
	return n;
}

void R_tree_iterator::free_neighbor(Neighbor* n)
{
	n->next = free;
	free = n;
}

record_t R_tree_iterator::next()
{
	if (update_count != tree->update_count) {
		// Index was updated since cursor initialziation
		return NULL;
	}
	if (op == SOP_NEIGHBOR) {
		// To return element in order of increasing distance from specified point,
		// we build sorted list of R-Tree items
		// (ordered by distance from specified point) starting from root page.
		// Algorithm is the following:
		//
		// insert root R-Tree page in the sorted list
		// while sorted list is not empty:
		//      get top element from the sorted list
		//      if it is tree leaf (record) then return it as current element
		//      otherwise (R-Tree page) get siblings of this R-Tree page and insert them in sorted list
		while (true) {
			Neighbor* neighbor = list;
			if (neighbor == NULL) {
				return NULL;
			}
			R_page* pg = (R_page*)neighbor->child;
			int level = neighbor->level;
			list = neighbor->next;
			free_neighbor(neighbor);
			if (level == 0) {
				return (record_t*)pg;
			}
			for (int i = 0, n = pg->n; i < n; i++) {
				insert(new_neighbor(pg->b[i].p, pg->b[i].r.distance2(r.boundary), level-1));
			}
		}
	}
	int sp = tree->height-1;
	if (!eof && goto_next(sp)) {
		return stack[sp].page->b[stack[sp].pos].p;
	}
	eof = true;
	return NULL;
}

bool R_tree::search(rectangle_t const& r, Spatial_search_op op, R_tree_iterator& iterator) const
{
	return iterator.init(this, r, op);
}

void R_tree::purge()
{
	if (root != NULL) {
		root->purge(this, height);
		root = NULL;
		n_records = 0;
		height = 0;
	}
}

//-------------------------------------------------------------------------
// R-tree page methods
//-------------------------------------------------------------------------

//
// Create root page
//
R_page::R_page(rectangle_t const& r, record_t obj)
{
	n = 1;
	b[0].r = r;
	b[0].p = (R_page*)obj;
}

//
// Create new root page (root splitting)
//
R_page::R_page(R_page* old_root, R_page* new_page)
{
	n = 2;
	b[0].r = old_root->cover();
	b[0].p = old_root;
	b[1].r = new_page->cover();
	b[1].p = new_page;
}

//
// Calculate cover of all rectangles at page
//
rectangle_t R_page::cover() const
{
	rectangle_t r = b[0].r;
	for (int i = 1; i < n; i++) {
		r += b[i].r;
	}
	return r;
}

R_page* R_page::split_page(R_tree* tree, branch const& br)
{
	int i, j, seed[2] = {0,0};
	area_t rect_area[card+1], waste, worst_waste = AREA_MIN;
	//
	// As the seeds for the two groups, find two rectangles which waste
	// the most area if covered by a single rectangle.
	//
	rect_area[0] = area(br.r);
	for (i = 0; i < card; i++) {
		rect_area[i+1] = area(b[i].r);
	}
	branch const* bp = &br;
	for (i = 0; i < card; i++) {
		for (j = i+1; j <= card; j++) {
			waste = area(bp->r + b[j-1].r) - rect_area[i] - rect_area[j];
			if (waste > worst_waste) {
				worst_waste = waste;
				seed[0] = i;
				seed[1] = j;
			}
		}
		bp = &b[i];
	}
	char taken[card];
	rectangle_t group[2];
	area_t group_area[2];
	int group_card[2];
	R_page* p;

	memset(taken, 0, sizeof taken);
	taken[seed[1]-1] = 2;
	group[1] = b[seed[1]-1].r;

	if (seed[0] == 0) {
		group[0] = br.r;
		p = new (tree->page_allocator->alloc()) R_page(br.r, br.p);
	} else {
		group[0] = b[seed[0]-1].r;
		p = new (tree->page_allocator->alloc()) R_page(group[0], b[seed[0]-1].p);
		b[seed[0]-1] = br;
	}
	group_card[0] = group_card[1] = 1;
	group_area[0] = rect_area[seed[0]];
	group_area[1] = rect_area[seed[1]];
	//
	// Split remaining rectangles between two groups.
	// The one chosen is the one with the greatest difference in area
	// expansion depending on which group - the rect most strongly
	// attracted to one group and repelled from the other.
	//
	while (group_card[0] + group_card[1] < card + 1
	       && group_card[0] < card + 1 - min_fill
	       && group_card[1] < card + 1 - min_fill)
	{
		int better_group = -1, chosen = -1;
		area_t biggest_diff = -1;
		for (i = 0; i < card; i++) {
			if (!taken[i]) {
				area_t diff = (area(group[0] + b[i].r) - group_area[0])
					- (area(group[1] + b[i].r) - group_area[1]);
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
		}
		assert(chosen >= 0);
		group_card[better_group] += 1;
		group[better_group] += b[chosen].r;
		group_area[better_group] = area(group[better_group]);
		taken[chosen] = better_group+1;
		if (better_group == 0) {
			p->b[group_card[0]-1] = b[chosen];
		}
	}
	//
	// If one group gets too full, then remaining rectangle are
	// split between two groups in such way to balance cards of two groups.
	//
	if (group_card[0] + group_card[1] < card + 1) {
		for (i = 0; i < card; i++) {
			if (!taken[i]) {
				if (group_card[0] >= group_card[1]) {
					taken[i] = 2;
					group_card[1] += 1;
				} else {
					taken[i] = 1;
					p->b[group_card[0]++] = b[i];
				}
			}
		}
	}
	p->n = group_card[0];
	n = group_card[1];
	for (i = 0, j = 0; i < n; j++) {
		if (taken[j] == 2) {
			b[i++] = b[j];
		}
	}
	return p;
}

void R_page::remove_branch(int i)
{
	n -= 1;
	memmove(&b[i], &b[i+1], (n-i)*sizeof(branch));
}

R_page* R_page::insert(R_tree* tree, rectangle_t const& r, record_t obj, int level)
{
	branch br;
	if (--level != 0) {
		// not leaf page
		int i, mini = 0;
		area_t min_incr = AREA_MAX;
		area_t best_area = AREA_MAX;
		for (i = 0; i < n; i++) {
			area_t r_area = area(b[i].r);
			area_t incr = area(b[i].r + r) - r_area;
			if (incr < min_incr) {
				best_area = r_area;
				min_incr = incr;
				mini = i;
			} else if (incr == min_incr && r_area < best_area) {
				best_area = r_area;
				mini = i;
			}
		}
		R_page* p = b[mini].p;
		R_page* q = p->insert(tree, r, obj, level);
		if (q == NULL) {
			// child was not split
			b[mini].r += r;
			return NULL;
		} else {
			// child was split
			b[mini].r = p->cover();
			br.p = q;
			br.r = q->cover();
			return add_branch(tree, br);
		}
	} else {
		br.p = (R_page*)obj;
		br.r = r;
		return add_branch(tree, br);
	}
}

bool R_page::remove(R_tree* tree, rectangle_t const& r, record_t rec,
                    int level, reinsert_list& rlist)
{
	if (--level != 0) {
		for (int i = 0; i < n; i++) {
			if (b[i].r & r) {
				R_page* p = b[i].p;
				if (p->remove(tree, r, rec, level, rlist)) {
					if (p->n >= min_fill) {
						b[i].r = p->cover();
					} else {
						// not enough entries in child
						p->b[card-1].p = rlist.chain;
						rlist.chain = p;
						rlist.level = level - 1;
						remove_branch(i);
					}
					return true;
				}
			}
		}
	} else {
		for (int i = 0; i < n; i++) {
			if (b[i].p == rec) {
				remove_branch(i);
				return true;
			}
		}
	}
	return false;
}

void R_page::purge(R_tree* tree, int level)
{
	if (--level != 0) { /* this is an internal node in the tree */
		for (int i = 0; i < n; i++) {
			b[i].p->purge(tree, level);
		}
	}
	tree->page_allocator->free(this);
}


