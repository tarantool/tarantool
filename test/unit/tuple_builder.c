#include "fiber.h"
#include "memory.h"
#include "msgpuck.h"
#include "tuple.h"
#include "tuple_builder.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
test_tuple_builder_empty(void)
{
	plan(2);
	header();

	const char *data, *data_end;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	struct tuple_builder builder;
	tuple_builder_new(&builder, region);
	tuple_builder_finalize(&builder, &data, &data_end);

	is(mp_typeof(*data), MP_ARRAY, "type is MP_ARRAY");
	is(mp_decode_array(&data), 0, "array is empty");
	region_truncate(region, region_svp);

	footer();
	check_plan();
}

static void
test_tuple_builder_nulls(void)
{
	plan(4);
	header();

	const char *data, *data_end;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	struct tuple_builder builder;
	tuple_builder_new(&builder, region);
	tuple_builder_add_nil(&builder);
	tuple_builder_add_nil(&builder);
	tuple_builder_add_nil(&builder);
	tuple_builder_finalize(&builder, &data, &data_end);

	is(mp_decode_array(&data), 3, "array contains 3 elements");
	is(mp_typeof(*data), MP_NIL, "[0] MP_NIL");
	mp_decode_nil(&data);
	is(mp_typeof(*data), MP_NIL, "[1] MP_NIL");
	mp_decode_nil(&data);
	is(mp_typeof(*data), MP_NIL, "[2] MP_NIL");
	region_truncate(region, region_svp);

	footer();
	check_plan();
}

static struct tuple *
create_tuple1(void)
{
	char data[16];
	char *end = data;
	end = mp_encode_array(end, 5);
	end = mp_encode_uint(end, 0);
	end = mp_encode_uint(end, 111);
	end = mp_encode_uint(end, 222);
	end = mp_encode_uint(end, 333);
	end = mp_encode_uint(end, 444);

	struct tuple *tuple = tuple_new(tuple_format_runtime, data, end);
	tuple_ref(tuple);
	return tuple;
}

static struct tuple *
create_tuple2(void)
{
	char data[16];
	char *end = data;
	end = mp_encode_array(end, 3);
	end = mp_encode_str0(end, "xxx");
	end = mp_encode_str0(end, "yyy");
	end = mp_encode_str0(end, "zzz");

	struct tuple *tuple = tuple_new(tuple_format_runtime, data, end);
	tuple_ref(tuple);
	return tuple;
}

static void
test_tuple_builder_merge(void)
{
	plan(9);
	header();

	uint32_t len;
	const char *str, *data, *data_end;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	struct tuple *tuple1 = create_tuple1();
	struct tuple *tuple2 = create_tuple2();
	const char *t1f2 = tuple_field(tuple1, 2);
	const char *t1f3 = tuple_field(tuple1, 3);
	const char *t1f4 = tuple_field(tuple1, 4);
	const char *t2f0 = tuple_field(tuple2, 0);
	const char *t2f1 = tuple_field(tuple2, 1);
	const char *t2f2 = tuple_field(tuple2, 2);

	struct tuple_builder builder;
	tuple_builder_new(&builder, region);
	tuple_builder_add(&builder, t1f2, t1f4 - t1f2, 2);
	tuple_builder_add(&builder, t2f0, t2f2 - t2f0, 2);
	tuple_builder_add_nil(&builder);
	tuple_builder_add(&builder, t2f1, t2f2 - t2f1, 1);
	tuple_builder_add(&builder, t1f2, t1f3 - t1f2, 1);
	tuple_builder_add_nil(&builder);
	tuple_builder_finalize(&builder, &data, &data_end);

	tuple_unref(tuple1);
	tuple_unref(tuple2);

	is(mp_decode_array(&data), 8, "array contains 8 elements");
	is(mp_decode_uint(&data), 222, "[0] MP_UINT is 222");
	is(mp_decode_uint(&data), 333, "[1] MP_UINT is 333");
	str = mp_decode_str(&data, &len);
	is(strncmp(str, "xxx", 3), 0, "[2] MP_STR is xxx");
	str = mp_decode_str(&data, &len);
	is(strncmp(str, "yyy", 3), 0, "[3] MP_STR is yyy");
	is(mp_typeof(*data), MP_NIL, "[4] MP_NIL");
	mp_decode_nil(&data);
	str = mp_decode_str(&data, &len);
	is(strncmp(str, "yyy", 3), 0, "[5] MP_STR is yyy");
	is(mp_decode_uint(&data), 222, "[6] MP_UINT is 222");
	is(mp_typeof(*data), MP_NIL, "[7] MP_NIL");
	region_truncate(region, region_svp);

	footer();
	check_plan();
}

static int
test_tuple_builder(void)
{
	plan(3);
	header();

	test_tuple_builder_empty();
	test_tuple_builder_nulls();
	test_tuple_builder_merge();

	footer();
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);

	int rc = test_tuple_builder();

	tuple_free();
	fiber_free();
	memory_free();
	return rc;
}
