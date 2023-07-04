#include "box/mp_box_ctx.h"
#include "box/mp_tuple.h"
#include "box/tuple.h"
#include "box/tuple_format_map.h"

#include "core/fiber.h"
#include "core/memory.h"

#include "mpstream/mpstream.h"

#include "msgpuck/msgpuck.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
mpstream_error(void *is_err)
{
	*(bool *)is_err = true;
}

static int
test_mp_sizeof_tuple(void)
{
	plan(1);
	header();

	char buf[1024];
	size_t size = mp_format(buf, lengthof(buf), "[]");

	struct tuple *tuple = tuple_new(tuple_format_runtime, buf, buf + size);
	is(mp_sizeof_tuple(tuple),
	   mp_sizeof_ext(mp_sizeof_uint(tuple->format_id) + tuple_bsize(tuple)),
	   "sizeof tuple works correctly");
	tuple_delete(tuple);

	footer();
	return check_plan();
}

static int
test_mp_encode_tuple(void)
{
	plan(2);
	header();

	char buf[1024];
	size_t size = mp_format(buf, lengthof(buf), "[]");
	struct tuple *tuple = tuple_new(tuple_format_runtime, buf, buf + size);
	char *expected = mp_encode_tuple(buf, tuple);
	size_t encoded_len = expected - buf;
	size_t ext_len =
		mp_sizeof_uint(tuple->format_id) + tuple_bsize(tuple);
	size_t expected_len = mp_sizeof_ext(ext_len);
	is(encoded_len, expected_len);
	char *w = expected;
	w = mp_encode_extl(w, MP_TUPLE, ext_len);
	w = mp_encode_uint(w, tuple->format_id);
	mp_encode_array(w, 0);
	is(memcmp(buf, expected, encoded_len), 0,
	   "MP_TUPLE encoding works correctly");
	tuple_delete(tuple);

	footer();
	return check_plan();
}

static int
test_tuple_to_mpstream_as_ext(void)
{
	plan(2);
	header();

	char buf[1024];
	size_t size = mp_format(buf, lengthof(buf), "[]");
	struct tuple *tuple = tuple_new(tuple_format_runtime, buf, buf + size);
	struct mpstream stream;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	bool is_err = false;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      mpstream_error, &is_err);
	tuple_to_mpstream_as_ext(tuple, &stream);
	mpstream_flush(&stream);
	fail_if(is_err);
	size_t data_len = region_used(region) - region_svp;
	const char *data = xregion_join(region, data_len);
	char *expected = mp_encode_tuple(buf, tuple);
	tuple_delete(tuple);
	size_t expected_len = expected - buf;
	is(data_len, expected_len);
	is(memcmp(data, buf, data_len), 0,
	   "MP_TUPLE encoding to MsgPack stream works correctly");
	region_truncate(region, region_svp);

	footer();
	return check_plan();
}

static int
test_mp_validate_tuple(void)
{
	plan(8);
	header();

	char buf[1024];
	size_t size = mp_format(buf, lengthof(buf), "[]");
	struct tuple *tuple = tuple_new(tuple_format_runtime, buf, buf + size);
	char *w = mp_encode_tuple(buf, tuple);
	tuple_delete(tuple);
	size_t encoded_len = w - buf;
	int8_t unused;
	const char *r = buf;
	uint32_t ext_len = mp_decode_extl(&r, &unused);
	is(mp_validate_tuple(r, ext_len), 0,
	   "MP_TUPLE validation works correctly for valid tuple");

	encoded_len = mp_sizeof_nil();
	w = buf;
	w = mp_encode_extl(w, MP_TUPLE, encoded_len);
	mp_encode_nil(w);
	r = buf;
	ext_len = mp_decode_extl(&r, &unused);
	isnt(mp_validate_tuple(r, ext_len), 0,
	     "MP_TUPLE validation works correctly for invalid tuple");

	encoded_len = mp_sizeof_uint(UINT32_MAX) - 1;
	w = buf;
	w = mp_encode_extl(w, MP_TUPLE, encoded_len);
	mp_encode_uint(w, UINT32_MAX);
	r = buf;
	ext_len = mp_decode_extl(&r, &unused);
	isnt(mp_validate_tuple(w, ext_len), 0,
	     "MP_TUPLE validation works correctly for invalid tuple");

	encoded_len = mp_sizeof_uint(777);
	w = buf;
	w = mp_encode_extl(w, MP_TUPLE, encoded_len);
	mp_encode_uint(w, 777);
	r = buf;
	ext_len = mp_decode_extl(&r, &unused);
	isnt(mp_validate_tuple(r, ext_len), 0,
	     "MP_TUPLE validation works correctly for invalid tuple");

	encoded_len = mp_sizeof_uint(777) + mp_sizeof_nil();
	w = buf;
	w = mp_encode_extl(w, MP_TUPLE, encoded_len);
	w = mp_encode_uint(w, 777);
	mp_encode_nil(w);
	r = buf;
	ext_len = mp_decode_extl(&r, &unused);
	isnt(mp_validate_tuple(r, ext_len), 0,
	     "MP_TUPLE validation works correctly for invalid tuple");

	encoded_len = mp_sizeof_uint(777) + mp_sizeof_nil();
	w = buf;
	w = mp_encode_extl(w, MP_TUPLE, encoded_len);
	w = mp_encode_uint(w, 777);
	mp_encode_nil(w);
	r = buf;
	ext_len = mp_decode_extl(&r, &unused);
	isnt(mp_validate_tuple(r, ext_len), 0,
	     "MP_TUPLE validation works correctly for invalid tuple");

	encoded_len = mp_sizeof_uint(777) + mp_sizeof_array(1);
	w = buf;
	w = mp_encode_extl(w, MP_TUPLE, encoded_len);
	w = mp_encode_uint(w, 777);
	w = mp_encode_array(w, 1);
	r = buf;
	ext_len = mp_decode_extl(&r, &unused);
	isnt(mp_validate_tuple(r, ext_len), 0,
	     "MP_TUPLE validation works correctly for invalid tuple");

	encoded_len = mp_sizeof_uint(777) + mp_sizeof_array(1);
	w = buf;
	w = mp_encode_extl(w, MP_TUPLE, encoded_len);
	w = mp_encode_uint(w, 777);
	w = mp_encode_array(w, 1);
	mp_encode_nil(w);
	r = buf;
	ext_len = mp_decode_extl(&r, &unused);
	isnt(mp_validate_tuple(r, ext_len), 0,
	     "MP_TUPLE validation works correctly for invalid tuple");

	footer();
	return check_plan();
}

static int
test_tuple_unpack(void)
{
	plan(5);
	header();

	struct tuple_format_map format_map;
	tuple_format_map_create_empty(&format_map);
	tuple_format_map_add_format(&format_map, tuple_format_runtime->id);

	char buf[1024];
	size_t size = mp_format(buf, lengthof(buf), "[]");
	struct tuple *tuple = tuple_new(tuple_format_runtime, buf, buf + size);
	mp_encode_tuple(buf, tuple);
	tuple_delete(tuple);
	struct tuple *decoded;
	const char *r = buf;
	int8_t unused;
	mp_decode_extl(&r, &unused);
	decoded = tuple_unpack(&r, &format_map);
	isnt(decoded, NULL, "valid MP_TUPLE is unpacked correctly");
	is(decoded->format_id, tuple_format_runtime->id,
	   "valid MP_TUPLE is unpacked correctly");
	tuple_delete(decoded);
	tuple_format_map_destroy(&format_map);

	tuple_format_map_create_empty(&format_map);
	r = buf;
	mp_decode_extl(&r, &unused);
	is(tuple_unpack(&r, &format_map), NULL,
	   "MP_TUPLE with invalid format is unpacked correctly");

	r = buf;
	mp_decode_extl(&r, &unused);
	decoded = tuple_unpack_without_format(&r);
	isnt(decoded, NULL,
	     "valid MP_TUPLE is unpacked without format correctly");
	is(decoded->format_id, tuple_format_runtime->id,
	   "valid MP_TUPLE is unpacked without format correctly");
	tuple_delete(decoded);

	footer();
	return check_plan();
}

static int
test_mp_decode_tuple(void)
{
	plan(4);
	header();

	struct tuple_format_map format_map;
	tuple_format_map_create_empty(&format_map);
	tuple_format_map_add_format(&format_map, tuple_format_runtime->id);

	char buf[1024];
	size_t size = mp_format(buf, lengthof(buf), "[]");
	struct tuple *tuple = tuple_new(tuple_format_runtime, buf, buf + size);
	mp_encode_tuple(buf, tuple);
	tuple_delete(tuple);
	struct tuple *decoded;
	const char *r = buf;
	decoded = mp_decode_tuple(&r, &format_map);
	isnt(decoded, NULL, "valid MP_TUPLE is decoded correctly");
	is(decoded->format_id, tuple_format_runtime->id,
	   "valid MP_TUPLE is decoded correctly");
	tuple_delete(decoded);

	char *w = buf;
	mp_encode_nil(w);
	r = buf;
	is(mp_decode_tuple(&r, &format_map), NULL,
	   "invalid MP_TUPLE is decoded correctly");

	w = buf;
	mp_encode_extl(w, mp_extension_type_MAX, 1);
	r = buf;
	is(mp_decode_tuple(&r, &format_map), NULL,
	   "invalid MP_TUPLE is decoded correctly");

	footer();
	return check_plan();
}

static int
test_mp_tuple(void)
{
	plan(6);
	header();

	test_mp_sizeof_tuple();
	test_mp_encode_tuple();
	test_tuple_to_mpstream_as_ext();
	test_mp_validate_tuple();
	test_tuple_unpack();
	test_mp_decode_tuple();

	footer();
	return check_plan();
}

static uint32_t
field_name_hash_impl(const char *str, uint32_t len)
{
	return str[0] + len;
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(field_name_hash_impl);

	int rc = test_mp_tuple();

	tuple_free();
	fiber_free();
	memory_free();
	return rc;
}
