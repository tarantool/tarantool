#include <stdio.h>
#include <string.h>

#include "trivia/util.h"
#include "http_parser/http_parser.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
test_protocol_version(void)
{
	static const struct {
		const char *status;
		int major;
		int minor;
	} tests[] = {
		{ "HTTP/1.1 200\r\n", 1, 1 },
		{ "HTTP/2.0 301\r\n", 2, 0 },
		{ "HTTP/2 200\r\n", 2, 0 },
	};
	char buf[10];

	plan(6);
	header();

	for (size_t i = 0; i < lengthof(tests); i++) {
		struct http_parser p;
		const char *l;

		http_parser_create(&p);
		p.hdr_name = buf;
		l = tests[i].status;
		http_parse_header_line(&p, &l, l + strlen(l), lengthof(buf));
		is(tests[i].major, p.http_major,
		   "expected major number is '%d', received '%d' for '%s'",
		   tests[i].major, p.http_major, tests[i].status);
		is(tests[i].minor, p.http_minor,
		   "expected minor number is '%d', received '%d' for '%s'",
		   tests[i].minor, p.http_minor, tests[i].status);
	}

	footer();
	check_plan();
}

static void
test_incomplete_header(void)
{
	const char *header = "Content-Type: text/plain";
	const char *end = header + strlen(header);
	char buf[32];
	struct http_parser parser;
	int rc;

	plan(2);
	header();

	http_parser_create(&parser);
	parser.hdr_name = buf;
	rc = http_parse_header_line(&parser, &header, end, lengthof(buf));
	is(HTTP_PARSE_CONTINUE, rc, "incomplete header needs more data");
	ok(header == end, "parser stops at the end of the input buffer");

	footer();
	check_plan();
}

int
main(void)
{
	plan(2);
	test_protocol_version();
	test_incomplete_header();
	return check_plan();
}
