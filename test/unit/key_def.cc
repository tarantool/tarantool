#include <vector>

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
#include "tt_static.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static char *
test_key_new_va(const char *format, va_list ap)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	/* Format the MsgPack key definition. */
	const size_t mp_buf_size = 1024;
	char *mp_buf = (char *)region_alloc(region, mp_buf_size);
	fail_if(mp_buf == NULL);
	size_t mp_size = mp_vformat(mp_buf, mp_buf_size, format, ap);
	fail_if(mp_size > mp_buf_size);

	/* Create a key. */
	char *key = (char *)xmalloc(mp_size);
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
	char *mp_buf = (char *)region_alloc(region, mp_buf_size);
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
	char *mp_buf = (char *)region_alloc(region, mp_buf_size);
	fail_if(mp_buf == NULL);
	fail_if(mp_vformat(mp_buf, mp_buf_size, format, ap) > mp_buf_size);

	/* Decode the key parts. */
	const char *parts = mp_buf;
	uint32_t part_count = mp_decode_array(&parts);
	size_t stub;
	struct key_part_def *part_def =
		(struct key_part_def *)region_alloc_array(region,
							  struct key_part_def,
							  part_count, &stub);
	fail_if(part_def == NULL);
	fail_if(key_def_decode_parts(part_def, part_count, &parts,
				     /*fields=*/NULL, /*field_count=*/0,
				     region) != 0);

	/* Create a key def. */
	struct key_def *def = key_def_new(part_def, part_count, for_func_index ?
					  KEY_DEF_FOR_FUNC_INDEX : 0);
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
 * Checks that tuple_compare() -> func_index_compare() return value equals
 * `expected`.
 */
static void
test_check_tuple_compare_func(struct key_def *cmp_def,
			      struct tuple *tuple_a, struct tuple *func_key_a,
			      struct tuple *tuple_b, struct tuple *func_key_b,
			      int expected)
{
	int r = tuple_compare(tuple_a, (hint_t)func_key_a,
			      tuple_b, (hint_t)func_key_b, cmp_def);
	r = r > 0 ? 1 : r < 0 ? -1 : 0;
	is(r, expected, "func_index_compare(%s/%s, %s/%s) = %d, expected %d",
	   tuple_str(tuple_a), tuple_str(func_key_a),
	   tuple_str(tuple_b), tuple_str(func_key_b), r, expected);
}

static void
test_func_compare(void)
{
	plan(6);
	header();

	struct key_def *func_def = test_key_def_new_func(
		"[{%s%u%s%s%s%b}{%s%u%s%s%s%b}]",
		"field", 0, "type", "string", "is_nullable", 1,
		"field", 1, "type", "string", "is_nullable", 1);

	struct key_def *pk_def = test_key_def_new(
		"[{%s%u%s%s}]",
		"field", 1, "type", "unsigned");

	struct key_def *cmp_def = key_def_merge(func_def, pk_def);
	/* Just like when `opts->is_unique == true`, see index_def_new(). */
	cmp_def->unique_part_count = func_def->part_count;

	struct testcase {
		int expected_result;
		struct tuple *tuple_a;
		struct tuple *tuple_b;
		struct tuple *func_key_a;
		struct tuple *func_key_b;
	};

	struct testcase testcases[] = {
		{
			-1, /* func_key_a < func_key_b */
			test_tuple_new("[%s%u%s]", "--", 0, "--"),
			test_tuple_new("[%s%u%s]", "--", 0, "--"),
			test_tuple_new("[%sNIL]", "aa"),
			test_tuple_new("[%s%s]", "aa", "bb"),
		}, {
			1, /* func_key_a > func_key_b */
			test_tuple_new("[%s%u%s]", "--", 0, "--"),
			test_tuple_new("[%s%u%s]", "--", 0, "--"),
			test_tuple_new("[%s%s]", "aa", "bb"),
			test_tuple_new("[%sNIL]", "aa"),
		}, {
			0, /* func_key_a == func_key_b, pk not compared */
			test_tuple_new("[%s%u%s]", "--", 10, "--"),
			test_tuple_new("[%s%u%s]", "--", 20, "--"),
			test_tuple_new("[%s%s]", "aa", "bb"),
			test_tuple_new("[%s%s]", "aa", "bb"),
		}, {
			-1, /* func_key_a == func_key_b, pk_a < pk_b */
			test_tuple_new("[%s%u%s]", "--", 30, "--"),
			test_tuple_new("[%s%u%s]", "--", 40, "--"),
			test_tuple_new("[%sNIL]", "aa"),
			test_tuple_new("[%sNIL]", "aa"),
		}, {
			1, /* func_key_a == func_key_b, pk_a > pk_b */
			test_tuple_new("[%s%u%s]", "--", 60, "--"),
			test_tuple_new("[%s%u%s]", "--", 50, "--"),
			test_tuple_new("[%sNIL]", "aa"),
			test_tuple_new("[%sNIL]", "aa"),
		}, {
			0, /* func_key_a == func_key_b, pk_a == pk_b */
			test_tuple_new("[%s%u%s]", "--", 70, "--"),
			test_tuple_new("[%s%u%s]", "--", 70, "--"),
			test_tuple_new("[%sNIL]", "aa"),
			test_tuple_new("[%sNIL]", "aa"),
		}
	};

	for (size_t i = 0; i < lengthof(testcases); i++) {
		struct testcase *t = &testcases[i];
		test_check_tuple_compare_func(cmp_def,
					      t->tuple_a, t->func_key_a,
					      t->tuple_b, t->func_key_b,
					      t->expected_result);
	}

	for (size_t i = 0; i < lengthof(testcases); i++) {
		struct testcase *t = &testcases[i];
		tuple_delete(t->tuple_a);
		tuple_delete(t->tuple_b);
		tuple_delete(t->func_key_a);
		tuple_delete(t->func_key_b);
	}
	key_def_delete(func_def);
	key_def_delete(pk_def);
	key_def_delete(cmp_def);

	footer();
	check_plan();
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
	tuple_delete(tuple);
	tuple_delete(model);
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

static uint32_t
key_def_field_part(const struct key_def *key_def, uint32_t fieldno)
{
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		if (key_def->parts[part_id].fieldno == fieldno)
			return part_id;
	}
	return UINT32_MAX;
}

static bool
key_def_field_is_indexed(const struct key_def *key_def, uint32_t fieldno)
{
	return key_def_field_part(key_def, fieldno) != UINT32_MAX;
}

static bool
key_def_field_is_unique_indexed(const struct key_def *key_def, uint32_t fieldno)
{
	uint32_t part_id = key_def_field_part(key_def, fieldno);
	return part_id < key_def->unique_part_count;
}

static bool
key_def_field_is_nullable(const struct key_def *key_def, uint32_t fieldno)
{
	uint32_t part_id = key_def_field_part(key_def, fieldno);
	bool is_indexed = part_id != UINT32_MAX;
	if (is_indexed && key_part_is_nullable(&key_def->parts[part_id])) {
		/* If the field is nullable then it's a secondary key part. */
		fail_unless(part_id < key_def->unique_part_count);
		return true;
	}
	return false;
}

/*
 * Generates test cases according to the given \p key_def. The tests mostly
 * cover the unique-parts-only comparisons, because some secondary+primary
 * key comparisons give inconsistent results in different comparators under
 * specific conditions (see FIXMEs in the file). These cases are covered by
 * specialized comparator tests.
 *
 * \pre first unique parts of the \p key_def index sequential fields, example:
 *      {{1, 'string'}, {2, 'string'}} or {{2, 'string'}, {3, 'string'}}.
 */
static void
test_generate_common_cases(std::vector<struct tuple *> &tuples_eq,
			   std::vector<struct tuple *> &tuples_gt,
			   const struct key_def *kd)
{
	bool field_2_is_nullable = key_def_field_is_nullable(kd, 2);
	bool field_3_is_nullable = key_def_field_is_nullable(kd, 3);

	bool last_2_are_nullable = field_2_is_nullable && field_3_is_nullable;

	bool field_0_is_indexed = key_def_field_is_indexed(kd, 0);

	bool field_0_is_unique_indexed = key_def_field_is_unique_indexed(kd, 0);
	bool field_1_is_unique_indexed = key_def_field_is_unique_indexed(kd, 1);
	bool field_2_is_unique_indexed = key_def_field_is_unique_indexed(kd, 2);
	bool field_3_is_unique_indexed = key_def_field_is_unique_indexed(kd, 3);

	/* EQ - regular cases. */
	tuples_eq.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
	tuples_eq.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));

	if (!field_0_is_indexed) {
		tuples_eq.push_back(test_tuple_new("[%u%u%u%u]", 1, 0, 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
	}

	/* EQ - NILs and unexisting fields. */
	if (field_3_is_nullable) {
		tuples_eq.push_back(test_tuple_new("[%u%u%uNIL]", 0, 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%u%uNIL]", 0, 0, 0));

		tuples_eq.push_back(test_tuple_new("[%u%u%u]", 0, 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%u%uNIL]", 0, 0, 0));

		tuples_eq.push_back(test_tuple_new("[%u%u%u]", 0, 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%u%u]", 0, 0, 0));
	}

	if (field_2_is_nullable) {
		tuples_eq.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));
	}

	if (last_2_are_nullable) {
		tuples_eq.push_back(test_tuple_new("[%u%uNILNIL]", 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%uNILNIL]", 0, 0));

		tuples_eq.push_back(test_tuple_new("[%u%uNIL]", 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%uNILNIL]", 0, 0));

		tuples_eq.push_back(test_tuple_new("[%u%u]", 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%uNILNIL]", 0, 0));

		tuples_eq.push_back(test_tuple_new("[%u%uNIL]", 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%uNIL]", 0, 0));

		tuples_eq.push_back(test_tuple_new("[%u%u]", 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%uNIL]", 0, 0));

		tuples_eq.push_back(test_tuple_new("[%u%u]", 0, 0));
		tuples_eq.push_back(test_tuple_new("[%u%u]", 0, 0));
	}

	/* GT - regular cases. */
	if (field_0_is_unique_indexed) {
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 1, 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
	}

	if (!field_0_is_unique_indexed && field_1_is_unique_indexed) {
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 1, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 1, 0, 0, 0));
	}

	if (field_1_is_unique_indexed) {
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 1, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 1, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 1, 1));

		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 1, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 1));
	}

	if (field_2_is_unique_indexed) {
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 1, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 1));
	}

	if (field_3_is_unique_indexed) {
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 1));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
	}

	/* GT - NILs and unexisting fields. */
	if (field_2_is_nullable) {
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 1));

		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 1));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));
	}

	if (field_1_is_unique_indexed && field_2_is_nullable) {
		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 1, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 1, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 1, 1));
	}

	if (field_3_is_nullable) {
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%uNIL]", 0, 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u]", 0, 0, 0));
	}

	if (last_2_are_nullable) {
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNILNIL]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNILNIL]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%u]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%uNIL]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%uNIL]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNILNIL]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%u]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNILNIL]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%uNIL]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%u]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%uNIL]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%u]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u]", 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%uNIL]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));

		tuples_gt.push_back(test_tuple_new("[%u%u%u]", 0, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));
	}

	if (field_1_is_unique_indexed && last_2_are_nullable) {
		tuples_gt.push_back(test_tuple_new("[%u%uNILNIL]", 0, 1));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 1));

		tuples_gt.push_back(test_tuple_new("[%u%uNILNIL]", 0, 1));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 1, 1));

		tuples_gt.push_back(test_tuple_new("[%u%uNIL]", 0, 1));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 1, 1));

		tuples_gt.push_back(test_tuple_new("[%u%u]", 0, 1));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 1, 1));

		tuples_gt.push_back(test_tuple_new("[%u%uNILNIL]", 0, 1));
		tuples_gt.push_back(test_tuple_new("[%u%u%uNIL]", 0, 0, 1));

		tuples_gt.push_back(test_tuple_new("[%u%u%uNIL]", 0, 1, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 1, 1));

		tuples_gt.push_back(test_tuple_new("[%u%u%u]", 0, 1, 0));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 1, 1));
	}
}

static void
test_check_cases(std::vector<struct tuple *> tuples_eq,
		 std::vector<struct tuple *> tuples_gt,
		 struct key_def *key_def,
		 const char *funcname,
		 void (*test_check_func)(struct tuple *tuple_a,
					 struct tuple *tuple_b,
					 struct key_def *key_def,
					 int expected,
					 const char *funcname))
{
	bool ascending_key = !key_def_has_desc_parts(key_def);
	int mul = ascending_key ? 1 : -1;
	enum sort_order expect_sort_order = ascending_key ?
					    SORT_ORDER_ASC : SORT_ORDER_DESC;

	/* All parts are to be either ascending or descending. */
	for (uint32_t i = 0; i < key_def->part_count; i++)
		fail_unless(key_def->parts[i].sort_order == expect_sort_order);

	fail_unless(tuples_eq.size() % 2 == 0);
	for (size_t i = 0; i < tuples_eq.size(); i += 2) {
		struct tuple *a = tuples_eq[i];
		struct tuple *b = tuples_eq[i + 1];
		test_check_func(a, b, key_def, 0, funcname);
		test_check_func(b, a, key_def, 0, funcname);
	}

	fail_unless(tuples_gt.size() % 2 == 0);
	for (size_t i = 0; i < tuples_gt.size(); i += 2) {
		struct tuple *a = tuples_gt[i];
		struct tuple *b = tuples_gt[i + 1];
		test_check_func(a, b, key_def, 1 * mul, funcname);
		test_check_func(b, a, key_def, -1 * mul, funcname);
	}
}

static void
test_delete_cases(std::vector<struct tuple *> tuples_eq,
		  std::vector<struct tuple *> tuples_gt)
{
	for (size_t i = 0; i < tuples_eq.size(); i++) {
		tuple_delete(tuples_eq[i]);
	}

	for (size_t i = 0; i < tuples_gt.size(); i++) {
		tuple_delete(tuples_gt[i]);
	}
}

static void
test_check_key_compare(struct tuple *tuple_a, struct tuple *tuple_b,
		       struct key_def *key_def, int expected,
		       const char *funcname)
{
	size_t region_svp = region_used(&fiber()->gc);
	const char *key_a = tuple_extract_key(tuple_a, key_def,
					      MULTIKEY_NONE, NULL);
	const char *key_b = tuple_extract_key(tuple_b, key_def,
					      MULTIKEY_NONE, NULL);
	const char *key_a_full = key_a;
	const char *key_b_full = key_b;
	size_t key_a_len = mp_decode_array(&key_a);
	size_t key_b_len = mp_decode_array(&key_b);
	size_t key_part_count = key_def->part_count;
	fail_unless(key_a_len == key_b_len);
	fail_unless(key_a_len == key_part_count);
	int rc = key_compare(key_a, key_part_count, HINT_NONE,
			     key_b, key_part_count, HINT_NONE,
			     key_def);
	hint_t key_a_hint = key_hint(key_a, key_part_count, key_def);
	hint_t key_b_hint = key_hint(key_b, key_part_count, key_def);
	int rc_hint = key_compare(key_a, key_part_count, key_a_hint,
				  key_b, key_part_count, key_b_hint,
				  key_def);
	ok(rc == expected, "%s(%s, %s) = %d, expected %d.", funcname,
	   mp_str(key_a_full), mp_str(key_b_full), rc, expected);
	fail_unless(rc == rc_hint); /* The fail cond is printed above. */
	region_truncate(&fiber()->gc, region_svp);
}

static void
test_key_compare_singlepart(bool ascending_key, bool is_nullable)
{
	size_t p = 4 + (is_nullable ? 4 : 0);
	plan(p);
	header();

	const char *sort_order = ascending_key ? "asc" : "desc";

	/* Type is number to prevent using precompiled comparators. */
	struct key_def *key_def = test_key_def_new(
		"[{%s%u%s%s%s%b%s%s}]",
		"field", 0, "type", "number", "is_nullable", is_nullable,
			    "sort_order", sort_order);

	fail_unless(key_def->is_nullable == is_nullable);

	const char *funcname = (const char *)xstrdup(tt_sprintf(
		"key_compare<%s, key_def: singlepart, %s>", is_nullable ? "true"
		: "false", sort_order));

	std::vector<struct tuple *> tuples_eq = {
		/* Regular case. */
		test_tuple_new("[%u]", 0),
		test_tuple_new("[%u]", 0),
	};

	if (is_nullable) {
		/* NILs. */
		tuples_eq.push_back(test_tuple_new("[NIL]"));
		tuples_eq.push_back(test_tuple_new("[NIL]"));
	}

	std::vector<struct tuple *> tuples_gt = {
		/* regular cases. */
		test_tuple_new("[%u]", 1),
		test_tuple_new("[%u]", 0),
	};

	if (is_nullable) {
		/* NILs. */
		tuples_gt.push_back(test_tuple_new("[%u]", 0));
		tuples_gt.push_back(test_tuple_new("[NIL]"));
	}

	test_check_cases(tuples_eq, tuples_gt, key_def, funcname,
			 test_check_key_compare);

	test_delete_cases(tuples_eq, tuples_gt);
	key_def_delete(key_def);
	free((void *)funcname);

	footer();
	check_plan();
}

static void
test_key_compare(bool ascending_key, bool is_nullable)
{
	size_t p = 14 + (is_nullable ? 80 : 0);
	plan(p);
	header();

	const char *sort_order = ascending_key ? "asc" : "desc";

	struct key_def *key_def = test_key_def_new(
		"[{%s%u%s%s%s%s}{%s%u%s%s%s%s}{%s%u%s%s%s%b%s%s}"
		"{%s%u%s%s%s%b%s%s}]",
		"field", 0, "type", "number", "sort_order", sort_order,
		"field", 1, "type", "number", "sort_order", sort_order,
		"field", 2, "type", "number", "is_nullable", is_nullable,
			    "sort_order", sort_order,
		"field", 3, "type", "number", "is_nullable", is_nullable,
			    "sort_order", sort_order);

	fail_unless(key_def->is_nullable == is_nullable);

	const char *funcname = (const char *)xstrdup(tt_sprintf(
		"key_compare<%s, key_def: %s>", is_nullable ? "true" : "false",
		sort_order));

	std::vector<struct tuple *> tuples_eq;
	std::vector<struct tuple *> tuples_gt;

	test_generate_common_cases(tuples_eq, tuples_gt, key_def);

	test_check_cases(tuples_eq, tuples_gt, key_def, funcname,
			 test_check_key_compare);

	test_delete_cases(tuples_eq, tuples_gt);
	key_def_delete(key_def);
	free((void *)funcname);

	footer();
	check_plan();
}

static void
test_check_tuple_compare_with_key(struct tuple *tuple_a, struct tuple *tuple_b,
				  struct key_def *key_def, int expected,
				  const char *funcname)
{
	size_t region_svp = region_used(&fiber()->gc);
	const char *key = tuple_extract_key(tuple_b, key_def,
					    MULTIKEY_NONE, NULL);
	mp_decode_array(&key);
	int rc = tuple_compare_with_key(tuple_a, HINT_NONE, key,
					key_def->part_count,
					HINT_NONE, key_def);
	hint_t tuple_a_hint = tuple_hint(tuple_a, key_def);
	hint_t tuple_b_key_hint = key_hint(key, key_def->part_count, key_def);
	int rc_hint = tuple_compare_with_key(tuple_a, tuple_a_hint, key,
					     key_def->part_count,
					     tuple_b_key_hint, key_def);
	ok(rc == expected, "%s(%s, %s) = %d, expected %d.", funcname,
	   tuple_str(tuple_a), tuple_str(tuple_b), rc, expected);
	fail_unless(rc == rc_hint); /* The fail cond is printed above. */
	region_truncate(&fiber()->gc, region_svp);
}

static void
test_tuple_compare_with_key_slowpath_singlepart(
	bool ascending_key,
	bool is_nullable_and_has_optional_parts)
{
	size_t p = 8 + (is_nullable_and_has_optional_parts ? 10 : 0);
	plan(p);
	header();

	const char *sort_order = ascending_key ? "asc" : "desc";

	/* Type is number to prevent using precompiled comparators. */
	struct key_def *key_def = test_key_def_new(
		"[{%s%u%s%s%s%b%s%s}]", "field", 1, "type", "number",
		"is_nullable", is_nullable_and_has_optional_parts,
		"sort_order", sort_order);

	/* Update has_optional_parts if the last parts can't be nil. */
	size_t min_field_count = tuple_format_min_field_count(&key_def, 1,
							      NULL, 0);
	key_def_update_optionality(key_def, min_field_count);

	fail_unless(key_def->is_nullable == is_nullable_and_has_optional_parts);
	fail_unless(key_def->has_optional_parts == key_def->is_nullable);

	const char *funcname = (const char *)xstrdup(tt_sprintf(
		"tuple_compare_with_key_slowpath<%s, key_def: singlepart, %s>",
		is_nullable_and_has_optional_parts ?
		"true, true" : "false, false", sort_order));

	std::vector<struct tuple *> tuples_eq = {
		/* Regular case. */
		test_tuple_new("[%u%u]", 0, 0),
		test_tuple_new("[%u%u]", 0, 0),

		/* The first field is not indexed. */
		test_tuple_new("[%u%u]", 1, 0),
		test_tuple_new("[%u%u]", 0, 0),
	};

	if (is_nullable_and_has_optional_parts) {
		/* NILs and unexisting parts. */
		tuples_eq.push_back(test_tuple_new("[%uNIL]", 0));
		tuples_eq.push_back(test_tuple_new("[%uNIL]", 0));

		tuples_eq.push_back(test_tuple_new("[%u]", 0));
		tuples_eq.push_back(test_tuple_new("[%uNIL]", 0));

		tuples_eq.push_back(test_tuple_new("[%u]", 0));
		tuples_eq.push_back(test_tuple_new("[%u]", 0));
	}

	std::vector<struct tuple *> tuples_gt = {
		/* Regular cases. */
		test_tuple_new("[%u%u]", 0, 1),
		test_tuple_new("[%u%u]", 0, 0),

		/* The first field is not indexed. */
		test_tuple_new("[%u%u]", 0, 1),
		test_tuple_new("[%u%u]", 1, 0),
	};

	if (is_nullable_and_has_optional_parts) {
		/* NILs and unexisting parts. */
		tuples_gt.push_back(test_tuple_new("[%u%u]", 0, 0));
		tuples_gt.push_back(test_tuple_new("[%uNIL]", 0));

		tuples_gt.push_back(test_tuple_new("[%u%u]", 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u]", 0));
	}

	test_check_cases(tuples_eq, tuples_gt, key_def, funcname,
			 test_check_tuple_compare_with_key);

	test_delete_cases(tuples_eq, tuples_gt);
	key_def_delete(key_def);
	free((void *)funcname);

	footer();
	check_plan();
}

static void
test_tuple_compare_with_key_slowpath(bool ascending_key, bool is_nullable,
				     bool has_optional_parts)
{
	size_t p = 16 + (is_nullable ? 12 : 0) + (has_optional_parts ? 68 : 0);
	plan(p);
	header();

	const char *sort_order = ascending_key ? "asc" : "desc";

	/* has_optional_parts is only valid if is_nullable. */
	fail_unless(!has_optional_parts || is_nullable);

	/* Type is number to prevent using precompiled comparators. */
	const bool last_is_nullable = has_optional_parts;
	struct key_def *key_def = test_key_def_new(
		"[{%s%u%s%s%s%s}{%s%u%s%s%s%b%s%s}{%s%u%s%s%s%b%s%s}]",
		"field", 1, "type", "number", "sort_order", sort_order,
		"field", 2, "type", "number", "is_nullable", is_nullable,
			    "sort_order", sort_order,
		"field", 3, "type", "number", "is_nullable", last_is_nullable,
			    "sort_order", sort_order);

	/* Update has_optional_parts if the last parts can't be nil. */
	size_t min_field_count = tuple_format_min_field_count(&key_def, 1,
							      NULL, 0);
	key_def_update_optionality(key_def, min_field_count);

	fail_unless(key_def->is_nullable == is_nullable);
	fail_unless(key_def->has_optional_parts == has_optional_parts);

	const char *funcname = (const char *)xstrdup(tt_sprintf(
		"tuple_compare_with_key_slowpath<%s, %s, key_def: %s>",
		is_nullable ? "true" : "false", has_optional_parts ? "true" :
		"false", sort_order));

	std::vector<struct tuple *> tuples_eq;
	std::vector<struct tuple *> tuples_gt;

	test_generate_common_cases(tuples_eq, tuples_gt, key_def);

	test_check_cases(tuples_eq, tuples_gt, key_def, funcname,
			 test_check_tuple_compare_with_key);

	test_delete_cases(tuples_eq, tuples_gt);
	key_def_delete(key_def);
	free((void *)funcname);

	footer();
	check_plan();
}

static void
test_check_tuple_compare(struct tuple *tuple_a, struct tuple *tuple_b,
			 struct key_def *cmp_def, int expected,
			 const char *funcname)
{
	int rc = tuple_compare(tuple_a, HINT_NONE,
			       tuple_b, HINT_NONE,
			       cmp_def);
	int rc_hint = tuple_compare(tuple_a, tuple_hint(tuple_a, cmp_def),
				    tuple_b, tuple_hint(tuple_b, cmp_def),
				    cmp_def);
	ok(rc == expected, "%s(%s, %s) = %d, expected %d.", funcname,
	   tuple_str(tuple_a), tuple_str(tuple_b), rc, expected);
	fail_unless(rc == rc_hint); /* The fail cond is printed above. */
}

static void
test_tuple_compare_slowpath(bool ascending_key, bool is_nullable,
			    bool has_optional_parts, bool is_unique)
{
	size_t p = 14 + (is_nullable ? 14 : 0) + (has_optional_parts ? 68 : 0) +
		   (is_unique ? 2 : 0);
	plan(p);
	header();

	const char *sort_order = ascending_key ? "asc" : "desc";

	/* has_optional_parts is only valid if is_nullable. */
	fail_unless(!has_optional_parts || is_nullable);

	struct key_def *pk_def = test_key_def_new(
		"[{%s%u%s%s%s%s}]",
		"field", 0, "type", "unsigned", "sort_order", sort_order);

	/* Type is number to prevent using precompiled comparators. */
	const bool last_is_nullable = has_optional_parts;
	struct key_def *key_def = test_key_def_new(
		"[{%s%u%s%s%s%s}{%s%u%s%s%s%b%s%s}{%s%u%s%s%s%b%s%s}]",
		"field", 1, "type", "number", "sort_order", sort_order,
		"field", 2, "type", "number", "is_nullable", is_nullable,
			    "sort_order", sort_order,
		"field", 3, "type", "number", "is_nullable", last_is_nullable,
			    "sort_order", sort_order);

	struct key_def *cmp_def = key_def_merge(key_def, pk_def);
	fail_unless(cmp_def->unique_part_count > key_def->part_count);

	if (is_unique) {
		/*
		 * It's assumed that PK and SK index different parts. So we
		 * cover cmp_def->unique_part_count < cmp_def->part_count
		 * branch of the slowpath comparator (its last loop).
		 */
		cmp_def->unique_part_count = key_def->part_count;
	}

	/* Update has_optional_parts if the last parts can't be nil. */
	struct key_def *keys[] = {pk_def, key_def};
	size_t min_field_count = tuple_format_min_field_count(
		keys, lengthof(keys), NULL, 0);
	key_def_update_optionality(cmp_def, min_field_count);

	fail_unless(cmp_def->is_nullable == is_nullable);
	fail_unless(cmp_def->has_optional_parts == has_optional_parts);

	const char *funcname = (const char *)xstrdup(tt_sprintf(
		"tuple_compare_slowpath<%s, %s, key_def: %sunique, %s>",
		is_nullable ? "true" : "false",
		has_optional_parts ? "true" : "false",
		is_unique ? "" : "not ", sort_order));

	std::vector<struct tuple *> tuples_eq;
	std::vector<struct tuple *> tuples_gt;

	test_generate_common_cases(tuples_eq, tuples_gt, cmp_def);

	if (is_unique) {
		if (is_nullable) {
			/* Tuples are equal by SK, so PK is ignored. */
			tuples_eq.push_back(
				test_tuple_new("[%u%u%u%u]", 1, 0, 0, 0));
			tuples_eq.push_back(
				test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
		} else {
			/*
			 * FIXME: tuple_compare_slowpath has a logic I don't
			 * quite understand. If the tuples are equal by SK and
			 * we have no nils met, we should skip the PK comparison
			 * and conclude the tuples are equal, but the comparator
			 * has this `!is_nullable` condition making it compare
			 * all parts of the key (including PK).
			 *
			 * Please remove this `if` statement and only keep its
			 * `then` clause if the behaviour is fixed.
			 */
			tuples_gt.push_back(
				test_tuple_new("[%u%u%u%u]", 1, 0, 0, 0));
			tuples_gt.push_back(
				test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
		}
	}

	if (is_nullable) {
		/*
		 * Even if the SK is unique and the tuples are equal,
		 * they contain nils, so PK is compared too.
		 */
		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 1, 0, 0));
		tuples_gt.push_back(test_tuple_new("[%u%uNIL%u]", 0, 0, 0));
	}

	test_check_cases(tuples_eq, tuples_gt, cmp_def, funcname,
			 test_check_tuple_compare);

	test_delete_cases(tuples_eq, tuples_gt);
	key_def_delete(key_def);
	key_def_delete(pk_def);
	key_def_delete(cmp_def);
	free((void *)funcname);

	footer();
	check_plan();
}

static void
test_tuple_compare_with_key_sequential(bool ascending_key, bool is_nullable,
				       bool has_optional_parts)
{
	size_t p = 14 + (is_nullable ? 12 : 0) + (has_optional_parts ? 68 : 0);
	plan(p);
	header();

	const char *sort_order = ascending_key ? "asc" : "desc";

	/* has_optional_parts is only valid if is_nullable. */
	fail_unless(!has_optional_parts || is_nullable);

	/* Type is number to prevent using precompiled comparators. */
	const bool last_is_nullable = has_optional_parts;
	struct key_def *key_def = test_key_def_new(
		"[{%s%u%s%s%s%s}{%s%u%s%s%s%s}{%s%u%s%s%s%b%s%s}"
		"{%s%u%s%s%s%b%s%s}]",
		"field", 0, "type", "number", "sort_order", sort_order,
		"field", 1, "type", "number", "sort_order", sort_order,
		"field", 2, "type", "number", "is_nullable", is_nullable,
			    "sort_order", sort_order,
		"field", 3, "type", "number", "is_nullable", last_is_nullable,
			    "sort_order", sort_order);

	/* Update has_optional_parts if the last parts can't be nil. */
	size_t min_field_count = tuple_format_min_field_count(&key_def, 1,
							      NULL, 0);
	key_def_update_optionality(key_def, min_field_count);

	fail_unless(key_def->is_nullable == is_nullable);
	fail_unless(key_def->has_optional_parts == has_optional_parts);

	const char *funcname = (const char *)xstrdup(tt_sprintf(
		"tuple_compare_with_key_sequential<%s, %s, key_def: %s>",
		is_nullable ? "true" : "false", has_optional_parts ? "true" :
		"false", sort_order));

	std::vector<struct tuple *> tuples_eq;
	std::vector<struct tuple *> tuples_gt;

	test_generate_common_cases(tuples_eq, tuples_gt, key_def);

	test_check_cases(tuples_eq, tuples_gt, key_def, funcname,
			 test_check_tuple_compare_with_key);

	test_delete_cases(tuples_eq, tuples_gt);
	key_def_delete(key_def);
	free((void *)funcname);

	footer();
	check_plan();
}

static void
test_tuple_compare_sequential(bool ascending_key, bool is_nullable,
			      bool has_optional_parts)
{
	size_t p = 14 + (is_nullable ? 12 : 0) + (has_optional_parts ? 68 : 0);
	plan(p);
	header();

	const char *sort_order = ascending_key ? "asc" : "desc";

	/* has_optional_parts is only valid if is_nullable. */
	fail_unless(!has_optional_parts || is_nullable);

	/* Type is number to prevent using precompiled comparators. */
	const bool last_is_nullable = has_optional_parts;
	struct key_def *key_def = test_key_def_new(
		"[{%s%u%s%s%s%s}{%s%u%s%s%s%s}{%s%u%s%s%s%b%s%s}"
		"{%s%u%s%s%s%b%s%s}]",
		"field", 0, "type", "number", "sort_order", sort_order,
		"field", 1, "type", "number", "sort_order", sort_order,
		"field", 2, "type", "number", "is_nullable", is_nullable,
			    "sort_order", sort_order,
		"field", 3, "type", "number", "is_nullable", last_is_nullable,
			    "sort_order", sort_order);

	/* Update has_optional_parts if the last parts can't be nil. */
	size_t min_field_count = tuple_format_min_field_count(&key_def, 1,
							      NULL, 0);
	key_def_update_optionality(key_def, min_field_count);

	fail_unless(key_def->is_nullable == is_nullable);
	fail_unless(key_def->has_optional_parts == has_optional_parts);

	const char *funcname = (const char *)xstrdup(tt_sprintf(
		"tuple_compare_sequential<%s, %s, key_def: %s>", is_nullable ?
		"true" : "false", has_optional_parts ? "true" : "false",
		sort_order));

	std::vector<struct tuple *> tuples_eq;
	std::vector<struct tuple *> tuples_gt;

	test_generate_common_cases(tuples_eq, tuples_gt, key_def);

	test_check_cases(tuples_eq, tuples_gt, key_def, funcname,
			 test_check_tuple_compare);

	test_delete_cases(tuples_eq, tuples_gt);
	key_def_delete(key_def);

	footer();
	check_plan();
}

static void
test_tuple_compare_sequential_no_optional_parts_unique(bool ascending_key,
						       bool is_nullable)
{
	plan(is_nullable ? 18 : 14);
	header();

	const char *sort_order = ascending_key ? "asc" : "desc";

	/* The primary key (PK). */
	struct key_def *pk_def = test_key_def_new(
		"[{%s%u%s%s%s%s}]",
		"field", 3, "type", "number", "sort_order", sort_order);

	/* The secondary key (SK). */
	struct key_def *key_def = test_key_def_new(
		"[{%s%u%s%s%s%s}{%s%u%s%s%s%b%s%s}{%s%u%s%s%s%s}]",
		"field", 0, "type", "number", "sort_order", sort_order,
		"field", 1, "type", "number", "is_nullable", is_nullable,
			    "sort_order", sort_order,
		"field", 2, "type", "number", "sort_order", sort_order);

	struct key_def *cmp_def = key_def_merge(key_def, pk_def);
	fail_unless(cmp_def->unique_part_count > key_def->part_count);

	/*
	 * It's assumed that PK and SK index different parts. So we
	 * cover cmp_def->unique_part_count < cmp_def->part_count
	 * branch of the sequential comparator (its last loop).
	 */
	cmp_def->unique_part_count = key_def->part_count;

	/* Update has_optional_parts (the last parts can't be nil). */
	struct key_def *keys[] = {pk_def, key_def};
	size_t min_field_count = tuple_format_min_field_count(
		keys, lengthof(keys), NULL, 0);
	key_def_update_optionality(cmp_def, min_field_count);

	fail_unless(cmp_def->is_nullable == is_nullable);
	fail_unless(!cmp_def->has_optional_parts);

	const char *funcname = (const char *)xstrdup(tt_sprintf(
		"tuple_compare_sequential<%s, false, key_def: unique, %s>",
		is_nullable ? "true" : "false", sort_order));

	std::vector<struct tuple *> tuples_eq;
	std::vector<struct tuple *> tuples_gt;

	test_generate_common_cases(tuples_eq, tuples_gt, cmp_def);

	if (is_nullable) {
		/* NILs (PK is compared even for unique SK). */
		tuples_eq.push_back(test_tuple_new("[%uNIL%u%u]", 0, 0, 0));
		tuples_eq.push_back(test_tuple_new("[%uNIL%u%u]", 0, 0, 0));
	}

	/*
	 * FIXME: We have inconsistent sequential comparator behavior in case of
	 * !is_nullable && !has_optional_parts with unique key. Please remove
	 * the condition and its `else` clause if #8902 is solved.
	 */
	if (is_nullable) {
		/* PK (field 3) does not count, if SK is unique. */
		tuples_eq.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 1));
		tuples_eq.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
	} else {
		/*
		 * If these tests are failed that means the issue that was
		 * mentioned in the comment above the current `if` has been
		 * fixed. If this is the case - please remove this `else`.
		 */
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 1));
		tuples_gt.push_back(test_tuple_new("[%u%u%u%u]", 0, 0, 0, 0));
	}

	if (is_nullable) {
		/* Here PK is compared even for unique SK. */
		tuples_gt.push_back(test_tuple_new("[%uNIL%u%u]", 0, 0, 1));
		tuples_gt.push_back(test_tuple_new("[%uNIL%u%u]", 0, 0, 0));
	}

	test_check_cases(tuples_eq, tuples_gt, cmp_def, funcname,
			 test_check_tuple_compare);

	test_delete_cases(tuples_eq, tuples_gt);
	key_def_delete(key_def);
	key_def_delete(pk_def);
	key_def_delete(cmp_def);
	free((void *)funcname);

	footer();
	check_plan();
}

static int
test_main(void)
{
	plan(50);
	header();

	test_func_compare();
	test_func_compare_with_key();
	test_tuple_extract_key_raw_slowpath_nullable();
	test_tuple_validate_key_parts_raw();
	test_tuple_compare_sequential(true, true, true);
	test_tuple_compare_sequential(true, true, false);
	test_tuple_compare_sequential(true, false, false);
	test_tuple_compare_sequential(false, true, true);
	test_tuple_compare_sequential(false, true, false);
	test_tuple_compare_sequential(false, false, false);
	test_tuple_compare_sequential_no_optional_parts_unique(true, true);
	test_tuple_compare_sequential_no_optional_parts_unique(true, false);
	test_tuple_compare_sequential_no_optional_parts_unique(false, true);
	test_tuple_compare_sequential_no_optional_parts_unique(false, false);
	test_tuple_compare_with_key_sequential(true, true, true);
	test_tuple_compare_with_key_sequential(true, true, false);
	test_tuple_compare_with_key_sequential(true, false, false);
	test_tuple_compare_with_key_sequential(false, true, true);
	test_tuple_compare_with_key_sequential(false, true, false);
	test_tuple_compare_with_key_sequential(false, false, false);
	test_tuple_compare_slowpath(true, true, true, true);
	test_tuple_compare_slowpath(true, true, true, false);
	test_tuple_compare_slowpath(true, true, false, true);
	test_tuple_compare_slowpath(true, true, false, false);
	test_tuple_compare_slowpath(true, false, false, true);
	test_tuple_compare_slowpath(true, false, false, false);
	test_tuple_compare_slowpath(false, true, true, true);
	test_tuple_compare_slowpath(false, true, true, false);
	test_tuple_compare_slowpath(false, true, false, true);
	test_tuple_compare_slowpath(false, true, false, false);
	test_tuple_compare_slowpath(false, false, false, true);
	test_tuple_compare_slowpath(false, false, false, false);
	test_tuple_compare_with_key_slowpath(true, true, true);
	test_tuple_compare_with_key_slowpath(true, true, false);
	test_tuple_compare_with_key_slowpath(true, false, false);
	test_tuple_compare_with_key_slowpath(false, true, true);
	test_tuple_compare_with_key_slowpath(false, true, false);
	test_tuple_compare_with_key_slowpath(false, false, false);
	test_tuple_compare_with_key_slowpath_singlepart(true, true);
	test_tuple_compare_with_key_slowpath_singlepart(true, false);
	test_tuple_compare_with_key_slowpath_singlepart(false, true);
	test_tuple_compare_with_key_slowpath_singlepart(false, false);
	test_key_compare(true, true);
	test_key_compare(true, false);
	test_key_compare(false, true);
	test_key_compare(false, false);
	test_key_compare_singlepart(true, true);
	test_key_compare_singlepart(true, false);
	test_key_compare_singlepart(false, true);
	test_key_compare_singlepart(false, false);

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
