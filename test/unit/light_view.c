#include <stddef.h>
#include <stdint.h>
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static const size_t extent_size = 1024;

struct data {
	int key;
	int val;
};

static inline uint32_t
hash(int key)
{
	return (uint32_t)key;
}

static bool
equal(struct data a, struct data b)
{
	return a.key == b.key;
}

static bool
equal_key(struct data a, int b)
{
	return a.key == b;
}

#define LIGHT_NAME
#define LIGHT_DATA_TYPE struct data
#define LIGHT_KEY_TYPE int
#define LIGHT_CMP_ARG_TYPE void *
#define LIGHT_EQUAL(a, b, arg) equal(a, b)
#define LIGHT_EQUAL_KEY(a, b, arg) equal_key(a, b)
#include "salad/light.h"

static void *
alloc_extent(void *ctx)
{
	(void)ctx;
	return xmalloc(extent_size);
}

static void
free_extent(void *ctx, void *p)
{
	(void)ctx;
	free(p);
}

static void
light_do_create(struct light_core *ht)
{
	light_create(ht, NULL, extent_size, alloc_extent, free_extent,
		     NULL, NULL);
}

static void
light_do_insert(struct light_core *ht, int key, int val)
{
	struct data data = {key, val};
	fail_if(light_insert(ht, hash(key), data) == light_end);
}

static void
light_do_delete(struct light_core *ht, int key)
{
	uint32_t slot = light_find_key(ht, hash(key), key);
	fail_if(slot == light_end);
	fail_if(light_delete(ht, slot) != 0);
}

static void
test_count(void)
{
	plan(4);
	header();

	struct light_core ht;
	light_do_create(&ht);

	struct light_view view;
	light_view_create(&view, &ht);
	is(light_view_count(&view), 0,
	   "empty view size before hash table change");
	for (int i = 0; i < 1000; i++)
		light_do_insert(&ht, i, i * 2);
	is(light_view_count(&view), 0,
	   "empty view size after hash table change");
	light_view_destroy(&view);

	light_view_create(&view, &ht);
	is(light_view_count(&view), 1000,
	   "non-empty view size before hash table change");
	for (int i = 0; i < 1000; i++) {
		light_do_insert(&ht, i + 1000, i);
		if (i % 2 == 0)
			light_do_delete(&ht, i);
	}
	is(light_view_count(&view), 1000,
	   "non-empty view size after hash table change");
	light_view_destroy(&view);
	light_destroy(&ht);

	footer();
	check_plan();
}

static void
test_find(void)
{
	plan(4);
	header();

	struct light_core ht;
	light_do_create(&ht);
	for (int i = 0; i < 1000; i++)
		light_do_insert(&ht, i, i * 2);

	struct light_view view;
	light_view_create(&view, &ht);

	for (int i = 0; i < 1000; i++) {
		light_do_insert(&ht, i + 1000, i);
		if (i % 2 == 0)
			light_do_delete(&ht, i);
	}

	bool success = true;
	for (int i = 0; i < 1000; i++) {
		struct data data = {i, i};
		uint32_t slot = light_view_find(&view, hash(i), data);
		if (slot == light_end) {
			success = false;
			continue;
		}
		data = light_view_get(&view, slot);
		if (data.key != i || data.val != i * 2)
			success = false;
	}
	ok(success, "old values found by value");

	success = true;
	for (int i = 0; i < 1000; i++) {
		uint32_t slot = light_view_find_key(&view, hash(i), i);
		if (slot == light_end) {
			success = false;
			continue;
		}
		struct data data = light_view_get(&view, slot);
		if (data.key != i || data.val != i * 2)
			success = false;
	}
	ok(success, "old values found by key");

	success = true;
	for (int i = 0; i < 1000; i++) {
		struct data data = {i + 1000, i};
		uint32_t slot = light_view_find(&view, hash(data.key), data);
		if (slot != light_end)
			success = false;
	}
	ok(success, "new values not found by value");

	success = true;
	for (int i = 0; i < 1000; i++) {
		int key = i + 1000;
		uint32_t slot = light_view_find_key(&view, hash(key), key);
		if (slot != light_end)
			success = false;
	}
	ok(success, "new values not found by key");

	light_view_destroy(&view);
	light_destroy(&ht);

	footer();
	check_plan();
}

static void
test_iterator(void)
{
	plan(2);
	header();

	struct light_core ht;
	light_do_create(&ht);
	for (int i = 0; i < 1000; i++) {
		if (i % 3 == 0)
			light_do_insert(&ht, i, i * 2);
	}

	struct light_view view;
	light_view_create(&view, &ht);

	for (int i = 0; i < 1000; i++) {
		if (i % 6 == 0)
			light_do_delete(&ht, i);
		if (i % 3 != 0 && i % 5 == 0)
			light_do_insert(&ht, i, i * 2);
	}

	bool success = true;
	bool seen[1000] = {false};
	struct light_iterator it;
	light_view_iterator_begin(&view, &it);
	for (struct data *p = light_view_iterator_get_and_next(&view, &it);
	     p != NULL; p = light_view_iterator_get_and_next(&view, &it)) {
		if (p->val != p->key * 2)
			success = false;
		if (p->key < 0 || p->key >= 1000) {
			success = false;
		} else if (seen[p->key]) {
			success = false;
		} else {
			seen[p->key] = true;
		}
	}
	for (int i = 0; i < 1000; i++) {
		if (seen[i] != (i % 3 == 0))
			success = false;
	}
	ok(success, "full scan");

	success = true;
	for (int i = 0; i < 1000; i++) {
		light_view_iterator_key(&view, &it, hash(i), i);
		struct data *p = light_view_iterator_get_and_next(&view, &it);
		if (i % 3 == 0) {
			if (p == NULL || p->key != i || p->val != i * 2)
				success = false;
		} else {
			if (p != NULL)
				success = false;
		}
	}
	ok(success, "point lookup");

	light_view_destroy(&view);
	light_destroy(&ht);

	footer();
	check_plan();
}

int
main(void)
{
	plan(3);
	header();

	test_count();
	test_find();
	test_iterator();

	footer();
	return check_plan();
}
