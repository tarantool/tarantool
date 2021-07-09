#include <base64.h>
#include "unit.h"
#include "trivia/util.h"
#include <string.h>

static void
base64_test(const char *str, int options, const char *no_symbols,
	    int no_symbols_len)
{
	plan(3 + no_symbols_len);

	int len = strlen(str);
	int base64_buflen = base64_bufsize(len + 1, options);
	char *base64_buf = malloc(base64_buflen);
	char *strbuf = malloc(len + 1);
	int rc = base64_encode(str, len + 1, base64_buf, base64_buflen,
			       options);
	ok(rc <= base64_buflen, "length");
	for (int i = 0; i < no_symbols_len; ++i) {
		char c = no_symbols[i];
		if (c == '\n') {
			is(memchr(base64_buf, no_symbols[i], base64_buflen),
			   NULL, "no \\n symbols");
		} else {
			is(memchr(base64_buf, no_symbols[i], base64_buflen),
			   NULL, "no %c symbols", no_symbols[i]);
		}
	}

	is(base64_decode(base64_buf, rc, strbuf, len + 1), len + 1,
	   "decode length ok");
	is(strcmp(str, strbuf), 0, "encode/decode");

	free(base64_buf);
	free(strbuf);

	check_plan();
}

static void
base64_urlsafe_test(const char *str)
{
	const char symbols[] = { '\n', '+', '=' };
	base64_test(str, BASE64_URLSAFE, symbols, lengthof(symbols));
}

static void
base64_nopad_test(const char *str)
{
	const char symbols[] = { '=' };
	base64_test(str, BASE64_NOPAD, symbols, lengthof(symbols));
}

static void
base64_nowrap_test(const char *str)
{
	const char symbols[] = { '\n' };
	base64_test(str, BASE64_NOWRAP, symbols, lengthof(symbols));
}

static void
base64_invalid_chars_test(void)
{
	plan(1);

	/* Upper bit must be cleared */
	const char invalid_data[] = { '\x7b', '\x7c', '\x7d', '\x7e' };
	char outbuf[8];

	/* Invalid chars should be ignored, not decoded into garbage */
	is(base64_decode(invalid_data, sizeof(invalid_data),
	                 outbuf, sizeof(outbuf)),
	   0, "ignoring invalid chars");

	check_plan();
}

static void
base64_no_space_test(void)
{
	plan(1);

	const char *const in = "sIIpHw==";
	const int in_len = strlen(in);
	const int rc = base64_decode(in, in_len, NULL, 0);
	is(rc, 0, "no space in out buffer");

	check_plan();
}

int main(int argc, char *argv[])
{
	plan(30);
	header();

	const char *option_tests[] = {
		"", "a", "123", "1234567", "12345678",
		"\001\002\003\004\005\006\253\254\255",
		"Test +/+/+/ test test test test test test test test test "\
		"test test test test test test test test test test test test "\
		"test test test test test test test test test test test test "\
		"test test test test test test test test test test\n\n"
	};
	for (size_t i = 0; i < lengthof(option_tests); ++i) {
		base64_test(option_tests[i], 0, NULL, 0);
		base64_urlsafe_test(option_tests[i]);
		base64_nopad_test(option_tests[i]);
		base64_nowrap_test(option_tests[i]);
	}

	base64_invalid_chars_test();
	base64_no_space_test();

	footer();
	return check_plan();
}
