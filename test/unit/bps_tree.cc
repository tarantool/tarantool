#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "unit.h"
#include "sptree.h"
#include "qsort_arg.h"

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif //#ifndef MAX

SPTREE_DEF(test, realloc, qsort_arg);

typedef long type_t;

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
#define BPS_TREE_BLOCK_SIZE 64 /* value is to low specially for tests */
#define BPS_TREE_EXTENT_SIZE 1024 /* value is to low specially for tests */
#define BPS_TREE_COMPARE(a, b, arg) compare(a, b)
#define BPS_TREE_COMPARE_KEY(a, b, arg) compare(a, b)
#define bps_tree_elem_t type_t
#define bps_tree_key_t type_t
#define bps_tree_arg_t int
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
static void *
extent_alloc()
{
	return malloc(BPS_TREE_EXTENT_SIZE);
}

static void
extent_free(void *extent)
{
	free(extent);
}

static void
simple_check()
{
	header();

	const int rounds = 1000;
	bps_tree_test tree;
	bps_tree_test_create(&tree, 0, extent_alloc, extent_free);

	printf("Insert 1..X, remove 1..X\n");
	for (int i = 0; i < rounds; i++) {
		long v = i;
		if (bps_tree_test_find(&tree, v) != NULL)
			fail("element already in tree (1)", "true");
		bps_tree_test_insert(&tree, v, 0);
		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
	}
	if (bps_tree_test_size(&tree) != rounds)
		fail("Tree count mismatch (1)", "true");

	for (int i = 0; i < rounds; i++) {
		long v = i;
		if (bps_tree_test_find(&tree, v) == NULL)
			fail("element in tree (1)", "false");
		bps_tree_test_delete(&tree, v);
		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
	}
	if (bps_tree_test_size(&tree) != 0)
		fail("Tree count mismatch (2)", "true");

	printf("Insert 1..X, remove X..1\n");
	for (int i = 0; i < rounds; i++) {
		long v = i;
		if (bps_tree_test_find(&tree, v) != NULL)
			fail("element already in tree (2)", "true");
		bps_tree_test_insert(&tree, v, 0);
		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
	}
	if (bps_tree_test_size(&tree) != rounds)
		fail("Tree count mismatch (3)", "true");

	for (int i = 0; i < rounds; i++) {
		long v = rounds - 1 - i;
		if (bps_tree_test_find(&tree, v) == NULL)
			fail("element in tree (2)", "false");
		bps_tree_test_delete(&tree, v);
		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
	}
	if (bps_tree_test_size(&tree) != 0)
		fail("Tree count mismatch (4)", "true");

	printf("Insert X..1, remove 1..X\n");
	for (int i = 0; i < rounds; i++) {
		long v = rounds - 1 - i;
		if (bps_tree_test_find(&tree, v) != NULL)
			fail("element already in tree (3)", "true");
		bps_tree_test_insert(&tree, v, 0);
		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
	}
	if (bps_tree_test_size(&tree) != rounds)
		fail("Tree count mismatch (5)", "true");

	for (int i = 0; i < rounds; i++) {
		long v = i;
		if (bps_tree_test_find(&tree, v) == NULL)
			fail("element in tree (3)", "false");
		bps_tree_test_delete(&tree, v);
		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
	}
	if (bps_tree_test_size(&tree) != 0)
		fail("Tree count mismatch (6)", "true");

	printf("Insert X..1, remove X..1\n");
	for (int i = 0; i < rounds; i++) {
		long v = rounds - 1 - i;
		if (bps_tree_test_find(&tree, v) != NULL)
			fail("element already in tree (4)", "true");
		bps_tree_test_insert(&tree, v, 0);
		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
	}
	if (bps_tree_test_size(&tree) != rounds)
		fail("Tree count mismatch (7)", "true");

	for (int i = 0; i < rounds; i++) {
		long v = rounds - 1 - i;
		if (bps_tree_test_find(&tree, v) == NULL)
			fail("element in tree (4)", "false");
		bps_tree_test_delete(&tree, v);
		if (bps_tree_test_debug_check(&tree))
			fail("debug check nonzero", "true");
	}
	if (bps_tree_test_size(&tree) != 0)
		fail("Tree count mismatch (8)", "true");

	bps_tree_test_destroy(&tree);

	footer();
}

static void
compare_with_sptree_check()
{
	header();

	sptree_test spt_test;
	sptree_test_init(&spt_test, sizeof(type_t), 0, 0, 0, &node_comp, 0, 0);

	bps_tree_test tree;
	bps_tree_test_create(&tree, 0, extent_alloc, extent_free);

	const int rounds = 2 * 1024;
	const int elem_limit = 32 * 1024;

	for (int i = 0; i < rounds; i++) {
		long rnd = rand() % elem_limit;
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
	}
	sptree_test_destroy(&spt_test);

	bps_tree_test_destroy(&tree);

	footer();
}

static void
bps_tree_debug_self_check()
{
	header();

	int res = bps_tree_test_debug_check_internal_functions(false);
	if (res == 0)
		printf("self test finished successfully\n");
	else
		printf("self test returned error %d\n", res);

	bps_tree_test_debug_check_internal_functions(true);

	footer();
}

static void
printing_test()
{
	header();

	bps_tree_test tree;
	bps_tree_test_create(&tree, 0, extent_alloc, extent_free);

	const long rounds = 10;

	for (long i = 0; i < rounds; i++) {
		long v = rounds + i;
		printf("Inserting %ld\n", v);
		bps_tree_test_insert(&tree, v, 0);
		bps_tree_test_print(&tree, "%ld");
		v = rounds - i - 1;
		printf("Inserting %ld\n", v);
		bps_tree_test_insert(&tree, v, 0);
		bps_tree_test_print(&tree, "%ld");
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

	assert(BPS_TREE_test_MAX_COUNT_IN_LEAF == 6);
	assert(BPS_TREE_test_MAX_COUNT_IN_INNER == 5);

	printf("full leaf:\n");
	for (long i = 0; i < 6; i++) {
		bps_tree_test_insert(&tree, i, 0);
	}
	bps_tree_test_print(&tree, "%ld");

	printf("split now:\n");
	bps_tree_test_insert(&tree, 6, 0);
	bps_tree_test_print(&tree, "%ld");

	printf("full 2 leafs:\n");
	for (long i = 7; i < 12; i++) {
		bps_tree_test_insert(&tree, i, 0);
	}
	bps_tree_test_print(&tree, "%ld");

	printf("split now:\n");
	bps_tree_test_insert(&tree, 12, 0);
	bps_tree_test_print(&tree, "%ld");

	printf("full 3 leafs:\n");
	for (long i = 13; i < 18; i++) {
		bps_tree_test_insert(&tree, i, 0);
	}
	bps_tree_test_print(&tree, "%ld");

	printf("split now:\n");
	bps_tree_test_insert(&tree, 18, 0);
	bps_tree_test_print(&tree, "%ld");

	bps_tree_test_destroy(&tree);
	bps_tree_test_create(&tree, 0, extent_alloc, extent_free);

	printf("full 5 leafs:\n");
	for (long i = 0; i < 15; i++) {
		bps_tree_test_insert(&tree, 15 + i, 0);
		bps_tree_test_insert(&tree, 14 - i, 0);
	}
	bps_tree_test_print(&tree, "%ld");

	printf("2-level split now:\n");
	bps_tree_test_insert(&tree, 30, 0);
	bps_tree_test_print(&tree, "%ld");

	bps_tree_test_destroy(&tree);

	footer();
}

int
main(void)
{
	simple_check();
	compare_with_sptree_check();
	bps_tree_debug_self_check();
	printing_test();
	white_box_test();
}
