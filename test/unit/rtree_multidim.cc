#include <algorithm>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <time.h>

#include "unit.h"
#include "salad/rtree.h"
#include "../../src/lib/salad/rtree.h"

#include <vector>
#include <set>
using namespace std;

const uint32_t extent_size = 1024 * 16;

const coord_t SPACE_LIMIT = 100;
const coord_t BOX_LIMIT = 10;
const unsigned BOX_POINT_CHANCE_PERCENT = 5;
const unsigned NEIGH_COUNT = 5;
const unsigned AVERAGE_COUNT = 500;
const unsigned TEST_ROUNDS = 1000;

static int page_count = 0;

static void *
extent_alloc(void *ctx)
{
	int *p_page_count = (int *)ctx;
	assert(p_page_count == &page_count);
	++*p_page_count;
	return malloc(extent_size);
}

static void
extent_free(void *ctx, void *page)
{
	int *p_page_count = (int *)ctx;
	assert(p_page_count == &page_count);
	--*p_page_count;
	free(page);
}

struct CCoordPair {
	coord_t a, b;
};

coord_t
rand(coord_t lim)
{
	return rand() % 1024 * lim / 1024;
}

template<unsigned DIMENSION>
struct CBox {
	CCoordPair pairs[DIMENSION];
	void RandomPoint()
	{
		for (unsigned i = 0; i < DIMENSION; i++) {
			pairs[i].b = pairs[i].a = rand(SPACE_LIMIT);
		}
	}
	void Randomize()
	{
		coord_t widths[DIMENSION] = {0};
		if (rand() % 100 >= (int)BOX_POINT_CHANCE_PERCENT)
			for (unsigned i = 0; i < DIMENSION; i++)
				widths[i] = rand(BOX_LIMIT);
		for (unsigned i = 0; i < DIMENSION; i++) {
			pairs[i].a = rand(SPACE_LIMIT - widths[i]);
			pairs[i].b = pairs[i].a + widths[i];
		}
	}
	void RandomizeBig()
	{
		coord_t widths[DIMENSION] = {0};
		if (DIMENSION == 1)
			for (unsigned i = 0; i < DIMENSION; i++)
				widths[i] = rand(SPACE_LIMIT / 4);
		else if (DIMENSION == 2)
			for (unsigned i = 0; i < DIMENSION; i++)
				widths[i] = rand(SPACE_LIMIT / 3);
		else if (DIMENSION == 3)
			for (unsigned i = 0; i < DIMENSION; i++)
				widths[i] = rand(SPACE_LIMIT / 2);
		else
			for (unsigned i = 0; i < DIMENSION; i++)
				widths[i] = rand(SPACE_LIMIT);

		for (unsigned i = 0; i < DIMENSION; i++) {
			pairs[i].a = rand(SPACE_LIMIT - widths[i]);
			pairs[i].b = pairs[i].a + widths[i];
		}
	}
	void FillRTreeRect(struct rtree_rect *rt)
	{
		for (unsigned i = 0; i < DIMENSION; i++) {
			rt->coords[2 * i] = pairs[i].a;
			rt->coords[2 * i + 1] = pairs[i].b;
		}
	}
	bool operator== (const struct rtree_rect *rt) const
	{
		for (unsigned i = 0; i < DIMENSION; i++) {
			if (rt->coords[2 * i] != pairs[i].a ||
			    rt->coords[2 * i + 1] != pairs[i].b)
				return false;
		}
		return true;
	}
	bool In(const CBox<DIMENSION> &another) const
	{
		for (unsigned i = 0; i < DIMENSION; i++) {
			if (pairs[i].a < another.pairs[i].a ||
			    pairs[i].b > another.pairs[i].b)
				return false;
		}
		return true;
	}
	bool InStrictly(const CBox<DIMENSION> &another) const
	{
		for (unsigned i = 0; i < DIMENSION; i++) {
			if (pairs[i].a <= another.pairs[i].a ||
			    pairs[i].b >= another.pairs[i].b)
				return false;
		}
		return true;
	}
	coord_t Distance2(const CBox<DIMENSION> &point) const
	{
		coord_t res = 0;
		for (unsigned i = 0; i < DIMENSION; i++) {
			if (point.pairs[i].a < pairs[i].a) {
				coord_t d = pairs[i].a - point.pairs[i].a;
				res += d * d;
			} else if (point.pairs[i].a > pairs[i].b) {
				coord_t d = point.pairs[i].a - pairs[i].b;
				res += d * d;
			}
		}
		return res;
	}
	coord_t DistanceMan(const CBox<DIMENSION> &point) const
	{
		coord_t res = 0;
		for (unsigned i = 0; i < DIMENSION; i++) {
			if (point.pairs[i].a < pairs[i].a) {
				coord_t d = pairs[i].a - point.pairs[i].a;
				res += d;
			} else if (point.pairs[i].a > pairs[i].b) {
				coord_t d = point.pairs[i].a - pairs[i].b;
				res += d;
			}
		}
		return res;
	}
};

template<unsigned DIMENSION>
struct CBoxSetEntry {
	CBox<DIMENSION> box;
	size_t id;
	size_t next;
	bool used;
	bool operator<(const CBoxSetEntry<DIMENSION> &a) const
	{
		return id < a.id;
	}
};

template<unsigned DIMENSION>
struct CBoxSet {
	vector<CBoxSetEntry<DIMENSION> > entries;
	size_t boxCount;
	size_t free;
	CBoxSet() : boxCount(0), free(SIZE_MAX) {}
	size_t getNewID()
	{
		size_t res;
		if (free != SIZE_MAX) {
			res = free;
			free = entries[free].next;
		} else {
			res = entries.size();
			entries.resize(res + 1);
		}
		return res;
	}
	size_t AddBox(const CBox<DIMENSION> &box)
	{
		size_t id = getNewID();
		entries[id].box = box;
		entries[id].id = id;
		entries[id].next = SIZE_MAX;
		entries[id].used = true;
		boxCount++;
		return id;
	}
	size_t RandUsedID() const
	{
		assert(boxCount);
		size_t res = rand() % entries.size();
		while (!entries[res].used)
			if (++res >= entries.size())
				res = 0;
		return res;
	}
	void DeleteBox(size_t id)
	{
		entries[id].used = false;
		entries[id].next = free;
		free = id;
		boxCount--;
	}
	void SelectIn(const CBox<DIMENSION> &box,
		      vector<CBoxSetEntry<DIMENSION> > &result) const
	{
		result.clear();
		for (size_t i = 0; i < entries.size(); i++)
			if (entries[i].used && entries[i].box.In(box))
				result.push_back(entries[i]);
	}
	void SelectInStrictly(const CBox<DIMENSION> &box,
			      vector<CBoxSetEntry<DIMENSION> > &result) const
	{
		result.clear();
		for (size_t i = 0; i < entries.size(); i++)
			if (entries[i].used && entries[i].box.InStrictly(box))
				result.push_back(entries[i]);
	}
	void SelectNeigh(const CBox<DIMENSION> &point,
			 vector<CBoxSetEntry<DIMENSION> > &result) const;
	void SelectNeighMan(const CBox<DIMENSION> &point,
			    vector<CBoxSetEntry<DIMENSION> > &result) const;
};

template<unsigned DIMENSION>
struct CEntryByDistance {
	const CBox<DIMENSION> &point;
	CEntryByDistance(const CBox<DIMENSION> &point_) : point(point_) {}
	bool operator()(const CBoxSetEntry<DIMENSION> &a,
			const CBoxSetEntry<DIMENSION> &b) const
	{
		coord_t da = a.box.Distance2(point);
		coord_t db = b.box.Distance2(point);
		return da < db ? true : da > db ? false : a.id < b.id;
	}
};

template<unsigned DIMENSION>
struct CEntryByDistanceMan {
	const CBox<DIMENSION> &point;
	CEntryByDistanceMan(const CBox<DIMENSION> &point_) : point(point_) {}
	bool operator()(const CBoxSetEntry<DIMENSION> &a,
			const CBoxSetEntry<DIMENSION> &b) const
	{
		coord_t da = a.box.DistanceMan(point);
		coord_t db = b.box.DistanceMan(point);
		return da < db ? true : da > db ? false : a.id < b.id;
	}
};

template<unsigned DIMENSION>
void CBoxSet<DIMENSION>::SelectNeigh(const CBox<DIMENSION> &point,
				     vector<CBoxSetEntry<DIMENSION> > &result) const
{
	result.clear();
	CEntryByDistance<DIMENSION> comp(point);
	set<CBoxSetEntry<DIMENSION>, CEntryByDistance<DIMENSION> > set(comp);
	size_t i = 0;
	for (; i < entries.size() && set.size() < NEIGH_COUNT; i++) {
		if (!entries[i].used)
			continue;
		set.insert(entries[i]);
	}
	if (set.empty())
		return;
	coord_t max_d = set.rbegin()->box.Distance2(point);
	for (; i < entries.size(); i++) {
		if (!entries[i].used)
			continue;
		coord_t d = entries[i].box.Distance2(point);
		if (d < max_d) {
			auto itr = set.end();
			--itr;
			set.erase(itr);
			set.insert(entries[i]);
			max_d = set.rbegin()->box.Distance2(point);
		}
	}
	for (auto itr : set)
		result.push_back(itr);
}

template<unsigned DIMENSION>
void CBoxSet<DIMENSION>::SelectNeighMan(const CBox<DIMENSION> &point,
	vector<CBoxSetEntry<DIMENSION> > &result) const
{
	result.clear();
	CEntryByDistanceMan<DIMENSION> comp(point);
	set<CBoxSetEntry<DIMENSION>, CEntryByDistanceMan<DIMENSION> > set(comp);
	size_t i = 0;
	for (; i < entries.size() && set.size() < NEIGH_COUNT; i++) {
		if (!entries[i].used)
			continue;
		set.insert(entries[i]);
	}
	if (set.empty())
		return;
	coord_t max_d = set.rbegin()->box.DistanceMan(point);
	for (; i < entries.size(); i++) {
		if (!entries[i].used)
			continue;
		coord_t d = entries[i].box.DistanceMan(point);
		if (d < max_d) {
			auto itr = set.end();
			--itr;
			set.erase(itr);
			set.insert(entries[i]);
			max_d = set.rbegin()->box.DistanceMan(point);
		}
	}
	for (auto itr : set)
		result.push_back(itr);
}

template<unsigned DIMENSION>
static void
test_select_neigh(const CBoxSet<DIMENSION> &set, const struct rtree *tree)
{
	CBox<DIMENSION> box;
	box.RandomizeBig();
	vector<CBoxSetEntry<DIMENSION> > res1;
	set.SelectNeigh(box, res1);

	struct rtree_rect rt;
	box.FillRTreeRect(&rt);
	struct rtree_iterator iterator;
	rtree_iterator_init(&iterator);
	vector<CBoxSetEntry<DIMENSION> > res2;
	if (rtree_search(tree, &rt, SOP_NEIGHBOR, &iterator)) {
		void *record;
		while((record = rtree_iterator_next(&iterator))) {
			CBoxSetEntry<DIMENSION> entry;
			entry.id = ((unsigned)(uintptr_t)record) - 1;
			entry.box = set.entries[entry.id].box;
			res2.push_back(entry);
			if (res2.size() == NEIGH_COUNT)
				break;
		}
	}
	if (res1.size() != res2.size()) {
		printf("%s result size differ %d %d\n", __func__,
		       (int)res1.size(), (int)res2.size());
	} else {
		for (size_t i = 0; i < res1.size(); i++)
			if (res1[i].id != res2[i].id &&
			    res1[i].box.Distance2(box) !=
			    res2[i].box.Distance2(box))
				printf("%s result differ!\n", __func__);
	}
	rtree_iterator_destroy(&iterator);

}

template<unsigned DIMENSION>
static void
test_select_neigh_man(const CBoxSet<DIMENSION> &set, struct rtree *tree)
{
	CBox<DIMENSION> box;
	box.RandomizeBig();
	vector<CBoxSetEntry<DIMENSION> > res1;
	set.SelectNeighMan(box, res1);

	struct rtree_rect rt;
	box.FillRTreeRect(&rt);
	struct rtree_iterator iterator;
	rtree_iterator_init(&iterator);
	vector<CBoxSetEntry<DIMENSION> > res2;
	tree->distance_type = RTREE_MANHATTAN; /* dirty hack */
	if (rtree_search(tree, &rt, SOP_NEIGHBOR, &iterator)) {
		void *record;
		while((record = rtree_iterator_next(&iterator))) {
			CBoxSetEntry<DIMENSION> entry;
			entry.id = ((unsigned)(uintptr_t)record) - 1;
			entry.box = set.entries[entry.id].box;
			res2.push_back(entry);
			if (res2.size() == NEIGH_COUNT)
				break;
		}
	}
	if (res1.size() != res2.size()) {
		printf("%s result size differ %d %d\n", __func__,
		       (int)res1.size(), (int)res2.size());
	} else {
		for (size_t i = 0; i < res1.size(); i++)
			if (res1[i].id != res2[i].id &&
			    res1[i].box.DistanceMan(box) !=
			    res2[i].box.DistanceMan(box))
				printf("%s result differ!\n", __func__);
	}
	tree->distance_type = RTREE_EUCLID; /* dirty hack */
	rtree_iterator_destroy(&iterator);

}

template<unsigned DIMENSION>
static void
test_select_in(const CBoxSet<DIMENSION> &set, const struct rtree *tree)
{
	CBox<DIMENSION> box;
	box.RandomizeBig();
	vector<CBoxSetEntry<DIMENSION> > res1;
	set.SelectIn(box, res1);

	struct rtree_rect rt;
	box.FillRTreeRect(&rt);
	struct rtree_iterator iterator;
	rtree_iterator_init(&iterator);
	vector<CBoxSetEntry<DIMENSION> > res2;
	if (rtree_search(tree, &rt, SOP_BELONGS, &iterator)) {
		void *record;
		while((record = rtree_iterator_next(&iterator))) {
			CBoxSetEntry<DIMENSION> entry;
			entry.id = ((unsigned)(uintptr_t)record) - 1;
			entry.box = set.entries[entry.id].box;
			res2.push_back(entry);
		}
	}
	sort(res1.begin(), res1.end());
	sort(res2.begin(), res2.end());
	if (res1.size() != res2.size()) {
		printf("%s result size differ %d %d\n", __func__,
		       (int)res1.size(), (int)res2.size());
	} else {
		for (size_t i = 0; i < res1.size(); i++)
			if (res1[i].id != res2[i].id)
				printf("%s result differ!\n", __func__);
	}
	rtree_iterator_destroy(&iterator);

}

template<unsigned DIMENSION>
static void
test_select_strict_in(const CBoxSet<DIMENSION> &set, const struct rtree *tree)
{
	CBox<DIMENSION> box;
	box.RandomizeBig();
	vector<CBoxSetEntry<DIMENSION> > res1;
	set.SelectInStrictly(box, res1);

	struct rtree_rect rt;
	box.FillRTreeRect(&rt);
	struct rtree_iterator iterator;
	rtree_iterator_init(&iterator);
	vector<CBoxSetEntry<DIMENSION> > res2;
	if (rtree_search(tree, &rt, SOP_STRICT_BELONGS, &iterator)) {
		void *record;
		while((record = rtree_iterator_next(&iterator))) {
			CBoxSetEntry<DIMENSION> entry;
			entry.id = ((unsigned)(uintptr_t)record) - 1;
			entry.box = set.entries[entry.id].box;
			res2.push_back(entry);
		}
	}
	sort(res1.begin(), res1.end());
	sort(res2.begin(), res2.end());
	if (res1.size() != res2.size()) {
		printf("%s result size differ %d %d\n", __func__,
		       (int)res1.size(), (int)res2.size());
	} else {
		for (size_t i = 0; i < res1.size(); i++)
			if (res1[i].id != res2[i].id)
				printf("%s result differ!\n", __func__);
	}
	rtree_iterator_destroy(&iterator);

}

template<unsigned DIMENSION>
static void
rand_test()
{
	header();

	CBoxSet<DIMENSION> set;

	struct rtree tree;
	rtree_init(&tree, DIMENSION, extent_size,
		   extent_alloc, extent_free, &page_count,
		   RTREE_EUCLID);

	printf("\tDIMENSION: %u, page size: %u, max fill good: %d\n",
	       DIMENSION, tree.page_size, tree.page_max_fill >= 10);

	for (unsigned i = 0; i < TEST_ROUNDS; i++) {
		bool insert;
		if (set.boxCount == 0) {
			insert = true;
		} else if (set.boxCount == AVERAGE_COUNT) {
			insert = false;
		} else {
			insert = rand() % (AVERAGE_COUNT * 2) > set.boxCount;
		}
		if (insert) {
			CBox<DIMENSION> box;
			box.Randomize();
			size_t id = set.AddBox(box);
			struct rtree_rect rt;
			box.FillRTreeRect(&rt);
			rtree_insert(&tree, &rt, (void *)(id + 1));
		} else {
			size_t id = set.RandUsedID();
			struct rtree_rect rt;
			set.entries[id].box.FillRTreeRect(&rt);
			rtree_remove(&tree, &rt, (void *)(id + 1));
			set.DeleteBox(id);
		}
		assert(set.boxCount == tree.n_records);
		test_select_neigh<DIMENSION>(set, &tree);
		test_select_neigh_man<DIMENSION>(set, &tree);
		test_select_in<DIMENSION>(set, &tree);
		test_select_strict_in<DIMENSION>(set, &tree);
	}

	rtree_destroy(&tree);

	footer();
}

int
main(void)
{
	srand(time(0));
	rand_test<1>();
	rand_test<2>();
	rand_test<3>();
	rand_test<8>();
	rand_test<16>();
	if (page_count != 0) {
		fail("memory leak!", "true");
	}
}
