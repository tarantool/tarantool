#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "tuple.h"
#include "memory.h"
#include "msgpuck.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

void
check(const char *key, uint32_t part_count, const char *expect, const char *info)
{
	const char *str = key_str(key, part_count);
	ok(strcmp(str, expect) == 0, "%s: %s == %s", info, str, expect);
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);

	plan(7);
	header();

	char data[1024] = {};
	char *ptr = NULL;

	check(NULL, 0, "[]", "Empty key");

	mp_encode_uint(data, 37);
	check(data, 1, "[37]", "Single unsigned");

	mp_encode_int(data, -37);
	check(data, 1, "[-37]", "Single integer");

	mp_encode_str0(data, "37");
	check(data, 1, "[\"37\"]", "Single string");

	ptr = mp_encode_array(data, 2);
	ptr = mp_encode_uint(ptr, 1);
	mp_encode_str0(ptr, "2");
	check(data, 1, "[[1, \"2\"]]", "Array");

	ptr = mp_encode_map(data, 2);
	ptr = mp_encode_str0(ptr, "key1");
	ptr = mp_encode_uint(ptr, 1);
	ptr = mp_encode_str0(ptr, "key2");
	mp_encode_uint(ptr, 2);
	check(data, 1, "[{\"key1\": 1, \"key2\": 2}]", "Map");

	ptr = mp_encode_int(data, -1);
	ptr = mp_encode_uint(ptr, 0);
	ptr = mp_encode_str0(ptr, "1");
	ptr = mp_encode_array(ptr, 2);
	ptr = mp_encode_uint(ptr, 2);
	ptr = mp_encode_map(ptr, 2);
	ptr = mp_encode_str0(ptr, "3");
	ptr = mp_encode_uint(ptr, 4);
	ptr = mp_encode_uint(ptr, 5);
	mp_encode_str0(ptr, "6");
	check(data, 4, "[-1, 0, \"1\", [2, {\"3\": 4, 5: \"6\"}]]",
	      "Everything at once");

	footer();

	fiber_free();
	memory_free();
	return check_plan();
}
