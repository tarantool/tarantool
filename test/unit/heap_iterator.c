#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <memory.h>

#include "unit.h"

#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"
#undef HEAP_FORWARD_DECLARATION

struct test_type {
		uint32_t val1;
		uint32_t val2;
		char c;
		struct heap_node node;
};

int test_type_less(const heap_t *heap,
			const struct heap_node *a,
			const struct heap_node *b) {

	const struct test_type *left = (struct test_type *)((char *)a -
					offsetof(struct test_type, node));
	const struct test_type *right = (struct test_type *)((char *)b -
					offsetof(struct test_type, node));
	return left->val1 < right->val1;
}

#define HEAP_NAME test_heap
#define HEAP_LESS(h, a, b) test_type_less(h, a, b)

#include "salad/heap.h"

void free_all_nodes(heap_t *p_heap)
{
	struct test_type *root_value;
	for (heap_off_t i = 0; i < p_heap->size; ++i) {
		root_value = (struct test_type *) ((char *)p_heap->harr[i] -
				offsetof(struct test_type, node));
		free(root_value);
	}
}

static void
test_iterator_create()
{
	header();
	struct test_type *value, *root_value;
	heap_t heap;
	test_heap_create(&heap);

	value = (struct test_type *)malloc(sizeof(struct test_type));
	value->val1 = 0;
	test_heap_insert(&heap, &value->node);

	struct heap_iterator it;
	test_heap_iterator_init(&heap, &it);

	if (it.curr_pos != 0)
		fail("incorrect position after create", "it.curr_pos != 0");

	free_all_nodes(&heap);
	test_heap_destroy(&heap);
	footer();
}

static void
test_iterator_empty()
{
	header();
	struct heap_node *nd;
	heap_t heap;
	test_heap_create(&heap);

	struct heap_iterator it;
	test_heap_iterator_init(&heap, &it);

	nd = test_heap_iterator_next(&it);

	if (nd != NULL)
		fail("incorrect node", "nd != NULL");

	free_all_nodes(&heap);
	test_heap_destroy(&heap);
	footer();
}


static void
test_iterator_small()
{
	header();
	struct test_type *value, *root_value;
	struct heap_node *test_node;
	heap_t heap;
	test_heap_create(&heap);

	for (uint32_t i = 4; i > 0; --i) {
		value = (struct test_type *)malloc(sizeof(struct test_type));
		value->val1 = i;
		test_heap_insert(&heap, &value->node);
	}

	struct heap_iterator it;
	bool used_key[5];
	memset((void *)used_key, 0, sizeof(used_key));

	test_heap_iterator_init(&heap, &it);
	test_node = NULL;
	for (uint32_t i = 0; i < 4; ++i) {
		test_node = test_heap_iterator_next(&it);

		if (test_node == NULL)
			fail("NULL returned from iterator",
			     "test_node == NULL");

		value = (struct test_type *)((char *)test_node -
					offsetof(struct test_type, node));
		uint32_t val = value->val1;
		if (val < 1 || val > 5)
			fail("from iterator returned incorrect value",
			     "val < 1 || val > 5");
		if (used_key[val])
			fail("from iterator some value returned twice",
			     "used[val]");
		used_key[val] = 1;
	}

	bool f = true;
	for (uint32_t i = 1; i < 5; ++i)
		f = used_key[i] && f;
	if (!f)
		fail("some node was skipped", "!f");

	test_node = test_heap_iterator_next(&it);
	if (test_node != NULL)
		fail("after all iterator returns not NULL",
		     "test_node != NULL");

	free_all_nodes(&heap);
	test_heap_destroy(&heap);
	footer();
}

static void
test_iterator_large()
{
	header();
	uint32_t const TEST_CASE_SIZE = 1000;
	struct test_type *value, *root_value;
	struct heap_node *test_node;
	heap_t heap;
	test_heap_create(&heap);

	for (uint32_t i = TEST_CASE_SIZE; i > 0; --i) {
		value = (struct test_type *)malloc(sizeof(struct test_type));
		value->val1 = i;
		test_heap_insert(&heap, &value->node);
	}

	struct heap_iterator it;
	bool used_key[TEST_CASE_SIZE + 1];
	memset((void *)used_key, 0, sizeof(used_key));

	test_heap_iterator_init(&heap, &it);
	test_node = NULL;
	for (uint32_t i = 0; i < TEST_CASE_SIZE; ++i) {
		test_node = test_heap_iterator_next(&it);

		if (test_node == NULL)
			fail("NULL returned from iterator",
			     "test_node == NULL");

		value = (struct test_type *)((char *)test_node -
						offsetof(struct test_type, node));
		uint32_t val = value->val1;
		if (val == 0 || val > TEST_CASE_SIZE)
			fail("from iterator returned incorrect value",
			     "val < 0 || val > TEST_CASE_SIZE");
		if (used_key[val])
			fail("from iterator some value returned twice",
			     "used[val]");
		used_key[val] = 1;
	}

	bool f = true;
	for (uint32_t i = 1; i < TEST_CASE_SIZE; ++i) {
		f = used_key[i] && f;
	}
	if (!f)
		fail("some node was skipped", "!f");

	test_node = test_heap_iterator_next(&it);
	if (test_node != NULL)
		fail("after all iterator returns not nil",
		     "test_node != NULL");

	free_all_nodes(&heap);
	test_heap_destroy(&heap);
	footer();
}


int
main(int argc, const char** argv)
{
	srand(179);
	test_iterator_create();
	test_iterator_empty();
	test_iterator_small();
	test_iterator_large();
}
