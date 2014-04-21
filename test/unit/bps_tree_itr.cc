#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "unit.h"

struct elem_t {
	long first;
	long second;
	bool operator!= (const struct elem_t& another) const
	{
		return first == another.first && second == another.second; 
	}
};

static int compare(const elem_t &a, const elem_t &b);
static int compare_key(const elem_t &a, long b);

#define BPS_TREE_NAME _test
#define BPS_TREE_BLOCK_SIZE 128 /* value is to low specially for tests */
#define BPS_TREE_EXTENT_SIZE 1024 /* value is to low specially for tests */
#define BPS_TREE_COMPARE(a, b, arg) compare(a, b)
#define BPS_TREE_COMPARE_KEY(a, b, arg) compare_key(a, b)
#define bps_tree_elem_t struct elem_t
#define bps_tree_key_t long
#define bps_tree_arg_t int
#include "salad/bps_tree.h"

static int compare(const elem_t &a, const elem_t &b)
{
	return a.first < b.first ? -1 : a.first > b.first ? 1 :
	       a.second < b.second ? -1 : a.second > b.second ? 1 : 0;
}

static int compare_key(const elem_t &a, long b)
{
	return a.first < b ? -1 : a.first > b ? 1 : 0;
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
itr_check()
{
	header();

	bps_tree tree;
	bps_tree_create(&tree, 0, extent_alloc, extent_free);
	
	/* Stupid tests */
	{
		bps_tree_iterator tmp1, tmp2;
		tmp1 = bps_tree_invalid_iterator();
		tmp2 = bps_tree_invalid_iterator();
		if (!bps_tree_itr_is_invalid(&tmp1))
			fail("invalid iterator is not invalid", "true");
		if (!bps_tree_itr_are_equal(&tree, &tmp1, &tmp2))
			fail("invalid iterators are not equal", "true");
	}

	/* Filing tree */
	const long count1 = 10000;
	const long count2 = 5;
	for (long i = 0; i < count1; i++) {
		struct elem_t e;
		e.first = i * 2; /* note that filled with even numbers */
		for (long j = 0; j < count2; j++) {
			e.second = j;
			bps_tree_insert_or_replace(&tree, e, 0);
		}
	}
	printf("Test tree size: %d\n", (int)bps_tree_size(&tree));

	/* Test that tree filled ok */
	for (long i = 0; i < count1; i++) {
		for (long j = 0; j < count2; j++) {
			if (bps_tree_find(&tree, i * 2) == 0)
				fail("Integrity check failed (1)", "true");
			if (bps_tree_find(&tree, i * 2 + 1) != 0)
				fail("Integrity check failed (2)", "true");
		}
	}

	/* Print first 7 elems */
	{
		printf("--> ");
		bps_tree_iterator itr = bps_tree_itr_first(&tree);
		for (int i = 0; i < 7; i++) {
			elem_t *elem = bps_tree_itr_get_elem(&tree, &itr);
			printf("(%ld,%ld) ", elem->first, elem->second);
			bps_tree_itr_next(&tree, &itr);
		}
		printf("\n");
	}
	/* Print last 7 elems */
	{
		printf("<-- ");
		bps_tree_iterator itr = bps_tree_itr_last(&tree);
		for (int i = 0; i < 7; i++) {
			elem_t *elem = bps_tree_itr_get_elem(&tree, &itr);
			printf("(%ld,%ld) ", elem->first, elem->second);
			bps_tree_itr_prev(&tree, &itr);
		}
		printf("\n");
	}
	
	/* Iteratete forward all elements 5 times */
	{
		bps_tree_iterator itr = bps_tree_itr_first(&tree);
		for (long i = 0; i < count1 * count2 * 5; i++) {
			elem_t *elem = bps_tree_itr_get_elem(&tree, &itr);
			if (elem->first != ((i % (count1 * count2)) / count2) * 2)
				fail("iterate all failed (1)", "true");
			if (elem->second != i % count2)
				fail("iterate all failed (2)", "true");
			bool itr_res = bps_tree_itr_next(&tree, &itr);
			if (!!itr_res == !!bps_tree_itr_is_invalid(&itr))
				fail("iterate all failed (3)", "true");
			if (!itr_res) {
				itr_res = bps_tree_itr_next(&tree, &itr);
				if (!itr_res || bps_tree_itr_is_invalid(&itr))
					fail("iterate all failed (4)", "true");
			}
		}
	}
	
	/* Iteratete backward all elements 5 times */
	{
		bps_tree_iterator itr = bps_tree_itr_last(&tree);
		for (long i = 0; i < count1 * count2 * 5; i++) {
			elem_t *elem = bps_tree_itr_get_elem(&tree, &itr);
			long j = count1 * count2 - 1 - (i % (count1 * count2)); 
			if (elem->first != (j / count2) * 2)
				fail("iterate all failed (5)", "true");
			if (elem->second != j % count2)
				fail("iterate all failed (6)", "true");
			bool itr_res = bps_tree_itr_prev(&tree, &itr);
			if (!!itr_res == !!bps_tree_itr_is_invalid(&itr))
				fail("iterate all failed (7)", "true");
			if (!itr_res) {
				itr_res = bps_tree_itr_prev(&tree, &itr);
				if (!itr_res || bps_tree_itr_is_invalid(&itr))
					fail("iterate all failed (8)", "true");
			}
		}
	}
	
	/* Check iterating in range from lower bound to upper bound */
	/* Several probes */
	const long keys[] = {-1, 0, 10, 15, count1*2 - 2, count1 * 2};
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		const long key = keys[i];
		bool has_this_key1;
		bps_tree_iterator begin = bps_tree_lower_bound(&tree, key, &has_this_key1);
		bool has_this_key2;
		bps_tree_iterator end = bps_tree_upper_bound(&tree, key, &has_this_key2);
		if (has_this_key1 != has_this_key2)
			fail("Exact flag is broken", "true");
		printf("Key %ld, %s range [%s, %s): ", key,
			has_this_key1 ? "not empty" : "empty",
			bps_tree_itr_is_invalid(&begin) ? "eof" : "ptr",
			bps_tree_itr_is_invalid(&end) ? "eof" : "ptr");
		bps_tree_iterator runner = begin;
		while (!bps_tree_itr_are_equal(&tree, &runner, &end)) {
			elem_t *elem = bps_tree_itr_get_elem(&tree, &runner);
			printf("(%ld,%ld) ", elem->first, elem->second);
			bps_tree_itr_next(&tree, &runner);
		}
		printf(" <-> ");
		runner = end;
		while (!bps_tree_itr_are_equal(&tree, &runner, &begin)) {
			bps_tree_itr_prev(&tree, &runner);
			elem_t *elem = bps_tree_itr_get_elem(&tree, &runner);
			printf("(%ld,%ld) ", elem->first, elem->second);
		}
		printf("\n");
	}

	/* Check iterating in range from lower bound to upper bound */
	/* Automated */
	for (long i = -1; i <= count1 + 1; i++) {
		bps_tree_iterator begin = bps_tree_lower_bound(&tree, i, 0);
		bps_tree_iterator end = bps_tree_upper_bound(&tree, i, 0);
		long real_count = 0;
		while (!bps_tree_itr_are_equal(&tree, &begin, &end)) {
			elem_t *elem = bps_tree_itr_get_elem(&tree, &begin);
			if (elem->first != i)
				fail("range itr failed (1)", "true");
			if (elem->second != real_count)
				fail("range itr failed (2)", "true");
			real_count++;
			bps_tree_itr_next(&tree, &begin);
		}
		long must_be_count = 0;
		if (i >= 0 && i / 2 <= count1 - 1 && (i & 1) == 0)
			must_be_count = count2;
		if (real_count != must_be_count)
			fail("range itr failed (3)", "true");
	}

	bps_tree_destroy(&tree);

	footer();
}

int
main(void)
{
	itr_check();
}
