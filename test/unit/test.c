#include "test.h"

#include <stdio.h>
#include <stdarg.h>

static int tests_done = 0;
static int tests_failed = 0;
static int plan_test = 0;

void
plan(int count)
{
	plan_test = count;
	static int showed_plan = 0;
	if (!showed_plan)
		printf("%d..%d\n", 1, plan_test);
	showed_plan = 1;
}

int
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

int
_ok(int condition, const char *fmt, ...)
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
