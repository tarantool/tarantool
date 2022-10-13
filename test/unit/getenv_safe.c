#include <stdlib.h>
#include <string.h>
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
test_getenv_safe(void)
{
	header();
	plan(10);

	static const char str[] = "some env value";
	static const char name[] = "TT_GH_7797_ENV_TEST";
	size_t size = sizeof(str);
	char buf[size];
	bool exceeded = true;

	is(getenv(name), NULL, "Getenv finds nothing initially");
	is(getenv_safe(name, buf, size), NULL,
	   "Getenv_safe finds nothing");

	is(setenv(name, str, 1), 0, "Setenv succeeeds");

	char *ret1 = getenv(name);
	isnt(ret1, NULL, "Getenv finds the value");
	char *ret2 = getenv_safe(name, buf, size);
	isnt(ret2, NULL, "Getenv_safe finds the value");
	is(ret2, buf, "Getenv_safe returns pointer to passed buffer");
	is(strcmp(ret1, ret2), 0, "Returns are the same");
	char *ret3 = getenv_safe(name, buf, size - 1);
	is(ret3, NULL, "Getenv_safe returns nothing when size doesn't fit");
	char *ret4 = getenv_safe(name, NULL, 0);
	isnt(ret4, NULL, "Getenv_safe returns allocated memory when not "
			 "provided with a buffer");
	is(strcmp(ret4, ret1), 0, "Returns are the same");
	free(ret4);

	unsetenv(name);

	footer();
}

int
main(void)
{
	header();
	plan(1);

	test_getenv_safe();

	footer();
	return check_plan();
}
