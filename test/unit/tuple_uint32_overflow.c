#include "tuple.h"
#include "unit.h"
#include <stdio.h>
#include <memory.h>
#include <fiber.h>


static char data[500];

struct tuple *
tuple_new_data()
{
	char* end = data;
	end = mp_encode_array(end, 2);
	end = mp_encode_uint(end, UINT32_MAX);
	end = mp_encode_uint(end, UINT64_MAX);
	struct tuple *tuple;
	tuple = tuple_new(tuple_format_runtime, data, end);
	tuple_ref(tuple);

	return tuple;
}


static void
tuple_next_u32_test()
{
	header();
	plan(2);

	struct tuple *tuple = tuple_new_data();
	uint32_t field;
	struct tuple_iterator it;
	tuple_rewind(&it, tuple);
	tuple_next_u32(&it, &field);
	is(field, UINT32_MAX, "can read next uint32_t");
	is(tuple_next_u32(&it, &field), -1, "can't read next uint64_t");
	tuple_unref(tuple);

	check_plan();
	footer();
}

static void
tuple_field_u32_test()
{
	header();
	plan(2);

	struct tuple *tuple = tuple_new_data();
	uint32_t field;
	tuple_field_u32(tuple, 0, &field);
	is(field, UINT32_MAX, "can read uint32_t");
	is(tuple_field_u32(tuple, 1, &field), -1, "can't read uint64_t");
	tuple_unref(tuple);

	check_plan();
	footer();
}

int
main()
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);


	tuple_next_u32_test();
	tuple_field_u32_test();

	tuple_free();
	fiber_free();
	memory_free();

	return 0;
}
