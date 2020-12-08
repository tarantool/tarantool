#include <core/random.h>

#include <stdio.h>

#include "unit.h"

static void
test_random_in_range_one(int64_t min, int64_t max)
{
	plan(2);
	int64_t result = pseudo_random_in_range(min, max);
	is(min <= result && result <= max, 1, "pseudo random is in range "
					      "%lld %lld", (long long)min,
							   (long long)max);

	result = real_random_in_range(min, max);
	is(min <= result && result <= max, 1, "real random is in range "
					      "%lld %lld", (long long)min,
							   (long long)max);
	check_plan();
}

static void
test_random_in_range(void)
{
	header();

	test_random_in_range_one(INT64_MIN, INT64_MAX);
	test_random_in_range_one(INT64_MIN, INT64_MIN);
	test_random_in_range_one(INT64_MAX, INT64_MAX);
	test_random_in_range_one(-1, -1);
	test_random_in_range_one(0, 0);
	test_random_in_range_one(1, 1);

	test_random_in_range_one(INT64_MIN + 1, INT64_MAX - 1);
	test_random_in_range_one(INT64_MIN / 2, INT64_MAX / 2);
	test_random_in_range_one(INT64_MIN, INT64_MIN / 2);
	test_random_in_range_one(INT64_MAX / 2, INT64_MAX);
	test_random_in_range_one(-2, -1);
	test_random_in_range_one(1, 2);
	test_random_in_range_one(-1, 1);
	test_random_in_range_one(0, 1);

	footer();
}

int
main(void)
{
	random_init();

	plan(14);

	test_random_in_range();

	random_free();

	return check_plan();
}
