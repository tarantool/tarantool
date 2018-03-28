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
					bloom_maybe_has(&bloom, h(i));
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
					bloom_maybe_has(&test, h(i));
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

int
main(void)
{
	simple_test();
	store_load_test();
}
