#include "string.h"
#include "unit.h"
#include "trivia/util.h"

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

int
main(void)
{
	plan(1);
	header();

	test_strlcat();

	footer();
	return check_plan();
}
