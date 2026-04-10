/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <string.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "trivia/util.h"

static void
test_fmt(void)
{
	header();
	plan(3);

	const char *s1, *s2;

	/* s1 points to buf start. */
	s1 = fmt("%03d", 9);
	ok(strcmp("009", s1) == 0);

	s2 = fmt("12345");
	ok(s1 < s2);

	/* Make buf rewind <-> invalidate s1. */
	size_t n = FMT_BUF_SIZE / (strlen(s1) + 1);
	for (size_t i = 0; i < n; i++)
		/* Same len as s1. */
		fmt("xxx");
	ok(strcmp("xxx", s1) == 0);

	check_plan();
	footer();
}

int
main(void)
{
	plan(1);

	test_fmt();

	return check_plan();
}
