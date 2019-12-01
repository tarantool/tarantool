#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>

#include "trivia/util.h"
#include "unit.h"


#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"

const uint32_t TEST_CASE_SIZE = 1000;

struct test_type {
	uint32_t val1;
	uint32_t val2;
	char c;
	struct heap_node node;
};

/* If set, order by test_type->val2, otherwise by test_type->val1. */
static bool order_by_val2;

int
test_type_less(const struct test_type *left, const struct test_type *right)
{
	if (order_by_val2)
		return left->val2 < right->val2;
	return left->val1 < right->val1;
}

#define HEAP_NAME test_heap
#define HEAP_LESS(h, a, b) test_type_less(a, b)
#define heap_value_t struct test_type
#define heap_value_attr node

#include "salad/heap.h"

void free_all_nodes(heap_t *p_heap)
{
	struct test_type *value;
	for (heap_off_t i = 0; i < p_heap->size; ++i) {
		value = (struct test_type *) ((char *)p_heap->harr[i] -
					      offsetof(struct test_type, node));
		free(value);
	}
}

static void
test_insert_1_to_3()
{
	header();
	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	for (uint32_t i = 0; i < 4; ++i) {
		value = (struct test_type *)malloc(sizeof(struct test_type));
		value->val1 = i;
		test_heap_insert(&heap, value);

		root_value = test_heap_top(&heap);
		if (root_value->val1 != 0) {
			fail("check that min.val1 is incorrect",
			     "root_value->val1 != 0");
		}
		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}
	}

	free_all_nodes(&heap);
	test_heap_destroy(&heap);

	footer();
}

static void
test_insert_3_to_1()
{
	header();
	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	for (uint32_t i = 3; i > 0; --i) {
		value = (struct test_type *)malloc(sizeof(struct test_type));
		value->val1 = i;
		test_heap_insert(&heap, value);

		root_value = test_heap_top(&heap);
		if (root_value->val1 != i) {
			fail("check that min.val1 is incorrect",
			     "root_value->val1 != i");
		}
		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}
	}

	free_all_nodes(&heap);
	test_heap_destroy(&heap);

	footer();
}

static void
test_insert_50_to_150_mod_100()
{
	header();
	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	for (uint32_t i = 50; i < 150; ++i) {
		value = (struct test_type *)malloc(sizeof(struct test_type));
		value->val1 = i % 100;
		test_heap_insert(&heap, value);

		root_value = test_heap_top(&heap);

		if (i < 100 && root_value->val1 != 50) {
			fail("min.val1 is incorrect",
			     "i < 100 && root_value->val1 != 50");
		}
		if (i >= 100 && root_value->val1 != 0) {
			fail("min.val1 is incorrect",
			     "i >= 100 && root_value->val1 != 0");
		}

		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}
	}

	for (int i = 0; i < 100; ++i) {
		root_value = test_heap_top(&heap);
		test_heap_pop(&heap);
		free(root_value);
	}
	test_heap_destroy(&heap);

	footer();
}

static void
test_insert_many_random()
{
	header();
	uint32_t ans = UINT_MAX;
	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	for (uint32_t i = 0; i < TEST_CASE_SIZE; ++i) {
		value = (struct test_type *)malloc(sizeof(struct test_type));
		value->val1 = rand();

		ans = (value->val1 < ans ? value->val1 : ans);
		test_heap_insert(&heap, value);

		root_value = test_heap_top(&heap);
		if (root_value->val1 != ans) {
			fail("min.val1 is incorrect", "root_value->val1 != ans");
		}
		if (heap.size != i + 1) {
			fail("check that size is correct failed", "root->size != i + 2");
		}

		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}
	}

	free_all_nodes(&heap);
	test_heap_destroy(&heap);
	footer();
}

static void
test_insert_10_to_1_pop()
{
	header();
	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	for (uint32_t i = 10; i > 0; --i) {
		value = (struct test_type *)malloc(sizeof(struct test_type));
		value->val1 = i;

		test_heap_insert(&heap, value);
		root_value = test_heap_top(&heap);
		if (root_value->val1 != i) {
			fail("check that min.val1 is correct failed",
			     "root_value->val1 != i");
		}
		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}
	}

	for (uint32_t i = 1; i <= 10; ++i) {
		root_value = test_heap_top(&heap);

		test_heap_pop(&heap);
		if (root_value->val1 != i) {
			fail("check that min.val1 is correct failed",
			     "root_value->val1 != i");
		}
		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}
		free(root_value);
	}
	test_heap_destroy(&heap);

	footer();
}

int uint32_compare(const void *a, const void *b)
{
	const uint32_t *ua = (const uint32_t *)a;
	const uint32_t *ub = (const uint32_t *)b;
	if (*ua < *ub) {
		return -1;
	}
	else if (*ua > *ub) {
		return 1;
	}

	return 0;
}

static void
test_insert_many_pop_many_random()
{
	header();
	uint32_t ans = UINT_MAX;

	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	uint32_t keys_it = 0;
	uint32_t *keys = (uint32_t *)malloc(sizeof(uint32_t) * TEST_CASE_SIZE);
	if (keys == NULL) {
		fail("keys == NULL", "fail to alloc memory for keys array");
	}

	for (uint32_t i = 0; i < TEST_CASE_SIZE; ++i) {
		value = (struct test_type *)malloc(sizeof(struct test_type));
		keys[keys_it++] = value->val1 = rand();
		ans = (value->val1 < ans ? value->val1 : ans);

		test_heap_insert(&heap, value);

		root_value = test_heap_top(&heap);
		if (root_value->val1 != ans) {
			fail("check that min.val1 is correct failed",
			     "root_value->val1 != ans");
		}
		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}

		if (heap.size != i + 1) {
			fail("check that size is correct",
			     "heap.size != i + 1");
		}
	}

	qsort(keys, TEST_CASE_SIZE, sizeof(uint32_t), uint32_compare);
	bool f = true;
	for (uint32_t i = 0; i + 1 < TEST_CASE_SIZE; ++i) {
		f = f && (keys[i] < keys[i + 1]);
	}
	if(!f)  {
		fail("check that keys is sorted failed", "!f");
	}

	uint32_t full_size = heap.size;
	for (uint32_t i = 0; i < TEST_CASE_SIZE; ++i) {
		root_value = test_heap_top(&heap);

		test_heap_pop(&heap);

		if (root_value->val1 != keys[i]) {
			fail("check that min.val1 is correct failed",
			     "root_value->val1 != keys[i]");
		}
		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}

		if (heap.size != full_size - 1 - i) {
			fail("check that size is correct",
			     "heap_test_size(root) != full_size - 1 - i");
		}
		free(root_value);
	}
	test_heap_destroy(&heap);

	free(keys);
	footer();
}

static void
test_insert_pop_workload()
{
	header();
	uint32_t ans = UINT_MAX;

	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	uint32_t current_size = 0;

	for(uint32_t i = 0; i < TEST_CASE_SIZE; ++i) {
		if (heap.size == 0 || rand() % 5) {
			current_size++;
			value = (struct test_type *)
				malloc(sizeof(struct test_type));
			value->val1 = rand();
			test_heap_insert(&heap, value);
		}
		else {
			current_size--;
			root_value = test_heap_top(&heap);

			test_heap_pop(&heap);
			free(root_value);
		}

		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}
		if (heap.size != current_size) {
			fail("check that size is correct",
			     "heap.size != current_size");
		}
	}

	free_all_nodes(&heap);
	test_heap_destroy(&heap);
	footer();
}

static void
test_pop_last()
{
	header();
	uint32_t ans = UINT_MAX;

	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	value = (struct test_type *)malloc(sizeof(struct test_type));
	test_heap_insert(&heap, value);

	test_heap_pop(&heap);
	if (heap.size != 0) {
		fail("test delete last node failed", "heap.size != 0");
	}

	test_heap_destroy(&heap);
	free(value);
	footer();
}

static void
test_insert_update_workload()
{
	header();
	uint32_t nodes_it = 0;
	uint64_t current_size = 0;
	uint32_t ans = UINT_MAX;

	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	struct test_type **nodes = (struct test_type **)
		malloc(sizeof(struct test_type *) * TEST_CASE_SIZE);

	struct heap_node *test_node = NULL, *root = NULL;
	for(uint32_t i = 0; i < TEST_CASE_SIZE; ++i) {
		if (nodes_it == current_size ||
		    heap.size == 0 ||
		    rand() % 5) {

			value = (struct test_type *)
				malloc(sizeof(struct test_type));
			value->val1 = rand();

			nodes[current_size++] = value;
			test_heap_insert(&heap, value);
		}
		else {
			nodes[nodes_it]->val1 = rand();
			test_heap_update(&heap, nodes[nodes_it]);
			nodes_it++;
		}

		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}
		if (heap.size != current_size) {
			fail("check that size is correct",
			     "heap_test_size(root) != current_size");
		}
	}

	free_all_nodes(&heap);
	test_heap_destroy(&heap);
	free(nodes);
	footer();
}

static void
test_random_delete_workload()
{
	header();
	uint32_t nodes_it = 0;
	uint64_t current_size = 0;
	uint32_t ans = UINT_MAX;

	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	struct test_type **nodes = (struct test_type **)
		malloc(sizeof(struct test_type *) * TEST_CASE_SIZE);

	struct heap_node *test_node = NULL, *root = NULL;
	for(uint32_t i = 0; i < TEST_CASE_SIZE; ++i) {
		if (nodes_it == current_size ||
		    heap.size == 0 ||
		    rand() % 5) {

			value = (struct test_type *)
				malloc(sizeof(struct test_type));
			value->val1 = rand();

			nodes[current_size++] = value;
			test_heap_insert(&heap, value);
		}
		else {
			test_heap_delete(&heap, nodes[nodes_it]);
			current_size--;
			nodes_it++;
		}

		if (test_heap_check(&heap)) {
			fail("check heap invariants failed",
			     "test_heap_check(&heap)");
		}
		if (heap.size != current_size) {
			fail("check that size is correct",
			     "heap.size != current_size");
		}
	}

	free_all_nodes(&heap);
	test_heap_destroy(&heap);
	free(nodes);
	footer();
}

static void
test_delete_last_node()
{
	header();
	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	for (int i = 0; i < 4; ++i) {
		value = (struct test_type *)
			malloc(sizeof(struct test_type));
		value->val1 = 0;
		test_heap_insert(&heap, value);
	}

	test_heap_delete(&heap, value);
	if (test_heap_check(&heap)) {
		fail("check heap invariants failed", "test_heap_check(&heap)");
	}
	test_heap_destroy(&heap);
	footer();
}

static void
test_heapify()
{
	header();
	heap_t heap;
	test_heap_create(&heap);

	for (uint32_t i = 0; i < TEST_CASE_SIZE; ++i) {
		struct test_type *value = malloc(sizeof(struct test_type));
		value->val1 = rand();
		value->val2 = rand();
		test_heap_insert(&heap, value);
	}

	order_by_val2 = true;
	test_heap_update_all(&heap);
	if (test_heap_check(&heap)) {
		fail("check heap invariants failed",
		     "test_heap_check(&heap)");
	}
	order_by_val2 = false;

	free_all_nodes(&heap);
	test_heap_destroy(&heap);
	footer();
}

int
main(int argc, const char** argv)
{
	srand(179);
	test_insert_1_to_3();
	test_insert_3_to_1();
	test_insert_50_to_150_mod_100();
	test_insert_many_random();
	test_insert_10_to_1_pop();
	test_insert_many_pop_many_random();
	test_insert_pop_workload();
	test_pop_last();
	test_insert_update_workload();
	test_random_delete_workload();
	test_delete_last_node();
	test_heapify();
}
