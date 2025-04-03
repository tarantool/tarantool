#include <cstdint>
#include <cstdio>
#include <cinttypes>
#include <ctime>
#include <climits>

#include "sptree.h"
#include "qsort_arg.h"
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

/* Select the tree flavor to test. */
#if defined(TEST_DEFAULT)
# define SMALL_BLOCK_SIZE 128
#elif defined(TEST_INNER_CARD)
# define BPS_INNER_CARD
# define SMALL_BLOCK_SIZE 128
#elif defined(TEST_INNER_CHILD_CARDS)
# define BPS_INNER_CHILD_CARDS
/*
 * Some branches lead to dummy rebalancing (moving data of zero size) when
 * the block size is small because of integer arithmetic on reballancing.
 * This raises assertions, because some data movement routines are designed
 * to only move non-zero amount of data. Let's make the block size greater
 * for this tree flavor to prevent this.
 */
# define SMALL_BLOCK_SIZE 256
#else
# error "Please define TEST_DEFAULT, TEST_INNER_CARD or TEST_INNER_CHILD_CARDS."
#endif

SPTREE_DEF(test, realloc, qsort_arg);

typedef int64_t type_t;
#define TYPE_F "%" PRId64

static int
compare(type_t a, type_t b);

/* check compiling with another name and settings */
#define BPS_TREE_NAME testtest
#define BPS_TREE_BLOCK_SIZE 512
#define BPS_TREE_EXTENT_SIZE 16*1024
#define BPS_TREE_IS_IDENTICAL(a, b) (a == b)
#define BPS_TREE_COMPARE(a, b, arg) compare(a, b)
#define BPS_TREE_COMPARE_KEY(a, b, arg) compare(a, b)
#define bps_tree_elem_t char
#define bps_tree_key_t char
#define bps_tree_arg_t int
#include "salad/bps_tree.h"
#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_EXTENT_SIZE
#undef BPS_TREE_IS_IDENTICAL
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef bps_tree_elem_t
#undef bps_tree_key_t
#undef bps_tree_arg_t

/* true tree with true settings */
#define BPS_TREE_NAME test
#define BPS_TREE_BLOCK_SIZE SMALL_BLOCK_SIZE /* small value for tests */
#define BPS_TREE_EXTENT_SIZE 2048 /* value is to low specially for tests */
#define BPS_TREE_IS_IDENTICAL(a, b) (a == b)
#define BPS_TREE_COMPARE(a, b, arg) compare(a, b)
#define BPS_TREE_COMPARE_KEY(a, b, arg) compare(a, b)
#define bps_tree_elem_t type_t
#define bps_tree_key_t type_t
#define bps_tree_arg_t int
#define BPS_TREE_DEBUG_BRANCH_VISIT
#include "salad/bps_tree.h"
#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_EXTENT_SIZE
#undef BPS_TREE_IS_IDENTICAL
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef bps_tree_elem_t
#undef bps_tree_key_t
#undef bps_tree_arg_t

struct elem_t {
	long info;
	long marker;
};

static bool
equal(const elem_t &a, const elem_t &b)
{
	return a.info == b.info && a.marker == b.marker;
}

static int compare(const elem_t &a, const elem_t &b)
{
	return a.info < b.info ? -1 : a.info > b.info ? 1 : 0;
}

static int compare_key(const elem_t &a, long b)
{
	return a.info < b ? -1 : a.info > b ? 1 : 0;
}

#define BPS_TREE_NAME struct_tree
#define BPS_TREE_BLOCK_SIZE SMALL_BLOCK_SIZE /* small value for tests */
#define BPS_TREE_EXTENT_SIZE 2048 /* value is to low specially for tests */
#define BPS_TREE_IS_IDENTICAL(a, b) equal(a, b)
#define BPS_TREE_COMPARE(a, b, arg) compare(a, b)
#define BPS_TREE_COMPARE_KEY(a, b, arg) compare_key(a, b)
#define bps_tree_elem_t struct elem_t
#define bps_tree_key_t long
#define bps_tree_arg_t int
#include "salad/bps_tree.h"
#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_EXTENT_SIZE
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef BPS_TREE_IS_IDENTICAL
#undef bps_tree_elem_t
#undef bps_tree_key_t
#undef bps_tree_arg_t

/* tree for approximate_count test */
#define BPS_TREE_NAME approx
#define BPS_TREE_BLOCK_SIZE SMALL_BLOCK_SIZE /* small value for tests */
#define BPS_TREE_EXTENT_SIZE 2048 /* value is to low specially for tests */
#define BPS_TREE_IS_IDENTICAL(a, b) (a == b)
#define BPS_TREE_COMPARE(a, b, arg) ((a) < (b) ? -1 : (a) > (b) ? 1 : 0)
#define BPS_TREE_COMPARE_KEY(a, b, arg) (((a) >> 32) < (b) ? -1 : ((a) >> 32) > (b) ? 1 : 0)
#define bps_tree_elem_t uint64_t
#define bps_tree_key_t uint32_t
#define bps_tree_arg_t int
#include "salad/bps_tree.h"

#define debug_check(tree) do { \
	int result = test_debug_check((tree)); \
	if (result) { \
		test_print((tree), TYPE_F); \
		printf("debug check = %08x", result); \
		fail("debug check nonzero", "true"); \
	} \
} while (false)

#define bps_insert_and_check(tree_name, tree, elem, replaced) \
{\
	tree_name##_iterator iter;\
	if (tree_name##_insert_get_iterator((tree), (elem), \
					    (replaced), &iter) == 0) {\
		type_t check_value =\
			*tree_name##_iterator_get_elem((tree), &iter);\
		if (check_value != (type_t)(elem)) {\
			printf("iterator doesn't point to the inserted "\
			       "element: %lld != %lld", (long long) (elem),\
			       (long long) check_value);\
			fail("elem != check_value", "true");\
		}\
	}\
	debug_check((tree));\
}

static int
node_comp(const void *p1, const void *p2, void* unused)
{
	(void)unused;
	return *((const type_t *)p1) < *((const type_t *)p2) ? -1 : *((const type_t *)p2) < *((const type_t *)p1) ? 1 : 0;
}

static int
compare(type_t a, type_t b)
{
	return a < b ? -1 : a > b ? 1 : 0;
}
static int extents_count = 0;
static bool extent_alloc_failure = false;

static void *
extent_alloc(struct matras_allocator *allocator)
{
	(void)allocator;
	if (extent_alloc_failure)
		return NULL;
	++extents_count;
	return xmalloc(BPS_TREE_EXTENT_SIZE);
}

static void
extent_free(struct matras_allocator *allocator, void *extent)
{
	(void)allocator;
	--extents_count;
	free(extent);
}

struct matras_allocator allocator;

static void
simple_check()
{
	plan(4);
	header();

	struct matras_stats stats;
	matras_stats_create(&stats);
	stats.extent_count = extents_count;

	const unsigned int rounds = 2000;
	test tree;
	test_create(&tree, 0, &allocator, &stats);

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = i;
		if (test_find(&tree, v) != NULL)
			fail("element already in tree (1)", "true");
		test_insert(&tree, v, 0, 0);
		debug_check(&tree);
	}
	if (test_size(&tree) != rounds)
		fail("Tree count mismatch (1)", "true");
	if ((int)stats.extent_count != extents_count)
		fail("Extent count mismatch (1)", "true");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = i;
		if (test_find(&tree, v) == NULL)
			fail("element in tree (1)", "false");
		test_delete(&tree, v, NULL);
		debug_check(&tree);
	}
	if (test_size(&tree) != 0)
		fail("Tree count mismatch (2)", "true");
	if ((int)stats.extent_count != extents_count)
		fail("Extent count mismatch (2)", "true");

	ok(true, "Insert 1..X, remove 1..X");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = i;
		if (test_find(&tree, v) != NULL)
			fail("element already in tree (2)", "true");
		test_insert(&tree, v, 0, 0);
		debug_check(&tree);
	}
	if (test_size(&tree) != rounds)
		fail("Tree count mismatch (3)", "true");
	if ((int)stats.extent_count != extents_count)
		fail("Extent count mismatch (3)", "true");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = rounds - 1 - i;
		if (test_find(&tree, v) == NULL)
			fail("element in tree (2)", "false");
		test_delete(&tree, v, NULL);
		debug_check(&tree);
	}
	if (test_size(&tree) != 0)
		fail("Tree count mismatch (4)", "true");
	if ((int)stats.extent_count != extents_count)
		fail("Extent count mismatch (4)", "true");

	ok(true, "Insert 1..X, remove X..1");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = rounds - 1 - i;
		if (test_find(&tree, v) != NULL)
			fail("element already in tree (3)", "true");
		test_insert(&tree, v, 0, 0);
		debug_check(&tree);
	}
	if (test_size(&tree) != rounds)
		fail("Tree count mismatch (5)", "true");
	if ((int)stats.extent_count != extents_count)
		fail("Extent count mismatch (5)", "true");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = i;
		if (test_find(&tree, v) == NULL)
			fail("element in tree (3)", "false");
		test_delete(&tree, v, NULL);
		debug_check(&tree);
	}
	if (test_size(&tree) != 0)
		fail("Tree count mismatch (6)", "true");
	if ((int)stats.extent_count != extents_count)
		fail("Extent count mismatch (6)", "true");

	ok(true, "Insert X..1, remove 1..X");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = rounds - 1 - i;
		if (test_find(&tree, v) != NULL)
			fail("element already in tree (4)", "true");
		test_insert(&tree, v, 0, 0);
		debug_check(&tree);
	}
	if (test_size(&tree) != rounds)
		fail("Tree count mismatch (7)", "true");
	if ((int)stats.extent_count != extents_count)
		fail("Extent count mismatch (7)", "true");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = rounds - 1 - i;
		if (test_find(&tree, v) == NULL)
			fail("element in tree (4)", "false");
		test_delete(&tree, v, NULL);
		debug_check(&tree);
	}
	if (test_size(&tree) != 0)
		fail("Tree count mismatch (8)", "true");
	if ((int)stats.extent_count != extents_count)
		fail("Extent count mismatch (8)", "true");

	ok(true, "Insert X..1, remove X..1");

	test_destroy(&tree);

	footer();
	check_plan();
}

static bool
check_trees_are_identical(test *tree, sptree_test *spt_test)
{
	if (test_size(tree) != spt_test->size)
		return false;
	int n = test_size(tree);
	test_iterator iterator = test_first(tree);
	sptree_test_iterator *spitr = sptree_test_iterator_init(spt_test);
	for (int i = 0; i < n; i++) {
		type_t v1 = *test_iterator_get_elem(tree, &iterator);
		type_t v2 = *(type_t *)sptree_test_iterator_next(spitr);
		test_iterator_next(tree, &iterator);
		if (v1 != v2) {
			sptree_test_iterator_free(spitr);
			return false;
		}
	}
	sptree_test_iterator_free(spitr);
	return true;
}

static void
compare_with_sptree_check()
{
	sptree_test spt_test;
	sptree_test_init(&spt_test, sizeof(type_t), 0, 0, 0, &node_comp, 0, 0);

	test tree;
	test_create(&tree, 0, &allocator, NULL);

	const int rounds = 16 * 1024;
	const int elem_limit = 	1024;

	for (int i = 0; i < rounds; i++) {
		type_t rnd = rand() % elem_limit;
		int find_res1 = sptree_test_find(&spt_test, &rnd) ? 1 : 0;
		int find_res2 = test_find(&tree, rnd) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");

		if (find_res1 == 0) {
			sptree_test_replace(&spt_test, &rnd, NULL);
			test_insert(&tree, rnd, 0, 0);
		} else {
			sptree_test_delete(&spt_test, &rnd);
			test_delete(&tree, rnd, NULL);
		}

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}
	sptree_test_destroy(&spt_test);

	test_destroy(&tree);

	ok(true, "compare with sptree");
}

static void
compare_with_sptree_check_branches()
{
	plan(4);
	header();

	sptree_test spt_test;
	sptree_test_init(&spt_test, sizeof(type_t), 0, 0, 0, &node_comp, 0, 0);

	test tree;
	test_create(&tree, 0, &allocator, NULL);

	const int elem_limit = 2048;

	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)i;
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (find_res1) {
			fail("trees integrity", "false");
		}

		sptree_test_replace(&spt_test, &v, NULL);
		test_insert(&tree, v, 0, 0);

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}
	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)i;
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (!find_res1)
			fail("trees integrity", "false");

		sptree_test_delete(&spt_test, &v);
		test_delete(&tree, v, NULL);

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}

	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)(elem_limit - i - 1);
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (find_res1)
			fail("trees integrity", "false");

		sptree_test_replace(&spt_test, &v, NULL);
		test_insert(&tree, v, 0, 0);

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}
	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)(elem_limit - i - 1);
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (!find_res1)
			fail("trees integrity", "false");

		sptree_test_delete(&spt_test, &v);
		test_delete(&tree, v, NULL);

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}

	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)i;
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (find_res1)
			fail("trees integrity", "false");

		sptree_test_replace(&spt_test, &v, NULL);
		test_insert(&tree, v, 0, 0);

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}
	for (int i = 0; i < elem_limit; i++) {
		type_t v;
		if (i & 1)
			v = (type_t)(elem_limit / 2 + i / 2);
		else
			v = (type_t)(elem_limit / 2 - i / 2 - 1);
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (!find_res1)
			fail("trees integrity", "false");

		sptree_test_delete(&spt_test, &v);
		test_delete(&tree, v, NULL);

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}

	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)i;
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (find_res1)
			fail("trees integrity", "false");

		sptree_test_replace(&spt_test, &v, NULL);
		test_insert(&tree, v, 0, 0);

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}
	for (int i = 0; i < elem_limit; i++) {
		type_t v;
		if (i & 1)
			v = (type_t)(i / 2);
		else
			v = (type_t)(elem_limit - i / 2 - 1);
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (!find_res1)
			fail("trees integrity", "false");

		sptree_test_delete(&spt_test, &v);
		test_delete(&tree, v, NULL);

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}

	for (int i = 0; i < elem_limit; i++) {
		type_t v;
		if (i < elem_limit / 2)
			v = (type_t)(i * 2);
		else
			v = (type_t)((i - elem_limit / 2) * 2 + 1);
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (find_res1)
			fail("trees integrity", "false");

		sptree_test_replace(&spt_test, &v, NULL);
		test_insert(&tree, v, 0, 0);

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}
	for (int i = 0; i < elem_limit; i++) {
		type_t v;
		if (i < elem_limit / 2)
			v = (type_t)(i * 2);
		else
			v = (type_t)((i - elem_limit / 2) * 2 + 1);
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (!find_res1)
			fail("trees integrity", "false");

		sptree_test_delete(&spt_test, &v);
		test_delete(&tree, v, NULL);

		debug_check(&tree);

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}

	is(tree.common.debug_insert_leaf_branches_mask,
	   tree.common.debug_insert_leaf_branches_max_mask,
	   "all insert leaf branches tested");
	is(tree.common.debug_insert_inner_branches_mask,
	   tree.common.debug_insert_inner_branches_max_mask,
	   "all insert inner branches tested");
	is(tree.common.debug_delete_leaf_branches_mask,
	   tree.common.debug_delete_leaf_branches_max_mask,
	   "all delete leaf branches tested");
	is(tree.common.debug_delete_inner_branches_mask,
	   tree.common.debug_delete_inner_branches_max_mask,
	   "all delete inner branches tested");

	sptree_test_destroy(&spt_test);

	test_destroy(&tree);

	footer();
	check_plan();
}

static void
bps_tree_debug_self_check()
{
	int res = test_debug_check_internal_functions(false);
	if (res) {
		printf("self test returned error %d\n", res);
		test_debug_check_internal_functions(true);
	}
	ok(res == 0, "debug self-check");
}

static void
loading_test()
{
	test tree;

	const type_t test_count = 1000;
	type_t arr[test_count];
	for (type_t i = 0; i < test_count; i++)
		arr[i] = i;

	for (type_t i = 0; i <= test_count; i++) {
		test_create(&tree, 0, &allocator, NULL);

		if (test_build(&tree, arr, i))
			fail("building failed", "true");

		debug_check(&tree);

		struct test_iterator iterator;
		iterator = test_first(&tree);
		for (type_t j = 0; j < i; j++) {
			type_t *v = test_iterator_get_elem(&tree, &iterator);
			if (!v || *v != j)
				fail("wrong build result", "true");
			test_iterator_next(&tree, &iterator);
		}
		if (!test_iterator_is_invalid(&iterator))
			fail("wrong build result", "true");

		test_destroy(&tree);
	}

	ok(true, "loading test");
}

static void
printing_test()
{
	test tree;
	test_create(&tree, 0, &allocator, NULL);
	const type_t rounds = 22;
	for (type_t i = 0; i < rounds; i++) {
		type_t v = rounds + i;
		note("Inserting " TYPE_F "\n", v);
		test_insert(&tree, v, 0, 0);
		test_print(&tree, TYPE_F);
		v = rounds - i - 1;
		note("Inserting " TYPE_F "\n", v);
		test_insert(&tree, v, 0, 0);
		test_print(&tree, TYPE_F);
	}
	test_destroy(&tree);
	ok(true, "printing test");
}

static void
white_box_test()
{
	plan(8);
	header();

	test tree;

	const int count_in_leaf = BPS_TREE_test_MAX_COUNT_IN_LEAF;
	const int count_in_inner = BPS_TREE_test_MAX_COUNT_IN_INNER;

	test_create(&tree, 0, &allocator, NULL);

	for (type_t i = 0; i < count_in_leaf; i++)
		test_insert(&tree, i, 0, 0);
	is(tree.common.leaf_count, 1, "full leaf");

	test_insert(&tree, count_in_leaf, 0, 0);
	is(tree.common.leaf_count, 2, "first split");

	for (type_t i = count_in_leaf + 1; i < count_in_leaf * 2; i++)
		test_insert(&tree, i, 0, 0);
	is(tree.common.leaf_count, 2, "full two leafs");

	test_insert(&tree, count_in_leaf * 2, 0, 0);
	is(tree.common.leaf_count, 3, "second split");

	for (type_t i = count_in_leaf * 2 + 1; i < count_in_leaf * 3; i++)
		test_insert(&tree, i, 0, 0);
	is(tree.common.leaf_count, 3, "full three leafs");

	test_insert(&tree, count_in_leaf * 3, 0, 0);
	is(tree.common.leaf_count, 4, "third split");

	test_destroy(&tree);

	test_create(&tree, 0, &allocator, NULL);

	type_t arr[count_in_leaf * count_in_inner];
	for (type_t i = 0; i < count_in_leaf * count_in_inner; i++)
		arr[i] = i;
	test_build(&tree, arr, count_in_leaf * count_in_inner);
	fail_unless(tree.common.leaf_count == count_in_inner);
	fail_unless(tree.common.inner_count == 1);
	is(tree.common.size, count_in_leaf * count_in_inner, "full 2 levels");

	test_insert(&tree, count_in_leaf * count_in_inner, 0, 0);
	is(tree.common.inner_count, 3, "2-level split");

	test_destroy(&tree);

	footer();
	check_plan();
}

static void
approximate_count()
{
	srand(0);

	approx tree;
	approx_create(&tree, 0, &allocator, NULL);

	uint32_t in_leaf_max_count = BPS_TREE_approx_MAX_COUNT_IN_LEAF;
	uint32_t in_leaf_min_count = in_leaf_max_count * 2 / 3;
	uint32_t in_leaf_ave_count = in_leaf_max_count * 5 / 6;
	uint32_t in_inner_max_count = BPS_TREE_approx_MAX_COUNT_IN_INNER;
	uint32_t in_inner_min_count = in_inner_max_count * 2 / 3;
	uint32_t in_inner_ave_count = in_inner_max_count * 5 / 6;
	double X = in_leaf_ave_count;
	double Y = in_inner_ave_count;
	double low_border_leaf = double(in_leaf_min_count) / in_leaf_ave_count;
	double upper_border_leaf = double(in_leaf_max_count) / in_leaf_ave_count;
	double low_border_inner = double(in_inner_min_count) / in_inner_ave_count;
	double upper_border_inner = double(in_inner_max_count) / in_inner_ave_count;

	const uint32_t short_sequence_count = 50;
	const uint32_t long_sequence_count = 30;
	const uint32_t long_sequence_multiplier = 20;
	const uint32_t arr_size = short_sequence_count *
				   (short_sequence_count + 1) / 2 +
				   long_sequence_count *
				   (long_sequence_count + 1) * long_sequence_multiplier / 2;
	uint64_t arr[arr_size];
	uint64_t count = 0;
	for (uint64_t i = 1; i <= short_sequence_count; i++)
		for (uint64_t j = 0; j < i; j++)
			arr[count++] = ((i * 100) << 32) | j;
	for (uint64_t i = 1; i <= long_sequence_count; i++)
		for (uint64_t j = 0; j < i * long_sequence_multiplier; j++)
			arr[count++] = ((i * 100 + 50) << 32) | j;
	fail_unless(count == arr_size);
	assert(count == arr_size);

	for (uint64_t i = 0; i < count * 10; i++) {
		uint64_t j = rand() % count;
		uint64_t k = rand() % count;
		uint64_t tmp = arr[j];
		arr[j] = arr[k];
		arr[k] = tmp;
	}

	for (uint64_t i = 0; i < count; i++)
		approx_insert(&tree, arr[i], NULL, NULL);
	fail_unless(approx_size(&tree) == arr_size);

	count = 0;
	int err_count = 0;
	const uint32_t over_possible =
		(short_sequence_count + long_sequence_count + 1) * 100;
	for (uint32_t i = 50; i < over_possible; i += 25) {
		uint64_t true_count = 0;
		if (i % 100 == 0) {
			uint64_t j = i / 100;
			if (j >= 1 && j <= short_sequence_count)
				true_count = j;
		} else if (i % 50 == 0) {
			uint64_t j = i / 100;
			if (j >= 1 && j <= long_sequence_count)
				true_count = j * long_sequence_multiplier;
		}
		count += true_count;

		uint64_t approx_count = approx_approximate_count(&tree, i);

		if (approx_count <= X) {
			if (approx_count != true_count) {
				err_count++;
				if (err_count <= 10)
					printf("searching %u found %llu expected %llu\n",
					       i, (unsigned long long)approx_count,
					       (unsigned long long)true_count);
			}
			continue;
		}
		double H = ceil(log(approx_count / X) / log(Y));
		double low = approx_count * low_border_leaf *
			pow(low_border_inner, H - 1);
		double up = approx_count * upper_border_leaf *
			pow(upper_border_inner, H - 1);
		if (true_count < low || true_count > up) {
			err_count++;
			if (err_count <= 10)
				printf("searching %u found %llu expected %llu\n",
				       i, (unsigned long long)approx_count,
				       (unsigned long long)true_count);
		}
	};

	fail_unless(err_count == 0);
	is(count, arr_size, "approximate count");

	approx_destroy(&tree);
}

static void
insert_get_iterator()
{
	test tree;
	test_create(&tree, 0, &allocator, NULL);
	type_t value = 100000;

	bps_insert_and_check(test, &tree, value, NULL)
	type_t i = 0;
	for (; i < 10000; i += 2)
		bps_insert_and_check(test, &tree, i, NULL);
	for (i = -2; i > -10000; i -= 2)
		bps_insert_and_check(test, &tree, i, NULL);
	for (i = -9999; i < 10000; i += 2)
		bps_insert_and_check(test, &tree, i, NULL);

	test_destroy(&tree);
	ok(true, "insert and get iterator");
}

static void
delete_value_check()
{
	plan(2);
	header();

	struct_tree tree;
	struct_tree_create(&tree, 0, &allocator, NULL);
	struct elem_t e1 = {1, 1};
	struct_tree_insert(&tree, e1, NULL, NULL);
	struct elem_t e2 = {1, 2};
	struct elem_t deleted = {LONG_MAX, LONG_MAX};

	fail_unless(struct_tree_delete_value(&tree, e2, &deleted) == 0);
	fail_unless(deleted.info == LONG_MAX);
	fail_unless(deleted.marker == LONG_MAX);
	fail_unless(struct_tree_find(&tree, 1) != NULL);
	fail_unless(struct_tree_debug_check(&tree) == 0);
	ok(true, "deletion of non-identical element fails");

	fail_unless(struct_tree_delete_value(&tree, e1, &deleted) == 0);
	fail_unless(deleted.info == e1.info);
	fail_unless(deleted.marker == e1.marker);
	fail_unless(struct_tree_find(&tree, 1) == NULL);
	fail_unless(struct_tree_debug_check(&tree) == 0);
	ok(true, "deletion of identical element succeeds");

	struct_tree_destroy(&tree);

	footer();
	check_plan();
}

static void
insert_successor_test()
{
	test tree;

	size_t limits[] = {20, 2000};

	for (size_t i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
		size_t limit = limits[i];
		test_create(&tree, 0, &allocator, NULL);

		for (size_t j = 0; j < limit; j++) {
			type_t v = 1 + rand() % (limit - 1);

			bool exact = false;
			test_iterator itr = test_lower_bound(&tree, v, &exact);

			type_t expect_replaced = 0;
			type_t expect_successor = 0;
			if (exact) {
				expect_replaced =
					*test_iterator_get_elem(&tree, &itr);
			} else if (!test_iterator_is_invalid(&itr)) {
				expect_successor =
					*test_iterator_get_elem(&tree, &itr);
			}

			type_t replaced = 0;
			type_t successor = 0;

			test_insert(&tree, v, &replaced, &successor);

			fail_unless(replaced == expect_replaced);
			fail_unless(successor == expect_successor);
		}

		test_destroy(&tree);
	}

	ok(true, "successor test");
}

static void
gh_11326_oom_on_insertion_test()
{
	plan(1);
	header();

	test tree;
	test_view view;
	type_t replaced;
	struct test_iterator iterator;

	test_create(&tree, 0, &allocator, NULL);
	test_insert(&tree, 0, &replaced, NULL);
	test_view_create(&view, &tree);

	extent_alloc_failure = true;
	fail_unless(test_insert(&tree, 1, &replaced, NULL) != 0);
	debug_check(&tree);
	fail_unless(test_size(&tree) == 1);
	iterator = test_first(&tree);
	type_t *v = test_iterator_get_elem(&tree, &iterator);
	fail_unless(v != NULL && *v == 0);
	fail_unless(test_iterator_next(&tree, &iterator) == false);
	extent_alloc_failure = false;

	test_view_destroy(&view);
	test_destroy(&tree);

	ok(true, "gh-11326: OOM on insertion test");

	footer();
	check_plan();
}

int
main(void)
{
	plan(13);
	header();

	matras_allocator_create(&allocator, BPS_TREE_EXTENT_SIZE,
				extent_alloc, extent_free);

	simple_check();
	compare_with_sptree_check();
	compare_with_sptree_check_branches();
	bps_tree_debug_self_check();
	loading_test();
	printing_test();
	white_box_test();
	approximate_count();
	ok(extents_count == allocator.num_reserved_extents, "leak check");
	insert_get_iterator();
	delete_value_check();
	insert_successor_test();

	matras_allocator_destroy(&allocator);

	matras_allocator_create(&allocator, BPS_TREE_EXTENT_SIZE,
				extent_alloc, extent_free);
	gh_11326_oom_on_insertion_test();
	matras_allocator_destroy(&allocator);

	footer();
	return check_plan();
}
