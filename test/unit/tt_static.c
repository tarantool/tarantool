/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "string.h"
#include "tt_static.h"

static void
test_tt_cstr(void)
{
	header();
	plan(2);

	char buf[SMALL_STATIC_SIZE + 4];
	memset(buf, 'x', sizeof(buf));
	const char *result;

	/* len <= SMALL_STATIC_SIZE - 1. */
	result = tt_cstr(buf, SMALL_STATIC_SIZE - 4);
	ok(strlen(result) == SMALL_STATIC_SIZE - 4);
	/* len > SMALL_STATIC_SIZE - 1. */
	result = tt_cstr(buf, SMALL_STATIC_SIZE + 4);
	ok(strlen(result) == SMALL_STATIC_SIZE - 1);

	check_plan();
	footer();
}

static void
test_tt_sprintf(void)
{
	header();
	plan(2);

	char buf[TT_STATIC_BUF_LEN + 4];
	memset(buf, 'x', sizeof(buf));
	const char *result;

	/* non_cropped_len(result) < TT_STATIC_BUF_LEN . */
	result = tt_sprintf("%.*s", TT_STATIC_BUF_LEN - 4, buf);
	ok(strlen(result) == TT_STATIC_BUF_LEN - 4);
	/* non_cropped_len(result) >= TT_STATIC_BUF_LEN. */
	result = tt_sprintf("%.*s", TT_STATIC_BUF_LEN + 4, buf);
	ok(strlen(result) == TT_STATIC_BUF_LEN - 1);

	check_plan();
	footer();
}

static void
test_tt_snprintf(void)
{
	header();
	plan(2);

	char buf[SMALL_STATIC_SIZE + 4];
	memset(buf, 'x', sizeof(buf));
	const char *result;

	/* non_cropped_len(result) < SMALL_STATIC_SIZE . */
	result = tt_snprintf(SMALL_STATIC_SIZE - 4 + 1,
			     "%.*s", SMALL_STATIC_SIZE - 4, buf);
	ok(strlen(result) == SMALL_STATIC_SIZE - 4);
	/* non_cropped_len(result) >= SMALL_STATIC_SIZE. */
	result = tt_snprintf(SMALL_STATIC_SIZE + 4 + 1,
			     "%.*s", SMALL_STATIC_SIZE + 4, buf);
	ok(strlen(result) == SMALL_STATIC_SIZE - 1);

	check_plan();
	footer();
}

CFORMAT(printf, 2, 3)
static const char *
tt_vsnprintf_tester(size_t size, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	const char *result = tt_vsnprintf(size, format, ap);
	va_end(ap);
	return result;
}

static void
test_tt_vsnprintf(void)
{
	header();
	plan(4);

	char buf[SMALL_STATIC_SIZE + 4];
	memset(buf, 'x', sizeof(buf));
	const char *result;

	/* Check max size. */
	result = tt_vsnprintf_tester(SMALL_STATIC_SIZE,
				     "%.*s", SMALL_STATIC_SIZE, buf);
	ok(strlen(result) == SMALL_STATIC_SIZE - 1);
	/* Check min size. */
	result = tt_vsnprintf_tester(1, "%.*s", 1, buf);
	ok(strlen(result) == 0);

	unsigned w1 = 4, w2 = 6;
	result = tt_vsnprintf_tester(15 + 1, "|%*s|%*s|", w1, "A", w2, "B");
	ok(strcmp(result, "|   A|     B|") == 0);

/* Check release time EOVERFLOW error case. */
#if defined(NDEBUG) && defined(__GNUC__) && !defined(__clang__)
	/* Corrupt w1. */
	w1 = (unsigned)INT_MAX + 1;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-overflow"
	result = tt_vsnprintf_tester(15 + 1, "|%*s|%*s|", w1, "A", w2, "B");
#pragma GCC diagnostic pop
	ok(strcmp(result, "<vsnprintf error>") == 0);
#else
	ok(true);
#endif

	check_plan();
	footer();
}

int
main(void)
{
	plan(4);

	test_tt_cstr();
	test_tt_sprintf();
	test_tt_snprintf();
	test_tt_vsnprintf();

	return check_plan();
}
