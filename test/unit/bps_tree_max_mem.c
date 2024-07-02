#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

/* Select the tree flavor to test. */
#if defined(TEST_INNER_CARD)
# define BPS_INNER_CARD
#elif defined(TEST_INNER_CHILD_CARDS)
# define BPS_INNER_CHILD_CARDS
#elif !defined(TEST_DEFAULT)
# error "Please define TEST_DEFAULT, TEST_INNER_CARD or TEST_INNER_CHILD_CARDS."
#endif

#define BPS_TREE_NAME test
#define BPS_TREE_BLOCK_SIZE 256
#define BPS_TREE_EXTENT_SIZE 16 * 1024
#define BPS_TREE_IS_IDENTICAL(a, b) ((a) == (b))
#define BPS_TREE_COMPARE(a, b, arg) ((a) - (b))
#define BPS_TREE_COMPARE_KEY(a, b, arg) ((a) - (b))
#define bps_tree_elem_t int64_t
#define bps_tree_key_t int64_t
#define bps_tree_arg_t int
#include "salad/bps_tree.h"

#define debug_check(tree) do { \
	int result = test_debug_check((tree)); \
	if (result) { \
		test_print((tree), "%" PRId64); \
		printf("debug check = %08x", result); \
		fail("debug check nonzero", "true"); \
	} \
} while (false)

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
check_max_mem_insert_batch_case(size_t initial_size, size_t count)
{
	struct test tree;
	test_create(&tree, 0, extent_alloc, extent_free, NULL, NULL);

	/* We insert in the middle to maximize the new block count. */
	size_t first_part_begin = 0;
	size_t first_part_end = initial_size / 2;
	size_t last_part_begin = first_part_end + count;
	size_t last_part_end = first_part_end + count + (initial_size -
							 first_part_end);

	/*
	 * Build the tree to make it as compact as it can, this increases the
	 * amount of touched blocks on following insertions.
	 */
	int64_t *arr = xcalloc(initial_size, sizeof(*arr));
	size_t arr_size = 0;

	for (size_t i = first_part_begin; i < first_part_end; i++) {
		fail_unless(arr_size < initial_size);
		arr[arr_size++] = i;
	}

	for (size_t i = last_part_begin; i < last_part_end; i++) {
		fail_unless(arr_size < initial_size);
		arr[arr_size++] = i;
	}

	fail_unless(arr_size == initial_size);

	test_build(&tree, arr, arr_size);
	fail_unless(test_size(&tree) == arr_size);

	/*
	 * Create a view, so any block modification will require a new block
	 * allocation. This maximizes the amount of required memory.
	 */
	struct test_view view;
	test_view_create(&view, &tree);

	size_t max_new_mem = test_max_mem_insert_batch(&tree, count);
	size_t max_mem = test_mem_used(&tree) + max_new_mem;

	for (size_t i = first_part_end; i < last_part_begin; i++) {
		test_insert(&tree, i, 0, 0);
		debug_check(&tree);
	}

	fail_unless(test_size(&tree) == initial_size + count);
	fail_unless(test_mem_used(&tree) <= max_mem);

	test_view_destroy(&view);
	test_destroy(&tree);
}

static void
check_max_mem_insert_batch(void)
{
	plan(16);
	header();

	/* Max sizes per height. */
	size_t height_max_size[4];

	height_max_size[0] = 0;
	height_max_size[1] = BPS_TREE_test_MAX_COUNT_IN_LEAF;

	for (unsigned i = 2; i < lengthof(height_max_size); i++) {
		height_max_size[i] = height_max_size[i - 1] *
				     BPS_TREE_test_MAX_COUNT_IN_INNER;
	}

	for (unsigned i = 0; i < lengthof(height_max_size); i++) {
		for (unsigned j = 0; j < lengthof(height_max_size); j++) {
			check_max_mem_insert_batch_case(height_max_size[i],
							height_max_size[j]);
			ok(true, "insert %d-level into %d-level tree", j, i);
		}
	}

	footer();
	check_plan();
}

int
main(void)
{
	plan(1);
	header();

	check_max_mem_insert_batch();

	footer();
	return check_plan();
}
