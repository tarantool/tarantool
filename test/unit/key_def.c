#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "fiber.h"
#include "key_def.h"
#include "memory.h"
#include "msgpuck.h"
#include "small/region.h"
#include "trivia/util.h"
#include "tuple.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static char *
test_key_new_va(const char *format, va_list ap)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	/* Format the MsgPack key definition. */
	const size_t mp_buf_size = 1024;
	char *mp_buf = region_alloc(region, mp_buf_size);
	fail_if(mp_buf == NULL);
	size_t mp_size = mp_vformat(mp_buf, mp_buf_size, format, ap);
	fail_if(mp_size > mp_buf_size);

	/* Create a key. */
	char *key = xmalloc(mp_size);
	memcpy(key, mp_buf, mp_size);

	region_truncate(region, region_svp);
	return key;
}

/** Creates a key from a MsgPack format (see mp_format). */
static char *
test_key_new(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	char *key = test_key_new_va(format, ap);
	fail_unless(mp_typeof(*key) == MP_ARRAY);
	va_end(ap);
	return key;
}

static struct tuple *
test_tuple_new_va(const char *format, va_list ap)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	/* Format the MsgPack key definition. */
	const size_t mp_buf_size = 1024;
	char *mp_buf = region_alloc(region, mp_buf_size);
	fail_if(mp_buf == NULL);
	size_t mp_size = mp_vformat(mp_buf, mp_buf_size, format, ap);
	fail_if(mp_size > mp_buf_size);

	/* Create a tuple. */
	struct tuple *tuple = tuple_new(tuple_format_runtime, mp_buf,
					mp_buf + mp_size);
	fail_if(tuple == NULL);

	region_truncate(region, region_svp);
	return tuple;
}

/** Creates a tuple from a MsgPack format (see mp_format). */
static struct tuple *
test_tuple_new(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	struct tuple *tuple = test_tuple_new_va(format, ap);
	va_end(ap);
	return tuple;
}

static struct key_def *
test_key_def_new_va(const char *format, va_list ap, bool for_func_index)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	/* Format the MsgPack key definition. */
	const size_t mp_buf_size = 1024;
	char *mp_buf = region_alloc(region, mp_buf_size);
	fail_if(mp_buf == NULL);
	fail_if(mp_vformat(mp_buf, mp_buf_size, format, ap) > mp_buf_size);

	/* Decode the key parts. */
	const char *parts = mp_buf;
	uint32_t part_count = mp_decode_array(&parts);
	struct key_part_def *part_def = region_alloc(
		region, sizeof(*part_def) * part_count);
	fail_if(part_def == NULL);
	fail_if(key_def_decode_parts(part_def, part_count, &parts,
				     /*fields=*/NULL, /*field_count=*/0,
				     region) != 0);

	/* Create a key def. */
	struct key_def *def = key_def_new(part_def, part_count, for_func_index);
	fail_if(def == NULL);
	key_def_update_optionality(def, 0);

	region_truncate(region, region_svp);
	return def;
}

/** Creates a key_def from a MsgPack format (see mp_format). */
static struct key_def *
test_key_def_new(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	struct key_def *def = test_key_def_new_va(format, ap,
						  /*for_func_index=*/false);
	va_end(ap);
	return def;
}

/** Creates a functional index key_def from a MsgPack format (see mp_format). */
static struct key_def *
test_key_def_new_func(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	struct key_def *def = test_key_def_new_va(format, ap,
						  /*for_func_index=*/true);
	va_end(ap);
	return def;
}

/**
 * Checks that tuple_compare_with_key with cmp_def of functional index
 * returns the same result as comparison of concatenated func and primary keys.
 */
static void
test_check_tuple_compare_with_key_func(
		struct key_def *cmp_def, struct tuple *tuple,
		struct tuple *func_key, struct key_def *model_def,
		struct tuple *model, const char *key)
{
	fail_unless(cmp_def->for_func_index);
	fail_if(model_def->for_func_index);
	const char *key_parts = key;
	uint32_t part_count = mp_decode_array(&key_parts);
	int a = tuple_compare_with_key(tuple, (hint_t)func_key, key_parts,
				       part_count, HINT_NONE, cmp_def);
	int b = tuple_compare_with_key(model, HINT_NONE, key_parts,
				       part_count, HINT_NONE, model_def);
	a = a > 0 ? 1 : a < 0 ? -1 : 0;
	b = b > 0 ? 1 : b < 0 ? -1 : 0;
	is(a, b, "tuple_compare_with_key_func(%s/%s, %s) = %d, expected %d",
	   tuple_str(tuple), tuple_str(func_key), mp_str(key), a, b);
}

static void
test_func_compare_with_key(void)
{
	plan(14);
	header();

	struct key_def *def = test_key_def_new_func(
		"[{%s%u%s%s}{%s%u%s%s}]",
		"field", 0, "type", "unsigned",
		"field", 1, "type", "string");
	/* Skip first field to check if func comparator can handle this. */
	struct key_def *pk_def = test_key_def_new(
		"[{%s%u%s%s}{%s%u%s%s}]",
		"field", 1, "type", "unsigned",
		"field", 2, "type", "string");
	struct key_def *cmp_def = key_def_merge(def, pk_def);
	/*
	 * Model def is a copy of cmp_def, but not for_func_index, and hence
	 * it has general implementation of tuple_compare_with_key method.
	 */
	struct key_def *model_def = test_key_def_new(
		"[{%s%u%s%s}{%s%u%s%s}{%s%u%s%s}{%s%u%s%s}]",
		"field", 0, "type", "unsigned",
		"field", 1, "type", "string",
		"field", 3, "type", "unsigned",
		"field", 4, "type", "string");
	struct tuple *func_key = test_tuple_new("[%u%s]", 20, "foo");
	struct tuple *tuple = test_tuple_new("[%u%u%s]", 200, 10, "cpp");
	/*
	 * Model tuple is concatenated func_key and tuple's primary key.
	 * Note that the 3rd field does not take part in comparison, so it
	 * is intentionally different from the first field of tuple, which is
	 * not compared too.
	 */
	struct tuple *model =
		test_tuple_new("[%u%s%u%u%s]", 20, "foo", 100, 10, "cpp");
	char *keys[] = {
		test_key_new("[]"),
		test_key_new("[%u]", 10),
		test_key_new("[%u]", 20),
		test_key_new("[%u]", 30),
		test_key_new("[%u%s]", 10, "foo"),
		test_key_new("[%u%s]", 20, "foo"),
		test_key_new("[%u%s]", 20, "bar"),
		test_key_new("[%u%s]", 30, "foo"),
		test_key_new("[%u%s%u]", 20, "foo", 5),
		test_key_new("[%u%s%u]", 20, "foo", 10),
		test_key_new("[%u%s%u]", 20, "foo", 15),
		test_key_new("[%u%s%u%s]", 20, "foo", 10, "bar"),
		test_key_new("[%u%s%u%s]", 20, "foo", 10, "cpp"),
		test_key_new("[%u%s%u%s]", 20, "foo", 10, "foo"),
	};
	for (unsigned int i = 0; i < lengthof(keys); i++) {
		test_check_tuple_compare_with_key_func(
			cmp_def, tuple, func_key, model_def, model, keys[i]);
	}
	for (unsigned int i = 0; i < lengthof(keys); i++)
		free(keys[i]);
	tuple_delete(func_key);
	key_def_delete(def);
	key_def_delete(pk_def);
	key_def_delete(cmp_def);

	footer();
	check_plan();
}

static void
test_check_tuple_extract_key_raw(struct key_def *key_def, struct tuple *tuple,
				 const char *key)
{
	uint32_t tuple_size;
	const char *tuple_data = tuple_data_range(tuple, &tuple_size);
	const char *tuple_key =
		tuple_extract_key_raw(tuple_data, tuple_data + tuple_size,
				      key_def, MULTIKEY_NONE, NULL);
	/*
	 * Set zeroes next to extracted key to check if it has not gone
	 * beyond the bounds of its memory.
	 */
	void *alloc = region_alloc(&fiber()->gc, 10);
	memset(alloc, 0, 10);
	const char *key_a = tuple_key;
	uint32_t part_count_a = mp_decode_array(&key_a);
	const char *key_b = key;
	uint32_t part_count_b = mp_decode_array(&key_b);
	ok(key_compare(key_a, part_count_a, HINT_NONE,
		       key_b, part_count_b, HINT_NONE, key_def) == 0 &&
	   part_count_a == part_count_b,
	   "Extracted key of tuple %s is %s, expected %s",
	   tuple_str(tuple), mp_str(tuple_key), mp_str(key));
}

static void
test_tuple_extract_key_raw_slowpath_nullable(void)
{
	plan(3);
	header();

	/* Create non-sequential key_defs to use slowpath implementation. */
	struct key_def *key_defs[] = {
		test_key_def_new(
			"[{%s%u%s%s}{%s%u%s%s%s%b}{%s%u%s%s%s%b}]",
			"field", 0, "type", "unsigned",
			"field", 2, "type", "unsigned", "is_nullable", 1,
			"field", 5, "type", "unsigned", "is_nullable", 1
		),
		test_key_def_new(
			"[{%s%u%s%s%s%b}{%s%u%s%s%s%b}{%s%u%s%s}]",
			"field", 2, "type", "unsigned", "is_nullable", 1,
			"field", 5, "type", "unsigned", "is_nullable", 1,
			"field", 0, "type", "unsigned"
		),
		test_key_def_new(
			"[{%s%u%s%s%s%b}{%s%u%s%s%s%b}]",
			"field", 1, "type", "unsigned", "is_nullable", 1,
			"field", 2, "type", "unsigned", "is_nullable", 1
		),
	};
	struct tuple *tuple = test_tuple_new("[%u]", 10);
	fail_if(tuple == NULL);
	size_t region_svp = region_used(&fiber()->gc);
	char *keys[] = {
		test_key_new("[%uNILNIL]", 10),
		test_key_new("[NILNIL%u]", 10),
		test_key_new("[NILNIL]"),
	};
	static_assert(lengthof(keys) == lengthof(key_defs),
		      "One key for one key_def");
	for (size_t i = 0; i < lengthof(keys); ++i)
		test_check_tuple_extract_key_raw(key_defs[i], tuple, keys[i]);

	for (size_t i = 0; i < lengthof(keys); ++i) {
		key_def_delete(key_defs[i]);
		free(keys[i]);
	}
	tuple_delete(tuple);
	region_truncate(&fiber()->gc, region_svp);

	footer();
	check_plan();
}

static void
test_tuple_validate_key_parts_raw(void)
{
	plan(7);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s}{%s%u%s%s%s%b}]",
		"field", 0, "type", "unsigned",
		"field", 2, "type", "unsigned", "is_nullable", 1);
	fail_if(def == NULL);
	struct tuple *invalid_tuples[3] = {
		test_tuple_new("[%s]", "abc"),
		test_tuple_new("[%u%u%s]", 1, 20, "abc"),
		test_tuple_new("[%s%u%u]", "abc", 5, 10),
	};
	struct tuple *valid_tuples[4] = {
		test_tuple_new("[%u]", 10),
		test_tuple_new("[%u%u]", 10, 20),
		test_tuple_new("[%u%u%u]", 1, 5, 10),
		test_tuple_new("[%u%s%u%u]", 1, "dce", 5, 10),
	};
	for (size_t i = 0; i < lengthof(invalid_tuples); ++i)
		fail_if(invalid_tuples[i] == NULL);
	for (size_t i = 0; i < lengthof(valid_tuples); ++i)
		fail_if(valid_tuples[i] == NULL);

	for (size_t i = 0; i < lengthof(invalid_tuples); ++i)
		is(tuple_validate_key_parts_raw(def,
						tuple_data(invalid_tuples[i])),
		   -1, "tuple %zu must be invalid", i);
	for (size_t i = 0; i < lengthof(valid_tuples); ++i)
		is(tuple_validate_key_parts_raw(def,
						tuple_data(valid_tuples[i])),
		   0, "tuple %zu must be valid", i);

	key_def_delete(def);
	for (size_t i = 0; i < lengthof(invalid_tuples); ++i)
		tuple_delete(invalid_tuples[i]);
	for (size_t i = 0; i < lengthof(valid_tuples); ++i)
		tuple_delete(valid_tuples[i]);

	footer();
	check_plan();
}

static int
test_main(void)
{
	plan(3);
	header();

	test_func_compare_with_key();
	test_tuple_extract_key_raw_slowpath_nullable();
	test_tuple_validate_key_parts_raw();

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

	int rc = test_main();

	tuple_free();
	fiber_free();
	memory_free();
	return rc;
}
