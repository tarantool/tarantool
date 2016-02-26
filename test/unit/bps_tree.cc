#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

#include "unit.h"
#include "sptree.h"
#include "qsort_arg.h"

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif //#ifndef MAX

SPTREE_DEF(test, realloc, qsort_arg);

typedef int64_t type_t;
#define TYPE_F "%" PRId64

static int
compare(type_t a, type_t b);

/* check compiling with another name and settings */
#define BPS_TREE_NAME _testtest
#define BPS_TREE_BLOCK_SIZE 512 /* value is to low specially for tests */
#define BPS_TREE_EXTENT_SIZE 16*1024 /* value is to low specially for tests */
#define BPS_TREE_COMPARE(a, b, arg) compare(a, b)
#define BPS_TREE_COMPARE_KEY(a, b, arg) compare(a, b)
#define bps_tree_elem_t char
#define bps_tree_key_t char
#define bps_tree_arg_t int
#include "salad/bps_tree.h"
#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_EXTENT_SIZE
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef bps_tree_elem_t
#undef bps_tree_key_t
#undef bps_tree_arg_t

/* true tree with true settings */
#define BPS_TREE_NAME _test
#define BPS_TREE_BLOCK_SIZE 128 /* value is to low specially for tests */
#define BPS_TREE_EXTENT_SIZE 2048 /* value is to low specially for tests */
#define BPS_TREE_COMPARE(a, b, arg) compare(a, b)
#define BPS_TREE_COMPARE_KEY(a, b, arg) compare(a, b)
#define bps_tree_elem_t type_t
#define bps_tree_key_t type_t
#define bps_tree_arg_t int
#define BPS_TREE_DEBUG_BRANCH_VISIT
#include "salad/bps_tree.h"

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

static void *
extent_alloc()
{
	extents_count++;
	return malloc(BPS_TREE_EXTENT_SIZE);
}

static void
extent_free(void *extent)
{
	extents_count--;
	free(extent);
}

static void
simple_check()
{
	header();

	const unsigned int rounds = 2000;
	bps_tree_test tree;
	bps_tree_test_create(&tree, 0, extent_alloc, extent_free);

	printf("Insert 1..X, remove 1..X\n");
	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = i;
		if (bps_tree_test_find(&tree, v) != NULL)
			fail("element already in tree (1)", "true");
		bps_tree_test_insert(&tree, v, 0);
		if (bps_tree_test_debug_check(&tree)) {
			bps_tree_test_print(&tree, TYPE_F);
			fail("debug check nonzero", "true");
		}
	}
	if (bps_tree_test_size(&tree) != rounds)
		fail("Tree count mismatch (1)", "true");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = i;
		if (bps_tree_test_find(&tree, v) == NULL)
			fail("element in tree (1)", "false");
		bps_tree_test_delete(&tree, v);
		if (bps_tree_test_debug_check(&tree)) {
			bps_tree_test_print(&tree, TYPE_F);
			fail("debug check nonzero", "true");
		}
	}
	if (bps_tree_test_size(&tree) != 0)
		fail("Tree count mismatch (2)", "true");

	printf("Insert 1..X, remove X..1\n");
	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = i;
		if (bps_tree_test_find(&tree, v) != NULL)
			fail("element already in tree (2)", "true");
		bps_tree_test_insert(&tree, v, 0);
		if (bps_tree_test_debug_check(&tree)) {
			bps_tree_test_print(&tree, TYPE_F);
			fail("debug check nonzero", "true");
		}
	}
	if (bps_tree_test_size(&tree) != rounds)
		fail("Tree count mismatch (3)", "true");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = rounds - 1 - i;
		if (bps_tree_test_find(&tree, v) == NULL)
			fail("element in tree (2)", "false");
		bps_tree_test_delete(&tree, v);
		if (bps_tree_test_debug_check(&tree)) {
			bps_tree_test_print(&tree, TYPE_F);
			fail("debug check nonzero", "true");
		}
	}
	if (bps_tree_test_size(&tree) != 0)
		fail("Tree count mismatch (4)", "true");

	printf("Insert X..1, remove 1..X\n");
	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = rounds - 1 - i;
		if (bps_tree_test_find(&tree, v) != NULL)
			fail("element already in tree (3)", "true");
		bps_tree_test_insert(&tree, v, 0);
		if (bps_tree_test_debug_check(&tree)) {
			bps_tree_test_print(&tree, TYPE_F);
			fail("debug check nonzero", "true");
		}
	}
	if (bps_tree_test_size(&tree) != rounds)
		fail("Tree count mismatch (5)", "true");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = i;
		if (bps_tree_test_find(&tree, v) == NULL)
			fail("element in tree (3)", "false");
		bps_tree_test_delete(&tree, v);
		if (bps_tree_test_debug_check(&tree)) {
			bps_tree_test_print(&tree, TYPE_F);
			fail("debug check nonzero", "true");
		}
	}
	if (bps_tree_test_size(&tree) != 0)
		fail("Tree count mismatch (6)", "true");

	printf("Insert X..1, remove X..1\n");
	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = rounds - 1 - i;
		if (bps_tree_test_find(&tree, v) != NULL)
			fail("element already in tree (4)", "true");
		bps_tree_test_insert(&tree, v, 0);
		if (bps_tree_test_debug_check(&tree)) {
			fail("debug check nonzero", "true");
			bps_tree_test_print(&tree, TYPE_F);
		}
	}
	if (bps_tree_test_size(&tree) != rounds)
		fail("Tree count mismatch (7)", "true");

	for (unsigned int i = 0; i < rounds; i++) {
		type_t v = rounds - 1 - i;
		if (bps_tree_test_find(&tree, v) == NULL)
			fail("element in tree (4)", "false");
		bps_tree_test_delete(&tree, v);
		if (bps_tree_test_debug_check(&tree)) {
			bps_tree_test_print(&tree, TYPE_F);
			fail("debug check nonzero", "true");
		}
	}
	if (bps_tree_test_size(&tree) != 0)
		fail("Tree count mismatch (8)", "true");

	bps_tree_test_destroy(&tree);

	footer();
}

static bool
check_trees_are_identical(bps_tree_test *tree, sptree_test *spt_test)
{
	if (bps_tree_test_size(tree) != spt_test->size)
		return false;
	int n = bps_tree_test_size(tree);
	bps_tree_test_iterator itr = bps_tree_test_itr_first(tree);
	sptree_test_iterator *spitr = sptree_test_iterator_init(spt_test);
	for (int i = 0; i < n; i++) {
		type_t v1 = *bps_tree_test_itr_get_elem(tree, &itr);
		type_t v2 = *(type_t *)sptree_test_iterator_next(spitr);
		bps_tree_test_itr_next(tree, &itr);
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
	header();

	sptree_test spt_test;
	sptree_test_init(&spt_test, sizeof(type_t), 0, 0, 0, &node_comp, 0, 0);

	bps_tree_test tree;
	bps_tree_test_create(&tree, 0, extent_alloc, extent_free);

	const int rounds = 16 * 1024;
	const int elem_limit = 	1024;

	for (int i = 0; i < rounds; i++) {
		type_t rnd = rand() % elem_limit;
		int find_res1 = sptree_test_find(&spt_test, &rnd) ? 1 : 0;
		int find_res2 = bps_tree_test_find(&tree, rnd) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");

		if (find_res1 == 0) {
			sptree_test_replace(&spt_test, &rnd, NULL);
			bps_tree_test_insert(&tree, rnd, 0);
		} else {
			sptree_test_delete(&spt_test, &rnd);
			bps_tree_test_delete(&tree, rnd);
		}

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");

		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}
	sptree_test_destroy(&spt_test);

	bps_tree_test_destroy(&tree);

	footer();
}

static void
compare_with_sptree_check_branches()
{
	header();

	sptree_test spt_test;
	sptree_test_init(&spt_test, sizeof(type_t), 0, 0, 0, &node_comp, 0, 0);

	bps_tree_test tree;
	bps_tree_test_create(&tree, 0, extent_alloc, extent_free);

	const int elem_limit = 1024;

	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)i;
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = bps_tree_test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (find_res1) {
			throw 0;
			fail("trees integrity", "false");
		}

		sptree_test_replace(&spt_test, &v, NULL);
		bps_tree_test_insert(&tree, v, 0);

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}
	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)i;
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = bps_tree_test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (!find_res1)
			fail("trees integrity", "false");

		sptree_test_delete(&spt_test, &v);
		bps_tree_test_delete(&tree, v);

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}

	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)(elem_limit - i - 1);
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = bps_tree_test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (find_res1)
			fail("trees integrity", "false");

		sptree_test_replace(&spt_test, &v, NULL);
		bps_tree_test_insert(&tree, v, 0);

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}
	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)(elem_limit - i - 1);
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = bps_tree_test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (!find_res1)
			fail("trees integrity", "false");

		sptree_test_delete(&spt_test, &v);
		bps_tree_test_delete(&tree, v);

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}

	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)i;
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = bps_tree_test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (find_res1)
			fail("trees integrity", "false");

		sptree_test_replace(&spt_test, &v, NULL);
		bps_tree_test_insert(&tree, v, 0);

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
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
		int find_res2 = bps_tree_test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (!find_res1)
			fail("trees integrity", "false");

		sptree_test_delete(&spt_test, &v);
		bps_tree_test_delete(&tree, v);

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}

	for (int i = 0; i < elem_limit; i++) {
		type_t v = (type_t)i;
		int find_res1 = sptree_test_find(&spt_test, &v) ? 1 : 0;
		int find_res2 = bps_tree_test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (find_res1)
			fail("trees integrity", "false");

		sptree_test_replace(&spt_test, &v, NULL);
		bps_tree_test_insert(&tree, v, 0);

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
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
		int find_res2 = bps_tree_test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (!find_res1)
			fail("trees integrity", "false");

		sptree_test_delete(&spt_test, &v);
		bps_tree_test_delete(&tree, v);

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
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
		int find_res2 = bps_tree_test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (find_res1)
			fail("trees integrity", "false");

		sptree_test_replace(&spt_test, &v, NULL);
		bps_tree_test_insert(&tree, v, 0);

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
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
		int find_res2 = bps_tree_test_find(&tree, v) ? 1 : 0;
		if (find_res1 ^ find_res2)
			fail("trees identity", "false");
		if (!find_res1)
			fail("trees integrity", "false");

		sptree_test_delete(&spt_test, &v);
		bps_tree_test_delete(&tree, v);

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
		if (!check_trees_are_identical(&tree, &spt_test))
			fail("trees identity", "false");
	}

	if (tree.debug_insert_leaf_branches_mask !=
	    tree.debug_insert_leaf_branches_max_mask)
		fail("not all insert leaf branches was tested", "true");
	if (tree.debug_insert_inner_branches_mask !=
	    tree.debug_insert_inner_branches_max_mask)
		fail("not all insert inner branches was tested", "true");
	if (tree.debug_delete_leaf_branches_mask !=
	    tree.debug_delete_leaf_branches_max_mask)
		fail("not all delete leaf branches was tested", "true");
	if (tree.debug_delete_inner_branches_mask !=
	    tree.debug_delete_inner_branches_max_mask)
		fail("not all delete inner branches was tested", "true");

	sptree_test_destroy(&spt_test);

	bps_tree_test_destroy(&tree);

	footer();
}

static void
bps_tree_debug_self_check()
{
	header();

	int res = bps_tree_test_debug_check_internal_functions(false);
	if (res)
		printf("self test returned error %d\n", res);

	bps_tree_test_debug_check_internal_functions(true);

	footer();
}

#include <signal.h>

static void
loading_test()
{
	header();

	bps_tree_test tree;

	const type_t test_count = 1000;
	type_t arr[test_count];
	for (type_t i = 0; i < test_count; i++)
		arr[i] = i;

	for (type_t i = 0; i <= test_count; i++) {
		bps_tree_test_create(&tree, 0, extent_alloc, extent_free);

		if (bps_tree_test_build(&tree, arr, i))
			fail("building failed", "true");

		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");

		struct bps_tree_test_iterator itr;
		itr = bps_tree_test_itr_first(&tree);
		for (type_t j = 0; j < i; j++) {
			type_t *v = bps_tree_test_itr_get_elem(&tree, &itr);
			if (!v || *v != j)
				fail("wrong build result", "true");
			bps_tree_test_itr_next(&tree, &itr);
		}
		if (!bps_tree_test_itr_is_invalid(&itr))
			fail("wrong build result", "true");

		bps_tree_test_destroy(&tree);
	}

	footer();
}

static void
printing_test()
{
	header();

	bps_tree_test tree;
	bps_tree_test_create(&tree, 0, extent_alloc, extent_free);

	const type_t rounds = 22;

	for (type_t i = 0; i < rounds; i++) {
		type_t v = rounds + i;
		printf("Inserting " TYPE_F "\n", v);
		bps_tree_test_insert(&tree, v, 0);
		bps_tree_test_print(&tree, TYPE_F);
		v = rounds - i - 1;
		printf("Inserting " TYPE_F "\n", v);
		bps_tree_test_insert(&tree, v, 0);
		bps_tree_test_print(&tree, TYPE_F);
	}

	bps_tree_test_destroy(&tree);

	footer();
}

static void
white_box_test()
{
	header();

	bps_tree_test tree;
	bps_tree_test_create(&tree, 0, extent_alloc, extent_free);

	assert(BPS_TREE_test_MAX_COUNT_IN_LEAF == 14);
	assert(BPS_TREE_test_MAX_COUNT_IN_INNER == 10);

	printf("full leaf:\n");
	for (type_t i = 0; i < 14; i++) {
		bps_tree_test_insert(&tree, i, 0);
	}
	bps_tree_test_print(&tree, TYPE_F);

	printf("split now:\n");
	bps_tree_test_insert(&tree, 14, 0);
	bps_tree_test_print(&tree, TYPE_F);

	printf("full 2 leafs:\n");
	for (type_t i = 15; i < 28; i++) {
		bps_tree_test_insert(&tree, i, 0);
	}
	bps_tree_test_print(&tree, TYPE_F);

	printf("split now:\n");
	bps_tree_test_insert(&tree, 28, 0);
	bps_tree_test_print(&tree, TYPE_F);

	printf("full 3 leafs:\n");
	for (type_t i = 29; i < 42; i++) {
		bps_tree_test_insert(&tree, i, 0);
	}
	bps_tree_test_print(&tree, TYPE_F);

	printf("split now:\n");
	bps_tree_test_insert(&tree, 42, 0);
	bps_tree_test_print(&tree, TYPE_F);

	bps_tree_test_destroy(&tree);
	bps_tree_test_create(&tree, 0, extent_alloc, extent_free);
	type_t arr[140];
	for (type_t i = 0; i < 140; i++)
		arr[i] = i;
	bps_tree_test_build(&tree, arr, 140);
	printf("full 10 leafs:\n");
	bps_tree_test_print(&tree, TYPE_F);

	printf("2-level split now:\n");
	bps_tree_test_insert(&tree, 140, 0);
	bps_tree_test_print(&tree, TYPE_F);

	bps_tree_test_destroy(&tree);

	footer();
}

int
main(void)
{
	simple_check();
	compare_with_sptree_check();
	compare_with_sptree_check_branches();
	bps_tree_debug_self_check();
	loading_test();
	printing_test();
	white_box_test();
	if (extents_count != 0)
		fail("memory leak!", "true");
}
