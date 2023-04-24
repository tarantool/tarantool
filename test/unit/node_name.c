#include "node_name.h"

#include "trivia/util.h"
#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
test_node_name_is_valid(void)
{
	header();
	plan(27);

	const char *bad_names[] = {
		"",
		"1",
		"1abc",
		"*",
		"a_b",
		"aBcD",
		"a~b",
		"{ab}",
	};
	for (int i = 0; i < (int)lengthof(bad_names); ++i) {
		const char *str = bad_names[i];
		ok(!node_name_is_valid(str), "bad name %d", i);
		ok(!node_name_is_valid_n(str, strlen(str)),
		   "bad name n %d", i);
	}
	const char *good_names[] = {
		"a",
		"a-b-c",
		"abc",
		"a1b2c3-d4-e5-",
	};
	for (int i = 0; i < (int)lengthof(good_names); ++i) {
		const char *str = good_names[i];
		ok(node_name_is_valid(str), "bad name %d", i);
		ok(node_name_is_valid_n(str, strlen(str)),
		   "bad name n %d", i);
	}
	char name[NODE_NAME_SIZE_MAX + 1];
	memset(name, 'a', sizeof(name));
	ok(!node_name_is_valid_n(name, NODE_NAME_SIZE_MAX), "max + 1");
	ok(node_name_is_valid_n(name, NODE_NAME_LEN_MAX), "max n");
	name[NODE_NAME_SIZE_MAX - 1] = 0;
	ok(node_name_is_valid(name), "max");

	check_plan();
	footer();
}

static void
test_node_name_str(void)
{
	header();
	plan(3);

	const char *stub = "<no-name>";
	is(strcmp(node_name_str("abc"), "abc"), 0, "name");
	is(strcmp(node_name_str(""), stub), 0, "empty");
	is(strcmp(node_name_str(NULL), stub), 0, "null");

	check_plan();
	footer();
}

int
main(void)
{
	header();
	plan(2);

	test_node_name_is_valid();
	test_node_name_str();

	int rc = check_plan();
	footer();
	return rc;
}
