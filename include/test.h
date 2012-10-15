#ifndef TARANTOOL_TEST_H_INCLUDED
#define TARANTOOL_TEST_H_INCLUDED

#include <stdio.h>
#include <stdarg.h>

static int tests_done = 0;
static int tests_failed = 0;
static int plan_test = 0;

static inline void
plan(int count)
{
	plan_test = count;
	static showed_plan = 0;
	if (!showed_plan)
		printf("%d..%d\n", 1, plan_test);
	showed_plan = 1;
}

static inline int
check_plan(void)
{
	int res;
	if (tests_done != plan_test) {
		fprintf(stderr,
			"# Looks like you planned %d tests but ran %d.\n",
			plan_test, tests_done);
		res = -1;
	}

	if (tests_failed) {
		fprintf(stderr,
			"# Looks like you failed %d test of %d run.\n",
			tests_failed, tests_done);
		res = tests_failed;
	}
	return res;
}

static inline int
__ok(int condition, const char *fmt, ...)
{
	va_list ap;

	printf("%s %d - ", condition ? "ok" : "not ok", ++tests_done);
	if (!condition)
		tests_failed++;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	printf("\n");
	return condition;
}

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

