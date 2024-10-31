#include <set>
#include <cstdint>
#include <cstdio>
#include <cinttypes>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#include "trivia/util.h"

/*
 * The definition of a simple tree used in the tests.
 */

/* Select the tree flavor to test. */
#if defined(TEST_INNER_CARD)
# define BPS_INNER_CARD
#elif defined(TEST_INNER_CHILD_CARDS)
# define BPS_INNER_CHILD_CARDS
#else
# error "Please define TEST_INNER_CARD or TEST_INNER_CHILD_CARDS."
#endif

typedef int64_t type_t;
#define TYPE_F "%" PRId64

#define BPS_TREE_NAME test
#define BPS_TREE_BLOCK_SIZE 256
#define BPS_TREE_EXTENT_SIZE 2048
#define BPS_TREE_IS_IDENTICAL(a, b) ((a) == (b))
#define BPS_TREE_COMPARE(a, b, arg) ((a) - (b))
#define BPS_TREE_COMPARE_KEY(a, b, arg) ((a) - (b))
#define bps_tree_elem_t type_t
#define bps_tree_key_t type_t
#define bps_tree_arg_t int
#define BPS_TREE_DEBUG_BRANCH_VISIT
#include "salad/bps_tree.h"
#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_IS_IDENTICAL
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef bps_tree_elem_t
#undef bps_tree_key_t
#undef bps_tree_arg_t

/**
 * Test helpers to prevent the test code bloat. These are macros to make the
 * failed test case lines explicit.
 */

#define test_do_iterator_at(tree_arg, offset, expected) do { \
	struct test *t = (tree_arg); \
	struct test_iterator it = test_iterator_at(t, (offset)); \
	fail_unless(!test_iterator_is_invalid(&it)); \
	int64_t *e = test_iterator_get_elem(t, &it); \
	fail_unless(e != NULL); \
	fail_unless(*e == (int64_t)(expected)); \
} while (false)

#define test_do_iterator_at_invalid(tree, offset) do { \
	struct test_iterator it = test_iterator_at((tree), (offset)); \
	fail_unless(test_iterator_is_invalid(&it)); \
} while (false)

#define test_do_find(tree, value_arg, expected_offset) do { \
	int64_t value = (value_arg); \
	size_t offset = SIZE_MAX; \
	int64_t *e = test_find_get_offset((tree), value, &offset); \
	fail_unless(e != NULL); \
	fail_unless(*e == value); \
	fail_unless(offset == (expected_offset)); \
} while (false)

#define test_do_find_invalid(tree, value) do { \
	size_t offset; \
	fail_unless(test_find_get_offset((tree), (value), &offset) == NULL); \
} while (false)

#define test_do_bounds(tree_arg, key_arg, key_offset_arg) do { \
	struct test *t = (tree_arg); \
	int64_t key = (key_arg); \
	size_t key_offset = (key_offset_arg); \
	bool exact; \
	size_t lb, ub, lbe, ube; \
	struct test_iterator it_lb, it_ub, it_lbe, it_ube; \
	bool key_is_last = test_size(t) == key_offset + 1; \
	it_lb = test_lower_bound_get_offset(t, key, &exact, &lb); \
	it_ub = test_upper_bound_get_offset(t, key, &exact, &ub); \
	it_lbe = test_lower_bound_elem_get_offset(t, key, &exact, &lbe); \
	it_ube = test_upper_bound_elem_get_offset(t, key, &exact, &ube); \
	fail_unless(!test_iterator_is_invalid(&it_lb)); \
	fail_unless(!test_iterator_is_invalid(&it_ub) || key_is_last); \
	fail_unless(!test_iterator_is_invalid(&it_lbe)); \
	fail_unless(!test_iterator_is_invalid(&it_ube) || key_is_last); \
	fail_unless(test_iterator_is_equal(t, &it_lb, &it_lbe)); \
	fail_unless(test_iterator_is_equal(t, &it_ub, &it_ube)); \
	fail_unless(lb == (size_t)key_offset); \
	fail_unless(ub == (size_t)key_offset + exact); \
	fail_unless(lbe == lb); \
	fail_unless(ube == ub); \
} while (false)

#define test_do_bounds_invalid(tree_arg, key_arg) do { \
	struct test *t = (tree_arg); \
	int64_t key = (key_arg); \
	bool _; /* Unused stub. */ \
	size_t lb = SIZE_MAX, ub = SIZE_MAX, lbe = SIZE_MAX, ube = SIZE_MAX; \
	struct test_iterator it_lb, it_ub, it_lbe, it_ube; \
	size_t tree_size = test_size(t); \
	it_lb = test_lower_bound_get_offset(t, key, &_, &lb); \
	it_ub = test_upper_bound_get_offset(t, key, &_, &ub); \
	it_lbe = test_lower_bound_elem_get_offset(t, key, &_, &lbe); \
	it_ube = test_upper_bound_elem_get_offset(t, key, &_, &ube); \
	fail_unless(test_iterator_is_invalid(&it_lb)); \
	fail_unless(test_iterator_is_invalid(&it_ub)); \
	fail_unless(test_iterator_is_invalid(&it_lbe)); \
	fail_unless(test_iterator_is_invalid(&it_ube)); \
	fail_unless(lb == tree_size); \
	fail_unless(ub == tree_size); \
	fail_unless(lbe == tree_size); \
	fail_unless(ube == tree_size); \
} while (false)

#define insert_and_check(tree_arg, value_arg, expected_pos_arg) do { \
	struct test *t = (tree_arg); \
	int64_t value = (value_arg); \
	size_t expected_pos = (expected_pos_arg); \
	size_t pos; \
	fail_unless(test_find(t, value) == NULL); \
	fail_unless(test_insert_get_offset(t, value, 0, &pos) == 0); \
	fail_unless(pos == (expected_pos)); \
	int64_t *found = test_find(t, value); \
	fail_unless(found != NULL); \
	fail_unless(test_find_get_offset(t, value, &pos) == found); \
	fail_unless(pos == (expected_pos)); \
	int result = test_debug_check(t); \
	if (result) { \
		test_print(t, TYPE_F); \
		note("debug check returned %08x", result); \
		fail("debug check nonzero", "true"); \
	} \
} while (false)

#define delete_and_check(tree_arg, value_arg, expected_pos_arg) do { \
	struct test *t = (tree_arg); \
	int64_t value = (value_arg); \
	size_t expected_pos = (expected_pos_arg); \
	size_t pos = SIZE_MAX; \
	fail_unless(test_find(t, value) != NULL); \
	fail_unless(test_delete_get_offset(t, value, &pos) == 0); \
	fail_unless(pos == (expected_pos)); \
	fail_unless(test_find(t, value) == NULL); \
	int result = test_debug_check(t); \
	if (result) { \
		test_print(t, TYPE_F); \
		note("debug check returned %08x", result); \
		fail("debug check nonzero", "true"); \
	} \
} while (false)

/**
 * Utility functions.
 */

static int extent_count = 0;

static void *
extent_alloc(struct matras_allocator *allocator)
{
	(void)allocator;
	++extent_count;
	return xmalloc(BPS_TREE_EXTENT_SIZE);
}

static void
extent_free(struct matras_allocator *allocator, void *extent)
{
	(void)allocator;
	--extent_count;
	free(extent);
}

struct matras_allocator allocator;

static uint32_t
rng()
{
	static uint32_t state = 1;
	return state = (uint64_t)state * 48271 % 0x7fffffff;
}

static void
shuffle(int64_t *arr, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		size_t j = rng() % (size - i);
		int64_t tmp = arr[i];
		arr[i] = arr[i + j];
		arr[i + j] = tmp;
	}
}

static int64_t *
arr_seq(size_t size)
{
	int64_t *arr = (int64_t *)xcalloc(size, sizeof(*arr));
	for (size_t i = 0; i < size; i++)
		arr[i] = i;
	return arr;
}

static int64_t *
arr_seq_shuffled(size_t size)
{
	int64_t *arr = arr_seq(size);
	shuffle(arr, size);
	return arr;
}

/**
 * The offset-based API tests.
 */

static void
iterator_at()
{
	plan(3);
	header();

	const size_t count = 1000;
	int64_t *rand_i = arr_seq_shuffled(count);
	struct test tree;
	std::set<int64_t> set;

	test_create(&tree, 0, &allocator, NULL);
	test_do_iterator_at_invalid(&tree, 0);
	test_do_iterator_at_invalid(&tree, 37);
	test_do_iterator_at_invalid(&tree, SIZE_MAX);
	test_destroy(&tree);
	ok(true, "Iterator at on an empty tree");

	test_create(&tree, 0, &allocator, NULL);
	for (size_t i = 0; i < count; i++) {
		fail_unless(test_insert(&tree, i, NULL, NULL) == 0);
		for (size_t j = 0; j <= i; j++)
			test_do_iterator_at(&tree, j, j);
		for (size_t j = i + 1; j < count * 2; j++)
			test_do_iterator_at_invalid(&tree, j);
	}
	test_destroy(&tree);
	ok(true, "Iterator at on sequential insertion");

	test_create(&tree, 0, &allocator, NULL);
	for (size_t i = 0; i < count; i++) {
		int64_t v = rand_i[i];
		fail_unless(test_insert(&tree, v, NULL, NULL) == 0);
		set.insert(v);
		size_t expected_offset = 0;
		for (auto sit = set.begin(); sit != set.end(); sit++)
			test_do_iterator_at(&tree, expected_offset++, *sit);
		fail_unless(set.size() == i + 1);
		for (size_t j = i + 1; j < count * 2; j++)
			test_do_iterator_at_invalid(&tree, j);
	}
	test_destroy(&tree);
	free(rand_i);
	ok(true, "Iterator at on random insertion");

	footer();
	check_plan();
}

static void
find_get_offset()
{
	plan(3);
	header();

	const size_t count = 1000;
	int64_t *rand_i = arr_seq_shuffled(count);
	struct test tree;
	std::set<int64_t> set;

	test_create(&tree, 0, &allocator, NULL);
	test_do_find_invalid(&tree, 0);
	test_do_find_invalid(&tree, -1);
	test_do_find_invalid(&tree, 37);
	test_do_find_invalid(&tree, INT64_MAX);
	test_do_find_invalid(&tree, INT64_MIN);
	test_destroy(&tree);
	ok(true, "Find in an empty tree");

	test_create(&tree, 0, &allocator, NULL);
	for (size_t i = 0; i < count; i++) {
		test_insert(&tree, i, NULL, NULL);
		for (size_t j = 0; j <= i; j++)
			test_do_find(&tree, j, j);
		for (size_t j = i + 1; j < count * 2; j++)
			test_do_find_invalid(&tree, j);
	}
	test_destroy(&tree);
	ok(true, "Find on sequential insertion");

	test_create(&tree, 0, &allocator, NULL);
	for (size_t i = 0; i < count; i++) {
		int64_t v = rand_i[i];
		fail_unless(test_insert(&tree, v, NULL, NULL) == 0);
		set.insert(v);
		size_t expected_offset = 0;
		for (auto sit = set.begin(); sit != set.end(); sit++)
			test_do_find(&tree, *sit, expected_offset++);
		fail_unless(set.size() == i + 1);
		for (size_t j = i + 1; j < count; j++)
			test_do_find_invalid(&tree, rand_i[j]);
		for (size_t j = count; j < count * 2; j++)
			test_do_find_invalid(&tree, j);
	}
	test_destroy(&tree);
	free(rand_i);
	ok(true, "Find on random insertion");

	footer();
	check_plan();
}

static void
bounds_get_offset()
{
	plan(3);
	header();

	const int count = 1000;
	int64_t *rand_i = arr_seq_shuffled(count);
	struct test tree;
	std::set<int64_t> set;

	test_create(&tree, 0, &allocator, NULL);
	test_do_bounds_invalid(&tree, 0);
	test_do_bounds_invalid(&tree, -1);
	test_do_bounds_invalid(&tree, 37);
	test_do_bounds_invalid(&tree, INT64_MAX);
	test_do_bounds_invalid(&tree, INT64_MIN);
	test_destroy(&tree);
	ok(true, "Upper & lower bound on an empty tree");

	test_create(&tree, 0, &allocator, NULL);
	for (int i = 0; i < count; i++) {
		fail_unless(test_insert(&tree, i, NULL, NULL) == 0);
		for (int j = 0; j <= i; j++)
			test_do_bounds(&tree, j, j);
		for (int j = i + 1; j < count * 2; j++)
			test_do_bounds_invalid(&tree, j);
	}
	test_destroy(&tree);
	ok(true, "Upper & lower bound on sequential insertion");

	test_create(&tree, 0, &allocator, NULL);
	for (int i = 0; i < count; i++) {
		int64_t v = rand_i[i];
		fail_unless(test_insert(&tree, v, NULL, NULL) == 0);
		set.insert(v);
		int j = 0;
		size_t expected_offset = 0;
		for (auto sit = set.begin(); sit != set.end(); sit++, j++) {
			while (j < *sit)
				test_do_bounds(&tree, j++, expected_offset);
			test_do_bounds(&tree, *sit, expected_offset++);
		}
		while (j < count * 2)
			test_do_bounds_invalid(&tree, j++);
	}
	test_destroy(&tree);
	ok(true, "Upper & lower bound on random insertion");

	free(rand_i);

	footer();
	check_plan();
}

static void
insert_delete_get_offset()
{
	plan(4);
	header();

	fail_unless(extent_count == 0);

	struct matras_stats stats;
	matras_stats_create(&stats);
	stats.extent_count = extent_count;

	const int count = 2000;
	struct test tree;
	test_create(&tree, 0, &allocator, &stats);

	for (int i = 0; i < count; i++)
		insert_and_check(&tree, i, (size_t)i);
	fail_unless(test_size(&tree) == (size_t)count);
	fail_unless((int)stats.extent_count == extent_count);
	for (int i = 0; i < count; i++)
		delete_and_check(&tree, i, 0);
	fail_unless(test_size(&tree) == 0);
	fail_unless((int)stats.extent_count == extent_count);
	ok(true, "Insert 1..X, delete 1..X");

	for (int i = 0; i < count; i++)
		insert_and_check(&tree, i, (size_t)i);
	fail_unless(test_size(&tree) == (size_t)count);
	fail_unless((int)stats.extent_count == extent_count);
	for (int i = count - 1; i >= 0; i--)
		delete_and_check(&tree, i, (size_t)i);
	fail_unless(test_size(&tree) == 0);
	fail_unless((int)stats.extent_count == extent_count);
	ok(true, "Insert 1..X, delete X..1");

	for (int i = count - 1; i >= 0; i--)
		insert_and_check(&tree, i, 0);
	fail_unless(test_size(&tree) == (size_t)count);
	fail_unless((int)stats.extent_count == extent_count);
	for (int i = 0; i < count; i++)
		delete_and_check(&tree, i, 0);
	fail_unless(test_size(&tree) == 0);
	fail_unless((int)stats.extent_count == extent_count);
	ok(true, "Insert X..1, delete 1..X");

	for (int i = count - 1; i >= 0; i--)
		insert_and_check(&tree, i, 0);
	fail_unless(test_size(&tree) == (size_t)count);
	fail_unless((int)stats.extent_count == extent_count);
	for (int i = count - 1; i >= 0; i--)
		delete_and_check(&tree, i, (size_t)i);
	fail_unless(test_size(&tree) == 0);
	fail_unless((int)stats.extent_count == extent_count);
	ok(true, "Insert X..1, delete X..1");

	test_destroy(&tree);
	fail_unless(extent_count == 0);

	footer();
	check_plan();
}

int
main(void)
{
	plan(4);
	header();

	matras_allocator_create(&allocator, BPS_TREE_EXTENT_SIZE,
				extent_alloc, extent_free);

	iterator_at();
	find_get_offset();
	bounds_get_offset();
	insert_delete_get_offset();

	matras_allocator_destroy(&allocator);

	footer();
	return check_plan();
}
