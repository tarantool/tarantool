#include "box/box.h"
#include "box/coll_id_cache.h"
#include "box/coll_id_def.h"
#include "box/sql.h"
#include "box/tuple.h"
#include "box/tuple_format.h"

#include "coll/coll.h"

#include "core/event.h"
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

/*
 * Checks that tuple format comparison for runtime tuple formats works
 * correctly.
 */
static int
test_tuple_format_cmp(void)
{
	plan(18);
	header();

	char buf[1024];
	size_t size = mp_format(buf, lengthof(buf), "[{%s%s} {%s%s}]",
				"name", "f1", "name", "f2");
	struct tuple_format *f1 = runtime_tuple_format_new(buf, size, false);
	struct tuple_format *f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 == f2, "tuple formats with same field counts are equal");
	size = mp_format(buf, lengthof(buf), "[{%s%s}]", "name", "f1");
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 != f2, "tuple formats with different field counts are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	size = mp_format(buf, lengthof(buf), "[{%s%s}]", "name", "f1");
	f1 = runtime_tuple_format_new(buf, size, false);
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 == f2, "tuple formats with same 'name' definitions are equal");
	size = mp_format(buf, lengthof(buf), "[{%s%s}]", "name", "f2");
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 != f2, "tuple formats with different 'name' definitions "
	   "are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	size = mp_format(buf, lengthof(buf), "[{%s%s %s%s}]",
			 "name", "f", "type", "integer");
	f1 = runtime_tuple_format_new(buf, size, false);
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 == f2, "tuple formats with same 'name' definitions are equal");
	size = mp_format(buf, lengthof(buf), "[{%s%s %s%s}]",
			 "name", "f", "type", "string");
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 != f2, "tuple formats with different 'type' definitions "
	   "are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	size = mp_format(buf, lengthof(buf), "[{%s%s %s%s}]",
			 "name", "f", "nullable_action", "default");
	f1 = runtime_tuple_format_new(buf, size, false);
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 == f2, "tuple formats with same 'is_nullable' definitions "
	   "are equal");
	size = mp_format(buf, lengthof(buf), "[{%s%s %s%b %s%s}]",
			 "name", "f", "is_nullable", true,
			 "nullable_action", "none");
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 != f2, "tuple formats with different 'is_nullable' definitions "
	   "are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	struct coll_def coll_def;
	memset(&coll_def, 0, sizeof(coll_def));
	snprintf(coll_def.locale, sizeof(coll_def.locale), "%s", "ru_RU");
	coll_def.type = COLL_TYPE_ICU;
	coll_def.icu.strength = COLL_ICU_STRENGTH_IDENTICAL;
	struct coll_id_def coll_id_def = {
		.id = 1,
		.owner_id = 0,
		.name_len = strlen("c1"),
		.name = "c1",
		.base = coll_def,
	};
	struct coll_id *coll_id1 = coll_id_new(&coll_id_def);
	coll_id_def.id = 2;
	coll_id_def.name = "c2";
	struct coll_id *coll_id2 = coll_id_new(&coll_id_def);
	struct coll_id *replaced_id;
	coll_id_cache_replace(coll_id1, &replaced_id);
	coll_id_cache_replace(coll_id2, &replaced_id);

	size = mp_format(buf, lengthof(buf), "[{%s%s %s%d}]",
			 "name", "f", "collation", 1);
	f1 = runtime_tuple_format_new(buf, size, false);
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 == f2, "tuple formats with same 'collation' definitions "
	   "are equal");
	size = mp_format(buf, lengthof(buf), "[{%s%s %s%d}]",
			 "name", "f", "collation", 2);
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 != f2, "tuple formats with different 'collation' definitions "
	   "are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	coll_id_cache_delete(coll_id2);
	coll_id_cache_delete(coll_id1);
	coll_id_delete(coll_id2);
	coll_id_delete(coll_id1);

	size = mp_format(buf, lengthof(buf), "[{%s%s %s{%s%d %s%d}}]",
			 "name", "f", "constraint", "c1", 1,
			 "c2", 2);
	f1 = runtime_tuple_format_new(buf, size, false);
	size = mp_format(buf, lengthof(buf), "[{%s%s %s{%s%d}}]",
			 "name", "f", "constraint", "c1", 1);
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 != f2, "tuple formats with different number of constraints in "
	   "'constraint' definitions are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	size = mp_format(buf, lengthof(buf), "[{%s%s %s{%s%d}}]",
			 "name", "f", "constraint", "c1", 1);
	f1 = runtime_tuple_format_new(buf, size, false);
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 == f2, "tuple formats with same 'constraint' definitions "
	   "are equal");
	size = mp_format(buf, lengthof(buf), "[{%s%s %s{%s%d}}]",
			 "name", "f", "constraint", "c2", 2);
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 != f2, "tuple formats with different 'constraint' definitions "
	   "are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	size = mp_format(buf, lengthof(buf), "[{%s%s %s%p}]",
			 "name", "f", "default", "\xcc\x00");
	f1 = runtime_tuple_format_new(buf, size, false);
	size = mp_format(buf, lengthof(buf), "[{%s%s %s%p}]",
			 "name", "f", "default", "\x01");
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 != f2, "tuple formats with different MsgPack sizes of 'default' "
	   "definitions are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	size = mp_format(buf, lengthof(buf), "[{%s%s %s%p}]",
			 "name", "f", "default", "\x00");
	f1 = runtime_tuple_format_new(buf, size, false);
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 == f2, "tuple formats with same MsgPacks 'default' definitions "
	   "are equal");
	size = mp_format(buf, lengthof(buf), "[{%s%s %s%p}]",
			 "name", "f", "default", "\x01");
	f2 = runtime_tuple_format_new(buf, size, false);
	ok(f1 != f2, "tuple formats with different MsgPacks of 'default' "
	   "definitions are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	size = mp_format(buf, lengthof(buf), "[{%s%s}]", "name", "f");
	f1 = runtime_tuple_format_new(buf, size, false);
	size = mp_format(buf, lengthof(buf), "[{%s%s %s%d}]",
			 "name", "f", "default_func", 66);
	f2 = runtime_tuple_format_new(buf, size, false);
	fail_if(f1 == NULL);
	fail_if(f2 == NULL);
	ok(f1 != f2, "tuple formats with/without 'default_func' are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	size = mp_format(buf, lengthof(buf), "[{%s%s %s%d}]",
			 "name", "f", "default_func", 66);
	f1 = runtime_tuple_format_new(buf, size, false);
	size = mp_format(buf, lengthof(buf), "[{%s%s %s%d}]",
			 "name", "f", "default_func", 67);
	f2 = runtime_tuple_format_new(buf, size, false);
	fail_if(f1 == NULL);
	fail_if(f2 == NULL);
	ok(f1 != f2, "tuple formats with different MsgPacks of 'default_func' "
	   "definitions are not equal");
	tuple_format_delete(f1);
	tuple_format_delete(f2);

	footer();
	return check_plan();
}

static int
test_tuple_format_to_mpstream(void)
{
	plan(1);
	header();

	struct mpstream stream;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	bool is_err = false;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      mpstream_error, &is_err);
	tuple_format_to_mpstream(tuple_format_runtime, &stream);
	mpstream_flush(&stream);
	fail_if(is_err);
	size_t data_len = region_used(region) - region_svp;
	const char *data = xregion_join(region, data_len);
	char buf[1024];
	char *w = mp_encode_uint(buf, tuple_format_runtime->id);
	w = mp_encode_array(w, 0);
	size_t expected_len = w - buf;
	is(memcmp(data, buf, MIN(data_len, expected_len)), 0,
	   "tuple format serialization works correctly");
	region_truncate(region, region_svp);

	footer();
	return check_plan();
}

/**
 * Table of a field types compatibility.
 * For an i row and j column the value is true, if the i type values can be
 * stored in the j type.
 */
static const bool field_type_compatibility[] = {
/*                  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 */
/*  0: ANY       */ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/*  1: UNSIGNED  */ 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
/*  2: STRING    */ 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/*  3: NUMBER    */ 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/*  4: DOUBLE    */ 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/*  5: INTEGER   */ 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/*  6: BOOLEAN   */ 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/*  7: VARBINARY */ 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/*  8: SCALAR    */ 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/*  9: DECIMAL   */ 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 10: UUID      */ 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 11: DATETIME  */ 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 12: INTERVAL  */ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 13: ARRAY     */ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 14: MAP       */ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 15: INT8      */ 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0,
/* 16: UINT8     */ 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0,
/* 17: INT16     */ 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0,
/* 18: UINT16    */ 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0,
/* 19: INT32     */ 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0,
/* 20: UINT32    */ 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0,
/* 21: INT64     */ 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
/* 22: UINT64    */ 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
/* 23: FLOAT32   */ 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
/* 24: FLOAT64   */ 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
};

static int
test_field_type1_contains_type2(void)
{
	plan(field_type_MAX * field_type_MAX);
	header();
	for (int i = 0; i < field_type_MAX; i++) {
		for (int j = 0; j < field_type_MAX; j++) {
			int idx = i * field_type_MAX + j;
			is(field_type1_contains_type2(j, i),
			   field_type_compatibility[idx],
			   "%s can store values of %s",
			   field_type_strs[j], field_type_strs[i]);
		}
	}
	footer();
	return check_plan();
}

static int
test_tuple_format(void)
{
	plan(3);
	header();

	test_tuple_format_cmp();
	test_tuple_format_to_mpstream();
	test_field_type1_contains_type2();

	footer();
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	coll_init();
	event_init();
	box_init();
	sql_init();

	int rc = test_tuple_format();

	box_free();
	event_free();
	coll_free();
	fiber_free();
	memory_free();
	return rc;
}
