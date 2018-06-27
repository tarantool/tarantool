#include "memory.h"
#include "fiber.h"
#include "tuple.h"
#include "unit.h"
#include <stdio.h>

enum {
	BIGREF_DIFF = 10,
	BIGREF_COUNT = 70003,
	BIGREF_CAPACITY = 107,
};

static char tuple_buf[64];
static char *tuple_end = tuple_buf;

/**
 * This function creates new tuple with refs == 1.
 */
static inline struct tuple *
create_tuple()
{
	struct tuple *ret =
		tuple_new(box_tuple_format_default(), tuple_buf, tuple_end);
	tuple_ref(ret);
	return ret;
}

/**
 * This test performs overall check of bigrefs.
 * What it checks:
 * 1) Till refs <= TUPLE_REF_MAX it shows number of refs
 * of tuple and it isn't a bigref.
 * 2) When refs > TUPLE_REF_MAX first 15 bits of it becomes
 * index of bigref and the last bit becomes true which
 * shows that it is bigref.
 * 3) Each of tuple has its own number of refs, but all
 * these numbers more than it is needed for getting a bigref.
 * 4) Indexes of bigrefs are given sequentially.
 * 5) After some tuples are sequentially deleted all of
 * others bigrefs are fine. In this test BIGREF_CAPACITY
 * tuples created and each of their ref counter increased
 * to (BIGREF_COUNT - index of tuple). Tuples are created
 * consistently.
 */
static void
test_bigrefs_overall()
{
	header();
	plan(3);
	uint16_t counter = 0;
	struct tuple **tuples = (struct tuple **) malloc(BIGREF_CAPACITY *
							 sizeof(*tuples));
	for(int i = 0; i < BIGREF_CAPACITY; ++i)
		tuples[i] = create_tuple();
	for(int i = 0; i < BIGREF_CAPACITY; ++i)
		counter += tuples[i]->refs == 1;
	is(counter, BIGREF_CAPACITY, "All tuples have refs == 1.");
	for(int i = 0; i < BIGREF_CAPACITY; ++i) {
		for(int j = 1; j < TUPLE_REF_MAX; ++j)
			tuple_ref(tuples[i]);
		tuple_ref(tuples[i]);
		for(int j = TUPLE_REF_MAX + 1; j < BIGREF_COUNT - i; ++j)
			tuple_ref(tuples[i]);
	}
	counter = 0;
	for(int i = 0; i < BIGREF_CAPACITY; ++i)
		counter += tuples[i]->is_bigref == true;
	is(counter, BIGREF_CAPACITY, "All tuples have bigrefs.");
	counter = 0;
	for(int i = 0; i < BIGREF_CAPACITY; ++i) {
		for(int j = 1; j < BIGREF_COUNT - i; ++j)
			tuple_unref(tuples[i]);
		counter += tuples[i]->refs == 1;
		tuple_unref(tuples[i]);
	}
	is(counter, BIGREF_CAPACITY, "All tuples were deleted.");
	free(tuples);
	footer();
	check_plan();
}

/**
 * This test checks that indexes are given as
 * intended.
 */
static void
test_bigrefs_non_consistent()
{
	header();
	plan(3);
	uint16_t counter = 0;
	uint16_t max_index = BIGREF_CAPACITY / BIGREF_DIFF;
	struct tuple **tuples = (struct tuple **) malloc(BIGREF_CAPACITY *
							 sizeof(*tuples));
	uint16_t *indexes = (uint16_t *) malloc(sizeof(*indexes) *
						(max_index + 1));
	for(int i = 0; i < BIGREF_CAPACITY; ++i)
		tuples[i] = create_tuple();
	for(int i = 0; i < BIGREF_CAPACITY; ++i) {
		for(int j = 1; j < BIGREF_COUNT; ++j)
			tuple_ref(tuples[i]);
		counter += tuples[i]->is_bigref == true;
	}
	is(counter, BIGREF_CAPACITY, "All tuples have bigrefs.");
	counter = 0;
	uint16_t index = 0;
	for(int i = 0; i < BIGREF_CAPACITY; i += BIGREF_DIFF) {
		indexes[index] = tuples[i]->ref_index;
		for(int j = 1; j < BIGREF_COUNT; ++j)
			tuple_unref(tuples[i]);
		index++;
		counter += tuples[i]->is_bigref == false;
	}
	is(counter, max_index + 1, "%d tuples don't have bigrefs "\
	   "and all other tuples have", max_index + 1);
	counter = 0;
	index = 0;
	for(int i = 0; i < BIGREF_CAPACITY; i += BIGREF_DIFF) {
		bool check_refs = tuples[i]->refs == 1;
		for(int j = 1; j < BIGREF_COUNT; ++j)
			tuple_ref(tuples[i]);
		counter += check_refs && tuples[i]->is_bigref &&
			   tuples[i]->ref_index == indexes[max_index - index];
		index++;
	}
	is(counter, max_index + 1, "All tuples have bigrefs and "\
	   "their indexes are in right order.");
	for (int i = 0; i < BIGREF_CAPACITY; ++i) {
		for (int j = 0; j < BIGREF_COUNT; ++j)
			tuple_unref(tuples[i]);
	}
	free(indexes);
	free(tuples);
	footer();
	check_plan();
}

int
main()
{
	header();
	plan(2);

	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);

	tuple_end = mp_encode_array(tuple_end, 1);
	tuple_end = mp_encode_uint(tuple_end, 2);

	test_bigrefs_overall();
	test_bigrefs_non_consistent();

	tuple_free();
	fiber_free();
	memory_free();

	footer();
	check_plan();
}
