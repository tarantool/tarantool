#include "unit.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>


enum { MAX_LEVELS = 10 };
static int tests_done[MAX_LEVELS];
static int tests_failed[MAX_LEVELS];
static int plan_test[MAX_LEVELS];
static int level = -1;

void
_space(FILE *stream)
{
	for (int i = 0 ; i < level; i++) {
		fprintf(stream, "    ");
	}
}

void
_plan(int count, bool tap)
{
	++level;
	plan_test[level] = count;
	tests_done[level] = 0;
	tests_failed[level] = 0;

	if (tap && level == 0)
		printf("TAP version 13\n");

	_space(stdout);
	printf("%d..%d\n", 1, plan_test[level]);
}

int
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

void
_ok(int condition, const char *expr, const char *file, int line,
    const char *fmt, ...)
{
	va_list ap;

	_space(stdout);
	printf("%s %d - ", condition ? "ok" : "not ok", ++tests_done[level]);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	if (!condition) {
		tests_failed[level]++;
		_space(stderr);
		fprintf(stderr, "#   Failed test `%s'\n", expr);
		_space(stderr);
		fprintf(stderr, "#   in %s at line %d\n", file, line);
	}
}

