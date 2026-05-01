/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "small/static.h"
#include "trivia/util.h"

static void
test_div_round_up(void)
{
	header();
	plan(6);

	ok(DIV_ROUND_UP(0, 1) == 0);
	ok(DIV_ROUND_UP(0, UINT64_MAX) == 0);
	ok(DIV_ROUND_UP(1, UINT64_MAX) == 1);
	ok(DIV_ROUND_UP(42, UINT64_MAX) == 1);
	ok(DIV_ROUND_UP(UINT64_MAX, 1) == UINT64_MAX);
	ok(DIV_ROUND_UP(UINT64_MAX, UINT64_MAX) == 1);

	check_plan();
	footer();
}

struct point {
	int x, y;
};

static int
point_snprint(char *buf, int len, const struct point *x)
{
	return snprintf(buf, len, "point(%d, %d)", x->x, x->y);
}

static int
point_snprint_err_mem(char *buf, int len, const struct point *x)
{
	return SMALL_STATIC_SIZE;
}

static int
uint32x_snprint(char *buf, int len, uint32_t x, bool full)
{
	const char *format = full ? "%08" PRIX32 : "%" PRIX32;
	return snprintf(buf, len, format, x);
}

static void
test_print_static(void)
{
	header();
	plan(4);

	struct point a = {1, 2};
	ok(strcmp(print_static(point_snprint, &a), "point(1, 2)") == 0);
	ok(strcmp(print_static(point_snprint_err_mem, &a),
		  "(error)") == 0);

	ok(strcmp(print_static(uint32x_snprint, 15, false), "F") == 0);
	ok(strcmp(print_static(uint32x_snprint, 15, true), "0000000F") == 0);

	check_plan();
	footer();
}

int
main(void)
{
	plan(2);

	test_div_round_up();
	test_print_static();

	return check_plan();
}
