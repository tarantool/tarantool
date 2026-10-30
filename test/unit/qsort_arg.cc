#include "qsort_arg.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

int
qsort_cmp(const void *a, const void *b, void *)
{
	uint64_t i, j;
	memcpy(&i, a, sizeof(i));
	memcpy(&j, b, sizeof(j));
	return i < j ? -1 : i > j;
}

/**
 * Checker of qsort_arg for different sizes. Calls one 'ok(..)' for each size.
 */
template <size_t Count>
static void
test_qsort_common(const size_t (&sizes)[Count])
{
	auto gen = std::mt19937_64{}; /* No seed for reproducibility. */
	std::vector<uint64_t> data;

	for (size_t N : sizes) {
		data.resize(N);
		for (auto &v : data)
			v = gen();

		qsort_arg(data.data(), N, sizeof(data[0]), qsort_cmp, nullptr);

		ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");
	}
}

/**
 * For low sizes a single-thread version of qsort is called.
 */
static void
test_qsort_st(void)
{
	const size_t sizes[] = {1000, 10000, 100000};
	plan(lengthof(sizes));
	header();

	test_qsort_common(sizes);

	footer();
	check_plan();
}

/**
 * For big sizes a multi-thread version of qsort is called.
 */
static void
test_qsort_mt(void)
{
	size_t sizes[] = {150000, 1000000, 4000000};
	plan(lengthof(sizes));
	header();

	test_qsort_common(sizes);

	footer();
	check_plan();
}

int
main(void)
{
	plan(2);
	header();

	test_qsort_st();
	test_qsort_mt();

	footer();
	return check_plan();
}
