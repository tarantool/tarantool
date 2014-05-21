#ifndef TARANTOOL_TEST_H_INCLUDED
#define TARANTOOL_TEST_H_INCLUDED

#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
@brief example

@code
	#include "test.h"

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
int _ok(int condition, const char *fmt, ...);

/* private function, use note(...) or diag(...) instead */
void _space(FILE *stream);

#define msg(stream, ...) ({ _space(stream); fprintf(stream, "# ");            \
	fprintf(stream, __VA_ARGS__); fprintf(stream, "\n"); })

#define note(...) msg(stdout, __VA_ARGS__)
#define diag(...) msg(stderr, __VA_ARGS__)

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
	res = res;					\
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
	res = res;					\
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
	res = res;					\
}

#define fail(fmt, args...)		\
	ok(0, fmt, ##args)


#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_TEST_H_INCLUDED */

