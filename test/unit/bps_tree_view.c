#include <stddef.h>
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#define BPS_TREE_NAME test_tree
#define BPS_TREE_BLOCK_SIZE 128
#define BPS_TREE_EXTENT_SIZE 1024
#define BPS_TREE_IS_IDENTICAL(a, b) ((a) == (b))
#define BPS_TREE_COMPARE(a, b, arg) ((a) < (b) ? -1 : ((a) > (b) ? 1 : 0))
#define BPS_TREE_COMPARE_KEY(a, b, arg) BPS_TREE_COMPARE(a, b, arg)
#define bps_tree_elem_t int
#define bps_tree_key_t int
#define bps_tree_arg_t void *
#include "salad/bps_tree.h"

static void *
extent_alloc(void *ctx)
{
	(void)ctx;
	return xmalloc(BPS_TREE_EXTENT_SIZE);
}

static void
extent_free(void *ctx, void *extent)
{
	(void)ctx;
	free(extent);
}

static void
test_tree_do_create(struct test_tree *tree)
{
	test_tree_create(tree, NULL, extent_alloc, extent_free, NULL, NULL);
}

static void
test_tree_do_insert(struct test_tree *tree, int val)
{
	fail_if(test_tree_insert(tree, val, NULL, NULL) != 0);
}

static void
test_size(void)
{
	plan(4);
	header();

	struct test_tree tree;
	test_tree_do_create(&tree);

	struct test_tree_view view;
	test_tree_view_create(&view, &tree);
	is(test_tree_view_size(&view), 0,
	   "empty view size before tree change");
	for (int i = 0; i < 1000; i++)
		test_tree_do_insert(&tree, i);
	is(test_tree_view_size(&view), 0,
	   "empty view size after tree change");
	test_tree_view_destroy(&view);

	test_tree_view_create(&view, &tree);
	is(test_tree_view_size(&view), 1000,
	   "non-empty view size before tree change");
	for (int i = 0; i < 1000; i++) {
		test_tree_do_insert(&tree, i + 1000);
		if (i % 2 == 0)
			test_tree_delete(&tree, i);
	}
	is(test_tree_view_size(&view), 1000,
	   "non-empty view size after tree change");
	test_tree_view_destroy(&view);

	test_tree_destroy(&tree);

	footer();
	check_plan();
}

static void
test_find(void)
{
	plan(2);
	header();

	struct test_tree tree;
	test_tree_do_create(&tree);
	for (int i = 0; i < 1000; i++)
		test_tree_do_insert(&tree, i);

	struct test_tree_view view;
	test_tree_view_create(&view, &tree);

	for (int i = 0; i < 1000; i++) {
		test_tree_do_insert(&tree, i + 1000);
		if (i % 2 == 0)
			test_tree_delete(&tree, i);
	}

	bool success = true;
	for (int i = 0; i < 1000; i++) {
		int *p = test_tree_view_find(&view, i);
		if (p == NULL || *p != i)
			success = false;
	}
	ok(success, "old values found");

	success = true;
	for (int i = 0; i < 1000; i++) {
		int *p = test_tree_view_find(&view, i + 1000);
		if (p != NULL)
			success = false;
	}
	ok(success, "new values not found");

	test_tree_view_destroy(&view);
	test_tree_destroy(&tree);

	footer();
	check_plan();
}

static void
test_first(void)
{
	plan(4);
	header();

	struct test_tree tree;
	test_tree_do_create(&tree);

	struct test_tree_view view;
	test_tree_view_create(&view, &tree);
	struct test_tree_iterator it = test_tree_view_first(&view);
	int *p = test_tree_view_iterator_get_elem(&view, &it);
	is(p, NULL, "empty view first before tree change");
	for (int i = 0; i < 1000; i++)
		test_tree_do_insert(&tree, i);
	it = test_tree_view_first(&view);
	p = test_tree_view_iterator_get_elem(&view, &it);
	is(p, NULL, "empty view first after tree change")
	test_tree_view_destroy(&view);

	test_tree_view_create(&view, &tree);
	it = test_tree_view_first(&view);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 0,
	   "non-empty view first before tree change")
	for (int i = 0; i < 100; i++)
		test_tree_delete(&tree, i);
	it = test_tree_view_first(&view);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 0,
	   "non-empty view first after tree change")
	test_tree_view_destroy(&view);

	test_tree_destroy(&tree);

	footer();
	check_plan();
}

static void
test_last(void)
{
	plan(4);
	header();

	struct test_tree tree;
	test_tree_do_create(&tree);

	struct test_tree_view view;
	test_tree_view_create(&view, &tree);
	struct test_tree_iterator it = test_tree_view_last(&view);
	int *p = test_tree_view_iterator_get_elem(&view, &it);
	is(p, NULL, "empty view last before tree change");
	for (int i = 0; i < 1000; i++)
		test_tree_do_insert(&tree, i);
	it = test_tree_view_last(&view);
	p = test_tree_view_iterator_get_elem(&view, &it);
	is(p, NULL, "empty view last after tree change")
	test_tree_view_destroy(&view);

	test_tree_view_create(&view, &tree);
	it = test_tree_view_last(&view);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 999,
	   "non-empty view last before tree change")
	for (int i = 900; i < 1000; i++)
		test_tree_delete(&tree, i);
	it = test_tree_view_last(&view);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 999,
	   "non-empty view last after tree change")
	test_tree_view_destroy(&view);

	test_tree_destroy(&tree);

	footer();
	check_plan();
}

static void
test_lower_bound(void)
{
	plan(5);
	header();

	struct test_tree tree;
	test_tree_do_create(&tree);
	for (int i = 0; i < 1000; i++)
		test_tree_do_insert(&tree, i * 2);

	struct test_tree_view view;
	test_tree_view_create(&view, &tree);

	for (int i = 0; i < 1000; i++) {
		test_tree_do_insert(&tree, i * 10);
		test_tree_delete(&tree, i * 2);
	}

	bool exact;
	struct test_tree_iterator it = test_tree_view_lower_bound(&view, 5000,
								  &exact);
	int *p = test_tree_view_iterator_get_elem(&view, &it);
	is(p, NULL, "not found");

	it = test_tree_view_lower_bound(&view, 99, &exact);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 100 && !exact, "found not exact");

	it = test_tree_view_lower_bound(&view, 100, &exact);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 100 && exact, "found exact");

	it = test_tree_view_lower_bound_elem(&view, 99, &exact);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 100 && !exact, "found elem not exact");

	it = test_tree_view_lower_bound_elem(&view, 100, &exact);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 100 && exact, "found elem exact");

	test_tree_view_destroy(&view);
	test_tree_destroy(&tree);

	footer();
	check_plan();
}

static void
test_upper_bound(void)
{
	plan(5);
	header();

	struct test_tree tree;
	test_tree_do_create(&tree);
	for (int i = 0; i < 1000; i++)
		test_tree_do_insert(&tree, i * 2);

	struct test_tree_view view;
	test_tree_view_create(&view, &tree);

	for (int i = 0; i < 1000; i++) {
		test_tree_do_insert(&tree, i * 10);
		test_tree_delete(&tree, i * 2);
	}

	bool exact;
	struct test_tree_iterator it = test_tree_view_upper_bound(&view, 5000,
								  &exact);
	int *p = test_tree_view_iterator_get_elem(&view, &it);
	is(p, NULL, "not found");

	it = test_tree_view_upper_bound(&view, 99, &exact);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 100 && !exact, "found not exact");

	it = test_tree_view_upper_bound(&view, 100, &exact);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 102 && exact, "found exact");

	it = test_tree_view_upper_bound_elem(&view, 99, &exact);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 100 && !exact, "found elem not exact");

	it = test_tree_view_upper_bound_elem(&view, 100, &exact);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 102 && exact, "found elem exact");

	test_tree_view_destroy(&view);
	test_tree_destroy(&tree);

	footer();
	check_plan();
}

static void
test_iterator(void)
{
	plan(4);
	header();

	struct test_tree tree;
	test_tree_do_create(&tree);
	for (int i = 0; i < 1000; i++) {
		if (i % 3 == 0)
			test_tree_do_insert(&tree, i);
	}

	struct test_tree_view view;
	test_tree_view_create(&view, &tree);

	for (int i = 0; i < 1000; i++) {
		if (i % 6 == 0)
			test_tree_delete(&tree, i);
		if (i % 5 == 0)
			test_tree_do_insert(&tree, i);
	}

	bool success = true;
	struct test_tree_iterator it = test_tree_view_first(&view);
	for (int i = 0; i < 1000; i++) {
		if (i % 3 == 0) {
			int *p = test_tree_view_iterator_get_elem(&view, &it);
			if (p == NULL || *p != i)
				success = false;
			test_tree_view_iterator_next(&view, &it);
		}
	}
	if (!test_tree_iterator_is_invalid(&it))
		success = false;
	ok(success, "scan forward");

	success = true;
	it = test_tree_view_last(&view);
	for (int i = 999; i >= 0; i--) {
		if (i % 3 == 0) {
			int *p = test_tree_view_iterator_get_elem(&view, &it);
			if (p == NULL || *p != i)
				success = false;
			test_tree_view_iterator_prev(&view, &it);
		}
	}
	if (!test_tree_iterator_is_invalid(&it))
		success = false;
	ok(success, "scan backward");

	it = test_tree_invalid_iterator();
	test_tree_view_iterator_next(&view, &it);
	int *p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 0, "next invalid");

	it = test_tree_invalid_iterator();
	test_tree_view_iterator_prev(&view, &it);
	p = test_tree_view_iterator_get_elem(&view, &it);
	ok(p != NULL && *p == 999, "prev invalid");

	test_tree_view_destroy(&view);
	test_tree_destroy(&tree);

	footer();
	check_plan();
}

static void
test_iterator_is_equal(void)
{
	plan(13);
	header();

	struct test_tree tree;
	test_tree_do_create(&tree);
	for (int i = 0; i < 1000; i++)
		test_tree_do_insert(&tree, i);

	struct test_tree_view view;
	test_tree_view_create(&view, &tree);

	struct test_tree_iterator it1 = test_tree_invalid_iterator();
	struct test_tree_iterator it2 = test_tree_invalid_iterator();
	ok(test_tree_view_iterator_is_equal(&view, &it1, &it2),
	   "invalid - invalid");

	it2 = test_tree_view_first(&view);
	ok(!test_tree_view_iterator_is_equal(&view, &it1, &it2) &&
	   !test_tree_view_iterator_is_equal(&view, &it2, &it1),
	   "invalid - first");
	test_tree_view_iterator_next(&view, &it2);
	ok(!test_tree_view_iterator_is_equal(&view, &it1, &it2) &&
	   !test_tree_view_iterator_is_equal(&view, &it2, &it1),
	   "invalid - next to first");

	it2 = test_tree_view_last(&view);
	ok(!test_tree_view_iterator_is_equal(&view, &it1, &it2) &&
	   !test_tree_view_iterator_is_equal(&view, &it2, &it1),
	   "invalid - last");
	test_tree_view_iterator_prev(&view, &it2);
	ok(!test_tree_view_iterator_is_equal(&view, &it1, &it2) &&
	   !test_tree_view_iterator_is_equal(&view, &it2, &it1),
	   "invalid - prev to last");

	it1 = test_tree_view_first(&view);
	it2 = test_tree_view_first(&view);
	ok(test_tree_view_iterator_is_equal(&view, &it1, &it2),
	   "first - first");
	test_tree_view_iterator_get_elem(&view, &it1);
	ok(test_tree_view_iterator_is_equal(&view, &it1, &it2) &&
	   test_tree_view_iterator_is_equal(&view, &it2, &it1),
	   "first - first after get");

	it1 = test_tree_view_last(&view);
	it2 = test_tree_view_last(&view);
	ok(test_tree_view_iterator_is_equal(&view, &it1, &it2),
	   "last - last");
	test_tree_view_iterator_get_elem(&view, &it1);
	ok(test_tree_view_iterator_is_equal(&view, &it1, &it2) &&
	   test_tree_view_iterator_is_equal(&view, &it2, &it1),
	   "last - last after get");

	it1 = test_tree_view_first(&view);
	it2 = test_tree_view_last(&view);
	ok(!test_tree_view_iterator_is_equal(&view, &it1, &it2) &&
	   !test_tree_view_iterator_is_equal(&view, &it2, &it1),
	   "first - last");
	test_tree_view_iterator_next(&view, &it1);
	ok(!test_tree_view_iterator_is_equal(&view, &it1, &it2) &&
	   !test_tree_view_iterator_is_equal(&view, &it2, &it1),
	   "next to first - last");

	it1 = test_tree_view_first(&view);
	it2 = test_tree_view_last(&view);
	test_tree_view_iterator_prev(&view, &it2);
	ok(!test_tree_view_iterator_is_equal(&view, &it1, &it2) &&
	   !test_tree_view_iterator_is_equal(&view, &it2, &it1),
	   "first - prev to last");
	test_tree_view_iterator_next(&view, &it1);
	ok(!test_tree_view_iterator_is_equal(&view, &it1, &it2) &&
	   !test_tree_view_iterator_is_equal(&view, &it2, &it1),
	   "next to first - prev to last");

	test_tree_view_destroy(&view);
	test_tree_destroy(&tree);

	footer();
	check_plan();
}

int
main(void)
{
	plan(8);
	header();

	test_size();
	test_find();
	test_first();
	test_last();
	test_lower_bound();
	test_upper_bound();
	test_iterator();
	test_iterator_is_equal();

	footer();
	return check_plan();
}
