#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <vector>
#include <time.h>

#include "unit.h"

typedef uint64_t hash_value_t;
typedef uint32_t hash_t;

static const size_t light_extent_size = 16 * 1024;
static size_t extents_count = 0;

hash_t
hash(hash_value_t value)
{
	return (hash_t) value;
}

bool
equal(hash_value_t v1, hash_value_t v2)
{
	return v1 == v2;
}

bool
equal_key(hash_value_t v1, hash_value_t v2)
{
	return v1 == v2;
}

#define LIGHT_NAME
#define LIGHT_DATA_TYPE uint64_t
#define LIGHT_KEY_TYPE uint64_t
#define LIGHT_CMP_ARG_TYPE int
#define LIGHT_EQUAL(a, b, arg) equal(a, b)
#define LIGHT_EQUAL_KEY(a, b, arg) equal_key(a, b)
#include "salad/light.h"

inline void *
my_light_alloc(void *ctx)
{
	size_t *p_extents_count = (size_t *)ctx;
	assert(p_extents_count == &extents_count);
	++*p_extents_count;
	return malloc(light_extent_size);
}

inline void
my_light_free(void *ctx, void *p)
{
	size_t *p_extents_count = (size_t *)ctx;
	assert(p_extents_count == &extents_count);
	--*p_extents_count;
	free(p);
}


static void
simple_test()
{
	header();

	struct light_core ht;
	light_create(&ht, light_extent_size,
		     my_light_alloc, my_light_free, &extents_count, 0);
	std::vector<bool> vect;
	size_t count = 0;
	const size_t rounds = 1000;
	const size_t start_limits = 20;
	for(size_t limits = start_limits; limits <= 2 * rounds; limits *= 10) {
		while (vect.size() < limits)
			vect.push_back(false);
		for (size_t i = 0; i < rounds; i++) {

			hash_value_t val = rand() % limits;
			hash_t h = hash(val);
			hash_t fnd = light_find(&ht, h, val);
			bool has1 = fnd != light_end;
			bool has2 = vect[val];
			assert(has1 == has2);
			if (has1 != has2) {
				fail("find key failed!", "true");
				return;
			}

			if (!has1) {
				count++;
				vect[val] = true;
				light_insert(&ht, h, val);
			} else {
				count--;
				vect[val] = false;
				light_delete(&ht, fnd);
			}

			if (count != ht.count)
				fail("count check failed!", "true");

			bool identical = true;
			for (hash_value_t test = 0; test < limits; test++) {
				if (vect[test]) {
					if (light_find(&ht, hash(test), test) == light_end)
						identical = false;
				} else {
					if (light_find(&ht, hash(test), test) != light_end)
						identical = false;
				}
			}
			if (!identical)
				fail("internal test failed!", "true");

			int check = light_selfcheck(&ht);
			if (check)
				fail("internal test failed!", "true");
		}
	}
	light_destroy(&ht);

	footer();
}

static void
collision_test()
{
	header();

	struct light_core ht;
	light_create(&ht, light_extent_size,
		     my_light_alloc, my_light_free, &extents_count, 0);
	std::vector<bool> vect;
	size_t count = 0;
	const size_t rounds = 100;
	const size_t start_limits = 20;
	for(size_t limits = start_limits; limits <= 2 * rounds; limits *= 10) {
		while (vect.size() < limits)
			vect.push_back(false);
		for (size_t i = 0; i < rounds; i++) {

			hash_value_t val = rand() % limits;
			hash_t h = hash(val);
			hash_t fnd = light_find(&ht, h * 1024, val);
			bool has1 = fnd != light_end;
			bool has2 = vect[val];
			assert(has1 == has2);
			if (has1 != has2) {
				fail("find key failed!", "true");
				return;
			}

			if (!has1) {
				count++;
				vect[val] = true;
				light_insert(&ht, h * 1024, val);
			} else {
				count--;
				vect[val] = false;
				light_delete(&ht, fnd);
			}

			if (count != ht.count)
				fail("count check failed!", "true");

			bool identical = true;
			for (hash_value_t test = 0; test < limits; test++) {
				if (vect[test]) {
					if (light_find(&ht, hash(test) * 1024, test) == light_end)
						identical = false;
				} else {
					if (light_find(&ht, hash(test) * 1024, test) != light_end)
						identical = false;
				}
			}
			if (!identical)
				fail("internal test failed!", "true");

			int check = light_selfcheck(&ht);
			if (check)
				fail("internal test failed!", "true");
		}
	}
	light_destroy(&ht);

	footer();
}

static void
iterator_test()
{
	header();

	struct light_core ht;
	light_create(&ht, light_extent_size,
		     my_light_alloc, my_light_free, &extents_count, 0);
	const size_t rounds = 1000;
	const size_t start_limits = 20;

	const size_t iterator_count = 16;
	struct light_iterator iterators[iterator_count];
	for (size_t i = 0; i < iterator_count; i++)
		light_iterator_begin(&ht, iterators + i);
	size_t cur_iterator = 0;
	hash_value_t strage_thing = 0;

	for(size_t limits = start_limits; limits <= 2 * rounds; limits *= 10) {
		for (size_t i = 0; i < rounds; i++) {
			hash_value_t val = rand() % limits;
			hash_t h = hash(val);
			hash_t fnd = light_find(&ht, h, val);

			if (fnd == light_end) {
				light_insert(&ht, h, val);
			} else {
				light_delete(&ht, fnd);
			}

			hash_value_t *pval = light_iterator_get_and_next(&ht, iterators + cur_iterator);
			if (pval)
				strage_thing ^= *pval;
			if (!pval || (rand() % iterator_count) == 0) {
				if (rand() % iterator_count) {
					hash_value_t val = rand() % limits;
					hash_t h = hash(val);
					light_iterator_key(&ht, iterators + cur_iterator, h, val);
				} else {
					light_iterator_begin(&ht, iterators + cur_iterator);
				}
			}

			cur_iterator++;
			if (cur_iterator >= iterator_count)
				cur_iterator = 0;
		}
	}
	light_destroy(&ht);

	if (strage_thing >> 20) {
		printf("impossible!\n"); // prevent strage_thing to be optimized out
	}

	footer();
}

static void
iterator_freeze_check()
{
	header();

	const int test_data_size = 1000;
	hash_value_t comp_buf[test_data_size];
	const int test_data_mod = 2000;
	srand(0);
	struct light_core ht;

	for (int i = 0; i < 10; i++) {
		light_create(&ht, light_extent_size,
			     my_light_alloc, my_light_free, &extents_count, 0);
		int comp_buf_size = 0;
		int comp_buf_size2 = 0;
		for (int j = 0; j < test_data_size; j++) {
			hash_value_t val = rand() % test_data_mod;
			hash_t h = hash(val);
			light_insert(&ht, h, val);
		}
		struct light_iterator iterator;
		light_iterator_begin(&ht, &iterator);
		hash_value_t *e;
		while ((e = light_iterator_get_and_next(&ht, &iterator))) {
			comp_buf[comp_buf_size++] = *e;
		}
		struct light_iterator iterator1;
		light_iterator_begin(&ht, &iterator1);
		light_iterator_freeze(&ht, &iterator1);
		struct light_iterator iterator2;
		light_iterator_begin(&ht, &iterator2);
		light_iterator_freeze(&ht, &iterator2);
		for (int j = 0; j < test_data_size; j++) {
			hash_value_t val = rand() % test_data_mod;
			hash_t h = hash(val);
			light_insert(&ht, h, val);
		}
		int tested_count = 0;
		while ((e = light_iterator_get_and_next(&ht, &iterator1))) {
			if (*e != comp_buf[tested_count]) {
				fail("version restore failed (1)", "true");
			}
			tested_count++;
			if (tested_count > comp_buf_size) {
				fail("version restore failed (2)", "true");
			}
		}
		light_iterator_destroy(&ht, &iterator1);
		for (int j = 0; j < test_data_size; j++) {
			hash_value_t val = rand() % test_data_mod;
			hash_t h = hash(val);
			hash_t pos = light_find(&ht, h, val);
			if (pos != light_end)
				light_delete(&ht, pos);
		}

		tested_count = 0;
		while ((e = light_iterator_get_and_next(&ht, &iterator2))) {
			if (*e != comp_buf[tested_count]) {
				fail("version restore failed (3)", "true");
			}
			tested_count++;
			if (tested_count > comp_buf_size) {
				fail("version restore failed (4)", "true");
			}
		}

		light_destroy(&ht);
	}

	footer();
}

int
main(int, const char**)
{
	srand(time(0));
	simple_test();
	collision_test();
	iterator_test();
	iterator_freeze_check();
	if (extents_count != 0)
		fail("memory leak!", "true");
}
