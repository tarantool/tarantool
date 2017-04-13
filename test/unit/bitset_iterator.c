#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include <bitset/iterator.h>
#include "unit.h"

enum { NUMS_SIZE = 1 << 16 };
static size_t NUMS[NUMS_SIZE];

static struct bitset **
bitsets_create(size_t count)
{
	struct bitset **bitsets = malloc(count * sizeof(*bitsets));
	fail_if(bitsets == NULL);
	for (size_t i = 0; i < count; i++) {
		bitsets[i] = malloc(sizeof(struct bitset));
		fail_if(bitsets[i] == NULL);
		bitset_create(bitsets[i], realloc);
	}

	return bitsets;
}

static void
bitsets_destroy(struct bitset **bitsets, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		bitset_destroy(bitsets[i]);
		free(bitsets[i]);
	}

	free(bitsets);
}

static void
nums_fill(size_t *nums, size_t size)
{
	const size_t STEP_MAX = 7;
	nums[0] = rand() % STEP_MAX;
	for (size_t i = 1; i < size; i++) {
		nums[i] = nums[i - 1] + 1 + rand() % STEP_MAX;
	}
}

static int
nums_comparator(const void *a, const void *b)
{
	size_t *aa = (size_t *) a;
	size_t *bb = (size_t *) b;

	if (*aa < *bb) {
		return -1;
	} else if (*aa > *bb) {
		return 1;
	} else {
		return 0;
	}
}

static void
nums_sort(size_t *nums, size_t size)
{
	qsort(nums, size, sizeof(*nums), nums_comparator);
}

static void
nums_shuffle(size_t *nums, size_t size)
{
	for (size_t i = 0; i < size - 1; i++) {
		size_t j = i + rand() / (RAND_MAX / (size- i) + 1);
		size_t tmp = nums[j];
		nums[j] = nums[i];
		nums[i] = tmp;
	}
}

static
void test_empty_expr(void)
{
	header();

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);
	struct bitset_iterator it;
	bitset_iterator_create(&it, realloc);

	fail_unless(bitset_iterator_init(&it, &expr, NULL, 0) == 0);
	bitset_expr_destroy(&expr);

	size_t pos = bitset_iterator_next(&it);
	fail_unless(pos == SIZE_MAX);

	bitset_iterator_destroy(&it);

	footer();
}

static
void test_empty_expr_conj1(void)
{
	header();

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);
	struct bitset_iterator it;
	bitset_iterator_create(&it, realloc);

	fail_unless(bitset_expr_add_conj(&expr) == 0);

	fail_unless(bitset_iterator_init(&it, &expr, NULL, 0) == 0);
	bitset_expr_destroy(&expr);

	size_t pos = bitset_iterator_next(&it);
	fail_unless(pos == SIZE_MAX);

	bitset_iterator_destroy(&it);

	footer();
}

static
void test_empty_expr_conj2(void)
{
	header();

	size_t big_i = (size_t) 1 << 15;
	struct bitset **bitsets = bitsets_create(2);
	bitset_set(bitsets[0], 1);
	bitset_set(bitsets[0], big_i);

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);
	struct bitset_iterator it;
	bitset_iterator_create(&it, realloc);

	fail_unless(bitset_expr_add_conj(&expr) == 0);
	fail_unless(bitset_expr_add_conj(&expr) == 0);
	fail_unless(bitset_expr_add_conj(&expr) == 0);

	fail_unless(bitset_expr_add_param(&expr, 0, false) == 0);
	fail_unless(bitset_expr_add_param(&expr, 1, true) == 0);
	fail_unless(bitset_expr_add_conj(&expr) == 0);
	fail_unless(bitset_expr_add_conj(&expr) == 0);

	fail_unless(bitset_iterator_init(&it, &expr, bitsets, 2) == 0);
	bitset_expr_destroy(&expr);

	fail_unless(bitset_iterator_next(&it) == 1);
	fail_unless(bitset_iterator_next(&it) == big_i);
	fail_unless(bitset_iterator_next(&it) == SIZE_MAX);

	bitset_iterator_destroy(&it);
	bitsets_destroy(bitsets, 2);

	footer();
}

static
void test_empty_result(void)
{
	header();

	struct bitset **bitsets = bitsets_create(2);

	bitset_set(bitsets[0], 1);
	bitset_set(bitsets[0], 2);
	bitset_set(bitsets[0], 3);
	bitset_set(bitsets[0], 193);
	bitset_set(bitsets[0], 1024);

	bitset_set(bitsets[0], 1025);
	bitset_set(bitsets[0], 16384);
	bitset_set(bitsets[0], 16385);

	bitset_set(bitsets[1], 17);
	bitset_set(bitsets[1], 194);
	bitset_set(bitsets[1], 1023);

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);

	fail_unless(bitset_expr_add_conj(&expr) == 0);

	fail_unless(bitset_expr_add_param(&expr, 0, false) == 0);
	fail_unless(bitset_expr_add_param(&expr, 1, false) == 0);

	struct bitset_iterator it;
	bitset_iterator_create(&it, realloc);

	fail_unless(bitset_iterator_init(&it, &expr, bitsets, 2) == 0);
	bitset_expr_destroy(&expr);

	size_t pos = bitset_iterator_next(&it);
	fail_unless(pos == SIZE_MAX);

	bitset_iterator_destroy(&it);

	bitsets_destroy(bitsets, 2);

	footer();
}

static
void test_first_result(void)
{
	header();

	struct bitset **bitsets = bitsets_create(2);

	bitset_set(bitsets[0], 0);
	bitset_set(bitsets[0], 1023);

	bitset_set(bitsets[1], 0);
	bitset_set(bitsets[1], 1025);

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);

	fail_unless(bitset_expr_add_conj(&expr) == 0);

	fail_unless(bitset_expr_add_param(&expr, 0, false) == 0);
	fail_unless(bitset_expr_add_param(&expr, 1, false) == 0);

	struct bitset_iterator it;
	bitset_iterator_create(&it, realloc);
	fail_unless(bitset_iterator_init(&it, &expr, bitsets, 2) == 0);
	bitset_expr_destroy(&expr);

	size_t pos = bitset_iterator_next(&it);

	fail_unless(pos == 0);
	fail_unless(bitset_iterator_next(&it) == SIZE_MAX);

	bitset_iterator_destroy(&it);

	bitsets_destroy(bitsets, 2);

	footer();
}

static
void test_simple()
{
	header();

	enum { BITSETS_SIZE = 32 };

	struct bitset **bitsets = bitsets_create(BITSETS_SIZE);

	nums_shuffle(NUMS, NUMS_SIZE);

	size_t NOISE_SIZE = NUMS_SIZE / 3;
	for (size_t i = 0; i < NOISE_SIZE; i++) {
		bitset_set(bitsets[i % BITSETS_SIZE], NUMS[i]);
	}

	for (size_t i = NOISE_SIZE; i < NUMS_SIZE; i++) {
		for (size_t b = 0; b < BITSETS_SIZE; b++) {
			bitset_set(bitsets[b], NUMS[i]);
		}
	}

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);
	fail_unless(bitset_expr_add_conj(&expr) == 0);

	for (size_t b = 0; b < BITSETS_SIZE; b++) {
		fail_unless(bitset_expr_add_param(&expr, b, false) == 0);
	}

	nums_sort(NUMS + NOISE_SIZE, NUMS_SIZE - NOISE_SIZE);

	struct bitset_iterator it;
	bitset_iterator_create(&it, realloc);
	fail_unless(bitset_iterator_init(&it, &expr, bitsets, BITSETS_SIZE) == 0);
	bitset_expr_destroy(&expr);

	for (size_t i = NOISE_SIZE; i < NUMS_SIZE; i++) {
		fail_unless(bitset_iterator_next(&it) == NUMS[i]);
	}
	fail_unless(bitset_iterator_next(&it) == SIZE_MAX);

	bitset_iterator_destroy(&it);
	bitsets_destroy(bitsets, BITSETS_SIZE);

	footer();
}

static
void test_big() {
	header();

	const size_t BITSETS_SIZE = 32;
	struct bitset **bitsets = bitsets_create(BITSETS_SIZE);

	nums_shuffle(NUMS, NUMS_SIZE);

	printf("Setting bits... ");
	for (size_t i = 0; i < NUMS_SIZE; i++) {
		for (size_t b = 0; b < BITSETS_SIZE; b++) {
			bitset_set(bitsets[b], NUMS[i]);
			if (b % 2 == 0 && i % 2 == 0)
				continue;
		}
	}
	printf("ok\n");

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);
	fail_unless(bitset_expr_add_conj(&expr) == 0);
	for(size_t b = 0; b < BITSETS_SIZE; b++) {
		fail_unless(bitset_expr_add_param(&expr, b, false) == 0);
	}

	struct bitset_iterator it;
	bitset_iterator_create(&it, realloc);
	fail_unless(bitset_iterator_init(&it, &expr, bitsets, BITSETS_SIZE) == 0);
	bitset_expr_destroy(&expr);

	printf("Iterating... ");
	size_t pos;
	while ((pos = bitset_iterator_next(&it)) != SIZE_MAX) {
		size_t b;
		for(b = 0; b < BITSETS_SIZE; b++) {
			if(bitset_test(bitsets[b], pos))
				continue;
		}

		fail_if(b < BITSETS_SIZE);
	}
	printf("ok\n");

	bitset_iterator_destroy(&it);

	bitsets_destroy(bitsets, BITSETS_SIZE);

	footer();
}

static
void test_not_last() {
	header();

	struct bitset **bitsets = bitsets_create(2);

	size_t big_i = (size_t) 1 << 15;

	bitset_set(bitsets[0], 0);
	bitset_set(bitsets[0], 11);
	bitset_set(bitsets[0], 1024);

	bitset_set(bitsets[1], 0);
	bitset_set(bitsets[1], 10);
	bitset_set(bitsets[1], 11);
	bitset_set(bitsets[1], 14);
	bitset_set(bitsets[1], big_i);

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);

	fail_unless(bitset_expr_add_conj(&expr) == 0);
	fail_unless(bitset_expr_add_param(&expr, 0, true) == 0);
	fail_unless(bitset_expr_add_param(&expr, 1, false) == 0);

	struct bitset_iterator it;
	bitset_iterator_create(&it, realloc);
	fail_unless(bitset_iterator_init(&it, &expr, bitsets, 2) == 0);
	bitset_expr_destroy(&expr);

	size_t result[] = {10, 14, big_i};
	size_t result_size = 3;

	size_t pos;
	for (size_t i = 0; i < result_size; i++) {
		pos = bitset_iterator_next(&it);
		fail_unless (result[i] == pos);
	}
	fail_unless ((pos = bitset_iterator_next(&it)) == SIZE_MAX);

	bitset_iterator_destroy(&it);

	bitsets_destroy(bitsets, 2);

	footer();
}

static
void test_not_empty() {
	header();

	enum {
		BITSETS_SIZE = 4,
		CHECK_COUNT = (size_t) 1 << 14
	};

	struct bitset **bitsets = bitsets_create(BITSETS_SIZE);

	nums_shuffle(NUMS, NUMS_SIZE);
	for (size_t i = 0; i < NUMS_SIZE; i++) {
		bitset_set(bitsets[i % BITSETS_SIZE], NUMS[i]);
	}

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);

	for(size_t b = 0; b < BITSETS_SIZE; b++) {
		fail_unless(bitset_expr_add_conj(&expr) == 0);
		fail_unless(bitset_expr_add_param(&expr, b, true) == 0);
	}

	struct bitset_iterator it;
	bitset_iterator_create(&it, realloc);
	fail_unless(bitset_iterator_init(&it, &expr, bitsets, BITSETS_SIZE) == 0);
	bitset_expr_destroy(&expr);


	for (size_t i = 0; i < CHECK_COUNT; i++) {
		size_t pos = bitset_iterator_next(&it);
		fail_unless (i == pos);
	}

	bitset_iterator_destroy(&it);

	bitsets_destroy(bitsets, BITSETS_SIZE);

	footer();
}

static
void test_disjunction()
{
	header();

	enum { BITSETS_SIZE = 32 };

	struct bitset **bitsets = bitsets_create(BITSETS_SIZE);

	nums_shuffle(NUMS, NUMS_SIZE);

	for (size_t i = 0; i < NUMS_SIZE; i++) {
		bitset_set(bitsets[i % BITSETS_SIZE], NUMS[i]);
	}

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);

	for (size_t b = 0; b < BITSETS_SIZE; b++) {
		fail_unless(bitset_expr_add_conj(&expr) == 0);
		fail_unless(bitset_expr_add_param(&expr, b, false) == 0);
	}

	nums_sort(NUMS, NUMS_SIZE);

	struct bitset_iterator it;
	bitset_iterator_create(&it, realloc);
	fail_unless(bitset_iterator_init(&it, &expr, bitsets, BITSETS_SIZE) == 0);
	bitset_expr_destroy(&expr);

	for (size_t i = 0; i < NUMS_SIZE; i++) {
		size_t pos = bitset_iterator_next(&it);
		fail_unless(pos == NUMS[i]);
	}

	size_t pos = bitset_iterator_next(&it);
	fail_unless(pos == SIZE_MAX);

	bitset_iterator_destroy(&it);

	bitsets_destroy(bitsets, BITSETS_SIZE);

	footer();
}

int main(void)
{
	setbuf(stdout, NULL);
	nums_fill(NUMS, NUMS_SIZE);

	test_empty_expr();
	test_empty_expr_conj1();
	test_empty_expr_conj2();
	test_empty_result();
	test_first_result();
	test_simple();
	test_big();
	test_not_empty();
	test_not_last();
	test_disjunction();

	return 0;
}
