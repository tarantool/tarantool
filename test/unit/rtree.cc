#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

#include "unit.h"
#include "salad/rtree.h"

static int page_count = 0;

const uint32_t extent_size = 1024 * 8;

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

static void
simple_check()
{
	struct rtree_rect rect;
	struct rtree_iterator iterator;
	rtree_iterator_init(&iterator);
	const size_t rounds = 2000;

	header();

	struct rtree tree;
	rtree_init(&tree, 2, extent_size,
		   extent_alloc, extent_free, &page_count,
		   RTREE_EUCLID);

	printf("Insert 1..X, remove 1..X\n");
	for (size_t i = 1; i <= rounds; i++) {
		record_t rec = (record_t)i;

		rtree_set2d(&rect, i, i, i + 0.5, i + 0.5);

		if (rtree_search(&tree, &rect, SOP_EQUALS, &iterator)) {
			fail("element already in tree (1)", "true");
		}
		rtree_insert(&tree, &rect, rec);
	}
	if (rtree_number_of_records(&tree) != rounds) {
		fail("Tree count mismatch (1)", "true");
	}
	for (size_t i = 1; i <= rounds; i++) {
		record_t rec = (record_t)i;

		rtree_set2d(&rect, i, i, i + 0.5, i + 0.5);

		if (!rtree_search(&tree, &rect, SOP_EQUALS, &iterator)) {
			fail("element in tree (1)", "false");
		}
		if (rtree_iterator_next(&iterator) != rec) {
			fail("right search result (1)", "true");
		}
		if (rtree_iterator_next(&iterator)) {
			fail("single search result (1)", "true");
		}
		if (!rtree_remove(&tree, &rect, rec)) {
			fail("delete element in tree (1)", "false");
		}
		if (rtree_search(&tree, &rect, SOP_EQUALS, &iterator)) {
			fail("element still in tree (1)", "true");
		}
	}
	if (rtree_number_of_records(&tree) != 0) {
		fail("Tree count mismatch (1)", "true");
	}

	printf("Insert 1..X, remove X..1\n");
	for (size_t i = 1; i <= rounds; i++) {
		record_t rec = (record_t)i;

		rtree_set2d(&rect, i, i, i + 0.5, i + 0.5);

		if (rtree_search(&tree, &rect, SOP_EQUALS, &iterator)) {
			fail("element already in tree (2)", "true");
		}
		rtree_insert(&tree, &rect, rec);
	}
	if (rtree_number_of_records(&tree) != rounds) {
		fail("Tree count mismatch (2)", "true");
	}
	for (size_t i = rounds; i != 0; i--) {
		record_t rec = (record_t)i;

		rtree_set2d(&rect, i, i, i + 0.5, i + 0.5);

		if (!rtree_search(&tree, &rect, SOP_OVERLAPS, &iterator)) {
			fail("element in tree (2)", "false");
		}
		if (rtree_iterator_next(&iterator) != rec) {
			fail("right search result (2)", "true");
		}
		if (rtree_iterator_next(&iterator)) {
			fail("single search result (2)", "true");
		}
		if (!rtree_remove(&tree, &rect, rec)) {
			fail("delete element in tree (2)", "false");
		}
		if (rtree_search(&tree, &rect, SOP_OVERLAPS, &iterator)) {
			fail("element still in tree (2)", "true");
		}
	}
	if (rtree_number_of_records(&tree) != 0) {
		fail("Tree count mismatch (2)", "true");
	}


	printf("Insert X..1, remove 1..X\n");
	for (size_t i = rounds; i != 0; i--) {
		record_t rec = (record_t)i;

		rtree_set2d(&rect, i, i, i + 0.5, i + 0.5);

		if (rtree_search(&tree, &rect, SOP_BELONGS, &iterator)) {
			fail("element already in tree (3)", "true");
		}
		rtree_insert(&tree, &rect, rec);
	}
	if (rtree_number_of_records(&tree) != rounds) {
		fail("Tree count mismatch (3)", "true");
	}
	for (size_t i = 1; i <= rounds; i++) {
		record_t rec = (record_t)i;

		rtree_set2d(&rect, i, i, i + 0.5, i + 0.5);

		if (!rtree_search(&tree, &rect, SOP_BELONGS, &iterator)) {
			fail("element in tree (3)", "false");
		}
		if (rtree_iterator_next(&iterator) != rec) {
			fail("right search result (3)", "true");
		}
		if (rtree_iterator_next(&iterator)) {
			fail("single search result (3)", "true");
		}
		if (!rtree_remove(&tree, &rect, rec)) {
			fail("delete element in tree (3)", "false");
		}
		if (rtree_search(&tree, &rect, SOP_BELONGS, &iterator)) {
			fail("element still in tree (3)", "true");
		}
	}
	if (rtree_number_of_records(&tree) != 0) {
		fail("Tree count mismatch (3)", "true");
	}


	printf("Insert X..1, remove X..1\n");
	for (size_t i = rounds; i != 0; i--) {
		record_t rec = (record_t)i;

		rtree_set2d(&rect, i, i, i + 0.5, i + 0.5);

		if (rtree_search(&tree, &rect, SOP_CONTAINS, &iterator)) {
			fail("element already in tree (4)", "true");
		}
		rtree_insert(&tree, &rect, rec);
	}
	if (rtree_number_of_records(&tree) != rounds) {
		fail("Tree count mismatch (4)", "true");
	}
	for (size_t i = rounds; i != 0; i--) {
		record_t rec = (record_t)i;

		rtree_set2d(&rect, i, i, i + 0.5, i + 0.5);

		if (!rtree_search(&tree, &rect, SOP_CONTAINS, &iterator)) {
			fail("element in tree (4)", "false");
		}
		if (rtree_iterator_next(&iterator) != rec) {
			fail("right search result (4)", "true");
		}
		if (rtree_iterator_next(&iterator)) {
			fail("single search result (4)", "true");
		}
		if (!rtree_remove(&tree, &rect, rec)) {
			fail("delete element in tree (4)", "false");
		}
		if (rtree_search(&tree, &rect, SOP_CONTAINS, &iterator)) {
			fail("element still in tree (4)", "true");
		}
	}
	if (rtree_number_of_records(&tree) != 0) {
		fail("Tree count mismatch (4)", "true");
	}

	rtree_purge(&tree);
	rtree_destroy(&tree);

	rtree_iterator_destroy(&iterator);

	footer();
}

static void
rtree_test_build(struct rtree *tree, struct rtree_rect *arr, int count)
{
	for (ssize_t i = 0; i < count; i++) {
		record_t rec = (record_t)(i + 1);
		rtree_insert(tree, &arr[i], rec);
	}
}

static void
neighbor_test()
{
	header();

	const unsigned int test_count = 1000;
	struct rtree_rect arr[test_count];
	static struct rtree_rect basis;

	for (size_t i = 0; i < test_count; i++) {
		rtree_set2d(&arr[i], i, i, i + 1, i + 1);
	}

	for (size_t i = 0; i <= test_count; i++) {
		struct rtree tree;
		rtree_init(&tree, 2, extent_size,
			   extent_alloc, extent_free, &page_count,
			   RTREE_EUCLID);

		rtree_test_build(&tree, arr, i);

		struct rtree_iterator iterator;
		rtree_iterator_init(&iterator);
		if (!rtree_search(&tree, &basis, SOP_NEIGHBOR, &iterator) && i != 0) {
			fail("search is successful", "true");
		}

		for (size_t j = 0; j < i; j++) {
			record_t rec = rtree_iterator_next(&iterator);
			if (rec != record_t(j+1)) {
				fail("wrong search result", "true");
			}
		}
		rtree_iterator_destroy(&iterator);
		rtree_destroy(&tree);
	}

	struct rtree_iterator iterator;
	rtree_iterator_init(&iterator);
	struct rtree tree;
	rtree_init(&tree, 2, extent_size, extent_alloc, extent_free, &page_count, 
			RTREE_EUCLID);
	if (rtree_search(&tree, &basis, SOP_NEIGHBOR, &iterator)) {
		fail("found in empty", "true");
	}
	/*
	 * Test unchecked return value from rtree_search
	 */
	record_t rec = rtree_iterator_next(&iterator);
	if (rec != NULL) {
		fail("something found from empty iterator ", "true");
	}
	rtree_iterator_destroy(&iterator);

	footer();
}


int
main(void)
{
	simple_check();
	neighbor_test();
	if (page_count != 0) {
		fail("memory leak!", "true");
	}
}
