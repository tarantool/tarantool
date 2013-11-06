#include "test.h"

#include <stdio.h>
#include <stdarg.h>

enum { MAX_LEVELS = 10 };
static int tests_done[MAX_LEVELS];
static int tests_failed[MAX_LEVELS];
static int plan_test[MAX_LEVELS];
static int level = -1;

void
__space(FILE *stream)
{
	for (int i = 0 ; i < level; i++) {
		fprintf(stream, "    ");
	}
}

void
plan(int count)
{
	++level;
	plan_test[level] = count;
	tests_done[level] = 0;
	tests_failed[level] = 0;

	__space(stdout);
	printf("%d..%d\n", 1, plan_test[level]);
}

int
check_plan(void)
{
	int r = 0;
	if (tests_done[level] != plan_test[level]) {
		__space(stderr);
		fprintf(stderr,
			"# Looks like you planned %d tests but ran %d.\n",
			plan_test[level], tests_done[level]);
		r = -1;
	}

	if (tests_failed[level]) {
		__space(stderr);
		fprintf(stderr,
			"# Looks like you failed %d test of %d run.\n",
			tests_failed[level], tests_done[level]);
		r = tests_failed[level];
	}
	--level;
	if (level >= 0) {
		is(r, 0, "subtests");
	}
	return r;
}

int
__ok(int condition, const char *fmt, ...)
{
	va_list ap;

	__space(stdout);
	printf("%s %d - ", condition ? "ok" : "not ok", ++tests_done[level]);
	if (!condition)
		tests_failed[level]++;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	printf("\n");
	return condition;
}

