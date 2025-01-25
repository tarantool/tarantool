#include "box/msgpack.h"
#include "mp_extension_types.h"

#include "trivia/util.h"
#include <stdio.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static int
test_mp_print(const char *sample, const char *ext_data)
{
	plan(2);

	char str[200] = {0};

	mp_snprint(str, sizeof(str), ext_data);
	is(strcmp(sample, str), 0, "mp_snprint unknown extension");

	memset(str, 0, sizeof(str));
	FILE *f  = tmpfile();
	assert(f != NULL);
	mp_fprint(f, ext_data);
	rewind(f);
	fread(str, 1, sizeof(str), f);
	is(strcmp(sample, str), 0, "mp_fprint unknown extension");

	return check_plan();
}

static int
test_mp_print_unknown_extention(void)
{
	plan(1);

	char sample[] = "(extension: type 0, len 10)";
	char data[] = { 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca };
	char *ext_data = xmalloc(mp_sizeof_ext(sizeof(data)));

	char *data_end = ext_data;
	data_end = mp_encode_ext(data_end, MP_UNKNOWN_EXTENSION, data, sizeof(data));

	test_mp_print(sample, ext_data);

	free(ext_data);

	return check_plan();
}

int
main(void)
{
	plan(1);

	msgpack_init();

	test_mp_print_unknown_extention();

	return check_plan();
}
