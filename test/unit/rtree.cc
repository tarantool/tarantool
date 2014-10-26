#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

#include "unit.h"
#include "rtree.h"

static int page_count = 0;

static void *
page_alloc()
{
	page_count++;
	return malloc(RTREE_PAGE_SIZE);
}

static void
page_free(void *page)
{
	page_count--;
	free(page);
}

static void
simple_check()
{
	rectangle_t r;
	R_tree_iterator iterator;
	const size_t rounds = 2000;

	header();

	R_tree tree(page_alloc, page_free);

	printf("Insert 1..X, remove 1..X\n");
	for (size_t i = 1; i <= rounds; i++) {
		record_t rec = (record_t)i;

		r.boundary[0] = r.boundary[1] = i;
		r.boundary[2] = r.boundary[3] = i + 0.5;

		if (tree.search(r, SOP_EQUALS, iterator)) {
			fail("element already in tree (1)", "true");
		}
		tree.insert(r, rec);
	}
	if (tree.number_of_records() != rounds) {
		fail("Tree count mismatch (1)", "true");
	}
	for (size_t i = 1; i <= rounds; i++) {
		record_t rec = (record_t)i;

		r.boundary[0] = r.boundary[1] = i;
		r.boundary[2] = r.boundary[3] = i + 0.5;

		if (!tree.search(r, SOP_EQUALS, iterator)) {
			fail("element in tree (1)", "false");
		}
		if (iterator.next() != rec) {
			fail("right search result (1)", "true");
		}
		if (iterator.next()) {
			fail("single search result (1)", "true");
		}
		if (!tree.remove(r, rec)) {
			fail("delete element in tree (1)", "false");
		}
		if (tree.search(r, SOP_EQUALS, iterator)) {
			fail("element still in tree (1)", "true");
		}
	}
	if (tree.number_of_records() != 0) {
		fail("Tree count mismatch (1)", "true");
	}

	printf("Insert 1..X, remove X..1\n");
	for (size_t i = 1; i <= rounds; i++) {
		record_t rec = (record_t)i;

		r.boundary[0] = r.boundary[1] = i;
		r.boundary[2] = r.boundary[3] = i + 0.5;

		if (tree.search(r, SOP_OVERLAPS, iterator)) {
			fail("element already in tree (2)", "true");
		}
		tree.insert(r, rec);
	}
	if (tree.number_of_records() != rounds) {
		fail("Tree count mismatch (2)", "true");
	}
	for (size_t i = rounds; i != 0; i--) {
		record_t rec = (record_t)i;

		r.boundary[0] = r.boundary[1] = i;
		r.boundary[2] = r.boundary[3] = i + 0.5;

		if (!tree.search(r, SOP_OVERLAPS, iterator)) {
			fail("element in tree (2)", "false");
		}
		if (iterator.next() != rec) {
			fail("right search result (2)", "true");
		}
		if (iterator.next()) {
			fail("single search result (2)", "true");
		}
		if (!tree.remove(r, rec)) {
			fail("delete element in tree (2)", "false");
		}
		if (tree.search(r, SOP_OVERLAPS, iterator)) {
			fail("element still in tree (2)", "true");
		}
	}
	if (tree.number_of_records() != 0) {
		fail("Tree count mismatch (2)", "true");
	}


	printf("Insert X..1, remove 1..X\n");
	for (size_t i = rounds; i != 0; i--) {
		record_t rec = (record_t)i;

		r.boundary[0] = r.boundary[1] = i;
		r.boundary[2] = r.boundary[3] = i + 0.5;

		if (tree.search(r, SOP_BELONGS, iterator)) {
			fail("element already in tree (3)", "true");
		}
		tree.insert(r, rec);
	}
	if (tree.number_of_records() != rounds) {
		fail("Tree count mismatch (3)", "true");
	}
	for (size_t i = 1; i <= rounds; i++) {
		record_t rec = (record_t)i;

		r.boundary[0] = r.boundary[1] = i;
		r.boundary[2] = r.boundary[3] = i + 0.5;

		if (!tree.search(r, SOP_BELONGS, iterator)) {
			fail("element in tree (3)", "false");
		}
		if (iterator.next() != rec) {
			fail("right search result (3)", "true");
		}
		if (iterator.next()) {
			fail("single search result (3)", "true");
		}
		if (!tree.remove(r, rec)) {
			fail("delete element in tree (3)", "false");
		}
		if (tree.search(r, SOP_BELONGS, iterator)) {
			fail("element still in tree (3)", "true");
		}
	}
	if (tree.number_of_records() != 0) {
		fail("Tree count mismatch (3)", "true");
	}


	printf("Insert X..1, remove X..1\n");
	for (size_t i = rounds; i != 0; i--) {
		record_t rec = (record_t)i;

		r.boundary[0] = r.boundary[1] = i;
		r.boundary[2] = r.boundary[3] = i + 0.5;

		if (tree.search(r, SOP_CONTAINS, iterator)) {
			fail("element already in tree (4)", "true");
		}
		tree.insert(r, rec);
	}
	if (tree.number_of_records() != rounds) {
		fail("Tree count mismatch (4)", "true");
	}
	for (size_t i = rounds; i != 0; i--) {
		record_t rec = (record_t)i;

		r.boundary[0] = r.boundary[1] = i;
		r.boundary[2] = r.boundary[3] = i + 0.5;

		if (!tree.search(r, SOP_CONTAINS, iterator)) {
			fail("element in tree (4)", "false");
		}
		if (iterator.next() != rec) {
			fail("right search result (4)", "true");
		}
		if (iterator.next()) {
			fail("single search result (4)", "true");
		}
		if (!tree.remove(r, rec)) {
			fail("delete element in tree (4)", "false");
		}
		if (tree.search(r, SOP_CONTAINS, iterator)) {
			fail("element still in tree (4)", "true");
		}
	}
	if (tree.number_of_records() != 0) {
		fail("Tree count mismatch (4)", "true");
	}

	tree.purge();

	footer();
}

static void
rtree_test_build(R_tree& tree, rectangle_t* arr, int count)
{
	for (size_t i = 0; i < count; i++) {
		record_t rec = (record_t)(i + 1);
		tree.insert(arr[i], rec);
	}
}

static void
neighbor_test()
{
	header();

	const int test_count = 1000;
	R_tree_iterator iterator;
	rectangle_t arr[test_count];
	static rectangle_t basis;

	for (size_t i = 0; i < test_count; i++) {
		arr[i].boundary[0] = arr[i].boundary[1] = i;
		arr[i].boundary[2] = arr[i].boundary[3] = i+1;
	}

	for (size_t i = 0; i <= test_count; i++) {
		R_tree tree(page_alloc, page_free);

		rtree_test_build(tree, arr, i);

		if (!tree.search(basis, SOP_NEIGHBOR, iterator) && i != 0) {
			fail("search is successful", "true");
		}

		for (size_t j = 0; j < i; j++) {
			record_t rec = iterator.next();
			if (rec != record_t(j+1)) {
				fail("wrong search result", "true");
			}
		}
	}

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
