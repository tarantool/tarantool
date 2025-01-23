#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>

/* Select the tree flavor to test. */
#if defined(TEST_INNER_CARD)
# define BPS_INNER_CARD
#elif defined(TEST_TIME_SERIES_OPTIMIZED)
# define BPS_TIME_SERIES_OPTIMIZED
#elif defined(TEST_INNER_CHILD_CARDS)
# define BPS_INNER_CHILD_CARDS
#elif !defined(TEST_DEFAULT)
# error "Please define TEST_DEFAULT, TEST_INNER_CARD or TEST_INNER_CHILD_CARDS."
#endif

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

struct elem_t {
	long first;
	long second;
};

static bool equal(const elem_t &a, const elem_t &b);
static int compare(const elem_t &a, const elem_t &b);
static int compare_key(const elem_t &a, long b);

#define BPS_TREE_NAME test
/**
 * On COW matras make a copy of extent while API requires only copy a block.
 * So bps tree may miss COW requests for its block but the block is copied
 * accidentally and the test passes. To avoid this issue let's make extent and
 * block the same size.
 */
#define BPS_TREE_BLOCK_SIZE 256
#define BPS_TREE_EXTENT_SIZE 256
#define BPS_TREE_IS_IDENTICAL(a, b) equal(a, b)
#define BPS_TREE_COMPARE(a, b, arg) compare(a, b)
#define BPS_TREE_COMPARE_KEY(a, b, arg) compare_key(a, b)
#define bps_tree_elem_t struct elem_t
#define bps_tree_key_t long
#define bps_tree_arg_t int
#include "salad/bps_tree.h"

static bool
equal(const elem_t &a, const elem_t &b)
{
	return a.first == b.first && a.second == b.second;
}

static int compare(const elem_t &a, const elem_t &b)
{
	return a.first < b.first ? -1 : a.first > b.first ? 1 :
	       a.second < b.second ? -1 : a.second > b.second ? 1 : 0;
}

static int compare_key(const elem_t &a, long b)
{
	return a.first < b ? -1 : a.first > b ? 1 : 0;
}

int total_extents_allocated = 0;

static void *
extent_alloc(struct matras_allocator *allocator)
{
	(void)allocator;
	++total_extents_allocated;
	return xmalloc(BPS_TREE_EXTENT_SIZE);
}

static void
extent_free(struct matras_allocator *allocator, void *extent)
{
	(void)allocator;
	--total_extents_allocated;
	free(extent);
}

struct matras_allocator allocator;

static void
iterator_check()
{
	plan(7);
	header();

	test tree;
	test_create(&tree, 0, &allocator, NULL);

	{
		test_iterator tmp1, tmp2;
		tmp1 = test_invalid_iterator();
		tmp2 = test_invalid_iterator();
		ok(test_iterator_is_invalid(&tmp1),
		   "invalid iterator is invalid");
		ok(test_iterator_is_equal(&tree, &tmp1, &tmp2),
		   "invalid iterators are equal");
	}

	const long count1 = 2000;
	const long count2 = 5;
	for (long i = 0; i < count1; i++) {
		struct elem_t e;
		e.first = i * 2; /* note that filled with even numbers */
		for (long j = 0; j < count2; j++) {
			e.second = j;
			test_insert(&tree, e, 0, 0);
		}
	}
	for (long i = 0; i < count1 * count2; i++) {
		long key = i % count1;
		if (test_find(&tree, key * 2) == 0)
			fail("Integrity check failed (1)", "true");
		if (test_find(&tree, key * 2 + 1) != 0)
			fail("Integrity check failed (2)", "true");
	}
	ok(test_debug_check(&tree) == 0, "the test tree is valid");

	{
		test_iterator iterator = test_first(&tree);
		for (long i = 0; i < count1 * count2 * 5; i++) {
			elem_t *elem = test_iterator_get_elem(&tree, &iterator);
			if (elem->first != ((i % (count1 * count2)) / count2) * 2)
				fail("iterate all failed (1)", "true");
			if (elem->second != i % count2)
				fail("iterate all failed (2)", "true");
			bool iterator_res = test_iterator_next(&tree, &iterator);
			if (!!iterator_res == !!test_iterator_is_invalid(&iterator))
				fail("iterate all failed (3)", "true");
			if (!iterator_res) {
				iterator_res = test_iterator_next(&tree, &iterator);
				if (!iterator_res || test_iterator_is_invalid(&iterator))
					fail("iterate all failed (4)", "true");
			}
		}
	}
	ok(true, "iteration forward all elements 5 times");

	{
		test_iterator iterator = test_last(&tree);
		for (long i = 0; i < count1 * count2 * 5; i++) {
			elem_t *elem = test_iterator_get_elem(&tree, &iterator);
			long j = count1 * count2 - 1 - (i % (count1 * count2));
			if (elem->first != (j / count2) * 2)
				fail("iterate all failed (5)", "true");
			if (elem->second != j % count2)
				fail("iterate all failed (6)", "true");
			bool iterator_res = test_iterator_prev(&tree, &iterator);
			if (!!iterator_res == !!test_iterator_is_invalid(&iterator))
				fail("iterate all failed (7)", "true");
			if (!iterator_res) {
				iterator_res = test_iterator_prev(&tree, &iterator);
				if (!iterator_res || test_iterator_is_invalid(&iterator))
					fail("iterate all failed (8)", "true");
			}
		}
	}
	ok(true, "iteration backward all elements 5 times");

	for (long i = -1; i <= count1 + 1; i++) {
		test_iterator begin = test_lower_bound(&tree, i, 0);
		test_iterator end = test_upper_bound(&tree, i, 0);
		test_iterator runner = begin;
		long real_count = 0;
		while (!test_iterator_is_equal(&tree, &runner, &end)) {
			elem_t *elem = test_iterator_get_elem(&tree, &runner);
			if (elem->first != i)
				fail("range iterator failed (1)", "true");
			if (elem->second != real_count)
				fail("range iterator failed (2)", "true");
			real_count++;
			test_iterator_next(&tree, &runner);
		}
		long back_count = real_count - 1;
		while (!test_iterator_is_equal(&tree, &runner, &begin)) {
			test_iterator_prev(&tree, &runner);
			elem_t *elem = test_iterator_get_elem(&tree, &runner);
			if (elem->first != i)
				fail("range iterator failed (1)", "true");
			if (elem->second != back_count)
				fail("range iterator failed (2)", "true");
			back_count--;
		}
		long must_be_count = 0;
		if (i >= 0 && i / 2 <= count1 - 1 && (i & 1) == 0)
			must_be_count = count2;
		if (real_count != must_be_count)
			fail("range iterator failed (3)", "true");
	}
	ok(true, "iteration in range from lower bound to upper bound");

	for (long i = -1; i <= count1 + 1; i++) {
		struct elem_t lower_elem_key = {i, 0};
		struct elem_t upper_elem_key = {i, LONG_MAX};
		test_iterator begin = test_lower_bound_elem(&tree, lower_elem_key, 0);
		test_iterator end = test_upper_bound_elem(&tree, upper_elem_key, 0);
		test_iterator runner = begin;
		long real_count = 0;
		while (!test_iterator_is_equal(&tree, &runner, &end)) {
			elem_t *elem = test_iterator_get_elem(&tree, &runner);
			if (elem->first != i)
				fail("range iterator failed (1)", "true");
			if (elem->second != real_count)
				fail("range iterator failed (2)", "true");
			real_count++;
			test_iterator_next(&tree, &runner);
		}
		long back_count = real_count - 1;
		while (!test_iterator_is_equal(&tree, &runner, &begin)) {
			test_iterator_prev(&tree, &runner);
			elem_t *elem = test_iterator_get_elem(&tree, &runner);
			if (elem->first != i)
				fail("range iterator failed (1)", "true");
			if (elem->second != back_count)
				fail("range iterator failed (2)", "true");
			back_count--;
		}
		long must_be_count = 0;
		if (i >= 0 && i / 2 <= count1 - 1 && (i & 1) == 0)
			must_be_count = count2;
		if (real_count != must_be_count)
			fail("range iterator failed (3)", "true");
	}
	ok(true, "iteration in range from lower bound to upper bound");

	test_destroy(&tree);

	footer();
	check_plan();
}

static void
iterator_invalidate_check()
{
	plan(3);
	header();

	const long test_size = 300;
	const long max_delete_count = 100;
	const long max_insert_count = 200;
	const long attempt_count = 100;
	struct test_iterator iterators[test_size];

	struct test tree;

	srand(0);
	for (long attempt = 0; attempt < attempt_count; attempt++) {
		long del_pos = rand() % test_size;
		long del_cnt = rand() % max_delete_count + 1;
		if (del_pos + del_cnt > test_size)
			del_cnt = test_size - del_pos;
		test_create(&tree, 0, &allocator, NULL);

		for (long i = 0; i < test_size; i++) {
			elem_t e;
			e.first = i * test_size * 2;
			e.second = i * test_size * 2;
			test_insert(&tree, e, 0, 0);
		}
		iterators[0] = test_first(&tree);
		assert(test_iterator_get_elem(&tree, iterators));
		for (long i = 1; i < test_size; i++) {
			iterators[i] = iterators[i - 1];
			test_iterator_next(&tree, iterators + i);
			assert(test_iterator_get_elem(&tree, iterators + i));
		}
		for (long i = del_pos; i < del_pos + del_cnt; i++) {
			elem_t e;
			e.first = i * test_size * 2;
			e.second = i * test_size * 2;
			elem_t deleted;
			deleted.first = LONG_MAX;
			deleted.second = LONG_MAX;
			fail_unless(test_delete(&tree, e, &deleted) == 0);
			fail_unless(deleted.first == e.first);
			fail_unless(deleted.second == e.second);
		}
		for (long i = 0; i < test_size; i++) {
			do {
				elem_t *e = test_iterator_get_elem(&tree, iterators + i);
				if (e) {
					if (e->first != e->second)
						fail("unexpected result of getting elem (1)", "true");
					if (e->first % (test_size * 2))
						fail("unexpected result of getting elem (2)", "true");
					long v = e->first / (test_size * 2);
					if ( (v < 0 || v >= del_pos) && (v < del_pos + del_cnt || v >= test_size) )
						fail("unexpected result of getting elem (3)", "true");
				}
			} while(test_iterator_next(&tree, iterators + i));
		}
		test_destroy(&tree);
	}
	ok(true, "invalidation during deletion");

	srand(0);
	for (long attempt = 0; attempt < attempt_count; attempt++) {
		long ins_pos = rand() % test_size;
		long ins_cnt = rand() % max_insert_count + 1;
		test_create(&tree, 0, &allocator, NULL);

		for (long i = 0; i < test_size; i++) {
			elem_t e;
			e.first = i * test_size * 2;
			e.second = i * test_size * 2;
			test_insert(&tree, e, 0, 0);
		}
		iterators[0] = test_first(&tree);
		assert(test_iterator_get_elem(&tree, iterators));
		for (long i = 1; i < test_size; i++) {
			iterators[i] = iterators[i - 1];
			test_iterator_next(&tree, iterators + i);
			assert(test_iterator_get_elem(&tree, iterators + i));
		}
		for (long i = 0; i < ins_cnt; i++) {
			elem_t e;
			e.first = ins_pos * test_size * 2 + i + 1;
			e.second = e.first;
			int res = test_insert(&tree, e, 0, 0);
			assert(res == 0);
		}
		for (long i = 0; i < test_size; i++) {
			do {
				elem_t *e = test_iterator_get_elem(&tree, iterators + i);
				if (e) {
					if (e->first != e->second)
						fail("unexpected result of getting elem (4)", "true");
					if (e->first % (test_size * 2)) {
						long v = e->first / (test_size * 2);
						long u = e->first % (test_size * 2);
						if (v != ins_pos)
							fail("unexpected result of getting elem (5)", "true");
						if (u <= 0 || u > ins_cnt)
							fail("unexpected result of getting elem (6)", "true");
					} else {
						long v = e->first / (test_size * 2);
						if ( (v < 0 || v >= test_size) )
							fail("unexpected result of getting elem (7)", "true");
					}
				}
			} while(test_iterator_next(&tree, iterators + i));
		}
		test_destroy(&tree);
	}
	ok(true, "invalidation during insertion");

	srand(0);
	for (long attempt = 0; attempt < attempt_count; attempt++) {
		long del_pos = rand() % test_size;
		long del_cnt = rand() % max_delete_count + 1;
		long ins_pos = rand() % test_size;
		long ins_cnt = rand() % max_insert_count + 1;
		if (del_pos + del_cnt > test_size)
			del_cnt = test_size - del_pos;
		test_create(&tree, 0, &allocator, NULL);

		for (long i = 0; i < test_size; i++) {
			elem_t e;
			e.first = i * test_size * 2;
			e.second = i * test_size * 2;
			test_insert(&tree, e, 0, 0);
		}
		iterators[0] = test_first(&tree);
		assert(test_iterator_get_elem(&tree, iterators));
		for (long i = 1; i < test_size; i++) {
			iterators[i] = iterators[i - 1];
			test_iterator_next(&tree, iterators + i);
			assert(test_iterator_get_elem(&tree, iterators + i));
		}
		for (long i = del_pos; i < del_pos + del_cnt; i++) {
			elem_t e;
			e.first = i * test_size * 2;
			e.second = i * test_size * 2;
			elem_t deleted;
			deleted.first = LONG_MAX;
			deleted.second = LONG_MAX;
			fail_unless(test_delete(&tree, e, &deleted) == 0);
			fail_unless(deleted.first == e.first);
			fail_unless(deleted.second == e.second);
		}
		for (long i = 0; i < ins_cnt; i++) {
			elem_t e;
			e.first = ins_pos * test_size * 2 + i + 1;
			e.second = e.first;
			int res = test_insert(&tree, e, 0, 0);
			assert(res == 0);
		}
		for (long i = 0; i < test_size; i++) {
			do {
				elem_t *e = test_iterator_get_elem(&tree, iterators + i);
				if (e) {
					if (e->first != e->second)
						fail("unexpected result of getting elem (8)", "true");
					if (e->first % (test_size * 2)) {
						long v = e->first / (test_size * 2);
						long u = e->first % (test_size * 2);
						if (v != ins_pos)
							fail("unexpected result of getting elem (9)", "true");
						if (u <= 0 || u > ins_cnt)
							fail("unexpected result of getting elem (a)", "true");
					} else {
						long v = e->first / (test_size * 2);
						if ( (v < 0 || v >= del_pos) && (v < del_pos + del_cnt || v >= test_size) )
							fail("unexpected result of getting elem (b)", "true");
					}
				}
			} while(test_iterator_next(&tree, iterators + i));
		}
		test_destroy(&tree);
	}
	ok(true, "invalidation during deletion and insertion");

	footer();
	check_plan();
}

static void
iterator_freeze_check()
{
	const int test_rounds_size = 10;
	const int test_data_size = 1000;
	elem_t comp_buf1[test_data_size];
	elem_t comp_buf2[test_data_size];
	const int test_data_mod = 2000;
	srand(0);
	struct test tree;

	for (int i = 0; i < 10; i++) {
		test_create(&tree, 0, &allocator, NULL);
		int comp_buf_size1 = 0;
		int comp_buf_size2 = 0;
		for (int j = 0; j < test_data_size; j++) {
			elem_t e;
			e.first = rand() % test_data_mod;
			e.second = 0;
			test_insert(&tree, e, 0, 0);
			int check = test_debug_check(&tree);
			fail_if(check);
			assert(check == 0);
		}
		struct test_iterator iterator = test_first(&tree);
		elem_t *e;
		while ((e = test_iterator_get_elem(&tree, &iterator))) {
			comp_buf1[comp_buf_size1++] = *e;
			test_iterator_next(&tree, &iterator);
		}
		struct test_view view1;
		test_view_create(&view1, &tree);
		struct test_iterator iterator1 = test_view_first(&view1);
		struct test_view view2;
		test_view_create(&view2, &tree);
		struct test_iterator iterator2 = test_view_first(&view2);
		for (int j = 0; j < test_data_size; j++) {
			elem_t e;
			e.first = rand() % test_data_mod;
			e.second = 0;
			test_insert(&tree, e, 0, 0);
			fail_if(test_debug_check(&tree));
			fail_if(test_view_debug_check(&view1));
			fail_if(test_view_debug_check(&view2));
		}
		int tested_count = 0;
		while ((e = test_view_iterator_get_elem(&view1, &iterator1))) {
			if (!equal(*e, comp_buf1[tested_count])) {
				fail("version restore failed (1)", "true");
			}
			tested_count++;
			if (tested_count > comp_buf_size1) {
				fail("version restore failed (2)", "true");
			}
			test_view_iterator_next(&view1, &iterator1);
		}
		test_view_destroy(&view1);
		for (int j = 0; j < test_data_size; j++) {
			elem_t e;
			e.first = rand() % test_data_mod;
			e.second = 0;
			test_delete(&tree, e, NULL);
			fail_if(test_debug_check(&tree));
			fail_if(test_view_debug_check(&view2));
		}

		tested_count = 0;
		while ((e = test_view_iterator_get_elem(&view2, &iterator2))) {
			if (!equal(*e, comp_buf1[tested_count])) {
				fail("version restore failed (1)", "true");
			}
			tested_count++;
			if (tested_count > comp_buf_size1) {
				fail("version restore failed (2)", "true");
			}
			test_view_iterator_next(&view2, &iterator2);
		}
		test_view_destroy(&view2);
		test_destroy(&tree);
	}
	ok(true, "tree view iteration");
}


int
main(void)
{
	plan(4);
	header();

	matras_allocator_create(&allocator, BPS_TREE_EXTENT_SIZE,
				extent_alloc, extent_free);

	srand(time(0));
	iterator_check();
	iterator_invalidate_check();
	iterator_freeze_check();
	ok(total_extents_allocated == allocator.num_reserved_extents,
	   "leak check");

	matras_allocator_destroy(&allocator);

	footer();
	return check_plan();
}
