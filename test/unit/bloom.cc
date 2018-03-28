#include "salad/bloom.h"
#include <unordered_set>
#include <vector>
#include <iostream>

using namespace std;

uint32_t h(uint32_t i)
{
	return i * 2654435761;
}

void
simple_test()
{
	cout << "*** " << __func__ << " ***" << endl;
	struct quota q;
	quota_init(&q, 100500);
	srand(time(0));
	uint32_t error_count = 0;
	uint32_t fp_rate_too_big = 0;
	for (double p = 0.001; p < 0.5; p *= 1.3) {
		uint64_t tests = 0;
		uint64_t false_positive = 0;
		for (uint32_t count = 1000; count <= 10000; count *= 2) {
			struct bloom bloom;
			bloom_create(&bloom, count, p, &q);
			unordered_set<uint32_t> check;
			for (uint32_t i = 0; i < count; i++) {
				uint32_t val = rand() % (count * 10);
				check.insert(val);
				bloom_add(&bloom, h(val));
			}
			for (uint32_t i = 0; i < count * 10; i++) {
				bool has = check.find(i) != check.end();
				bool bloom_possible =
					bloom_possible_has(&bloom, h(i));
				tests++;
				if (has && !bloom_possible)
					error_count++;
				if (!has && bloom_possible)
					false_positive++;
			}
			bloom_destroy(&bloom, &q);
		}
		double fp_rate = (double)false_positive / tests;
		if (fp_rate > p + 0.001)
			fp_rate_too_big++;
	}
	cout << "error_count = " << error_count << endl;
	cout << "fp_rate_too_big = " << fp_rate_too_big << endl;
	cout << "memory after destruction = " << quota_used(&q) << endl << endl;
}

void
store_load_test()
{
	cout << "*** " << __func__ << " ***" << endl;
	struct quota q;
	quota_init(&q, 100500);
	srand(time(0));
	uint32_t error_count = 0;
	uint32_t fp_rate_too_big = 0;
	for (double p = 0.01; p < 0.5; p *= 1.5) {
		uint64_t tests = 0;
		uint64_t false_positive = 0;
		for (uint32_t count = 300; count <= 3000; count *= 10) {
			struct bloom bloom;
			bloom_create(&bloom, count, p, &q);
			unordered_set<uint32_t> check;
			for (uint32_t i = 0; i < count; i++) {
				uint32_t val = rand() % (count * 10);
				check.insert(val);
				bloom_add(&bloom, h(val));
			}
			struct bloom test = bloom;
			char *buf = (char *)malloc(bloom_store_size(&bloom));
			bloom_store(&bloom, buf);
			bloom_destroy(&bloom, &q);
			memset(&bloom, '#', sizeof(bloom));
			bloom_load_table(&test, buf, &q);
			free(buf);
			for (uint32_t i = 0; i < count * 10; i++) {
				bool has = check.find(i) != check.end();
				bool bloom_possible =
					bloom_possible_has(&test, h(i));
				tests++;
				if (has && !bloom_possible)
					error_count++;
				if (!has && bloom_possible)
					false_positive++;
			}
			bloom_destroy(&test, &q);
		}
		double fp_rate = (double)false_positive / tests;
		double excess = fp_rate / p;
		if (fp_rate > p + 0.001)
			fp_rate_too_big++;
	}
	cout << "error_count = " << error_count << endl;
	cout << "fp_rate_too_big = " << fp_rate_too_big << endl;
	cout << "memory after destruction = " << quota_used(&q) << endl << endl;
}

void
spectrum_test()
{
	cout << "*** " << __func__ << " ***" << endl;
	struct quota q;
	quota_init(&q, 1005000);
	double p = 0.01;
	uint32_t count = 4000;
	struct bloom_spectrum spectrum;
	struct bloom bloom;

	/* using (count) */
	bloom_spectrum_create(&spectrum, count, p, &q);
	for (uint32_t i = 0; i < count; i++) {
		bloom_spectrum_add(&spectrum, h(i));
	}
	bloom_spectrum_choose(&spectrum, &bloom);
	bloom_spectrum_destroy(&spectrum, &q);

	uint64_t false_positive = 0;
	uint64_t error_count = 0;
	for (uint32_t i = 0; i < count; i++) {
		if (!bloom_possible_has(&bloom, h(i)))
			error_count++;
	}
	for (uint32_t i = count; i < 2 * count; i++) {
		if (bloom_possible_has(&bloom, h(i)))
			false_positive++;
	}
	bool fpr_rate_is_good = false_positive < 1.5 * p * count;
	cout << "bloom table size = " << bloom.table_size << endl;
	cout << "error_count = " << error_count << endl;
	cout << "fpr_rate_is_good = " << fpr_rate_is_good << endl;
	bloom_destroy(&bloom, &q);

	/* same test using (count * 10) */
	bloom_spectrum_create(&spectrum, count * 10, p, &q);
	for (uint32_t i = 0; i < count; i++) {
		bloom_spectrum_add(&spectrum, h(i));
	}
	bloom_spectrum_choose(&spectrum, &bloom);
	bloom_spectrum_destroy(&spectrum, &q);

	false_positive = 0;
	error_count = 0;
	for (uint32_t i = 0; i < count; i++) {
		if (!bloom_possible_has(&bloom, h(i)))
			error_count++;
	}
	for (uint32_t i = count; i < 2 * count; i++) {
		if (bloom_possible_has(&bloom, h(i)))
			false_positive++;
	}
	fpr_rate_is_good = false_positive < 1.5 * p * count;
	cout << "bloom table size = " << bloom.table_size << endl;
	cout << "error_count = " << error_count << endl;
	cout << "fpr_rate_is_good = " << fpr_rate_is_good << endl;
	bloom_destroy(&bloom, &q);

	cout << "memory after destruction = " << quota_used(&q) << endl << endl;
}

int
main(void)
{
	simple_test();
	store_load_test();
	spectrum_test();
}
