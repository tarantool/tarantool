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

int
main(void)
{
	plan(1);
	test_protocol_version();
	return check_plan();
}
