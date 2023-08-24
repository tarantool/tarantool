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
#include <stdbool.h>
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

/* private function, use ok(...) instead */
void
_ok(int condition, const char *expr, const char *file, int line,
    const char *fmt, ...);

/* private function, use note(...) or diag(...) instead */
void _space(FILE *stream);

/**
@brief set and print plan
@param count
Before anything else, you need a testing plan.  This basically declares
how many tests your program is going to run to protect against premature
failure.
*/
void
_plan(int count, bool tap);

/**
@brief check if plan is reached and print report
*/
int check_plan(void);

/*
 * The ok macro is defined so that it can be called without a message:
 *
 *   ok(true);
 *   ok(true, "message");
 *   ok(true, "message %d", i);
 *
 * It supports up to 7 format arguments.
 */
#define _select_10th(f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, ...) f10
#define _ok0(cond, expr, ...)						\
	_select_10th(, ##__VA_ARGS__,					\
		     _ok(cond, expr, __FILE__, __LINE__, __VA_ARGS__),	\
		     _ok(cond, expr, __FILE__, __LINE__, __VA_ARGS__),	\
		     _ok(cond, expr, __FILE__, __LINE__, __VA_ARGS__),	\
		     _ok(cond, expr, __FILE__, __LINE__, __VA_ARGS__),	\
		     _ok(cond, expr, __FILE__, __LINE__, __VA_ARGS__),	\
		     _ok(cond, expr, __FILE__, __LINE__, __VA_ARGS__),	\
		     _ok(cond, expr, __FILE__, __LINE__, __VA_ARGS__),	\
		     _ok(cond, expr, __FILE__, __LINE__, __VA_ARGS__),	\
		     _ok(cond, expr, __FILE__, __LINE__, "line %d", __LINE__))

#define ok(cond, ...)		_ok0(cond, #cond, ##__VA_ARGS__)
#define is(a, b, ...)		_ok0((a) == (b), #a " == " #b, ##__VA_ARGS__)
#define isnt(a, b, ...)		_ok0((a) != (b), #a " != " #b, ##__VA_ARGS__)

#if UNIT_TAP_COMPATIBLE

#define header()					\
	do {						\
		_space(stdout);				\
		printf("# *** %s ***\n", __func__);	\
	} while (0)

#define footer()					\
	do { 						\
		_space(stdout);				\
		printf("# *** %s: done ***\n", __func__); \
	} while (0)

#define plan(count) _plan(count, true)

#else /* !UNIT_TAP_COMPATIBLE */

#define header() printf("\t*** %s ***\n", __func__)
#define footer() printf("\t*** %s: done ***\n", __func__)
#define plan(count) _plan(count, false)

#endif /* !UNIT_TAP_COMPATIBLE */

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_TEST_UNIT_H */
