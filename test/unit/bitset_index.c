#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

#include <bitset/index.h>
#include "unit.h"

enum { NUMS_SIZE = 1 << 16 };

static
void test_resize(void)
{
	header();

	struct tt_bitset_index index;
	tt_bitset_index_create(&index, realloc);
	struct tt_bitset_iterator it;
	tt_bitset_iterator_create(&it, realloc);
	struct tt_bitset_expr expr;
	tt_bitset_expr_create(&expr, realloc);

	size_t key = 23411111;
	size_t value = 2321321;

	tt_bitset_index_insert(&index, &key, sizeof(key), value);

	fail_unless(tt_bitset_index_expr_equals(&expr, &key, sizeof(key)) == 0);

	fail_unless(tt_bitset_index_init_iterator(&index, &it, &expr) == 0);
	fail_unless(tt_bitset_iterator_next(&it) == value);
	fail_unless(tt_bitset_iterator_next(&it) == SIZE_MAX);

	tt_bitset_expr_destroy(&expr);
	tt_bitset_iterator_destroy(&it);
	tt_bitset_index_destroy(&index);

	footer();
}

static
void test_size_and_count(void)
{
	header();

	struct tt_bitset_index index;
	tt_bitset_index_create(&index, realloc);

	enum { P = 10, SIZE = (1 << P) + 1 };
	for(size_t i = 0; i < SIZE; i++) {
		tt_bitset_index_insert(&index, &i, sizeof(i), i);
	}

	fail_unless(tt_bitset_index_size(&index) == SIZE);
	fail_unless(tt_bitset_index_count(&index, 0) == SIZE / 2);
	fail_unless(tt_bitset_index_count(&index, 1) == SIZE / 2);
	fail_unless(tt_bitset_index_count(&index, 4) == SIZE / 2);
	fail_unless(tt_bitset_index_count(&index, P) == 1);
	fail_unless(tt_bitset_index_count(&index, P + 1) == 0);
	fail_unless(tt_bitset_index_count(&index, 2147483648) == 0);

	tt_bitset_index_destroy(&index);

	footer();
}

static
void check_keys(struct tt_bitset_index *index,
		size_t *keys, size_t *values, size_t size)
{
	struct tt_bitset_iterator it;
	tt_bitset_iterator_create(&it, realloc);
	struct tt_bitset_expr expr;
	tt_bitset_expr_create(&expr, realloc);

	printf("Checking keys... ");
	for (size_t i = 0; i < size; i++) {
		/* ignore removed keys */
		if (keys[i] == SIZE_MAX) {
			continue;
		}

		fail_unless(tt_bitset_index_expr_equals(&expr, &keys[i],
							sizeof(keys[i])) == 0);

		fail_unless(
			tt_bitset_index_init_iterator(index, &it, &expr) == 0);

		size_t pos;

		bool pair_found = false;
		while ( (pos = tt_bitset_iterator_next(&it)) != SIZE_MAX) {
			if (pos == values[i]) {
				pair_found = true;
				break;
			}
		}
		fail_unless(pair_found);
	}
	printf("ok\n");

	tt_bitset_iterator_destroy(&it);
	tt_bitset_expr_destroy(&expr);
}

static
void test_insert_remove(void)
{
	header();

	struct tt_bitset_index index;
	tt_bitset_index_create(&index, realloc);

	size_t NUMS_SIZE = 1 << 11;
	size_t *keys = malloc(NUMS_SIZE * sizeof(size_t));
	size_t *values = malloc(NUMS_SIZE * sizeof(size_t));

	size_t count0 = 0;
	size_t count1 = 0;
	printf("Generating test set... ");
	for(size_t i = 0; i < NUMS_SIZE; i++) {
		keys[i] = rand();
		values[i] = rand();
		count0 += (keys[i] & 1) != 0 ? 1 : 0;
		count1 += (keys[i] & 2) != 0 ? 1 : 0;
	}
	printf("ok\n");

	printf("Inserting pairs... ");
	for(size_t i = 0; i < NUMS_SIZE; i++) {
		tt_bitset_index_insert(&index, &keys[i], sizeof(keys[i]),
				       values[i]);

	}
	printf("ok\n");

	check_keys(&index, keys, values, NUMS_SIZE);

	fail_unless(tt_bitset_index_count(&index, 0) == count0);
	fail_unless(tt_bitset_index_count(&index, 1) == count1);

	printf("Removing random pairs... ");
	for(size_t i = 0; i < NUMS_SIZE; i++) {
		if (rand() % 5 == 0) {
			tt_bitset_index_remove_value(&index, values[i]);
			keys[i] = SIZE_MAX;
		}
	}
	printf("ok\n");

	check_keys(&index, keys, values, NUMS_SIZE);

	tt_bitset_index_destroy(&index);

	free(keys);
	free(values);

	footer();
}


static
void test_simple(int mode, size_t search_mask)
{
	fail_unless(mode >= 0 && mode < 3);

	struct tt_bitset_index index;
	tt_bitset_index_create(&index, realloc);
	struct tt_bitset_iterator it;
	tt_bitset_iterator_create(&it, realloc);
	struct tt_bitset_expr expr;
	tt_bitset_expr_create(&expr, realloc);

	size_t check_count = 0;
	for (size_t key = 0; key < NUMS_SIZE; key++) {
		tt_bitset_index_insert(&index, &key, sizeof(key), key);
		if (mode == 0) {
			check_count++;
		} else if (mode == 1 && (key & search_mask) == search_mask) {
			check_count++;
		} else if (mode == 2 && (key & search_mask) != 0) {
			check_count++;
		}
	}

	if (mode == 0) {
		fail_unless(tt_bitset_index_expr_all(&expr) == 0);
	} else if (mode == 1) {
		fail_unless(tt_bitset_index_expr_all_set(&expr,
							 &search_mask,
							 sizeof(search_mask)) ==
			    0);
	} else if (mode == 2) {
		fail_unless(tt_bitset_index_expr_any_set(&expr,
							 &search_mask,
							 sizeof(search_mask)) ==
			    0);
	}
	fail_unless(tt_bitset_index_init_iterator(&index, &it, &expr) == 0);

	size_t found_count = 0;
	for (size_t key = 0; key < NUMS_SIZE; key++) {
		size_t r = tt_bitset_iterator_next(&it);
		if (mode == 0) {
			fail_unless(key == r);
			found_count++;
		} else if (mode == 1 && (key & search_mask) == search_mask) {
			found_count++;
		} else if (mode == 2 && (key & search_mask) != 0){
			found_count++;
		}
	}
	fail_unless(tt_bitset_iterator_next(&it) == SIZE_MAX);
	fail_unless(found_count == check_count);

	tt_bitset_expr_destroy(&expr);
	tt_bitset_iterator_destroy(&it);
	tt_bitset_index_destroy(&index);
}

static void
test_empty_simple(void)
{
	header();
	test_simple(1, 0); /* empty result */
	footer();
}

static void
test_all_simple(void)
{
	header();
	test_simple(0, 0);  /* all */
	footer();
}

static void
test_all_set_simple(void)
{
	header();
	size_t search_mask = 66; /* 0b1000010 */
	test_simple(1, search_mask);  /* all bits set */
	footer();
}

static void
test_any_set_simple(void)
{
	header();
	size_t search_mask = 66; /* 0b1000010 */
	test_simple(2, search_mask);  /* any bits set */
	footer();
}

static
void test_equals_simple(void)
{
	header();

	struct tt_bitset_index index;
	tt_bitset_index_create(&index, realloc);
	struct tt_bitset_iterator it;
	tt_bitset_iterator_create(&it, realloc);
	struct tt_bitset_expr expr;
	tt_bitset_expr_create(&expr, realloc);

	size_t mask = ~((size_t ) 7);
	for (size_t i = 0; i < NUMS_SIZE; i++) {
		size_t key = i & mask;
		size_t value = i;
		tt_bitset_index_insert(&index, &key, sizeof(key), value);
	}

	size_t key1 = (rand() % NUMS_SIZE) & mask;
	fail_unless(tt_bitset_index_expr_equals(&expr, &key1,
						sizeof(key1)) == 0);
	fail_unless(tt_bitset_index_init_iterator(&index, &it, &expr) == 0);

	for (size_t i = key1; i <= (key1 + ~mask); i++) {
		fail_unless(i == tt_bitset_iterator_next(&it));
	}
	fail_unless(tt_bitset_iterator_next(&it) == SIZE_MAX);

	tt_bitset_expr_destroy(&expr);
	tt_bitset_iterator_destroy(&it);
	tt_bitset_index_destroy(&index);

	footer();
}

int main(void)
{
	setbuf(stdout, NULL);

	test_size_and_count();
	test_resize();
	test_insert_remove();
	test_empty_simple();
	test_all_simple();
	test_all_set_simple();
	test_any_set_simple();
	test_equals_simple();

	return 0;
}
