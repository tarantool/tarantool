#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <vector>

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

inline void *
my_light_alloc()
{
	extents_count++;
	char *draft = (char *)malloc(light_extent_size + 64 + 8);
	void *result = draft + 8 + (63 - ((uint64_t)(draft + 8) % 64));
	((void **)result)[-1] = draft;
	return result;
}

inline void
my_light_free(void *p)
{
	extents_count--;
	free(((void **)p)[-1]);
}

#define LIGHT_NAME
#define LIGHT_DATA_TYPE uint64_t
#define LIGHT_KEY_TYPE uint64_t
#define LIGHT_CMP_ARG_TYPE int
#define LIGHT_EQUAL(a, b, arg) equal(a, b)
#define LIGHT_EQUAL_KEY(a, b, arg) equal_key(a, b)
#include "salad/light.h"


static void
simple_test()
{
	header();

	struct light ht;
	light_create(&ht, light_extent_size, my_light_alloc, my_light_free, 0);
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

	struct light ht;
	light_create(&ht, light_extent_size, my_light_alloc, my_light_free, 0);
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

int
main(int, const char**)
{
	simple_test();
	collision_test();
	if (extents_count != 0)
		fail("memory leak!", "true");
}
