#include "salad/bloom.h"
#include <unordered_set>
#include <vector>
#include <iostream>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

using namespace std;

uint32_t h(uint32_t i)
{
	return i * 2654435761;
}

void
simple_test()
{
	plan(2);
	header();

	srand(time(0));
	uint32_t error_count = 0;
	uint32_t fp_rate_too_big = 0;
	for (double p = 0.001; p < 0.5; p *= 1.3) {
		uint64_t tests = 0;
		uint64_t false_positive = 0;
		for (uint32_t count = 1000; count <= 10000; count *= 2) {
			struct bloom bloom;
			bloom_create(&bloom, count, p);
			void *bloom_data = xcalloc(1, bloom_data_size(&bloom));
			unordered_set<uint32_t> check;
			for (uint32_t i = 0; i < count; i++) {
				uint32_t val = rand() % (count * 10);
				check.insert(val);
				bloom_add(&bloom, bloom_data, h(val));
			}
			for (uint32_t i = 0; i < count * 10; i++) {
				bool has = check.find(i) != check.end();
				bool bloom_possible = bloom_maybe_has(
					&bloom, bloom_data, h(i));
				tests++;
				if (has && !bloom_possible)
					error_count++;
				if (!has && bloom_possible)
					false_positive++;
			}
			free(bloom_data);
		}
		double fp_rate = (double)false_positive / tests;
		if (fp_rate > p + 0.001)
			fp_rate_too_big++;
	}
	ok(error_count == 0, "There were %u errors, 0 expected", error_count);
	ok(fp_rate_too_big == 0, "False positive rate was higher than "
	   "expected in %u cases", fp_rate_too_big);

	footer();
}

static void
merge_test()
{
	plan(2);
	header();

	srand(time(0));
	uint32_t error_count = 0;
	double p = 0.01;
	uint32_t count = 10000;

	uint64_t tests = 0;
	uint64_t false_positive = 0;
	for (uint32_t count = 1000; count <= 10000; count *= 2) {
		struct bloom bloom;
		bloom_create(&bloom, count, p);
		void *bloom_data_a = xcalloc(1, bloom_data_size(&bloom));
		void *bloom_data_b = xcalloc(1, bloom_data_size(&bloom));

		unordered_set<uint32_t> check;
		for (uint32_t i = 0; i < count; i++) {
			uint32_t val = rand() % (count * 10);
			check.insert(val);
			uint32_t bucket = rand() % 3;
			if (bucket == 0) {
				bloom_add(&bloom, bloom_data_a, h(val));
			} else if (bucket == 1) {
				bloom_add(&bloom, bloom_data_b, h(val));
			} else {
				bloom_add(&bloom, bloom_data_a, h(val));
				bloom_add(&bloom, bloom_data_b, h(val));
			}
		}

		bloom_merge(&bloom, bloom_data_a, bloom_data_b);
		for (uint32_t i = 0; i < count * 10; i++) {
			bool has = check.find(i) != check.end();
			bool bloom_possible = bloom_maybe_has(
				&bloom, bloom_data_a, h(i));
			tests++;
			if (has && !bloom_possible)
				error_count++;
			if (!has && bloom_possible)
				false_positive++;
		}
		free(bloom_data_a);
		free(bloom_data_b);
	}
	double fp_rate = (double)false_positive / tests;

	ok(error_count == 0, "There were %u errors, 0 expected", error_count);
	ok(fp_rate <= p + 0.001, "False positive %lf must be lower than %lf",
	   fp_rate, p + 0.001);

	footer();
}

int
main(void)
{
	simple_test();
	merge_test();
}
