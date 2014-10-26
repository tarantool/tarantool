#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

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
itr_check()
{
	header();

	R_tree tree(page_alloc, page_free);

	/* Filling tree */
	const size_t count1 = 10000;
	const size_t count2 = 5;
	rectangle_t r;
	size_t count = 0;
	record_t rec;
	R_tree_iterator iterator;

	for (size_t i = 0; i < count1; i++) {
		r.boundary[0] = r.boundary[1] = i * 2 * count2; /* note that filled with even numbers */
		for (size_t j = 0; j < count2; j++) {
			r.boundary[2] = r.boundary[3] = r.boundary[0] + j;
			tree.insert(r, record_t(++count));
		}
	}
	printf("Test tree size: %d\n", (int)tree.number_of_records());

	/* Test that tree filled ok */
	for (size_t i = 0; i < count1; i++) {
		for (size_t j = 0; j < count2; j++) {
			r.boundary[0] = r.boundary[1] = i * 2 * count2;
			r.boundary[2] = r.boundary[3] = r.boundary[0] + j;
			if (!tree.search(r, SOP_BELONGS, iterator)) {
				fail("Integrity check failed (1)", "false");
			}
			for (size_t k = 0; k <= j; k++) {
				if (!iterator.next()) {
					fail("Integrity check failed (2)", "false");
				}
			}
			if (iterator.next()) {
				fail("Integrity check failed (3)", "true");
			}
			r.boundary[0] = r.boundary[1] = (i * 2  + 1) * count2;
			r.boundary[2] = r.boundary[3] = r.boundary[0] + j;
			if (tree.search(r, SOP_BELONGS, iterator)) {
				fail("Integrity check failed (4)", "true");
			}
		}
	}

	/* Print 7 elems closest to coordinate basis */
	{
		static rectangle_t basis;
		printf("--> ");
		if (!tree.search(basis, SOP_NEIGHBOR, iterator)) {
			fail("Integrity check failed (5)", "false");
		}
		for (int i = 0; i < 7; i++) {
			rec = iterator.next();
			if (rec == 0) {
				fail("Integrity check failed (6)", "false");
			}
			printf("%p ", rec);
		}
		printf("\n");
	}
	/* Print 7 elems closest to the point [(count1-1)*count2*2, (count1-1)*count2*2] */
	{
		printf("<-- ");
		r.boundary[0] = r.boundary[1] = r.boundary[2] = r.boundary[3] = (count1-1)*count2*2;
		if (!tree.search(r, SOP_NEIGHBOR, iterator)) {
			fail("Integrity check failed (5)", "false");
		}
		for (int i = 0; i < 7; i++) {
		        rec = iterator.next();
			if (rec == 0) {
				fail("Integrity check failed (6)", "false");
			}
			printf("%p ", rec);
		}
		printf("\n");
	}

	/* Test strict besize_ts */
	for (size_t i = 0; i < count1; i++) {
		for (size_t j = 0; j < count2; j++) {
			r.boundary[0] = r.boundary[1] = i * 2 * count2;
			r.boundary[2] = r.boundary[3] = r.boundary[0] + j;
			if (!tree.search(r, SOP_STRICT_BELONGS, iterator) && j != 0) {
				fail("Integrity check failed (7)", "false");
			}
			for (size_t k = 0; k < j; k++) {
				if (!iterator.next()) {
					fail("Integrity check failed (8)", "false");
				}
			}
			if (iterator.next()) {
				fail("Integrity check failed (9)", "true");
			}
			r.boundary[0] = r.boundary[1] = (i * 2 + 1) * count2;
			r.boundary[2] = r.boundary[3] = r.boundary[0] + j;
			if (tree.search(r, SOP_STRICT_BELONGS, iterator)) {
				fail("Integrity check failed (10)", "true");
			}
		}
	}

	/* Test strict contains */
	for (size_t i = 0; i < count1; i++) {
		for (size_t j = 0; j < count2; j++) {
			r.boundary[0] = r.boundary[1] = i * 2 * count2;
			r.boundary[2] = r.boundary[3] = r.boundary[0] + j;
			if (!tree.search(r, SOP_STRICT_CONTAINS, iterator) && j != count2-1) {
				fail("Integrity check failed (11)", "false");
			}
			for (size_t k = j; k < count2-1; k++) {
				if (!iterator.next()) {
					fail("Integrity check failed (12)", "false");
				}
			}
			if (iterator.next()) {
				fail("Integrity check failed (13)", "true");
			}
			r.boundary[0] = r.boundary[1] = (i * 2 + 1) * count2;
			r.boundary[2] = r.boundary[3] = r.boundary[0] + j;
			if (tree.search(r, SOP_STRICT_CONTAINS, iterator)) {
				fail("Integrity check failed (14)", "true");
			}
		}
	}

	tree.purge();

	footer();
}

static void
itr_invalidate_check()
{
	header();

	const size_t test_size = 300;
	const size_t max_delete_count = 100;
	const size_t max_insert_count = 200;
	const size_t attempt_count = 100;
	struct R_tree_iterator iterators[test_size];

	R_tree tree(page_alloc, page_free);
	rectangle_t r;

	/* invalidation during deletion */
	srand(0);
	for (size_t attempt = 0; attempt < attempt_count; attempt++) {
		size_t del_pos = rand() % test_size;
		size_t del_cnt = rand() % max_delete_count + 1;
		if (del_pos + del_cnt > test_size) {
			del_cnt = test_size - del_pos;
		}
		R_tree tree(page_alloc, page_free);

		for (size_t i = 0; i < test_size; i++) {
			r.boundary[0] = r.boundary[1] = r.boundary[2] = r.boundary[3] = i;
			tree.insert(r, record_t(i+1));
		}
		r.boundary[0] = r.boundary[1] = 0;
		r.boundary[2] = r.boundary[3] = test_size;
		tree.search(r, SOP_BELONGS, iterators[0]);
		if (!iterators[0].next()) {
			fail("Integrity check failed (15)", "false");
		}
		for (size_t i = 1; i < test_size; i++) {
			iterators[i] = iterators[i - 1];
			if (!iterators[i].next()) {
				fail("Integrity check failed (16)", "false");
			}
		}
		for (size_t i = del_pos; i < del_pos + del_cnt; i++) {
			r.boundary[0] = r.boundary[1] = r.boundary[2] = r.boundary[3] = i;
			if (!tree.remove(r, record_t(i+1))) {
				fail("Integrity check failed (17)", "false");
			}
		}
		for (size_t i = 0; i < test_size; i++) {
			if (iterators[i].next()) {
				fail("Iterator was not invalidated (18)", "true");
			}
		}
	}

	/* invalidation during insertion */
	srand(0);
	for (size_t attempt = 0; attempt < attempt_count; attempt++) {
		size_t ins_pos = rand() % test_size;
		size_t ins_cnt = rand() % max_insert_count + 1;

		R_tree tree(page_alloc, page_free);

		for (size_t i = 0; i < test_size; i++) {
			r.boundary[0] = r.boundary[1] = r.boundary[2] = r.boundary[3] = i;
			tree.insert(r, record_t(i+1));
		}
		r.boundary[0] = r.boundary[1] = 0;
		r.boundary[2] = r.boundary[3] = test_size;
		tree.search(r, SOP_BELONGS, iterators[0]);
		if (!iterators[0].next()) {
			fail("Integrity check failed (19)", "false");
		}
		for (size_t i = 1; i < test_size; i++) {
			iterators[i] = iterators[i - 1];
			if (!iterators[0].next()) {
				fail("Integrity check failed (20)", "false");
			}
		}
		for (size_t i = ins_pos; i < ins_pos + ins_cnt; i++) {
			r.boundary[0] = r.boundary[1] = r.boundary[2] = r.boundary[3] = i;
			tree.insert(r, record_t(test_size + i - ins_pos + 1));
		}
		for (size_t i = 0; i < test_size; i++) {
			if (iterators[i].next()) {
				fail("Iterator was not invalidated (22)", "true");
			}
		}
	}

	footer();
}

int
main(void)
{
	itr_check();
	itr_invalidate_check();
	if (page_count != 0) {
		fail("memory leak!", "false");
	}
}
