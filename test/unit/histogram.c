#include <stdint.h>
#include <time.h>

#include "histogram.h"
#include "memory.h"
#include "unit.h"
#include "trivia/util.h"
#include "core/random.h"

static int
int64_cmp(const void *p1, const void *p2)
{
	int64_t v1 = *(int64_t *)p1;
	int64_t v2 = *(int64_t *)p2;
	if (v1 > v2)
		return 1;
	if (v1 < v2)
		return -1;
	return 0;
}

static void
int64_sort(int64_t *data, size_t len)
{
	qsort(data, len, sizeof(*data), int64_cmp);
}

static int64_t *
gen_buckets(size_t *p_n_buckets)
{
	size_t n_buckets = 1 + rand() % 20;
	int64_t *buckets = calloc(n_buckets, sizeof(*buckets));
	for (size_t i = 0; i < n_buckets; i++)
		buckets[i] = (i > 0 ? buckets[i - 1] : 0) + 1 + rand() % 2000;
	*p_n_buckets = n_buckets;
	return buckets;
}

static int64_t *
gen_rand_data(size_t *p_len)
{
	size_t len = 900 + rand() % 200;
	int64_t *data = calloc(len, sizeof(*data));
	for (size_t i = 0; i < len; i++)
		data[i] = rand() % 10000;
	*p_len = len;
	return data;
}

static void
test_counts(void)
{
	header();

	size_t n_buckets;
	int64_t *buckets = gen_buckets(&n_buckets);

	size_t data_len;
	int64_t *data = gen_rand_data(&data_len);

	struct histogram *hist = histogram_new(buckets, n_buckets);
	for (size_t i = 0; i < data_len; i++)
		histogram_collect(hist, data[i]);

	fail_if(hist->total != data_len);

	for (size_t b = 0; b < n_buckets; b++) {
		size_t expected = 0;
		for (size_t i = 0; i < data_len; i++) {
			if (data[i] <= buckets[b] &&
			    (b == 0 || data[i] > buckets[b - 1]))
				expected++;
		}
		fail_if(hist->buckets[b].count != expected);
	}

	histogram_delete(hist);
	free(data);
	free(buckets);

	footer();
}

static void
test_discard(void)
{
	header();

	size_t n_buckets;
	int64_t *buckets = gen_buckets(&n_buckets);

	struct histogram *hist = histogram_new(buckets, n_buckets);

	size_t bucket_sz = pseudo_random_in_range(2, 10);
	size_t data_len = (n_buckets + 1) * bucket_sz;
	int64_t *data = calloc(data_len, sizeof(*data));

	for (size_t b = 0; b <= n_buckets; b++) {
		int64_t min = (b == 0 ? INT64_MIN : buckets[b - 1] + 1);
		int64_t max = (b == n_buckets ? INT64_MAX : buckets[b]);
		for (size_t i = 0; i < bucket_sz; i++)
			data[b * bucket_sz + i] = pseudo_random_in_range(min, max);
	}

	for (size_t i = 0; i < data_len; i++)
		histogram_collect(hist, data[i]);

	for (size_t i = 0; i < data_len; i++) {
		if (i % bucket_sz < bucket_sz / 2)
			histogram_discard(hist, data[i]);
	}
	bucket_sz = (bucket_sz + 1) / 2;

	for (size_t b = 0; b < n_buckets; b++)
		fail_if(hist->buckets[b].count != bucket_sz);
	fail_if(hist->total != bucket_sz * (n_buckets + 1));

	histogram_delete(hist);
	free(data);
	free(buckets);

	footer();
}

static void
test_percentile(void)
{
	header();

	size_t n_buckets;
	int64_t *buckets = gen_buckets(&n_buckets);

	size_t data_len;
	int64_t *data = gen_rand_data(&data_len);

	int64_t max = -1;
	for (size_t i = 0; i < data_len; i++) {
		if (max < data[i])
			max = data[i];
	}

	struct histogram *hist = histogram_new(buckets, n_buckets);
	for (size_t i = 0; i < data_len; i++)
		histogram_collect(hist, data[i]);

	int64_sort(data, data_len);
	for (int pct = 5; pct < 100; pct += 5) {
		int64_t val = data[data_len * pct / 100];
		int64_t expected = max;
		int64_t expected_lo = max;
		for (size_t b = 0; b < n_buckets; b++) {
			if (buckets[b] >= val) {
				expected = buckets[b];
				expected_lo = buckets[b > 0 ? b - 1 : 0];
				break;
			}
		}
		int64_t result = histogram_percentile(hist, pct);
		fail_if(result != expected);
		int64_t result_lo = histogram_percentile_lower(hist, pct);
		fail_if(result_lo != expected_lo);
	}

	histogram_delete(hist);
	free(data);
	free(buckets);

	footer();
}

int
main()
{
	srand(time(NULL));
	test_counts();
	test_discard();
	test_percentile();
}
