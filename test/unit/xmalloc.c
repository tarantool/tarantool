#include "unit.h"
#include "trivia/util.h"

#include <stddef.h>
#include <string.h>

static void
test_xmalloc(void)
{
	header();
	plan(1);
	const int size = 9000;
	char *p = xmalloc(size);
	isnt(p, NULL, "p != NULL");
	if (p != NULL) {
		memset(p, 'x', size);
		free(p);
	}
	check_plan();
	footer();
}

static void
test_xcalloc(void)
{
	header();
	plan(2);
	const int nmemb = 42;
	const int size = 9000;
	char *p = xcalloc(nmemb, size);
	isnt(p, NULL, "p != NULL");
	if (p != NULL) {
		bool is_zeroed = true;
		for (int i = 0; i < nmemb * size && is_zeroed; i++) {
			if (p[i] != 0)
				is_zeroed = false;
		}
		ok(is_zeroed, "p is zeroed")
		free(p);
	}
	check_plan();
	footer();
}

static void
test_xrealloc(void)
{
	header();
	plan(3);
	const int size = 9000;
	char *p = xrealloc(NULL, size);
	isnt(p, NULL, "p != NULL on alloc");
	if (p != NULL)
		memset(p, 'x', size);
	p = xrealloc(p, size * 2);
	isnt(p, NULL, "p != NULL on realloc");
	if (p != NULL) {
		bool is_same = true;
		for (int i = 0; i < size && is_same; i++) {
			if (p[i] != 'x')
				is_same = false;
		}
		ok(is_same, "p is same after realloc");
		memset(p, 'x', size * 2);
		free(p);
	}
	check_plan();
	footer();
}

static void
test_xstrdup(void)
{
	header();
	plan(3);
	const int size = 9000;
	char *s = xmalloc(size);
	isnt(s, NULL, "s != NULL");
	if (s != NULL) {
		memset(s, 'x', size);
		s[size - 1] = 0;
		char *copy = xstrdup(s);
		isnt(copy, NULL, "copy != NULL");
		if (copy != NULL) {
			is(strcmp(s, copy), 0, "strcmp(s, copy) == 0");
			free(copy);
		}
		free(s);
	}
	check_plan();
	footer();
}

static void
test_xstrndup(void)
{
	header();
	plan(6);
	const int size = 9000;
	const int n = size / 2;
	char *s = xmalloc(size);
	isnt(s, NULL, "s != NULL");
	if (s != NULL) {
		memset(s, 'x', size);
		s[size - 1] = 0;
		char *copy = xstrndup(s, n);
		isnt(copy, NULL, "copy != NULL");
		if (copy != NULL) {
			is(strlen(copy), (size_t)n, "strlen(copy) == n");
			is(strncmp(s, copy, n), 0, "strncmp(s, copy, n) == 0");
			ok(strncmp(s, copy, n + 1) > 0,
			   "strncmp(s, copy, n + 1) > 0");
			ok(strcmp(s, copy) > 0, "strcmp(s, copy) > 0");
			free(copy);
		}
		free(s);
	}
	check_plan();
	footer();
}

int
main()
{
	header();
	plan(5);
	test_xmalloc();
	test_xcalloc();
	test_xrealloc();
	test_xstrdup();
	test_xstrndup();
	int rc = check_plan();
	footer();
	return rc;
}
