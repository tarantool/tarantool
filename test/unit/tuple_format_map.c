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
test_empty_tuple_format_map(void)
{
	plan(5);
	header();

	struct tuple_format_map map;
	tuple_format_map_create_empty(&map);
	is(map.cache_last_index, -1,
	   "empty map cache last index is correctly initialized");
	ok(tuple_format_map_is_empty(&map),
	   "tuple format `is_empty` method works correctly on empty map");
	is(map.hash_table, NULL,
	   "empty map hash table is correctly initialized");
	is(tuple_format_map_find(&map, 777), NULL,
	   "empty map lookup works correctly");

	struct mpstream stream;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	bool is_err = false;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      mpstream_error, &is_err);
	tuple_format_map_to_mpstream(&map, &stream);
	mpstream_flush(&stream);
	fail_if(is_err);
	size_t data_len = region_used(region) - region_svp;
	const char *data = xregion_join(region, data_len);
	is(memcmp(data, "\x80", MIN(data_len, 1)), 0,
	   "empty map serialization works correctly");
	region_truncate(region, region_svp);
	tuple_format_map_destroy(&map);

	footer();
	return check_plan();
}

static int
test_tuple_format_map_only_cache(void)
{
	plan(6 * TUPLE_FORMAT_MAP_CACHE_SIZE + 5);
	header();

	struct tuple_format_map map;
	tuple_format_map_create_empty(&map);
	char buf[1024];
	char *p = buf;
	char *format_data[TUPLE_FORMAT_MAP_CACHE_SIZE];
	size_t format_data_len[TUPLE_FORMAT_MAP_CACHE_SIZE];
	uint16_t format_ids[TUPLE_FORMAT_MAP_CACHE_SIZE];
	for (ssize_t i = 0; i < TUPLE_FORMAT_MAP_CACHE_SIZE; ++i) {
		char num[32];
		snprintf(num, lengthof(num), "%zu", i);
		size_t len = mp_format(p, lengthof(buf) - (p - buf),
				       "[{%s%s}]", "name", num);
		struct tuple_format *format =
			runtime_tuple_format_new(p, len, false);
		format_data[i] = p;
		format_data_len[i] = len;
		p += len;
		format_ids[i] = format->id;
		tuple_format_map_add_format(&map, format->id);
		is(map.cache_last_index, i,
		   "map cache last index is updated correctly");
		is(map.cache[i].key, format->id,
		   "map cache is updated correctly");
		is(map.hash_table, NULL,
		   "map hash table is not allocated");
		isnt(tuple_format_map_find(&map, format->id), NULL,
		     "map lookup works correctly");
	}
	ok(!tuple_format_map_is_empty(&map),
	   "tuple format `is_empty` method works correctly on non-empty map");
	for (ssize_t i = 0; i < TUPLE_FORMAT_MAP_CACHE_SIZE; ++i) {
		ssize_t idx = TUPLE_FORMAT_MAP_CACHE_SIZE - 1 - i;
		isnt(tuple_format_map_find(&map, format_ids[idx]), NULL,
		     "filled map lookup works correctly");
	}

	struct mpstream stream;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	bool is_err = false;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      mpstream_error, &is_err);
	tuple_format_map_to_mpstream(&map, &stream);
	mpstream_flush(&stream);
	fail_if(is_err);
	size_t data_len = region_used(region) - region_svp;
	const char *data = xregion_join(region, data_len);
	char expected_data[1024];
	p = expected_data;
	p = mp_encode_map(expected_data, TUPLE_FORMAT_MAP_CACHE_SIZE);
	for (ssize_t i = 0; i < TUPLE_FORMAT_MAP_CACHE_SIZE; ++i) {
		p = mp_encode_uint(p, format_ids[i]);
		p = mp_memcpy(p, format_data[i], format_data_len[i]);
	}
	size_t expected_data_len = p - expected_data;
	is(memcmp(data, expected_data, MIN(data_len, expected_data_len)), 0,
	   "filled map serialization works correctly");
	region_truncate(region, region_svp);

	struct tuple_format_map map_from_mp;
	tuple_format_map_create_from_mp(&map_from_mp, expected_data);
	is(map_from_mp.cache_last_index, TUPLE_FORMAT_MAP_CACHE_SIZE - 1,
	   "map from MsgPack cache last index is correct");
	is(map_from_mp.hash_table, NULL,
	   "map from MsgPack hash table is not allocated");
	for (ssize_t i = 0; i < TUPLE_FORMAT_MAP_CACHE_SIZE; ++i)
		isnt(tuple_format_map_find(&map_from_mp, format_ids[i]), NULL,
		     "map from MsgPack lookup works correctly");
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      mpstream_error, &is_err);
	tuple_format_map_to_mpstream(&map, &stream);
	mpstream_flush(&stream);
	fail_if(is_err);
	data_len = region_used(region) - region_svp;
	data = xregion_join(region, data_len);
	is(memcmp(data, expected_data, MIN(data_len, expected_data_len)), 0,
	   "map from MsgPack serialization works correctly");
	region_truncate(region, region_svp);
	tuple_format_map_destroy(&map_from_mp);
	tuple_format_map_destroy(&map);

	footer();
	return check_plan();
}

static int
test_tuple_format_map_cache_and_hash_table(void)
{
	plan(3 * (TUPLE_FORMAT_MAP_CACHE_SIZE + 2) + 8);
	header();

	struct tuple_format_map map;
	tuple_format_map_create_empty(&map);
	char buf[1024];
	char *p = buf;
	char *format_data[TUPLE_FORMAT_MAP_CACHE_SIZE + 2];
	size_t format_data_len[TUPLE_FORMAT_MAP_CACHE_SIZE + 2];
	uint16_t format_ids[TUPLE_FORMAT_MAP_CACHE_SIZE + 2];
	for (ssize_t i = 0; i < TUPLE_FORMAT_MAP_CACHE_SIZE + 2; ++i) {
		char num[32];
		snprintf(num, lengthof(num), "%zu", i);
		size_t len = mp_format(p, lengthof(buf) - (p - buf),
				       "[{%s%s}]", "name", num);
		struct tuple_format *format =
			runtime_tuple_format_new(p, len, false);
		format_data[i] = p;
		format_data_len[i] = len;
		p += len;
		format_ids[i] = format->id;
		tuple_format_map_add_format(&map, format->id);
		isnt(tuple_format_map_find(&map, format->id), NULL,
		     "map lookup works correctly");
	}
	ok(!tuple_format_map_is_empty(&map),
	   "tuple format `is_empty` method works correctly on non-empty map");
	is(map.cache_last_index, 1,
	   "map cache last index is wrapped correctly");
	is(map.cache[0].key, format_ids[TUPLE_FORMAT_MAP_CACHE_SIZE],
	   "map cache is updated correctly");
	is(map.cache[1].key, format_ids[TUPLE_FORMAT_MAP_CACHE_SIZE + 1],
	   "map cache is updated correctly");
	isnt(map.hash_table, NULL,
	     "map hash table is allocated");
	for (ssize_t i = 0; i < TUPLE_FORMAT_MAP_CACHE_SIZE + 2; ++i) {
		ssize_t idx = TUPLE_FORMAT_MAP_CACHE_SIZE + 1 - i;
		isnt(tuple_format_map_find(&map, format_ids[idx]), NULL,
		     "filled map lookup works correctly");
	}

	uint16_t uncached_format_id = UINT16_MAX;
	for (size_t i = 0; i < TUPLE_FORMAT_MAP_CACHE_SIZE + 2; ++i) {
		bool is_cached = false;
		for (size_t j = 0; j < TUPLE_FORMAT_MAP_CACHE_SIZE; ++j) {
			if (map.cache[j].key == format_ids[i]) {
				is_cached = true;
				break;
			}
		}
		if (!is_cached) {
			uncached_format_id = format_ids[i];
			break;
		}
	}
	fail_if(uncached_format_id == UINT16_MAX);
	tuple_format_map_find(&map, uncached_format_id);
	is(map.cache[map.cache_last_index].key, uncached_format_id,
	   "filled map cache is updated correctly");

	struct mpstream stream;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	bool is_err = false;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      mpstream_error, &is_err);
	tuple_format_map_to_mpstream(&map, &stream);
	mpstream_flush(&stream);
	fail_if(is_err);
	size_t data_len = region_used(region) - region_svp;
	const char *data = xregion_join(region, data_len);
	struct tuple_format_map map_from_mp;
	tuple_format_map_create_from_mp(&map_from_mp, data);
	region_truncate(region, region_svp);
	is(map_from_mp.cache_last_index, 1,
	   "map from MsgPack cache last index is wrapped correctly");
	isnt(map_from_mp.hash_table, NULL,
	     "map from MsgPack hash table is allocated");
	for (ssize_t i = 0; i < TUPLE_FORMAT_MAP_CACHE_SIZE + 2; ++i)
		isnt(tuple_format_map_find(&map_from_mp, format_ids[i]), NULL,
		     "map from MsgPack lookup works correctly");
	tuple_format_map_destroy(&map_from_mp);
	tuple_format_map_destroy(&map);

	footer();
	return check_plan();
}

/**
 * Test that formats that are added to format map does not leak after
 * destruction of the format map.
 */
static int
test_tuple_format_map_duplicate(size_t format_count, size_t add_count)
{
	plan(2 * format_count);
	header();

	uint16_t *format_ids = xmalloc(format_count * sizeof(format_ids[0]));

	struct tuple_format_map map;
	tuple_format_map_create_empty(&map);

	for (size_t i = 0; i < format_count; ++i) {
		char name[32];
		snprintf(name, lengthof(name), "test%zu", i);
		char str_format[16];
		size_t len = mp_format(str_format, lengthof(str_format),
				       "[{%s%s}]", "name", name);
		struct tuple_format *format =
			runtime_tuple_format_new(str_format, len, false);
		is(format->refs, 0, "the new format must have not refs");
		tuple_format_ref(format);
		format_ids[i] = format->id;
	}

	for (size_t j = 0; j < add_count; ++j)
		for (size_t i = 0; i < format_count; ++i)
			tuple_format_map_add_format(&map, format_ids[i]);

	tuple_format_map_destroy(&map);

	for (size_t i = 0; i < format_count; ++i) {
		struct tuple_format *format = tuple_format_by_id(format_ids[i]);
		is(format->refs, 1, "must be the last ref");
		tuple_format_unref(format);
	}

	free(format_ids);

	footer();
	return check_plan();
}

/**
 * Insert one format many times and check format leaks.
 */
static int
test_tuple_format_map_duplicate_one_format(void)
{
	header();

	size_t format_count = 1;
	size_t add_count = TUPLE_FORMAT_MAP_CACHE_SIZE * 10;
	int rc = test_tuple_format_map_duplicate(format_count, add_count);

	footer();
	return rc;
}

/**
 * Insert few formats (fits to cache) many times and check format leaks.
 */
static int
test_tuple_format_map_duplicate_few_formats(void)
{
	header();

	size_t format_count = TUPLE_FORMAT_MAP_CACHE_SIZE;
	size_t add_count = TUPLE_FORMAT_MAP_CACHE_SIZE * 10;
	int rc = test_tuple_format_map_duplicate(format_count, add_count);

	footer();
	return rc;
}

/**
 * Insert many formats (doesn't fit to cache) many times and check format leaks.
 */
static int
test_tuple_format_map_duplicate_many_formats(void)
{
	header();

	size_t format_count = TUPLE_FORMAT_MAP_CACHE_SIZE * 4;
	size_t add_count = TUPLE_FORMAT_MAP_CACHE_SIZE * 10;
	int rc = test_tuple_format_map_duplicate(format_count, add_count);

	footer();
	return rc;
}

/**
 * Check for format leaks after loading from msgpack.
 */
static int
test_tuple_format_map_decode_from_msgpack(void)
{
	plan(21);
	header();

	struct tuple_format *format[2];
	char name[2][32];
	for (size_t i = 0; i < lengthof(format); i++) {
		snprintf(name[i], lengthof(name[i]), "test%zu", i);
		char str_format[16];
		size_t len = mp_format(str_format, lengthof(str_format),
				       "[{%s%s}]", "name", name[i]);
		format[i] = runtime_tuple_format_new(str_format, len, true);
		is(format[i]->refs, 0, "the new format must have not refs");
		tuple_format_ref(format[i]);
	}

	char buf[1024];
	struct tuple_format_map map;

	/* Valid formats. */
	mp_format(buf, lengthof(buf), "{%u[{%s%s}]%u[{%s%s}]}",
		  0, "name", name[0], 1, "name", name[1]);
	is(tuple_format_map_create_from_mp(&map, buf), 0, "expected success");
	for (size_t i = 0; i < lengthof(format); i++)
		is(format[i]->refs, 2, "must be referenced from map");
	tuple_format_map_destroy(&map);
	for (size_t i = 0; i < lengthof(format); i++)
		is(format[i]->refs, 1, "must be unreferenced from map");

	/* Invalid format id. */
	mp_format(buf, lengthof(buf), "{%u[{%s%s}]%s[{%s%s}]}",
		  0, "name", name[0], "invalid", "name", name[1]);
	is(tuple_format_map_create_from_mp(&map, buf), -1, "expected failure");
	ok(!diag_is_empty(diag_get()), "diag must be set");
	diag_clear(diag_get());
	for (size_t i = 0; i < lengthof(format); i++)
		is(format[i]->refs, 1, "must not be referenced from map");

	/* Invalid format. */
	mp_format(buf, lengthof(buf), "{%u[{%s%s}]%u%s}",
		  0, "name", name[0], 1, "invalid");
	is(tuple_format_map_create_from_mp(&map, buf), -1, "expected failure");
	ok(!diag_is_empty(diag_get()), "diag must be set");
	diag_clear(diag_get());
	for (size_t i = 0; i < lengthof(format); i++)
		is(format[i]->refs, 1, "must not be referenced from map");

	/* Invalid format of format. */
	mp_format(buf, lengthof(buf), "{%u[{%s%s}]%u[{%s%s}]}",
		  0, "name", name[0], 1, "invalid", name[1]);
	is(tuple_format_map_create_from_mp(&map, buf), -1, "expected failure");
	ok(!diag_is_empty(diag_get()), "diag must be set");
	diag_clear(diag_get());
	for (size_t i = 0; i < lengthof(format); i++)
		is(format[i]->refs, 1, "must not be referenced from map");

	for (size_t i = 0; i < lengthof(format); i++) {
		is(format[i]->refs, 1, "must be the last ref");
		tuple_format_unref(format[i]);
	}

	footer();
	return check_plan();
}

static int
test_tuple_format_map(void)
{
	plan(7);
	header();

	test_empty_tuple_format_map();
	test_tuple_format_map_only_cache();
	test_tuple_format_map_cache_and_hash_table();
	test_tuple_format_map_duplicate_one_format();
	test_tuple_format_map_duplicate_few_formats();
	test_tuple_format_map_duplicate_many_formats();
	test_tuple_format_map_decode_from_msgpack();

	footer();
	return check_plan();
}

static uint32_t
test_field_name_hash(const char *str, uint32_t len)
{
	return str[0] + len;
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(test_field_name_hash);

	int rc = test_tuple_format_map();

	tuple_free();
	fiber_free();
	memory_free();
	return rc;
}
