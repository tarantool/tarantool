#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "coll/coll.h"
#include "coll_id.h"
#include "coll_id_def.h"
#include "coll_id_cache.h"
#include "fiber.h"
#include "key_def.h"
#include "key_def_raw.h"
#include "memory.h"
#include "mp_util.h"
#include "msgpuck.h"
#include "small/region.h"
#include "trivia/util.h"
#include "tuple.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#define is_same_sign(a, b, fmt, args...) \
	is((a) < 0 ? -1 : (a) > 0 ? 1 : 0, \
	   (b) < 0 ? -1 : (b) > 0 ? 1 : 0, \
	   fmt, ##args)

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
	struct key_part_def *part_def = region_aligned_alloc(
		region, sizeof(*part_def) * part_count,
		alignof(struct key_part_def));
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
 * Creates a 'raw' key_def from the given one.
 * A 'raw' key_def doesn't use tuple format for tuple comparison.
 */
static struct key_def *
test_key_def_new_raw(const struct key_def *def)
{
	struct key_def *def_raw = key_def_dup(def);
	fail_if(def_raw == NULL);
	key_def_set_func_raw(def_raw);
	return def_raw;
}

/**
 * Checks that tuple_hash returns the same result for def and def_raw.
 */
static void
test_check_tuple_hash(struct key_def *def, struct key_def *def_raw,
		      struct tuple *tuple)
{
	fail_if(def->is_multikey);
	fail_if(def_raw->is_multikey);
	fail_if(def->for_func_index);
	fail_if(def_raw->for_func_index);
	uint32_t a = tuple_hash(tuple, def);
	tuple->format_id = FORMAT_ID_NIL;
	uint32_t b = tuple_hash(tuple, def_raw);
	tuple->format_id = tuple_format_runtime->id;
	is(a, b, "tuple_hash(%s)", tuple_str(tuple));
}

/**
 * Checks that tuple_hint returns the same result for def and def_raw.
 */
static void
test_check_tuple_hint(struct key_def *def, struct key_def *def_raw,
		      struct tuple *tuple)
{
	fail_if(def->is_multikey);
	fail_if(def_raw->is_multikey);
	fail_if(def->for_func_index);
	fail_if(def_raw->for_func_index);
	uint32_t a = tuple_hint(tuple, def);
	uint32_t b = tuple_hint(tuple, def_raw);
	is(a, b, "tuple_hint(%s)", tuple_str(tuple));
}

/**
 * Checks that tuple_compare returns the same result for def and def_raw.
 */
static void
test_check_tuple_compare(struct key_def *def, struct key_def *def_raw,
			 struct tuple *tuple_a, hint_t hint_a,
			 struct tuple *tuple_b, hint_t hint_b)
{
	fail_if(def->is_multikey);
	fail_if(def_raw->is_multikey);
	fail_if(def->for_func_index);
	fail_if(def_raw->for_func_index);
	int a = tuple_compare(tuple_a, hint_a, tuple_b, hint_b, def);
	int b = tuple_compare(tuple_a, hint_a, tuple_b, hint_b, def_raw);
	is_same_sign(a, b, "tuple_compare(%s, %s)",
		     tuple_str(tuple_a), tuple_str(tuple_b));
}

/**
 * Checks that tuple_compare returns the same result for def and def_raw
 * multikey keys.
 */
static void
test_check_tuple_compare_multikey(struct key_def *def, struct key_def *def_raw,
				  struct tuple *tuple_a, int mk_idx_a,
				  struct tuple *tuple_b, int mk_idx_b)
{
	fail_if(!def->is_multikey);
	fail_if(!def_raw->is_multikey);
	fail_if(def->for_func_index);
	fail_if(def_raw->for_func_index);
	int a = tuple_compare(tuple_a, mk_idx_a, tuple_b, mk_idx_b, def);
	int b = tuple_compare(tuple_a, mk_idx_a, tuple_b, mk_idx_b, def_raw);
	is_same_sign(a, b, "tuple_compare(%s (%d), %s (%d))",
		     tuple_str(tuple_a), mk_idx_a,
		     tuple_str(tuple_b), mk_idx_b);
}

/**
 * Checks that tuple_compare returns the same result for def and def_raw
 * functional keys.
 */
static void
test_check_tuple_compare_func(struct key_def *def, struct key_def *def_raw,
			      struct tuple *tuple_a, struct tuple *key_a,
			      struct tuple *tuple_b, struct tuple *key_b)
{
	fail_if(def->is_multikey);
	fail_if(def_raw->is_multikey);
	fail_if(!def->for_func_index);
	fail_if(!def_raw->for_func_index);
	hint_t hint_a = (hint_t)key_a;
	hint_t hint_b = (hint_t)key_b;
	int a = tuple_compare(tuple_a, hint_a, tuple_b, hint_b, def);
	int b = tuple_compare(tuple_a, hint_a, tuple_b, hint_b, def_raw);
	is_same_sign(a, b, "tuple_compare(%s (%s), %s (%s))",
		     tuple_str(tuple_a), tuple_str(key_a),
		     tuple_str(tuple_b), tuple_str(key_b));
}

/**
 * Checks that tuple_compare_with_key returns the same result for def and
 * def_raw.
 */
static void
test_check_tuple_compare_with_key(struct key_def *def, struct key_def *def_raw,
				  struct tuple *tuple, const char *key)
{
	fail_if(def->is_multikey);
	fail_if(def_raw->is_multikey);
	fail_if(def->for_func_index);
	fail_if(def_raw->for_func_index);
	const char *key_parts = key;
	uint32_t part_count = mp_decode_array(&key_parts);
	hint_t h1 = tuple_hint(tuple, def);
	hint_t h2 = key_hint(key_parts, part_count, def);
	int a = tuple_compare_with_key(tuple, h1, key_parts, part_count, h2,
				       def);
	tuple->format_id = FORMAT_ID_NIL;
	int b = tuple_compare_with_key(tuple, h1, key_parts, part_count, h2,
				       def_raw);
	tuple->format_id = tuple_format_runtime->id;
	is_same_sign(a, b, "tuple_compare_with_key(%s, %s)",
		     tuple_str(tuple), mp_str(key));
}

/**
 * Checks that tuple_compare_with_key without hint returns the same result for
 * def and def_raw.
 */
static void
test_check_tuple_compare_with_key_no_hint(
	struct key_def *def, struct key_def *def_raw,
	struct tuple *tuple, const char *key)
{
	fail_if(def->is_multikey);
	fail_if(def_raw->is_multikey);
	fail_if(def->for_func_index);
	fail_if(def_raw->for_func_index);
	const char *key_parts = key;
	uint32_t part_count = mp_decode_array(&key_parts);
	int a = tuple_compare_with_key(tuple, HINT_NONE, key_parts, part_count,
				       HINT_NONE, def);
	tuple->format_id = FORMAT_ID_NIL;
	int b = tuple_compare_with_key(tuple, HINT_NONE, key_parts, part_count,
				       HINT_NONE, def_raw);
	tuple->format_id = tuple_format_runtime->id;
	is_same_sign(a, b, "tuple_compare_with_key_no_hint(%s, %s)",
		     tuple_str(tuple), mp_str(key));
}

/**
 * Checks that tuple_compare_with_key with multikey index returns
 * the same result for def and def_raw.
 */
static void
test_check_tuple_compare_with_key_multikey(
	struct key_def *def, struct key_def *def_raw,
	struct tuple *tuple, int multikey_idx, const char *key)
{
	fail_unless(def->is_multikey);
	fail_unless(def_raw->is_multikey);
	fail_if(def->for_func_index);
	fail_if(def_raw->for_func_index);
	const char *key_parts = key;
	uint32_t part_count = mp_decode_array(&key_parts);
	int a = tuple_compare_with_key(tuple, (hint_t)multikey_idx, key_parts,
				       part_count, HINT_NONE, def);
	tuple->format_id = FORMAT_ID_NIL;
	int b = tuple_compare_with_key(tuple, (hint_t)multikey_idx, key_parts,
				       part_count, HINT_NONE, def_raw);
	tuple->format_id = tuple_format_runtime->id;
	is_same_sign(a, b, "tuple_compare_with_key_multikey(%s, %d, %s)",
		     tuple_str(tuple), multikey_idx, mp_str(key));
}

/**
 * Checks that tuple_compare_with_key with functional index returns
 * the same result for def and def_raw.
 */
static void
test_check_tuple_compare_with_key_func(
	struct key_def *def, struct key_def *def_raw,
	struct tuple *tuple, struct tuple *func_key, const char *key)
{
	fail_unless(def->for_func_index);
	fail_unless(def_raw->for_func_index);
	const char *key_parts = key;
	uint32_t part_count = mp_decode_array(&key_parts);
	int a = tuple_compare_with_key(tuple, (hint_t)func_key, key_parts,
				       part_count, HINT_NONE, def);
	func_key->format_id = FORMAT_ID_NIL;
	int b = tuple_compare_with_key(tuple, (hint_t)func_key, key_parts,
				       part_count, HINT_NONE, def_raw);
	func_key->format_id = tuple_format_runtime->id;
	is_same_sign(a, b, "tuple_compare_with_key_func(%s, %s, %s)",
		     tuple_str(tuple), tuple_str(func_key), mp_str(key));
}

/**
 * Checks that tuple_extract_key returns the same result for def and def_raw.
 */
static void
test_check_tuple_extract_key(
	struct key_def *def, struct key_def *def_raw,
	struct tuple *tuple, int mk_idx)
{
	uint32_t size_a;
	const char *key_a = tuple_extract_key(tuple, def, mk_idx, &size_a);
	const char **key_a_ptr = &key_a;
	uint32_t part_count_a = mp_decode_array(key_a_ptr);
	uint32_t size_b;
	const char *key_b = tuple_extract_key(tuple, def_raw, mk_idx, &size_b);
	const char **key_b_ptr = &key_b;
	uint32_t part_count_b = mp_decode_array(key_b_ptr);
	int cmp = key_compare(key_a, part_count_a, HINT_NONE, key_b,
			      part_count_b, HINT_NONE, def);
	is(cmp, 0, "tuple_extract_key(%s)", tuple_str(tuple));
}

/**
 * Checks that virtual functions that may access tuple format were overridden
 * in 'raw' key_def.
 */
static void
test_check_vtab(struct key_def *def, struct key_def *def_raw)
{
	is(def->key_hint, def_raw->key_hint, "key_hint");
	is(def->tuple_extract_key_raw, def_raw->tuple_extract_key_raw,
	   "tuple_extract_key_raw");
	isnt(def->tuple_hash, def_raw->tuple_hash, "tuple_hash");
	isnt(def->tuple_hint, def_raw->tuple_hint, "tuple_hint");
	isnt(def->tuple_extract_key, def_raw->tuple_extract_key,
	     "tuple_extract_key");
	isnt(def->tuple_compare, def_raw->tuple_compare, "tuple_compare");
	isnt(def->tuple_compare_with_key, def_raw->tuple_compare_with_key,
	     "tuple_compare_with_key");
}

static void
test_plain(void)
{
	plan(46);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s}{%s%u%s%s}{%s%u%s%s}{%s%u%s%s}]",
		"field", 1, "type", "unsigned",
		"field", 2, "type", "string",
		"field", 5, "type", "unsigned",
		"field", 3, "type", "string");
	struct key_def *def_raw = test_key_def_new_raw(def);
	struct tuple *tuple = test_tuple_new("[NIL%u%s%sNIL%u]",
					     20, "foo", "bar", 30);
	char *keys[] = {
		test_key_new("[]"),
		test_key_new("[%u]", 10),
		test_key_new("[%u]", 20),
		test_key_new("[%u]", 30),
		test_key_new("[%u%s]", 10, "foo"),
		test_key_new("[%u%s]", 20, "foo"),
		test_key_new("[%u%s]", 20, "bar"),
		test_key_new("[%u%s]", 30, "foo"),
		test_key_new("[%u%s%u]", 10, "foo", 30),
		test_key_new("[%u%s%u]", 20, "foo", 20),
		test_key_new("[%u%s%u]", 20, "foo", 30),
		test_key_new("[%u%s%u]", 20, "foo", 40),
		test_key_new("[%u%s%u]", 20, "bar", 30),
		test_key_new("[%u%s%u]", 30, "foo", 30),
		test_key_new("[%u%s%u%s]", 10, "foo", 30, "bar"),
		test_key_new("[%u%s%u%s]", 20, "foo", 30, "foo"),
		test_key_new("[%u%s%u%s]", 20, "foo", 30, "bar"),
		test_key_new("[%u%s%u%s]", 30, "foo", 30, "foo"),
	};
	test_check_vtab(def, def_raw);
	test_check_tuple_hash(def, def_raw, tuple);
	test_check_tuple_hint(def, def_raw, tuple);
	test_check_tuple_extract_key(def, def_raw, tuple, MULTIKEY_NONE);
	for (unsigned int i = 0; i < lengthof(keys); i++) {
		test_check_tuple_compare_with_key(
			def, def_raw, tuple, keys[i]);
		test_check_tuple_compare_with_key_no_hint(
			def, def_raw, tuple, keys[i]);
	}
	for (unsigned int i = 0; i < lengthof(keys); i++)
		free(keys[i]);
	tuple_delete(tuple);
	key_def_delete(def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

static void
test_plain_tuple_compare(void)
{
	plan(19);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s}{%s%u%s%s}{%s%u%s%s}{%s%u%s%s}]",
		"field", 1, "type", "unsigned",
		"field", 2, "type", "string",
		"field", 5, "type", "unsigned",
		"field", 3, "type", "string");
	struct key_def *def_raw = test_key_def_new_raw(def);
	struct tuple *tuple = test_tuple_new("[NIL%u%s%sNIL%u]",
					     20, "foo", "bar", 30);
	struct tuple *tuples[] = {
		test_tuple_new("[NIL%u%s%sNIL%u]", 10, "foo", "bar", 30),
		test_tuple_new("[NIL%u%s%sNIL%u]", 20, "foo", "foo", 30),
		test_tuple_new("[NIL%u%s%sNIL%u]", 20, "foo", "bar", 30),
		test_tuple_new("[NIL%u%s%sNIL%u]", 30, "foo", "foo", 30),
	};
	test_check_vtab(def, def_raw);
	for (unsigned int i = 0; i < lengthof(tuples); i++) {
		test_check_tuple_hint(def, def_raw, tuples[i]);

		struct tuple *tuple_a = tuple;
		struct tuple *tuple_b = tuples[i];
		hint_t hint_a = tuple_hint(tuple_a, def);
		hint_t hint_b = tuple_hint(tuple_b, def);
		test_check_tuple_compare(def, def_raw, tuple_a,
					 hint_a, tuple_b, hint_b);
		test_check_tuple_compare(def, def_raw, tuple_a,
					 HINT_NONE, tuple_b, HINT_NONE);
	}
	for (unsigned int i = 0; i < lengthof(tuples); i++)
		tuple_delete(tuples[i]);
	tuple_delete(tuple);
	key_def_delete(def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

static void
test_json(void)
{
	plan(36);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s%s%s}{%s%u%s%s%s%s}{%s%u%s%s%s%s}]",
		"field", 0, "type", "unsigned", "path", "x.a",
		"field", 0, "type", "unsigned", "path", "x.b",
		"field", 2, "type", "string", "path", "y");
	struct key_def *def_raw = test_key_def_new_raw(def);
	struct tuple *tuple = test_tuple_new("[{%s{%s%u%s%u}}NIL{%s%s}]",
					     "x", "a", 20, "b", 30, "y", "foo");
	char *keys[] = {
		test_key_new("[]"),
		test_key_new("[%u]", 10),
		test_key_new("[%u]", 20),
		test_key_new("[%u]", 30),
		test_key_new("[%u%u]", 10, 30),
		test_key_new("[%u%u]", 20, 20),
		test_key_new("[%u%u]", 20, 30),
		test_key_new("[%u%u]", 20, 40),
		test_key_new("[%u%u]", 30, 30),
		test_key_new("[%u%u%s]", 10, 30, "foo"),
		test_key_new("[%u%u%s]", 20, 30, "foo"),
		test_key_new("[%u%u%s]", 20, 30, "bar"),
		test_key_new("[%u%u%s]", 30, 30, "foo"),
	};
	test_check_vtab(def, def_raw);
	test_check_tuple_hash(def, def_raw, tuple);
	test_check_tuple_hint(def, def_raw, tuple);
	test_check_tuple_extract_key(def, def_raw, tuple, MULTIKEY_NONE);
	for (unsigned int i = 0; i < lengthof(keys); i++) {
		test_check_tuple_compare_with_key(
			def, def_raw, tuple, keys[i]);
		test_check_tuple_compare_with_key_no_hint(
			def, def_raw, tuple, keys[i]);
	}
	for (unsigned int i = 0; i < lengthof(keys); i++)
		free(keys[i]);
	tuple_delete(tuple);
	key_def_delete(def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

static void
test_json_tuple_compare(void)
{
	plan(15);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s%s%s}{%s%u%s%s%s%s}{%s%u%s%s%s%s}]",
		"field", 0, "type", "unsigned", "path", "x.a",
		"field", 0, "type", "unsigned", "path", "x.b",
		"field", 2, "type", "string", "path", "y");
	struct key_def *def_raw = test_key_def_new_raw(def);
	const char *fmt = "[{%s{%s%u%s%u}}NIL{%s%s}]";
	struct tuple *tuple =
		test_tuple_new(fmt, "x", "a", 20, "b", 30, "y", "foo");
	struct tuple *tuples[] = {
		test_tuple_new(fmt, "x", "a", 10, "b", 30, "y", "foo"),
		test_tuple_new(fmt, "x", "a", 20, "b", 30, "y", "foo"),
		test_tuple_new(fmt, "x", "a", 20, "b", 30, "y", "bar"),
		test_tuple_new(fmt, "x", "a", 30, "b", 30, "y", "foo"),
	};
	test_check_vtab(def, def_raw);
	for (unsigned int i = 0; i < lengthof(tuples); i++) {
		struct tuple *tuple_a = tuple;
		struct tuple *tuple_b = tuples[i];
		hint_t hint_a = tuple_hint(tuple_a, def);
		hint_t hint_b = tuple_hint(tuple_b, def);
		test_check_tuple_compare(def, def_raw, tuple_a,
					 hint_a, tuple_b, hint_b);
		test_check_tuple_compare(def, def_raw, tuple_a,
					 HINT_NONE, tuple_b, HINT_NONE);
	}
	for (unsigned int i = 0; i < lengthof(tuples); i++)
		tuple_delete(tuples[i]);
	tuple_delete(tuple);
	key_def_delete(def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

static void
test_multikey(void)
{
	plan(35);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s%s%s}{%s%u%s%s}]",
		"field", 2, "type", "unsigned", "path", "x[*]",
		"field", 0, "type", "unsigned");
	struct key_def *def_raw = test_key_def_new_raw(def);
	struct tuple *tuple = test_tuple_new("[%uNIL{%s[%u%u%u]}]",
					     20, "x", 100, 500, 300);
	const unsigned int multikey_count = 3;
	struct {
		char *data;
		int idx;
	} keys[] = {
		{test_key_new("[]"), 0},
		{test_key_new("[]"), 1},
		{test_key_new("[]"), 2},
		{test_key_new("[%u]", 100), 0},
		{test_key_new("[%u]", 100), 1},
		{test_key_new("[%u]", 100), 2},
		{test_key_new("[%u]", 200), 0},
		{test_key_new("[%u]", 200), 1},
		{test_key_new("[%u]", 200), 2},
		{test_key_new("[%u]", 300), 0},
		{test_key_new("[%u]", 300), 1},
		{test_key_new("[%u]", 300), 2},
		{test_key_new("[%u]", 500), 0},
		{test_key_new("[%u]", 500), 1},
		{test_key_new("[%u]", 500), 2},
		{test_key_new("[%u%u]", 100, 10), 0},
		{test_key_new("[%u%u]", 100, 20), 0},
		{test_key_new("[%u%u]", 100, 30), 0},
		{test_key_new("[%u%u]", 100, 20), 1},
		{test_key_new("[%u%u]", 100, 20), 2},
		{test_key_new("[%u%u]", 500, 10), 1},
		{test_key_new("[%u%u]", 500, 20), 1},
		{test_key_new("[%u%u]", 500, 30), 1},
		{test_key_new("[%u%u]", 500, 20), 0},
		{test_key_new("[%u%u]", 500, 20), 2},
	};
	test_check_vtab(def, def_raw);
	for (unsigned int i = 0; i < multikey_count; ++i)
		test_check_tuple_extract_key(def, def_raw, tuple, i);
	for (unsigned int i = 0; i < lengthof(keys); i++) {
		test_check_tuple_compare_with_key_multikey(
			def, def_raw, tuple, keys[i].idx, keys[i].data);
	}
	for (unsigned int i = 0; i < lengthof(keys); i++)
		free(keys[i].data);
	tuple_delete(tuple);
	key_def_delete(def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

static void
test_multikey_tuple_compare(void)
{
	plan(115);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s%s%s}{%s%u%s%s}]",
		"field", 2, "type", "unsigned", "path", "x[*]",
		"field", 0, "type", "unsigned");
	struct key_def *def_raw = test_key_def_new_raw(def);
	const char *fmt = "[%uNIL{%s[%u%u%u]}]";
	struct tuple *tuple = test_tuple_new(fmt, 20, "x", 100, 500, 300);
	const unsigned int multikey_count = 3;
	struct tuple *tuples[] = {
		test_tuple_new(fmt, 10, "x", 100, 100, 100),
		test_tuple_new(fmt, 10, "x", 200, 200, 200),
		test_tuple_new(fmt, 10, "x", 300, 300, 300),
		test_tuple_new(fmt, 10, "x", 500, 500, 500),
		test_tuple_new(fmt, 20, "x", 100, 100, 100),
		test_tuple_new(fmt, 20, "x", 200, 200, 200),
		test_tuple_new(fmt, 20, "x", 300, 300, 300),
		test_tuple_new(fmt, 20, "x", 500, 500, 500),
		test_tuple_new(fmt, 30, "x", 100, 100, 100),
		test_tuple_new(fmt, 30, "x", 200, 200, 200),
		test_tuple_new(fmt, 30, "x", 300, 300, 300),
		test_tuple_new(fmt, 30, "x", 500, 500, 500),
	};
	test_check_vtab(def, def_raw);
	for (unsigned int i = 0; i < lengthof(tuples); i++) {
		struct tuple *tuple_a = tuple;
		struct tuple *tuple_b = tuples[i];
		for (int mk_idx_a = 0; mk_idx_a < 3; mk_idx_a++) {
			for (int mk_idx_b = 0; mk_idx_b < 3; mk_idx_b++) {
				test_check_tuple_compare_multikey(
					def, def_raw, tuple_a,
					mk_idx_a, tuple_b, mk_idx_b);
			}
		}
	}
	for (unsigned int i = 0; i < lengthof(tuples); i++)
		tuple_delete(tuples[i]);
	tuple_delete(tuple);
	key_def_delete(def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

static void
test_nullable(void)
{
	plan(287);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s%s%b}{%s%u%s%s%s%b}]",
		"field", 1, "type", "unsigned", "is_nullable", true,
		"field", 2, "type", "unsigned", "is_nullable", true);
	struct key_def *def_raw = test_key_def_new_raw(def);
	struct tuple *tuples[] = {
		test_tuple_new("[NIL%u%u]", 20, 30),
		test_tuple_new("[NIL%uNIL]", 20),
		test_tuple_new("[NIL%u]", 20),
		test_tuple_new("[NILNIL%u]", 30),
		test_tuple_new("[NILNILNIL]"),
		test_tuple_new("[NILNIL]"),
		test_tuple_new("[NIL]"),
		test_tuple_new("[]"),
	};
	char *keys[] = {
		test_key_new("[]"),
		test_key_new("[NIL]"),
		test_key_new("[NILNIL]"),
		test_key_new("[%u]", 10),
		test_key_new("[%u]", 20),
		test_key_new("[%u]", 30),
		test_key_new("[%uNIL]", 10),
		test_key_new("[%uNIL]", 20),
		test_key_new("[%uNIL]", 30),
		test_key_new("[NIL%u]", 20),
		test_key_new("[NIL%u]", 30),
		test_key_new("[NIL%u]", 40),
		test_key_new("[%u%u]", 10, 30),
		test_key_new("[%u%u]", 20, 20),
		test_key_new("[%u%u]", 20, 30),
		test_key_new("[%u%u]", 20, 40),
		test_key_new("[%u%u]", 30, 30),
	};
	test_check_vtab(def, def_raw);
	for (unsigned int i = 0; i < lengthof(tuples); i++) {
		test_check_tuple_hash(def, def_raw, tuples[i]);
	}
	for (unsigned int i = 0; i < lengthof(tuples); i++) {
		for (unsigned int j = 0; j < lengthof(keys); j++) {
			test_check_tuple_compare_with_key(
				def, def_raw, tuples[i], keys[j]);
			test_check_tuple_compare_with_key_no_hint(
				def, def_raw, tuples[i], keys[j]);
		}
	}

	for (unsigned int i = 0; i < lengthof(keys); i++)
		free(keys[i]);
	for (unsigned int i = 0; i < lengthof(tuples); i++)
		tuple_delete(tuples[i]);
	key_def_delete(def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

static void
test_nullable_tuple_compare(void)
{
	plan(602);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s%s%b}{%s%u%s%s%s%b}]",
		"field", 1, "type", "unsigned", "is_nullable", true,
		"field", 2, "type", "unsigned", "is_nullable", true);
	struct key_def *def_raw = test_key_def_new_raw(def);
	struct tuple *tuples[] = {
		test_tuple_new("[NIL]"),
		test_tuple_new("[NILNIL]"),
		test_tuple_new("[NILNILNIL]"),
		test_tuple_new("[NIL%u]", 10),
		test_tuple_new("[NIL%u]", 20),
		test_tuple_new("[NIL%u]", 30),
		test_tuple_new("[NIL%uNIL]", 10),
		test_tuple_new("[NIL%uNIL]", 20),
		test_tuple_new("[NIL%uNIL]", 30),
		test_tuple_new("[NILNIL%u]", 20),
		test_tuple_new("[NILNIL%u]", 30),
		test_tuple_new("[NILNIL%u]", 40),
		test_tuple_new("[NIL%u%u]", 10, 30),
		test_tuple_new("[NIL%u%u]", 20, 20),
		test_tuple_new("[NIL%u%u]", 20, 30),
		test_tuple_new("[NIL%u%u]", 20, 40),
		test_tuple_new("[NIL%u%u]", 30, 30),
	};
	test_check_vtab(def, def_raw);
	for (unsigned int i = 0; i < lengthof(tuples); i++) {
		test_check_tuple_hint(def, def_raw, tuples[i]);

		for (unsigned int j = 0; j < lengthof(tuples); j++) {
			struct tuple *tuple_a = tuples[i];
			struct tuple *tuple_b = tuples[j];
			hint_t hint_a = tuple_hint(tuple_a, def);
			hint_t hint_b = tuple_hint(tuple_b, def);
			test_check_tuple_compare(def, def_raw, tuple_a,
						 hint_a, tuple_b, hint_b);
			test_check_tuple_compare(def, def_raw, tuple_a,
						 HINT_NONE, tuple_b, HINT_NONE);
		}
	}

	for (unsigned int i = 0; i < lengthof(tuples); i++)
		tuple_delete(tuples[i]);
	key_def_delete(def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

/**
 * We have a separate test for tuple_extract_key with nullable key_def because
 * original one uses empty tuples and tuples filled with nil - such tuples will
 * never appear in index because they must suit primary key, which is not
 * nullable, and some tuple_extract_key implementations rely on it.
 */
static void
test_nullable_tuple_extract_key(void)
{
	plan(4);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s%s%b}{%s%u%s%s%s%b}]",
		"field", 1, "type", "unsigned", "is_nullable", true,
		"field", 2, "type", "unsigned", "is_nullable", true);
	struct key_def *def_raw = test_key_def_new_raw(def);
	struct tuple *tuples[] = {
		test_tuple_new("[NIL%u%u]", 20, 30),
		test_tuple_new("[NIL%uNIL]", 20),
		test_tuple_new("[NIL%u]", 20),
		test_tuple_new("[NILNIL%u]", 30),
	};
	for (unsigned int i = 0; i < lengthof(tuples); i++)
		test_check_tuple_extract_key(def, def_raw, tuples[i],
					     MULTIKEY_NONE);
	for (unsigned int i = 0; i < lengthof(tuples); i++)
		tuple_delete(tuples[i]);
	key_def_delete(def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

static void
test_collation(void)
{
	plan(30);
	header();

	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s%s%u}]",
		"field", 0, "type", "string", "collation", 1);
	struct key_def *def_raw = test_key_def_new_raw(def);
	struct tuple *tuple = test_tuple_new("[%s]", "ЁЁЁ");
	struct tuple *tuples[] = {
		test_tuple_new("[%s]", "ЕЕЕ"),
		test_tuple_new("[%s]", "ЁЁЁ"),
		test_tuple_new("[%s]", "ёёё"),
		test_tuple_new("[%s]", "АБВГД"),
	};
	test_check_vtab(def, def_raw);
	test_check_tuple_hash(def, def_raw, tuple);
	test_check_tuple_hint(def, def_raw, tuple);
	test_check_tuple_extract_key(def, def_raw, tuple, MULTIKEY_NONE);
	for (unsigned int i = 0; i < lengthof(tuples); i++) {
		test_check_tuple_hint(def, def_raw, tuples[i]);

		struct tuple *tuple_a = tuple;
		struct tuple *tuple_b = tuples[i];
		hint_t hint_a = tuple_hint(tuple_a, def);
		hint_t hint_b = tuple_hint(tuple_b, def);
		test_check_tuple_compare(def, def_raw, tuple_a,
					 hint_a, tuple_b, hint_b);
		test_check_tuple_compare(def, def_raw, tuple_a,
					 HINT_NONE, tuple_b, HINT_NONE);

		const char *key = tuple_data(tuples[i]);
		test_check_tuple_compare_with_key(
			def, def_raw, tuple, key);
		test_check_tuple_compare_with_key_no_hint(
			def, def_raw, tuple, key);
	}
	for (unsigned int i = 0; i < lengthof(tuples); i++)
		tuple_delete(tuples[i]);
	key_def_delete(def);
	key_def_delete(def_raw);
	tuple_delete(tuple);

	footer();
	check_plan();
}

static void
test_func(void)
{
	plan(21);
	header();

	struct key_def *def = test_key_def_new_func(
		"[{%s%u%s%s}{%s%u%s%s}]",
		"field", 0, "type", "unsigned",
		"field", 1, "type", "string");
	struct key_def *pk_def = test_key_def_new(
		"[{%s%u%s%s}{%s%u%s%s}]",
		"field", 1, "type", "unsigned",
		"field", 2, "type", "unsigned");
	struct key_def *cmp_def = key_def_merge(def, pk_def);
	struct key_def *def_raw = test_key_def_new_raw(cmp_def);
	struct tuple *func_key = test_tuple_new("[%u%s]", 20, "foo");
	struct tuple *tuple = test_tuple_new("[%u%u%u]", 95, 100, 200);
	char *keys[] = {
		test_key_new("[]"),
		test_key_new("[%u]", 10),
		test_key_new("[%u]", 20),
		test_key_new("[%u]", 30),
		test_key_new("[%u%s]", 10, "foo"),
		test_key_new("[%u%s]", 20, "foo"),
		test_key_new("[%u%s]", 20, "bar"),
		test_key_new("[%u%s]", 30, "foo"),
		test_key_new("[%u%s%u]", 20, "foo", 90),
		test_key_new("[%u%s%u]", 20, "foo", 100),
		test_key_new("[%u%s%u%u]", 20, "foo", 100, 150),
		test_key_new("[%u%s%u%u]", 20, "foo", 100, 200),
		test_key_new("[%u%s%u%u]", 20, "foo", 100, 250),
		test_key_new("[%u%s%u]", 20, "foo", 150),
	};
	test_check_vtab(cmp_def, def_raw);
	for (unsigned int i = 0; i < lengthof(keys); i++) {
		test_check_tuple_compare_with_key_func(
			cmp_def, def_raw, tuple, func_key, keys[i]);
	}
	for (unsigned int i = 0; i < lengthof(keys); i++)
		free(keys[i]);
	tuple_delete(func_key);
	tuple_delete(tuple);
	key_def_delete(def);
	key_def_delete(pk_def);
	key_def_delete(cmp_def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

static void
test_func_tuple_compare(void)
{
	plan(43);
	header();

	struct key_def *def = test_key_def_new_func(
		"[{%s%u%s%s}{%s%u%s%s}]",
		"field", 0, "type", "unsigned",
		"field", 1, "type", "string");
	struct key_def *pk_def = test_key_def_new(
		"[{%s%u%s%s}{%s%u%s%s}]",
		"field", 1, "type", "unsigned",
		"field", 2, "type", "unsigned");
	struct key_def *cmp_def = key_def_merge(def, pk_def);
	struct key_def *def_raw = test_key_def_new_raw(cmp_def);
	struct tuple *func_key = test_tuple_new("[%u%s]", 20, "foo");
	struct tuple *tuple = test_tuple_new("[%u%u%u]", 95, 100, 200);
	struct tuple *func_keys[] = {
		test_tuple_new("[%u%s]", 10, "foo"),
		test_tuple_new("[%u%s]", 20, "foo"),
		test_tuple_new("[%u%s]", 20, "bar"),
		test_tuple_new("[%u%s]", 30, "foo"),
	};
	struct tuple *tuples[] = {
		test_tuple_new("[NIL%u%u]", 50, 150),
		test_tuple_new("[NIL%u%u]", 50, 200),
		test_tuple_new("[NIL%u%u]", 50, 250),
		test_tuple_new("[NIL%u%u]", 100, 150),
		test_tuple_new("[NIL%u%u]", 100, 200),
		test_tuple_new("[NIL%u%u]", 100, 250),
		test_tuple_new("[NIL%u%u]", 150, 150),
		test_tuple_new("[NIL%u%u]", 150, 200),
		test_tuple_new("[NIL%u%u]", 150, 250),
	};
	test_check_vtab(cmp_def, def_raw);
	for (unsigned int i = 0; i < lengthof(func_keys); i++) {
		for (unsigned int j = 0; j < lengthof(tuples); j++) {
			struct tuple *tuple_a = tuple;
			struct tuple *tuple_b = tuples[j];
			struct tuple *func_key_a = func_key;
			struct tuple *func_key_b = func_keys[i];
			test_check_tuple_compare_func(cmp_def, def_raw,
						      tuple_a, func_key_a,
						      tuple_b, func_key_b);
		}
	}
	for (unsigned int i = 0; i < lengthof(func_keys); i++)
		tuple_delete(func_keys[i]);
	for (unsigned int i = 0; i < lengthof(tuples); i++)
		tuple_delete(tuples[i]);
	tuple_delete(func_key);
	tuple_delete(tuple);
	key_def_delete(def);
	key_def_delete(pk_def);
	key_def_delete(cmp_def);
	key_def_delete(def_raw);

	footer();
	check_plan();
}

static void
test_sort_order_case(const char *part_1_sort_order,
		     const char *part_2_sort_order)
{
	plan(340);
	header();

	note("sort orders: %s, %s", part_1_sort_order, part_2_sort_order);
	struct key_def *def = test_key_def_new(
		"[{%s%u%s%s%s%b%s%s}{%s%u%s%s%s%b%s%s}]",
		"field", 0, "type", "unsigned",
			    "is_nullable", true,
			    "sort_order", part_1_sort_order,
		"field", 1, "type", "unsigned",
			    "is_nullable", true,
			    "sort_order", part_2_sort_order);
	struct key_def *def_raw = test_key_def_new_raw(def);
	struct tuple *tuples[] = {
		test_tuple_new("[NILNIL]"),
		test_tuple_new("[NIL%d]", 0),
		test_tuple_new("[NIL%d]", 1),
		test_tuple_new("[%dNIL]", 0),
		test_tuple_new("[%d%d]", 0, 0),
		test_tuple_new("[%d%d]", 0, 1),
		test_tuple_new("[%dNIL]", 1),
		test_tuple_new("[%d%d]", 1, 0),
		test_tuple_new("[%d%d]", 1, 1),
	};
	test_check_vtab(def, def_raw);
	for (unsigned int i = 0; i < lengthof(tuples); i++) {
		test_check_tuple_hint(def, def_raw, tuples[i]);
		for (unsigned int j = 0; j < lengthof(tuples); j++) {
			struct tuple *tuple_a = tuples[i];
			struct tuple *tuple_b = tuples[j];
			hint_t hint_a = tuple_hint(tuple_a, def);
			hint_t hint_b = tuple_hint(tuple_b, def);
			test_check_tuple_compare(def, def_raw, tuple_a,
						 hint_a, tuple_b, hint_b);
			test_check_tuple_compare(def, def_raw, tuple_a,
						 HINT_NONE, tuple_b, HINT_NONE);

			const char *key = tuple_data(tuples[j]);
			test_check_tuple_compare_with_key(
				def, def_raw, tuples[i], key);
			test_check_tuple_compare_with_key_no_hint(
				def, def_raw, tuples[i], key);
		}
	}

	for (unsigned int i = 0; i < lengthof(tuples); i++)
		tuple_delete(tuples[i]);
	key_def_delete(def_raw);
	key_def_delete(def);

	footer();
	check_plan();
}

static void
test_sort_order(void)
{
	plan(4);
	header();

	test_sort_order_case("asc", "asc");
	test_sort_order_case("asc", "desc");
	test_sort_order_case("desc", "asc");
	test_sort_order_case("desc", "desc");

	footer();
	check_plan();
}

static void
test_sort_order_func_case(const char *sk_sort_order, const char *pk_sort_order)
{
	plan(79);
	header();

	note("sort orders: %s, %s", sk_sort_order, pk_sort_order);
	struct key_def *def = test_key_def_new_func(
		"[{%s%u%s%s%s%b%s%s}]",
		"field", 0, "type", "unsigned",
			    "is_nullable", true,
			    "sort_order", sk_sort_order);
	struct key_def *pk_def = test_key_def_new(
		"[{%s%u%s%s%s%s}]",
		"field", 1, "type", "unsigned",
			    "sort_order", pk_sort_order);
	struct key_def *cmp_def = key_def_merge(def, pk_def);
	struct key_def *def_raw = test_key_def_new_raw(cmp_def);
	struct tuple *tuples[] = {
		test_tuple_new("[NIL%d]", 0),
		test_tuple_new("[NIL%d]", 1),
		test_tuple_new("[%d%d]", 0, 0),
		test_tuple_new("[%d%d]", 0, 1),
		test_tuple_new("[%d%d]", 1, 0),
		test_tuple_new("[%d%d]", 1, 1),
	};
	test_check_vtab(cmp_def, def_raw);
	for (unsigned int i = 0; i < lengthof(tuples); i++) {
		for (unsigned int j = 0; j < lengthof(tuples); j++) {
			struct tuple *tuple_a = tuples[i];
			struct tuple *tuple_b = tuples[j];
			struct tuple *func_key_a = tuple_a;
			struct tuple *func_key_b = tuple_b;
			test_check_tuple_compare_func(cmp_def, def_raw,
						      tuple_a, func_key_a,
						      tuple_b, func_key_b);

			struct tuple *tuple = tuples[i];
			struct tuple *func_key = tuple;
			const char *key = tuple_data(tuples[j]);
			test_check_tuple_compare_with_key_func(
				cmp_def, def_raw, tuple, func_key, key);
		}
	}

	for (unsigned int i = 0; i < lengthof(tuples); i++)
		tuple_delete(tuples[i]);
	key_def_delete(def_raw);
	key_def_delete(cmp_def);
	key_def_delete(pk_def);
	key_def_delete(def);

	footer();
	check_plan();
}

static void
test_sort_order_func(void)
{
	plan(4);
	header();

	test_sort_order_func_case("asc", "asc");
	test_sort_order_func_case("asc", "desc");
	test_sort_order_func_case("desc", "asc");
	test_sort_order_func_case("desc", "desc");

	footer();
	check_plan();
}

static int
test_main(void)
{
	plan(14);
	header();

	test_plain();
	test_plain_tuple_compare();
	test_json();
	test_json_tuple_compare();
	test_multikey();
	test_multikey_tuple_compare();
	test_nullable();
	test_nullable_tuple_compare();
	test_nullable_tuple_extract_key();
	test_collation();
	test_func();
	test_func_tuple_compare();
	test_sort_order();
	test_sort_order_func();

	footer();
	return check_plan();
}

static void
test_coll_init(void)
{
	coll_init();
	struct coll_id_def def;
	memset(&def, 0, sizeof(def));
	def.id = 1;
	def.owner_id = 1;
	def.name = "unicode_ci";
	def.name_len = strlen(def.name);
	def.base.type = COLL_TYPE_ICU;
	def.base.icu.strength = COLL_ICU_STRENGTH_PRIMARY;
	struct coll_id *id = coll_id_new(&def);
	fail_if(id == NULL);
	struct coll_id *replaced;
	coll_id_cache_replace(id, &replaced);
	fail_unless(replaced == NULL);
}

static void
test_coll_free(void)
{
	struct coll_id *id = coll_by_id(1);
	fail_if(id == NULL);
	coll_id_cache_delete(id);
	coll_id_delete(id);
	coll_free();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init();
	coll_id_cache_init();
	test_coll_init();

	int rc = test_main();

	test_coll_free();
	coll_id_cache_destroy();
	tuple_free();
	fiber_free();
	memory_free();
	return rc;
}
