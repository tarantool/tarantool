#include "trivia/util.h"
#include <stdlib.h>
#include <string.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

typedef int
(*escape_inplace_f)(char *buf, int size);

/*
 * The escape helpers truncate the input so the escaped result plus the null
 * terminator fit into the buffer, and they must never write past buf[size - 1].
 * A run of characters that need no escaping fills the buffer up to size - 1
 * bytes - the largest value the length cap allows - which is exactly the case
 * where an off-by-one terminator write lands one byte past the end.
 */
static void
test_no_overflow(escape_inplace_f escape, const char *name, int size)
{
	plan(2);
	const unsigned char guard = 0x5a;
	/* One extra byte we own to catch a write past buf[size - 1]. */
	char *buf = xmalloc(size + 1);
	memset(buf, 'a', size - 1);
	buf[size - 1] = '\0';
	buf[size] = guard;

	int rc = escape(buf, size);
	is(rc, size - 1, "%s size %d: escaped length fits the buffer",
	   name, size);
	is((unsigned char)buf[size], guard, "%s size %d: no write past the end",
	   name, size);

	free(buf);
	check_plan();
}

/* Content that needs no escaping must pass through unchanged. */
static void
test_plain_passthrough(escape_inplace_f escape, const char *name)
{
	plan(2);
	const char *src = "hello";
	int len = strlen(src);
	char buf[16];
	memcpy(buf, src, len + 1);

	int rc = escape(buf, sizeof(buf));
	is(rc, len, "%s: length unchanged", name);
	ok(memcmp(buf, src, len) == 0, "%s: bytes unchanged", name);

	check_plan();
}

int
main(void)
{
	plan(8);
	header();

	const int sizes[] = {2, 5, 8, 16};
	for (size_t i = 0; i < lengthof(sizes); i++)
		test_no_overflow(syslog_escape_inplace, "syslog", sizes[i]);
	test_no_overflow(json_escape_inplace, "json", 8);
	test_no_overflow(json_syslog_escape_inplace, "json_syslog", 8);

	test_plain_passthrough(syslog_escape_inplace, "syslog");
	test_plain_passthrough(json_escape_inplace, "json");

	footer();
	return check_plan();
}
