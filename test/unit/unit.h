#ifndef INCLUDES_TARANTOOL_TEST_UNIT_H
#define INCLUDES_TARANTOOL_TEST_UNIT_H
/*
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h> /* exit() */

#define fail(expr, result) do {					\
	fprintf(stderr, "Test failed: %s is %s at %s:%d, in function '%s'\n",\
		expr, result, __FILE__, __LINE__, __func__);		\
	exit(-1);							\
} while (0)

#define fail_if(expr) if (expr) fail(#expr, "true")
#define fail_unless(expr) if (!(expr)) fail(#expr, "false")

#define note(...) msg(stdout, __VA_ARGS__)
#define diag(...) msg(stderr, __VA_ARGS__)

#define msg(stream, ...) ({ _space(stream); fprintf(stream, "# "); \
	fprintf(stream, __VA_ARGS__); fprintf(stream, "\n"); })

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
@brief example

@code
	#include "unit.h"

	int main(void) {
		plan(3);		// count of test You planned to check
		ok(1, "Test name 1");
		is(4, 2 * 2, "2 * 2 == 4");
		isnt(5, 2 * 2, "2 * 2 != 5);
		return check_plan();	// print resume
	}
@endcode


*/

#if !UNIT_TAP_COMPATIBLE

#define header() printf("\t*** %s ***\n", __func__)
#define footer() printf("\t*** %s: done ***\n", __func__)


/* private function, use ok(...) instead */
int _ok(int condition, const char *fmt, ...);

/* private function, use note(...) or diag(...) instead */
void _space(FILE *stream);

/**
@brief set and print plan
@param count
Before anything else, you need a testing plan.  This basically declares
how many tests your program is going to run to protect against premature
failure.
*/
void plan(int count);

/**
@brief check if plan is reached and print report
*/
int check_plan(void);

#endif

#define ok(condition, fmt, args...)	{		\
	int res = _ok(condition, fmt, ##args);		\
	if (!res) {					\
		_space(stderr);			\
		fprintf(stderr, "#   Failed test '");	\
		fprintf(stderr, fmt, ##args);		\
		fprintf(stderr, "'\n");			\
		_space(stderr);			\
		fprintf(stderr, "#   in %s at line %d\n", __FILE__, __LINE__); \
	}						\
}

#define is(a, b, fmt, args...)	{			\
	int res = _ok((a) == (b), fmt, ##args);	\
	if (!res) {					\
		_space(stderr);			\
		fprintf(stderr, "#   Failed test '");	\
		fprintf(stderr, fmt, ##args);		\
		fprintf(stderr, "'\n");			\
		_space(stderr);			\
		fprintf(stderr, "#   in %s at line %d\n", __FILE__, __LINE__); \
	}						\
}

#define isnt(a, b, fmt, args...) {			\
	int res = _ok((a) != (b), fmt, ##args);	\
	if (!res) {					\
		_space(stderr);			\
		fprintf(stderr, "#   Failed test '");	\
		fprintf(stderr, fmt, ##args);		\
		fprintf(stderr, "'\n");			\
		_space(stderr);			\
		fprintf(stderr, "#   in %s at line %d\n", __FILE__, __LINE__); \
	}						\
}

#if UNIT_TAP_COMPATIBLE

#define header()					\
	do { 						\
		_space(stdout);				\
		printf("# *** %s ***\n", __func__);	\
	} while (0)

#define footer()					\
	do { 						\
		_space(stdout);				\
		printf("# *** %s: done ***\n", __func__); \
	} while (0)

enum { MAX_LEVELS = 10 };

static int tests_done[MAX_LEVELS];
static int tests_failed[MAX_LEVELS];
static int plan_test[MAX_LEVELS];

static int level = -1;

/**
 * private function, use note(...) or diag(...) instead
 */
static inline void
_space(FILE *stream)
{
	for (int i = 0; i < level; i++) {
		fprintf(stream, "    ");
	}
}

/**
 * private function, use ok(...) instead
 */
static inline int
_ok(int condition, const char *fmt, ...)
{
	va_list ap;

	_space(stdout);
	printf("%s %d - ", condition ? "ok" : "not ok", ++tests_done[level]);
	if (!condition)
		tests_failed[level]++;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	return condition;
}

/**
 * @brief set and print plan
 * @param count
 * Before anything else, you need a testing plan.  This basically declares
 * how many tests your program is going to run to protect against premature
 * failure.
 */
static inline void
plan(int count)
{
	++level;
	plan_test[level] = count;
	tests_done[level] = 0;
	tests_failed[level] = 0;

	if (level == 0)
		printf("TAP version 13\n");

	_space(stdout);
	printf("%d..%d\n", 1, plan_test[level]);
}

/**
 * @brief check if plan is reached and print report
 */
static inline int
check_plan(void)
{
	int r = 0;
	if (tests_done[level] != plan_test[level]) {
		_space(stderr);
		fprintf(stderr,
			"# Looks like you planned %d tests but ran %d.\n",
			plan_test[level], tests_done[level]);
		r = -1;
	}

	if (tests_failed[level]) {
		_space(stderr);
		fprintf(stderr, "# Looks like you failed %d test of %d run.\n",
			tests_failed[level], tests_done[level]);
		r = tests_failed[level];
	}
	--level;
	if (level >= 0) {
		is(r, 0, "subtests");
	}
	return r;
}

#endif

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_TEST_UNIT_H */
