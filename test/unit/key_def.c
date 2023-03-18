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
	plan(2);
	header();

	/* Create non-sequential key_defs to use slowpath implementation. */
	struct key_def *def_nullable_end = test_key_def_new(
		"[{%s%u%s%s}{%s%u%s%s%s%b}{%s%u%s%s%s%b}]",
		"field", 0, "type", "unsigned",
		"field", 2, "type", "unsigned", "is_nullable", 1,
		"field", 5, "type", "unsigned", "is_nullable", 1);
	struct key_def *def_nullable_begin = test_key_def_new(
		"[{%s%u%s%s%s%b}{%s%u%s%s%s%b}{%s%u%s%s}]",
		"field", 2, "type", "unsigned", "is_nullable", 1,
		"field", 5, "type", "unsigned", "is_nullable", 1,
		"field", 0, "type", "unsigned");
	fail_if(def_nullable_end == NULL || def_nullable_begin == NULL);
	struct tuple *tuple = test_tuple_new("[%u]", 10);
	fail_if(tuple == NULL);
	char *key_null_end = test_key_new("[%uNILNIL]", 10);
	char *key_null_begin = test_key_new("[NILNIL%u]", 10);
	fail_if(key_null_end == NULL || key_null_begin == NULL);

	size_t region_svp = region_used(&fiber()->gc);
	test_check_tuple_extract_key_raw(def_nullable_end, tuple,
					 key_null_end);
	test_check_tuple_extract_key_raw(def_nullable_begin, tuple,
					 key_null_begin);

	key_def_delete(def_nullable_end);
	key_def_delete(def_nullable_begin);
	tuple_delete(tuple);
	free(key_null_end);
	free(key_null_begin);
	region_truncate(&fiber()->gc, region_svp);

	footer();
	check_plan();
}

static void
test_tuple_and_key_hash_equivalence(void)
{
	plan(3);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s}]",
		"field", 0, "type", "unsigned");
	uint32_t key_val = 777;
	struct tuple *tuple = test_tuple_new("[%u]", key_val);
	struct tuple_multikey tuple_multikey = {
		.tuple = tuple,
		.multikey_idx = (uint32_t)MULTIKEY_NONE,
	};
	char *key = test_key_new("%u", key_val);
	uint32_t tuple_hash = def->tuple_hash(tuple_multikey, def);
	uint32_t key_hash = def->key_hash(key, def);
	ok(tuple_hash == key_hash, "ordinary key definition: "
	   "tuple hash returned %u and key hash returned %u",
	   tuple_hash, key_hash);
	free(key);
	tuple_delete(tuple);
	key_def_delete(def);

	def = test_key_def_new(
		"[{%s%u%s%s%s%s}]",
		"field", 0, "type", "unsigned", "path", "[*]");
	uint32_t key_val0 = 666;
	uint32_t key_val1 = 777;
	tuple = test_tuple_new("[[%u%u]]", key_val0, key_val1);

	tuple_multikey = (struct tuple_multikey){
		.tuple = tuple,
		.multikey_idx = 0,
	};
	key = test_key_new("%u", key_val0);
	tuple_hash = def->tuple_hash(tuple_multikey, def);
	key_hash = def->key_hash(key, def);
	ok(tuple_hash == key_hash, "multikey key definition, first key: "
	   "tuple hash returned %u and key hash returned %u",
	   tuple_hash, key_hash);
	free(key);

	tuple_multikey = (struct tuple_multikey){
		.tuple = tuple,
		.multikey_idx = 1,
	};
	key = test_key_new("%u", key_val1);
	tuple_hash = def->tuple_hash(tuple_multikey, def);
	key_hash = def->key_hash(key, def);
	ok(tuple_hash == key_hash, "multikey key definition, second key: "
	   "tuple hash returned %u and key hash returned %u",
	   tuple_hash, key_hash);
	free(key);

	tuple_delete(tuple);
	key_def_delete(def);

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
	test_tuple_and_key_hash_equivalence();

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
