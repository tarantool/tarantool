#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"
#include "trivia/util.h" /* xmalloc() */
#include "nondet.h"

/* Heap element structure. */
struct test_type {
	/* Value 1. */
	uint32_t val1;
	/* Value 2. */
	uint32_t val2;
	/* Heap entry structure. */
	struct heap_node node;
};

/**
 * Data comparing function to construct a heap.
 */
static bool
test_type_less(const struct test_type *lhs,
	       const struct test_type *rhs)
{
	__CPROVER_assert(lhs != NULL, "left operand is not NULL");
	__CPROVER_assert(rhs != NULL, "right operand is not NULL");
	return lhs->val1 < rhs->val1;
}

#define HEAP_NAME test_heap
#define HEAP_LESS(h, a, b) test_type_less(a, b)
#define heap_value_t struct test_type
#define heap_value_attr node

#include "salad/heap.h"

static void
heap_create_harness(void)
{
	heap_t heap;

	test_heap_create(&heap);
	__CPROVER_assert(heap.size == 0, "heap size is equal to 0");
	__CPROVER_assert(heap.capacity == 0, "heap capacity is equal to 0");

	test_heap_destroy(&heap);
}

static void
heap_delete_harness(void)
{
	heap_t heap;
	test_heap_create(&heap);

	struct test_type *value = xmalloc(sizeof(*value));
	__CPROVER_assume(value != NULL);
	value->val1 = nondet_uint32_t();
	value->val2 = nondet_uint32_t();

	test_heap_insert(&heap, value);

	test_heap_delete(&heap, value);
	__CPROVER_assert(heap.size == 0, "heap size is equal to 0");
	__CPROVER_assert(test_heap_check(&heap) == 0, "check heap invariants");

	test_heap_destroy(&heap);
	free(value);
}

static void
heap_destroy_harness(void)
{
	heap_t heap;
	test_heap_create(&heap);

	test_heap_destroy(&heap);
	__CPROVER_assert(heap.size == 0, "heap size is equal to 0");
	__CPROVER_assert(heap.capacity == 0, "heap capacity is equal to 0");
}

static void
heap_pop_harness(void)
{
	heap_t heap;
	test_heap_create(&heap);

	struct test_type *value = xmalloc(sizeof(*value));
	__CPROVER_assume(value != NULL);
	uint32_t val1 = nondet_uint32_t();
	uint32_t val2 = nondet_uint32_t();
	value->val1 = val1;
	value->val2 = val2;

	int res = test_heap_insert(&heap, value);
	__CPROVER_assume(res != -1);

	/* Erase minimal value. */
	const struct test_type *min_value = test_heap_pop(&heap);
	__CPROVER_assert(heap.size == 0, "heap size is 0");
	__CPROVER_assert(min_value != 0, "minimal value is not NULL");
	__CPROVER_assert(min_value->val1 == val1, "val1 is correct");
	__CPROVER_assert(min_value->val2 == val2, "val2 is correct");
	__CPROVER_assert(test_heap_check(&heap) == 0, "check heap invariants");

	test_heap_destroy(&heap);
	free(value);
}

static void
heap_update_all_harness(void)
{
	heap_t heap;
	test_heap_create(&heap);

	struct test_type *value = xmalloc(sizeof(*value));
	__CPROVER_assume(value != NULL);
	value->val1 = nondet_uint32_t();
	value->val2 = nondet_uint32_t();

	int res = test_heap_insert(&heap, value);
	__CPROVER_assume(res != -1);

	test_heap_update_all(&heap);
	__CPROVER_assert(test_heap_check(&heap) == 0, "check heap invariants");

	test_heap_destroy(&heap);
	free(value);
}

static void
heap_update_harness(void)
{
	heap_t heap;
	test_heap_create(&heap);

	struct test_type *value = xmalloc(sizeof(*value));
	__CPROVER_assume(value != NULL);
	value->val1 = nondet_uint32_t();
	value->val2 = nondet_uint32_t();

	int res = test_heap_insert(&heap, value);
	__CPROVER_assume(res != -1);

	test_heap_update(&heap, value);
	__CPROVER_assert(test_heap_check(&heap) == 0, "check heap invariants");

	test_heap_destroy(&heap);
	free(value);
}

static void
heap_insert_harness(void)
{
	heap_t heap;
	test_heap_create(&heap);

	struct test_type *value = xmalloc(sizeof(*value));
	__CPROVER_assume(value != NULL);
	value->val1 = nondet_uint32_t();
	value->val2 = nondet_uint32_t();

	int res = test_heap_insert(&heap, value);
	__CPROVER_assume(res != -1);

	test_heap_destroy(&heap);
	free(value);
}

static void
heap_top_harness(void)
{
	heap_t heap;

	test_heap_create(&heap);

	/* Minimal value in empty heap. */
	const struct test_type *min_value = test_heap_top(&heap);
	__CPROVER_assert(min_value == NULL, "minimal value is NULL");

	struct test_type *value = xmalloc(sizeof(*value));
	__CPROVER_assume(value != NULL);
	uint32_t val1 = nondet_uint32_t();
	uint32_t val2 = nondet_uint32_t();
	value->val1 = val1;
	value->val2 = val2;

	int res = test_heap_insert(&heap, value);
	/* Insertion can fail. */
	__CPROVER_assume(res != -1);

	/* Minimal value in a heap with a single element. */
	min_value = test_heap_top(&heap);
	__CPROVER_assert(min_value != NULL, "minimal value is not NULL");
	__CPROVER_assert(test_heap_check(&heap) == 0, "check heap invariants");

	test_heap_destroy(&heap);
	free(value);
}

static void
heap_iterator_init_harness(void)
{
	heap_t heap;
	struct heap_iterator it;

	test_heap_create(&heap);

	struct test_type *value = xmalloc(sizeof(*value));
	__CPROVER_assume(value != NULL);
	value->val1 = nondet_uint32_t();
	value->val2 = nondet_uint32_t();
	test_heap_insert(&heap, value);

	test_heap_iterator_init(&heap, &it);
	__CPROVER_assert(it.curr_pos == NULL, "iterator is NULL");

	test_heap_destroy(&heap);
	free(value);
}

static void
heap_iterator_next_harness(void)
{
	heap_t heap;
	struct heap_iterator it;
	const struct test_type *it_value;

	test_heap_create(&heap);

	/* Heap iterator next value in empty heap. */
	test_heap_iterator_init(&heap, &it);
	it_value = test_heap_iterator_next(&it);
	__CPROVER_assert(it_value == NULL, "next value is NULL");

	struct test_type *value = xmalloc(sizeof(*value));
	__CPROVER_assume(value != NULL);
	value->val1 = nondet_uint32_t();
	value->val2 = nondet_uint32_t();

	test_heap_insert(&heap, value);

	/* Heap iterator next value in a heap with a single element. */
	test_heap_iterator_init(&heap, &it);
	it_value = test_heap_iterator_next(&it);
	/* __CPROVER_assert(it_value != NULL, "next value is not NULL"); */
	__CPROVER_assert(test_heap_check(&heap) == 0, "check heap invariants");

	test_heap_destroy(&heap);
	free(value);
}

int
main(int argc, const char **argv)
{
#if defined(HEAP_CREATE)
	/* Initialize the heap. */
	heap_create_harness();
#elif defined(HEAP_DELETE)
	/* Delete node from heap. */
	heap_delete_harness();
#elif defined(HEAP_DESTROY)
	/* Destroy current heap. */
	heap_destroy_harness();
#elif defined(HEAP_INSERT)
	/* Insert value. */
	heap_insert_harness();
#elif defined(HEAP_ITERATOR_INIT)
	/* Heap iterator init. */
	heap_iterator_init_harness();
#elif defined(HEAP_ITERATOR_NEXT)
	/* Heap iterator next. */
	heap_iterator_next_harness();
#elif defined(HEAP_POP)
	/* Erase min value. */
	heap_pop_harness();
#elif defined(HEAP_TOP)
	/* Return min value. */
	heap_top_harness();
#elif defined(HEAP_UPDATE_ALL)
	/* Heapify tree after updating all values. */
	heap_update_all_harness();
#elif defined(HEAP_UPDATE)
	/* Heapify tree after update of value. */
	heap_update_harness();
#endif
}
