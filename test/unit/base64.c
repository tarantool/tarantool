#include <third_party/base64.h>
#include "unit.h"
#include <string.h>


static void
base64_test(const char *str)
{
	header();

	int len = strlen(str);
	int base64_buflen = base64_bufsize(len);
	char *base64_buf = malloc(base64_buflen);
	char *strbuf = malloc(len + 1);

	int res = base64_encode(str, len, base64_buf, base64_buflen);

	fail_unless(strlen(base64_buf) == (unsigned)res);

	base64_decode(base64_buf, strlen(base64_buf), strbuf, len + 1);
	strbuf[len] = '\0';

	fail_unless(strcmp(str, strbuf) == 0);

	free(base64_buf);
	free(strbuf);

	footer();
}

int main(int argc, char *argv[])
{
	base64_test("");
	base64_test("a");
	base64_test("Something that doesn't fit into a single line, "
	            "something that doesn't fit into a single line, "
	            "something that doesn't fit into a single line, "
	            "something that doesn't fit into a single line, "
	            "something that doesn't fit into a single line. ");
	base64_test("\001\002\003\004\005\006\253\254\255");
}
