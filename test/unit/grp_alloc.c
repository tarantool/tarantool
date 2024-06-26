/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <stddef.h>
#include <string.h>

#include "salad/grp_alloc.h"
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

struct test {
	int *array;
	size_t array_size;
	const char *name;
	const char *description;
	const char *extra;
};

struct test *
test_new(int *array, size_t array_size,
	 const char *name, size_t name_len,
	 const char *description, size_t description_len,
	 const char *extra)
{
	struct grp_alloc bank = grp_alloc_initializer();
	size_t array_data_size = array_size * sizeof(*array);
	grp_alloc_reserve_data(&bank, array_data_size);
	grp_alloc_reserve_str(&bank, name_len);
	grp_alloc_reserve_str(&bank, description_len);
	grp_alloc_reserve_str0(&bank, extra);
	size_t total_size = sizeof(struct test) + grp_alloc_size(&bank);
	struct test *res = xmalloc(total_size);
	grp_alloc_use(&bank, res + 1);
	res->array = grp_alloc_create_data(&bank, array_data_size);
	memcpy(res->array, array, array_data_size);
	res->name = grp_alloc_create_str(&bank, name, name_len);
	res->description = grp_alloc_create_str(&bank, description,
						description_len);
	res->extra = grp_alloc_create_str0(&bank, extra);
	return res;
}

static void
check_test_new(int *array, size_t array_size,
	       const char *name, size_t name_len,
	       const char *description, size_t description_len,
	       const char *extra)
{
	header();
	plan(15);

	struct test *t = test_new(array, array_size, name, name_len,
				  description, description_len, extra);
	size_t array_data_size = array_size * sizeof(*array);
	size_t total = array_data_size + sizeof(*t) +
		       name_len + 1 + description_len + 1 + strlen(extra) + 1;
	char *b = (char *)t;
	char *e = b + total;

	ok((char *)t->array > b, "location");
	ok((char *)t->array < e, "location");
	ok((char *)t->name > b, "location");
	ok((char *)t->name < e, "location");
	ok((char *)t->description > b, "location");
	ok((char *)t->description < e, "location");
	ok((char *)t->extra > b, "location");
	ok((char *)t->extra < e, "location");
	is(memcmp(t->array, array, array_data_size), 0, "data");
	is(memcmp(t->name, name, name_len), 0, "data");
	is(t->name[name_len], 0, "null-termination symbol");
	is(memcmp(t->description, description, description_len), 0, "data");
	is(t->description[description_len], 0, "null-termination symbol");
	is(memcmp(t->extra, extra, strlen(extra)), 0, "data");
	is(t->extra[strlen(extra)], 0, "null-termination symbol");
	free(t);

	check_plan();
	footer();
}

static void
test_simple(void)
{
	header();
	plan(3);

	int arr[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	check_test_new(arr, 3, "test", 4, "abc", 3, "foo");
	check_test_new(arr, 10, "alligator", 9, "x", 1, "bar");
	check_test_new(arr, 1, "qwerty", 6, "as", 2, "buzz");

	check_plan();
	footer();
}

int
main(void)
{
	header();
	plan(1);
	test_simple();
	footer();
	return check_plan();
}
