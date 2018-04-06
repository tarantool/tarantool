#include "json/path.h"
#include "unit.h"
#include "trivia/util.h"
#include <string.h>

#define reset_to_new_path(value) \
	path = value; \
	len = strlen(value); \
	json_path_parser_create(&parser, path, len);

#define is_next_index(value_len, value) \
	path = parser.src + parser.offset; \
	is(json_path_next(&parser, &node), 0, "parse <%." #value_len "s>", \
	   path); \
	is(node.type, JSON_PATH_NUM, "<%." #value_len "s> is num", path); \
	is(node.num, value, "<%." #value_len "s> is " #value, path);

#define is_next_key(value) \
	len = strlen(value); \
	is(json_path_next(&parser, &node), 0, "parse <" value ">"); \
	is(node.type, JSON_PATH_STR, "<" value "> is str"); \
	is(node.len, len, "len is %d", len); \
	is(strncmp(node.str, value, len), 0, "str is " value);

void
test_basic()
{
	header();
	plan(71);
	const char *path;
	int len;
	struct json_path_parser parser;
	struct json_path_node node;

	reset_to_new_path("[0].field1.field2['field3'][5]");
	is_next_index(3, 0);
	is_next_key("field1");
	is_next_key("field2");
	is_next_key("field3");
	is_next_index(3, 5);

	reset_to_new_path("[3].field[2].field")
	is_next_index(3, 3);
	is_next_key("field");
	is_next_index(3, 2);
	is_next_key("field");

	reset_to_new_path("[\"f1\"][\"f2'3'\"]");
	is_next_key("f1");
	is_next_key("f2'3'");

	/* Support both '.field1...' and 'field1...'. */
	reset_to_new_path(".field1");
	is_next_key("field1");
	reset_to_new_path("field1");
	is_next_key("field1");

	/* Long number. */
	reset_to_new_path("[1234]");
	is_next_index(6, 1234);

	/* Empty path. */
	reset_to_new_path("");
	is(json_path_next(&parser, &node), 0, "parse empty path");
	is(node.type, JSON_PATH_END, "is str");

	/* Path with no '.' at the beginning. */
	reset_to_new_path("field1.field2");
	is_next_key("field1");

	/* Unicode. */
	reset_to_new_path("[2][6]['привет中国world']['中国a']");
	is_next_index(3, 2);
	is_next_index(3, 6);
	is_next_key("привет中国world");
	is_next_key("中国a");

	check_plan();
	footer();
}

#define check_new_path_on_error(value, errpos) \
	reset_to_new_path(value); \
	struct json_path_node node; \
	is(json_path_next(&parser, &node), errpos, "error on position %d" \
	   " for <%s>", errpos, path);

struct path_and_errpos {
	const char *path;
	int errpos;
};

void
test_errors()
{
	header();
	plan(20);
	const char *path;
	int len;
	struct json_path_parser parser;
	const struct path_and_errpos errors[] = {
		/* Double [[. */
		{"[[", 2},
		/* Not string inside []. */
		{"[field]", 2},
		/* String outside of []. */
		{"'field1'.field2", 1},
		/* Empty brackets. */
		{"[]", 2},
		/* Empty string. */
		{"''", 1},
		/* Spaces between identifiers. */
		{" field1", 1},
		/* Start from digit. */
		{"1field", 1},
		{".1field", 2},
		/* Unfinished identifiers. */
		{"['field", 8},
		{"['field'", 9},
		{"[123", 5},
		{"['']", 3},
		/*
		 * Not trivial error: can not write
		 * '[]' after '.'.
		 */
		{".[123]", 2},
		/* Misc. */
		{"[.]", 2},
		/* Invalid UNICODE */
		{"['aaa\xc2\xc2']", 6},
		{".\xc2\xc2", 2},
	};
	for (size_t i = 0; i < lengthof(errors); ++i) {
		reset_to_new_path(errors[i].path);
		int errpos = errors[i].errpos;
		struct json_path_node node;
		is(json_path_next(&parser, &node), errpos,
		   "error on position %d for <%s>", errpos, path);
	}

	reset_to_new_path("f.[2]")
	struct json_path_node node;
	json_path_next(&parser, &node);
	is(json_path_next(&parser, &node), 3, "can not write <field.[index]>")

	reset_to_new_path("f.")
	json_path_next(&parser, &node);
	is(json_path_next(&parser, &node), 3, "error in leading <.>");

	reset_to_new_path("fiel d1")
	json_path_next(&parser, &node);
	is(json_path_next(&parser, &node), 5, "space inside identifier");

	reset_to_new_path("field\t1")
	json_path_next(&parser, &node);
	is(json_path_next(&parser, &node), 6, "tab inside identifier");

	check_plan();
	footer();
}

int
main()
{
	header();
	plan(2);

	test_basic();
	test_errors();

	int rc = check_plan();
	footer();
	return rc;
}
