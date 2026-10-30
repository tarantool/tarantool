#include "string.h"
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static const char *const test_lower_case_conv_expected = "str";
static const char *const test_upper_case_conv_expected = "STR";
static const char *const test_case_conv_input[] = {
	"str", "Str", "sTr", "stR", "STr", "sTR", "StR", "STR"
};

static void
test_strlcat(void)
{
	plan(4 * 2);
	header();

	/* Normal case */
	char buf[14] = "Hello";
	size_t len1 = strlen(buf);
	const char *str2 = ", world!";
	size_t rc = strlcat(buf, str2, sizeof(buf));
	ok(rc == len1 + strlen(str2), "normal: length");
	ok(strcmp(buf, "Hello, world!") == 0, "normal: string");

	/* size == strlen(buf) + 1 */
	buf[len1] = '\0';
	rc = strlcat(buf, "aaa", len1 + 1);
	ok(rc == len1 + strlen("aaa"), "overflow 1: length");
	ok(strcmp(buf, "Hello") == 0, "overflow 1: string");

	/* size < strlen(buf) */
	rc = strlcat(buf, "hmm", 2);
	ok(rc == 2 + strlen("hmm"), "overflow 2: length");
	ok(strcmp(buf, "Hello") == 0, "overflow 2: string");

	/* Concatenated string bigger than `size` */
	buf[4] = '\0';
	len1 = strlen(buf);
	str2 = " yeah !!!OVERFLOW!!!";
	rc = strlcat(buf, str2, sizeof(buf));
	ok(rc == len1 + strlen(str2), "overflow 3: length");
	ok(strcmp(buf, "Hell yeah !!!") == 0, "overflow 3: string");

	footer();
	check_plan();
}

static void
test_strtolowerdup(void)
{
	header();
	plan(lengthof(test_case_conv_input) * 2);

	for (size_t i = 0; i < lengthof(test_case_conv_input); ++i) {
		char *test = strtolowerdup(test_case_conv_input[i]);
		isnt(test, test_case_conv_input[i],
		     "a copy of %s is returned", test_case_conv_input[i]);
		is(strcmp(test_lower_case_conv_expected, test), 0,
		   "%s is converted to lower case correctly",
		   test_case_conv_input[i]);
		free(test);
	}

	footer();
	check_plan();
}

static void
test_strtolower(void)
{
	header();
	plan(lengthof(test_case_conv_input) * 2);

	for (size_t i = 0; i < lengthof(test_case_conv_input); ++i) {
		char *cp = xstrdup(test_case_conv_input[i]);
		char *test = strtolower(cp);
		is(test, cp, "%s is converted in-place", cp);
		is(strcmp(test_lower_case_conv_expected, test), 0,
		   "%s is converted to lower case correctly",
		   test_case_conv_input[i]);
		free(cp);
	}

	footer();
	check_plan();
}

static void
test_strtoupperdup(void)
{
	header();
	plan(lengthof(test_case_conv_input) * 2);

	for (size_t i = 0; i < lengthof(test_case_conv_input); ++i) {
		char *test = strtoupperdup(test_case_conv_input[i]);
		isnt(test, test_case_conv_input[i],
		     "a copy of %s is returned", test_case_conv_input[i]);
		is(strcmp(test_upper_case_conv_expected, test), 0,
		   "%s is converted to upper case correctly",
		   test_case_conv_input[i]);
		free(test);
	}

	footer();
	check_plan();
}

static void
test_strtoupper(void)
{
	header();
	plan(lengthof(test_case_conv_input) * 2);

	for (size_t i = 0; i < lengthof(test_case_conv_input); ++i) {
		char *cp = xstrdup(test_case_conv_input[i]);
		char *test = strtoupper(cp);
		is(test, cp, "%s is converted in-place", cp);
		is(strcmp(test_upper_case_conv_expected, test), 0,
		   "%s is converted to upper case correctly",
		   test_case_conv_input[i]);
		free(cp);
	}

	footer();
	check_plan();
}

int
main(void)
{
	plan(5);
	header();

	test_strlcat();
	test_strtolowerdup();
	test_strtolower();
	test_strtoupperdup();
	test_strtoupper();

	footer();
	return check_plan();
}
