#include <stdlib.h>
#include <stdio.h>

#include <bitset/bitset.h>

#include "unit.h"

static
void test_cardinality()
{
	header();

	struct bitset bm;
	bitset_create(&bm, realloc);

	fail_unless(bitset_cardinality(&bm) == 0);

	size_t cnt = 0;
	fail_if(bitset_set(&bm, 10) < 0);
	cnt++;
	fail_if(bitset_set(&bm, 15) < 0);
	cnt++;
	fail_if(bitset_set(&bm, 20) < 0);
	cnt++;

	fail_unless(bitset_cardinality(&bm) == cnt);

	fail_if(bitset_set(&bm, 10) < 0);
	fail_unless(bitset_cardinality(&bm) == cnt);

	fail_if(bitset_clear(&bm, 20) < 0);
	cnt--;
	fail_unless(bitset_cardinality(&bm) == cnt);

	fail_if(bitset_clear(&bm, 20) < 0);
	fail_unless(bitset_cardinality(&bm) == cnt);

	fail_if(bitset_clear(&bm, 666) < 0);
	fail_unless(bitset_cardinality(&bm) == cnt);

	fail_if(bitset_clear(&bm, 10) < 0);
	cnt--;
	fail_unless(bitset_cardinality(&bm) == cnt);

	fail_if(bitset_clear(&bm, 15) < 0);
	cnt--;
	fail_unless(bitset_cardinality(&bm) == cnt);

	bitset_destroy(&bm);

	footer();
}

static
void shuffle(size_t *arr, size_t size)
{
	if (size <= 1) {
		return;
	}

	for (size_t i = 0; i < (size - 1); i++) {
		size_t j = i + rand() / (RAND_MAX / (size - i) + 1);
		size_t tmp = arr[i];
		arr[i] = arr[j];
		arr[j] = tmp;
	}
}

static
int size_compator(const void *a, const void *b)
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

static
void test_get_set()
{
	header();

	struct bitset bm;
	bitset_create(&bm, realloc);

	const size_t NUM_SIZE = (size_t) 1 << 14;
	size_t *nums = malloc(NUM_SIZE * sizeof(size_t));

	printf("Generating test set... ");
	for(size_t i = 0; i < NUM_SIZE; i++) {
		nums[i] = rand();
	}
	printf("ok\n");

	printf("Settings bits... ");
	for(size_t i = 0; i < NUM_SIZE; i++) {
		fail_if(bitset_set(&bm, nums[i]) < 0);
	}
	printf("ok\n");

	printf("Checking bits... ");
	shuffle(nums, NUM_SIZE);
	for(size_t i = 0; i < NUM_SIZE; i++) {
		fail_unless(bitset_test(&bm, nums[i]));
	}
	printf("ok\n");

	printf("Unsetting random bits... ");
	shuffle(nums, NUM_SIZE);
	for(size_t i = 0; i < NUM_SIZE; i++) {
		if (nums[i] % 5 == 0) {
			fail_if(bitset_clear(&bm, nums[i]) < 0);
			// printf("Clear :%zu\n", nums[i]);
			fail_if(bitset_test(&bm, nums[i]));
		}
	}
	printf("ok\n");

	printf("Checking set bits... ");
	shuffle(nums, NUM_SIZE);
	for(size_t i = 0; i < NUM_SIZE; i++) {
		if (nums[i] % 5 == 0) {
			continue;
		}

		if (!bitset_test(&bm, nums[i])) {
			printf("Fail :%zu\n", nums[i]);
		}
		fail_unless(bitset_test(&bm, nums[i]));
	}
	printf("ok\n");

	printf("Checking all bits... ");
	qsort(nums, NUM_SIZE, sizeof(size_t), size_compator);

	size_t *pn = nums;

	size_t i_max = (size_t) 1 << 14;
	if (i_max > RAND_MAX) {
		i_max = RAND_MAX;
	}

	for(size_t i = 0; i < i_max; i++) {
		if (*pn < SIZE_MAX && *pn == i) {
			fail_unless(bitset_test(&bm, *pn));
			pn++;
		} else {
			fail_if(bitset_test(&bm, i));
		}
	}
	printf("ok\n");


	printf("Unsetting all bits... ");
	shuffle(nums, NUM_SIZE);
	for(size_t i = 0; i < NUM_SIZE; i++) {
		if (nums[i] == SIZE_MAX) {
			continue;
		}

		fail_if(bitset_clear(&bm, nums[i]) < 0);
	}
	printf("ok\n");


	printf("Checking all bits... ");
	for(size_t i = 0; i < i_max; i++) {
		fail_if(bitset_test(&bm, i));
	}
	printf("ok\n");

	free(nums);

	bitset_destroy(&bm);

	footer();
}

int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);
	test_cardinality();
	test_get_set();

	return 0;
}
