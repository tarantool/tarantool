#include "memory.h"
#include "fiber.h"
#include "tuple.h"
#include <stdio.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

enum {
	FEW_REFS = 10,
	MANY_REFS = 1000,
	TEST_MAX_TUPLE_COUNT = 1024,
	RAND_TEST_ROUNDS = 1024 * 1024,
};

static char tuple_buf[64];
static char *tuple_end = tuple_buf;

size_t tuple_count = 0;
size_t tuple_delete_error_count = 0;
static struct tuple *allocated_tuples[TEST_MAX_TUPLE_COUNT];

static struct tuple_format *patched_format;
static struct tuple * (*save_tuple_new)(struct tuple_format *format,
					const char *data, const char *end);
static void (*save_tuple_delete)(struct tuple_format *format,
				 struct tuple *tuple);

static void patch_format();
static void restore_format();

static struct tuple *
test_tuple_new(struct tuple_format *format,
	       const char *data, const char *end)
{
	assert(format == patched_format);
	assert(tuple_count < TEST_MAX_TUPLE_COUNT);
	restore_format();
	struct tuple *tuple = save_tuple_new(format, data, end);
	patch_format();
	allocated_tuples[tuple_count++] = tuple;
	return tuple;
}

static void
test_tuple_delete(struct tuple_format *format, struct tuple *tuple)
{
	assert(format == patched_format);
	assert(tuple_count > 0);
	size_t pos = 0;
	while (pos < tuple_count && allocated_tuples[pos] != tuple)
		pos++;
	/* If the tuple was not found there's no reason to continue. */
	fail_unless(pos < tuple_count);
	allocated_tuples[pos] = allocated_tuples[--tuple_count];
	restore_format();
	save_tuple_delete(format, tuple);
	patch_format();
}

static void
patch_format()
{
	patched_format = box_tuple_format_default();
	save_tuple_new = patched_format->vtab.tuple_new;
	save_tuple_delete = patched_format->vtab.tuple_delete;
	patched_format->vtab.tuple_new = test_tuple_new;
	patched_format->vtab.tuple_delete = test_tuple_delete;
}

static void
restore_format()
{
	assert(patched_format == box_tuple_format_default());
	patched_format->vtab.tuple_new = save_tuple_new;
	patched_format->vtab.tuple_delete =save_tuple_delete;
}

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
 * The test references one tuple different amount of times and checks that
 * after corresponding amount of dereferences the tuple is deleted.
 */
static void
test_one()
{
	header();
	plan(12);

	struct tuple *tuple;

	/* one ref */
	tuple = create_tuple();

	is(tuple_count, 1, "allocated");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	tuple_unref(tuple);

	is(tuple_count, 0, "deallocated");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	/* few refs */
	tuple = create_tuple();
	for (size_t j = 1; j < FEW_REFS; j++)
		tuple_ref(tuple);

	is(tuple_count, 1, "allocated");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	for (size_t j = 0; j < FEW_REFS; j++)
		tuple_unref(tuple);

	is(tuple_count, 0, "deallocated");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	/* many refs */
	tuple = create_tuple();
	for (size_t j = 1; j < MANY_REFS; j++)
		tuple_ref(tuple);

	is(tuple_count, 1, "allocated");
	is(tuple_bigref_tuple_count(), 1, "bigrefs");

	for (size_t j = 0; j < MANY_REFS; j++)
		tuple_unref(tuple);

	is(tuple_count, 0, "all deallocated");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	footer();
	check_plan();
}

/**
 * The test references a bunch of tuples different amount of times and checks
 * that after corresponding amount of dereferences the tuples are deleted.
 */
static void
test_batch()
{
	header();
	plan(12);

	struct tuple *tuples[TEST_MAX_TUPLE_COUNT];

	/* one ref */
	for (size_t i = 0; i < TEST_MAX_TUPLE_COUNT; i++)
		tuples[i] = create_tuple();

	is(tuple_count, TEST_MAX_TUPLE_COUNT, "all allocated");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	for (size_t i = 0; i < TEST_MAX_TUPLE_COUNT; i++)
		tuple_unref(tuples[i]);

	is(tuple_count, 0, "all deallocated");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	/* few refs */
	for (size_t i = 0; i < TEST_MAX_TUPLE_COUNT; i++)
		tuples[i] = create_tuple();
	for (size_t i = 0; i < TEST_MAX_TUPLE_COUNT; i++)
		for (size_t j = 1; j < FEW_REFS; j++)
			tuple_ref(tuples[i]);

	is(tuple_count, TEST_MAX_TUPLE_COUNT, "all allocated");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	for (size_t i = 0; i < TEST_MAX_TUPLE_COUNT; i++)
		for (size_t j = 0; j < FEW_REFS; j++)
			tuple_unref(tuples[i]);

	is(tuple_count, 0, "all deallocated");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	/* many refs */
	for (size_t i = 0; i < TEST_MAX_TUPLE_COUNT; i++)
		tuples[i] = create_tuple();
	for (size_t i = 0; i < TEST_MAX_TUPLE_COUNT; i++) {
		for (size_t j = 1; j < MANY_REFS; j++)
			tuple_ref(tuples[i]);
	}

	is(tuple_count, TEST_MAX_TUPLE_COUNT, "all allocated");
	is(tuple_bigref_tuple_count(), TEST_MAX_TUPLE_COUNT, "all bigrefs");

	for (size_t i = 0; i < TEST_MAX_TUPLE_COUNT; i++) {
		for (size_t j = 0; j < MANY_REFS; j++)
			tuple_unref(tuples[i]);
	}

	is(tuple_count, 0, "all deallocated");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	footer();
	check_plan();
}

/**
 * The test performs lots of random reference/dereference operations on
 * random tuples and checks that all tuples are deleted at right moment.
 */
static void
test_random()
{
	header();
	plan(2);

	struct tuple *tuples[TEST_MAX_TUPLE_COUNT];
	size_t ref_count[TEST_MAX_TUPLE_COUNT];

	for (size_t i = 0; i < TEST_MAX_TUPLE_COUNT; i++) {
		tuples[i] = create_tuple();
		ref_count[i] = 1;
	}
	size_t expected_tuple_count = TEST_MAX_TUPLE_COUNT;

	bool no_erros = true;
	for (size_t i = 0; i < RAND_TEST_ROUNDS; i++) {
		size_t pos = rand() % TEST_MAX_TUPLE_COUNT;
		if (ref_count[pos] == 0)
			continue;
		size_t action = rand() % 4;
		if (action == 0) {
			tuple_unref(tuples[pos]);
			ref_count[pos]--;
			if (ref_count[pos] == 0)
				expected_tuple_count--;
		} else {
			tuple_ref(tuples[pos]);
			ref_count[pos]++;
		}
		assert(expected_tuple_count == tuple_count);
		if (expected_tuple_count != tuple_count)
			no_erros = false;
	}

	while (tuple_count != 0)
		tuple_unref(allocated_tuples[0]);

	ok(no_erros, "no errors");
	is(tuple_bigref_tuple_count(), 0, "no bigrefs");

	footer();
	check_plan();
}

int
main()
{
	header();
	plan(3);

	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);
	patch_format();

	tuple_end = mp_encode_array(tuple_end, 1);
	tuple_end = mp_encode_uint(tuple_end, 2);

	test_one();
	test_batch();
	test_random();

	restore_format();
	tuple_free();
	fiber_free();
	memory_free();

	footer();
	check_plan();
}
