/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "tt_static.h"
#include "small/static.h"
#include "tzcode/tzcode.h"
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

/* Simple type with synthetic errors for TOSTR() tests. */
struct point {
	int x, y;
};

static int
point_snprint(char *buf, int len, const struct point *x, bool with_type)
{
	if (x == NULL)
		return snprintf(buf, len, "<NULL>");
	char *type = with_type ? "point" : "";
	return snprintf(buf, len, "%s(%d, %d)", type, x->x, x->y);
}

static int
point_snprint_overflow(char *buf, int len, const struct point *x, int overflow)
{
	assert(overflow > 0);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	return snprintf(buf, len, "(%d, %d)%*s", x->x, x->y, overflow, "x");
#pragma GCC diagnostic pop
}

/* You may see a real example in tuple test unit. */
static int
point_snprint_err_data(char *buf, int len, const struct point *x)
{
	return -1;
}

static int
point_snprint_err_mem(char *buf, int len, const struct point *x)
{
	return SMALL_STATIC_SIZE;
}

static void
test_tostr(void)
{
	header();
	plan(5 + 6);

	struct point a = {123456789, 987654321};

	/* TOSTR. */

	is(strcmp(TOSTR(point_snprint, NULL, false),
		  "<NULL>"), 0);
	is(strcmp(TOSTR(point_snprint, &a, false),
		  "(123456789, 987654321)"), 0);
	is(strcmp(TOSTR(point_snprint, &a, true),
		  "point(123456789, 987654321)"), 0);
	/* Crop. */
	char *cropped = TOSTR(point_snprint_overflow, &a, 2000);
	/* more = (22 + 2000) - (1024 - 1) + (11 + 1) = 1011. */
	/* 22 + spaces + 13 = (1024 - 1) -> spaces = 988. */
	is(strcmp(cropped,
		  tt_sprintf("(123456789, 987654321)%988s<more ~1011b>", "")),
	   0);
	/* Error. */
	is(strcmp(TOSTR(point_snprint_err_data, &a),
		  "<TOSTR(point_snprint_err_data) err>"), 0);

	/* TOSTR_DEBUG. */

	is(strcmp(TOSTR_DEBUG(point_snprint, NULL, false),
		  "<NULL>"), 0);
	is(strcmp(TOSTR_DEBUG(point_snprint, &a, false),
		  "(123456789, 987654321)"), 0);
	is(strcmp(TOSTR_DEBUG(point_snprint, &a, true),
		  "point(123456789, 987654321)"), 0);
	char *not_cropped = TOSTR_DEBUG(point_snprint_overflow, &a, 2000);
	is(strcmp(not_cropped,
		  tt_snprintf(2100, "(123456789, 987654321)%2000s", "x")), 0);
	/* Errors. */
	is(strcmp(TOSTR_DEBUG(point_snprint_err_data, &a),
		  "<TOSTR(point_snprint_err_data) err>"), 0);
	is(strcmp(TOSTR_DEBUG(point_snprint_err_mem, &a),
		  "<TOSTR(point_snprint_err_mem) err>"), 0);

	check_plan();
	footer();
}

int
main(void)
{
	plan(2);

	test_div_round_up();
	test_tostr();

	return check_plan();
}
