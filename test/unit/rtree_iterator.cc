#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "unit.h"
#include "salad/rtree.h"

static int extent_count = 0;

const uint32_t extent_size = 1024 * 8;

static void *
extent_alloc(void *ctx)
{
	int *p_extent_count = (int *)ctx;
	assert(p_extent_count == &extent_count);
	++*p_extent_count;
	return malloc(extent_size);
}

static void
extent_free(void *ctx, void *page)
{
	int *p_extent_count = (int *)ctx;
	assert(p_extent_count == &extent_count);
	--*p_extent_count;
	free(page);
}

static void
iterator_check()
{
	header();

	struct rtree tree;
	rtree_init(&tree, 2, extent_size,
		   extent_alloc, extent_free, &extent_count,
		   RTREE_EUCLID);

	/* Filling tree */
	const size_t count1 = 10000;
	const size_t count2 = 5;
	struct rtree_rect rect;
	size_t count = 0;
	record_t rec;
	struct rtree_iterator iterator;
	rtree_iterator_init(&iterator);

	for (size_t i = 0; i < count1; i++) {
		coord_t coord = i * 2 * count2; /* note that filled with even numbers */
		for (size_t j = 0; j < count2; j++) {
			rtree_set2d(&rect, coord, coord, coord + j, coord + j);
			rtree_insert(&tree, &rect, record_t(++count));
		}
	}
	printf("Test tree size: %d\n", (int)rtree_number_of_records(&tree));

	/* Test that tree filled ok */
	for (size_t i = 0; i < count1; i++) {
		for (size_t j = 0; j < count2; j++) {
			coord_t coord = i * 2 * count2;
			rtree_set2d(&rect, coord, coord, coord + j, coord + j);
			if (!rtree_search(&tree, &rect, SOP_BELONGS, &iterator)) {
				fail("Integrity check failed (1)", "false");
			}
			for (size_t k = 0; k <= j; k++) {
				if (!rtree_iterator_next(&iterator)) {
					fail("Integrity check failed (2)", "false");
				}
			}
			if (rtree_iterator_next(&iterator)) {
				fail("Integrity check failed (3)", "true");
			}
			coord = (i * 2  + 1) * count2;;
			rtree_set2d(&rect, coord, coord, coord + j, coord + j);
			if (rtree_search(&tree, &rect, SOP_BELONGS, &iterator)) {
				fail("Integrity check failed (4)", "true");
			}
		}
	}

	/* Print 7 elems closest to coordinate basis */
	{
		static struct rtree_rect basis;
		printf("--> ");
		if (!rtree_search(&tree, &basis, SOP_NEIGHBOR, &iterator)) {
			fail("Integrity check failed (5)", "false");
		}
		for (int i = 0; i < 7; i++) {
			rec = rtree_iterator_next(&iterator);
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
		coord_t coord = (count1 - 1) * count2 * 2;
		rtree_set2d(&rect, coord, coord, coord, coord);
		if (!rtree_search(&tree, &rect, SOP_NEIGHBOR, &iterator)) {
			fail("Integrity check failed (5)", "false");
		}
		for (int i = 0; i < 7; i++) {
		        rec = rtree_iterator_next(&iterator);
			if (rec == 0) {
				fail("Integrity check failed (6)", "false");
			}
			printf("%p ", rec);
		}
		printf("\n");
	}

	/* Test strict belongs */
	for (size_t i = 0; i < count1; i++) {
		for (size_t j = 0; j < count2; j++) {
			coord_t coord = i * 2 * count2;
			rtree_set2d(&rect, coord - 0.1, coord - 0.1, coord + j, coord + j);
			if (!rtree_search(&tree, &rect, SOP_STRICT_BELONGS, &iterator) && j != 0) {
				fail("Integrity check failed (7)", "false");
			}
			for (size_t k = 0; k < j; k++) {
				if (!rtree_iterator_next(&iterator)) {
					fail("Integrity check failed (8)", "false");
				}
			}
			if (rtree_iterator_next(&iterator)) {
				fail("Integrity check failed (9)", "true");
			}
			coord = (i * 2 + 1) * count2;
			rtree_set2d(&rect, coord, coord, coord + j, coord + j);
			if (rtree_search(&tree, &rect, SOP_STRICT_BELONGS, &iterator)) {
				fail("Integrity check failed (10)", "true");
			}
		}
	}

	/* Test contains */
	for (size_t i = 0; i < count1; i++) {
		for (size_t j = 0; j < count2; j++) {
			coord_t coord = i * 2 * count2;
			rtree_set2d(&rect, coord, coord, coord + j, coord + j);
			if (!rtree_search(&tree, &rect, SOP_CONTAINS, &iterator)) {
				fail("Integrity check failed (11)", "false");
			}
			for (size_t k = j; k < count2; k++) {
				if (!rtree_iterator_next(&iterator)) {
					fail("Integrity check failed (12)", "false");
				}
			}
			if (rtree_iterator_next(&iterator)) {
				fail("Integrity check failed (13)", "true");
			}
			coord = (i * 2 + 1) * count2;
			rtree_set2d(&rect, coord, coord, coord + j, coord + j);
			if (rtree_search(&tree, &rect, SOP_CONTAINS, &iterator)) {
				fail("Integrity check failed (14)", "true");
			}
		}
	}

	/* Test strict contains */
	for (size_t i = 0; i < count1; i++) {
		for (size_t j = 0; j < count2; j++) {
			coord_t coord = i * 2 * count2;
			rtree_set2d(&rect, coord + 0.1, coord + 0.1, coord + j, coord + j);
			rtree_rect_normalize(&rect, 2);
			if (!rtree_search(&tree, &rect, SOP_STRICT_CONTAINS, &iterator) && j != 0 && j != count2 - 1) {
				fail("Integrity check failed (11)", "false");
			}
			if (j) {
				for (size_t k = j; k < count2 - 1; k++) {
					if (!rtree_iterator_next(&iterator)) {
						fail("Integrity check failed (12)", "false");
					}
				}
			}
			if (rtree_iterator_next(&iterator)) {
				fail("Integrity check failed (13)", "true");
			}
			coord = (i * 2 + 1) * count2;
			rtree_set2d(&rect, coord, coord, coord + j, coord + j);
			if (rtree_search(&tree, &rect, SOP_STRICT_CONTAINS, &iterator)) {
				fail("Integrity check failed (14)", "true");
			}
		}
	}

	rtree_purge(&tree);
	rtree_iterator_destroy(&iterator);
	rtree_destroy(&tree);

	footer();
}

static void
iterator_invalidate_check()
{
	header();

	const size_t test_size = 300;
	const size_t max_delete_count = 100;
	const size_t max_insert_count = 200;
	const size_t attempt_count = 100;

	struct rtree_rect rect;

	/* invalidation during deletion */
	srand(0);
	for (size_t attempt = 0; attempt < attempt_count; attempt++) {
		size_t del_pos = rand() % test_size;
		size_t del_cnt = rand() % max_delete_count + 1;
		if (del_pos + del_cnt > test_size) {
			del_cnt = test_size - del_pos;
		}
		struct rtree tree;
		rtree_init(&tree, 2, extent_size,
			   extent_alloc, extent_free, &extent_count,
			   RTREE_EUCLID);
		struct rtree_iterator iterators[test_size];
		for (size_t i = 0; i < test_size; i++)
			rtree_iterator_init(iterators + i);

		for (size_t i = 0; i < test_size; i++) {
			rtree_set2d(&rect, i, i, i, i);
			rtree_insert(&tree, &rect, record_t(i+1));
		}
		rtree_set2d(&rect, 0, 0, test_size, test_size);
		if (!rtree_search(&tree, &rect, SOP_BELONGS, &iterators[0]) ||
		    !rtree_iterator_next(&iterators[0])) {
			fail("Integrity check failed (15)", "false");
		}
		for (size_t i = 1; i < test_size; i++) {
			iterators[i] = iterators[i - 1];
			if (!rtree_iterator_next(&iterators[i])) {
				fail("Integrity check failed (16)", "false");
			}
		}
		for (size_t i = del_pos; i < del_pos + del_cnt; i++) {
			rtree_set2d(&rect, i, i, i, i);
			if (!rtree_remove(&tree, &rect, record_t(i+1))) {
				fail("Integrity check failed (17)", "false");
			}
		}
		for (size_t i = 0; i < test_size; i++) {
			if (rtree_iterator_next(&iterators[i])) {
				fail("Iterator was not invalidated (18)", "true");
			}
		}

		for (size_t i = 0; i < test_size; i++)
			rtree_iterator_destroy(iterators + i);
		rtree_destroy(&tree);
	}

	/* invalidation during insertion */
	srand(0);
	for (size_t attempt = 0; attempt < attempt_count; attempt++) {
		size_t ins_pos = rand() % test_size;
		size_t ins_cnt = rand() % max_insert_count + 1;

		struct rtree tree;
		rtree_init(&tree, 2, extent_size,
			   extent_alloc, extent_free, &extent_count,
			   RTREE_EUCLID);
		struct rtree_iterator iterators[test_size];
		for (size_t i = 0; i < test_size; i++)
			rtree_iterator_init(iterators + i);

		for (size_t i = 0; i < test_size; i++) {
			rtree_set2d(&rect, i, i, i, i);
			rtree_insert(&tree, &rect, record_t(i+1));
		}
		rtree_set2d(&rect, 0, 0, test_size, test_size);
		/*
		 * We don't care if rtree_search() does or does not find
		 * anything as far as we are fine anyway: iterator is
		 * initialized correctly and the case where nothing is found
		 * and rtree_iterator_next() returns NULL will be processed
		 * correctly as fail case in check #19.
		 */
		rtree_search(&tree, &rect, SOP_BELONGS, &iterators[0]);
		if (!rtree_iterator_next(&iterators[0])) {
			fail("Integrity check failed (19)", "false");
		}
		for (size_t i = 1; i < test_size; i++) {
			iterators[i] = iterators[i - 1];
			if (!rtree_iterator_next(&iterators[0])) {
				fail("Integrity check failed (20)", "false");
			}
		}
		for (size_t i = ins_pos; i < ins_pos + ins_cnt; i++) {
			rtree_set2d(&rect, i, i, i, i);
			rtree_insert(&tree, &rect, record_t(test_size + i - ins_pos + 1));
		}
		for (size_t i = 0; i < test_size; i++) {
			if (rtree_iterator_next(&iterators[i])) {
				fail("Iterator was not invalidated (22)", "true");
			}
		}

		for (size_t i = 0; i < test_size; i++)
			rtree_iterator_destroy(iterators + i);
		rtree_destroy(&tree);
	}

	footer();
}

int
main(void)
{
	iterator_check();
	iterator_invalidate_check();
	if (extent_count != 0) {
		fail("memory leak!", "false");
	}
}
