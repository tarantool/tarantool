#include "tt_sort.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "fiber.h"
#include "memory.h"
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static int test_result = 1;

static int
cmp_testing(const void *a, const void *b, void *arg)
{
	uint64_t i, j;
	memcpy(&i, a, sizeof(i));
	memcpy(&j, b, sizeof(j));
	int ret = i < j ? -1 : i > j;
	if (arg == nullptr)
		return ret;
	return -ret;
}

/**
 * For low sizes sorting is done in calling thread without yielding and
 * using qsort_arg_st.
 */
static void
test_no_extra_threads(void)
{
	/* Sizes less than 7 are sorted using n^2 algorithm. */
	const size_t sizes[] = {3, 5, 7, 8, 100, 207, 331};
	plan(lengthof(sizes));
	header();

	auto gen = std::mt19937_64{}; /* No seed for reproducibility. */
	std::vector<uint64_t> data;

	for (size_t N : sizes) {
		data.resize(N);
		for (auto &v : data)
			v = gen();

		tt_sort(data.data(), N, sizeof(data[0]), cmp_testing,
			nullptr, 4);

		ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");
	}

	footer();
	check_plan();
}

static void
test_no_extra_threads_presorted(void)
{
	plan(3);
	header();

	std::vector<uint64_t> data;
	int N = 100;
	data.resize(N);

	/* All elements are equal. */
	for (auto &v : data)
		v = 1;
	tt_sort(data.data(), N, sizeof(data[0]), cmp_testing, nullptr, 4);
	ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");

	/* Data is presorted. */
	for (int i = 0; i < N; i++)
		data[i] = i;
	tt_sort(data.data(), N, sizeof(data[0]), cmp_testing, nullptr, 4);
	ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");

	/* Data is presorted but in descending order. */
	for (int i = 0; i < N; i++)
		data[i] = N - i;
	tt_sort(data.data(), N, sizeof(data[0]), cmp_testing, nullptr, 4);
	ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");

	footer();
	check_plan();
}

/**
 * For big sizes sorting is done in multiple threads using sample sort
 * algorithm.
 */
static void
test_multi_threaded(void)
{
	size_t sizes[] = {10000, 100000, 200000};
	size_t threads[] = {1, 2, 3, 4, 7, 8};
	plan(lengthof(sizes) * lengthof(threads));
	header();

	auto gen = std::mt19937_64{}; /* No seed for reproducibility. */
	std::vector<uint64_t> data;

	for (size_t N : sizes) {
		data.resize(N);

		for (size_t t : threads) {
			for (auto &v : data)
				v = gen();

			tt_sort(data.data(), N, sizeof(data[0]), cmp_testing,
				nullptr, t);

			ok(std::is_sorted(data.begin(), data.end()),
			   "Must be sorted");
		}
	}

	footer();
	check_plan();
}

static void
test_presorted()
{
	plan(5);
	header();

	std::vector<uint64_t> data;
	int N = 20000;
	data.resize(N);

	/* All elements are equal. */
	for (auto &v : data)
		v = 1;
	tt_sort(data.data(), N, sizeof(data[0]), cmp_testing, nullptr, 4);
	ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");

	/* Data is presorted. */
	for (int i = 0; i < N; i++)
		data[i] = i;
	tt_sort(data.data(), N, sizeof(data[0]), cmp_testing, nullptr, 4);
	ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");

	/* Data is presorted but in descending order. */
	for (int i = 0; i < N; i++)
		data[i] = N - i;
	tt_sort(data.data(), N, sizeof(data[0]), cmp_testing, nullptr, 4);
	ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");

	/*
	 * Data is presorted in parts corresponding to threads but
	 * not globally.
	 */
	for (int i = 0; i < N / 2; i++)
		data[i] = i;
	for (int i = 0; i < N / 2; i++)
		data[N / 2 + i] = i;
	tt_sort(data.data(), N, sizeof(data[0]), cmp_testing, nullptr, 2);
	ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");

	/*
	 * Data is presorted on border of parts corresponding to threads
	 * but not in parts itself.
	 */
	for (int i = 0; i < N; i++)
		data[i] = i;
	data[N / 4] = 0;
	tt_sort(data.data(), N, sizeof(data[0]), cmp_testing, nullptr, 2);
	ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");

	footer();
	check_plan();
}

static void
test_degenerated_bucket()
{
	plan(1);
	header();

	auto gen = std::mt19937_64{}; /* No seed for reproducibility. */
	std::vector<uint64_t> data;
	int N = 20000;
	data.resize(N);

	/*
	 * Bucket splitters will be equal to 0 thus we put all elements to
	 * the last backet. First 3 buckets will have size 0.
	 */
	for (int i = 0; i < N; i++)
		data[i] = i % 7 == 0 ? gen() : 0;

	tt_sort(data.data(), N, sizeof(data[0]), cmp_testing, nullptr, 4);

	ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");

	footer();
	check_plan();
}

/*
 * Test extra argument is actually passed to compare callback.
 */
static void
test_extra_argument()
{
	plan(1);
	header();

	auto gen = std::mt19937_64{}; /* No seed for reproducibility. */
	std::vector<uint64_t> data;
	int N = 10000;
	data.resize(N);
	for (auto &v : data)
		v = gen();

	int arg;
	tt_sort(data.data(), N, sizeof(data[0]), cmp_testing, &arg, 3);
	std::reverse(data.begin(), data.end());
	ok(std::is_sorted(data.begin(), data.end()), "Must be sorted");

	footer();
	check_plan();
}

static int
main_f(va_list ap)
{
	plan(6);
	header();

	test_no_extra_threads();
	test_no_extra_threads_presorted();
	test_multi_threaded();
	test_presorted();
	test_degenerated_bucket();
	test_extra_argument();

	footer();
	test_result = check_plan();

	return 0;
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_cxx_invoke);

	struct fiber *main = fiber_new_xc("main", main_f);
	fiber_wakeup(main);
	ev_run(loop(), 0);

	fiber_free();
	memory_free();
	return test_result;
}
