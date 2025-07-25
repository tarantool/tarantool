/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
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

int
main(void)
{
	plan(1);

	test_div_round_up();

	return check_plan();
}
