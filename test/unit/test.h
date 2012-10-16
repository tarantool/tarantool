#ifndef TARANTOOL_TEST_H_INCLUDED
#define TARANTOOL_TEST_H_INCLUDED

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

int __ok(int condition, const char *fmt, ...);
void plan(int count);
int check_plan(void);

#define ok(condition, fmt, args...)	{		\
	int res = __ok(condition, fmt, ##args);		\
	if (!res) {					\
		fprintf(stderr, "#   Failed test '");	\
		fprintf(stderr, fmt, ##args);		\
		fprintf(stderr, "'\n");			\
		fprintf(stderr, "#   in %s at line %d\n", __FILE__, __LINE__); \
	}						\
	res;						\
}

#define is(a, b, fmt, args...)	{			\
	int res = __ok((a) == (b), fmt, ##args);	\
	if (!res) {					\
		fprintf(stderr, "#   Failed test '");	\
		fprintf(stderr, fmt, ##args);		\
		fprintf(stderr, "'\n");			\
		fprintf(stderr, "#   in %s at line %d\n", __FILE__, __LINE__); \
	}						\
	res;						\
}

#define isnt(a, b, fmt, args...) {			\
	int res = __ok((a) != (b), fmt, ##args);	\
	if (!res) {					\
		fprintf(stderr, "#   Failed test '");	\
		fprintf(stderr, fmt, ##args);		\
		fprintf(stderr, "'\n");			\
		fprintf(stderr, "#   in %s at line %d\n", __FILE__, __LINE__); \
	}						\
	res;						\
}

#define fail(fmt, args...)		\
	ok(0, fmt, ##args)


#endif /* TARANTOOL_TEST_H_INCLUDED */

