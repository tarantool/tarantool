#include "unit.h"              /* plan, header, footer, is, ok */
#include "memory.h"            /* memory_init() */
#include "fiber.h"             /* fiber_init() */
#include "box/tuple.h"         /* tuple_init(), tuple_*(),
				  tuple_validate() */
#include "box/tuple_format.h"  /* tuple_format_*,
				  box_tuple_format_new() */
#include "box/key_def.h"       /* key_def_new(),
				  key_def_delete() */
#include "box/merger.h"        /* merger_*() */

/* {{{ Array merge source */

struct merge_source_array {
	struct merge_source base;
	uint32_t tuple_count;
	struct tuple **tuples;
	uint32_t cur;
};

/* Virtual methods declarations */

static void
merge_source_array_destroy(struct merge_source *base);
static int
merge_source_array_next(struct merge_source *base, struct tuple_format *format,
			struct tuple **out);

/* Non-virtual methods */

static struct merge_source *
merge_source_array_new(bool even)
{
	static struct merge_source_vtab merge_source_array_vtab = {
		.destroy = merge_source_array_destroy,
		.next = merge_source_array_next,
	};

	struct merge_source_array *source = malloc(
		sizeof(struct merge_source_array));
	assert(source != NULL);

	merge_source_create(&source->base, &merge_source_array_vtab);

	uint32_t tuple_size = 2;
	const uint32_t tuple_count = 2;
	/* [1], [3] */
	static const char *data_odd[] = {"\x91\x01", "\x91\x03"};
	/* [2], [4] */
	static const char *data_even[] = {"\x91\x02", "\x91\x04"};
	const char **data = even ? data_even : data_odd;
	source->tuples = malloc(sizeof(struct tuple *) * tuple_count);
	assert(source->tuples != NULL);
	struct tuple_format *format = tuple_format_runtime;
	for (uint32_t i = 0; i < tuple_count; ++i) {
		const char *end = data[i] + tuple_size;
		source->tuples[i] = tuple_new(format, data[i], end);
		tuple_ref(source->tuples[i]);
	}
	source->tuple_count = tuple_count;
	source->cur = 0;

	return &source->base;
}

/* Virtual methods */

static void
merge_source_array_destroy(struct merge_source *base)
{
	struct merge_source_array *source = container_of(base,
		struct merge_source_array, base);

	for (uint32_t i = 0; i < source->tuple_count; ++i)
		tuple_unref(source->tuples[i]);

	free(source->tuples);
	free(source);
}

static int
merge_source_array_next(struct merge_source *base, struct tuple_format *format,
			struct tuple **out)
{
	struct merge_source_array *source = container_of(base,
		struct merge_source_array, base);

	if (source->cur == source->tuple_count) {
		*out = NULL;
		return 0;
	}

	struct tuple *tuple = source->tuples[source->cur];
	assert(tuple != NULL);

	/*
	 * Note: The source still stores the tuple (and will
	 * unreference it during destroy). Here we should give a
	 * referenced tuple (so a caller should unreference it on
	 * its side).
	 */
	tuple_ref(tuple);

	*out = tuple;
	++source->cur;
	return 0;
}

/* }}} */

static struct key_part_def key_part_unsigned = {
	.fieldno = 0,
	.type = FIELD_TYPE_UNSIGNED,
	.coll_id = COLL_NONE,
	.is_nullable = false,
	.nullable_action = ON_CONFLICT_ACTION_DEFAULT,
	.sort_order = SORT_ORDER_ASC,
	.path = NULL,
};

static struct key_part_def key_part_integer = {
	.fieldno = 0,
	.type = FIELD_TYPE_INTEGER,
	.coll_id = COLL_NONE,
	.is_nullable = false,
	.nullable_action = ON_CONFLICT_ACTION_DEFAULT,
	.sort_order = SORT_ORDER_ASC,
	.path = NULL,
};

uint32_t
min_u32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

void
check_tuple(struct tuple *tuple, struct tuple_format *format,
	    const char *exp_data, uint32_t exp_data_len, const char *case_name)
{
	uint32_t size;
	const char *data = tuple_data_range(tuple, &size);

	ok(tuple != NULL, "%s: tuple != NULL", case_name);
	if (format == NULL) {
		ok(true, "%s: skip tuple validation", case_name);
	} else {
		int rc = tuple_validate(format, tuple);
		is(rc, 0, "%s: validate tuple", case_name);
	}
	is(size, exp_data_len, "%s: check tuple size", case_name);
	ok(!strncmp(data, exp_data, min_u32(size, exp_data_len)),
	   "%s: check tuple data", case_name);
}

/**
 * Check array source itself (just in case).
 */
int
test_array_source(struct tuple_format *format)
{
	plan(9);
	header();

	/* [1], [3] */
	const uint32_t exp_tuple_size = 2;
	const uint32_t exp_tuple_count = 2;
	static const char *exp_tuples_data[] = {"\x91\x01", "\x91\x03"};

	struct merge_source *source = merge_source_array_new(false);
	assert(source != NULL);

	struct tuple *tuple = NULL;
	const char *msg = format == NULL ?
		"array source next() (any format)" :
		"array source next() (user's format)";
	for (uint32_t i = 0; i < exp_tuple_count; ++i) {
		int rc = merge_source_next(source, format, &tuple);
		(void) rc;
		assert(rc == 0);
		check_tuple(tuple, format, exp_tuples_data[i], exp_tuple_size,
			    msg);
		tuple_unref(tuple);
	}
	int rc = merge_source_next(source, format, &tuple);
	(void) rc;
	assert(rc == 0);
	is(tuple, NULL, format == NULL ?
	   "array source is empty (any format)" :
	   "array source is empty (user's format)");

	merge_source_unref(source);

	footer();
	return check_plan();
}

int
test_merger(struct tuple_format *format)
{
	plan(17);
	header();

	/* [1], [2], [3], [4] */
	const uint32_t exp_tuple_size = 2;
	const uint32_t exp_tuple_count = 4;
	static const char *exp_tuples_data[] = {
		"\x91\x01", "\x91\x02", "\x91\x03", "\x91\x04",
	};

	const uint32_t source_count = 2;
	struct merge_source *sources[] = {
		merge_source_array_new(false),
		merge_source_array_new(true),
	};

	struct key_def *key_def = key_def_new(&key_part_unsigned, 1, false);
	struct merge_source *merger = merger_new(key_def, sources, source_count,
						 false);
	key_def_delete(key_def);

	struct tuple *tuple = NULL;
	const char *msg = format == NULL ?
		"merger next() (any format)" :
		"merger next() (user's format)";
	for (uint32_t i = 0; i < exp_tuple_count; ++i) {
		int rc = merge_source_next(merger, format, &tuple);
		(void) rc;
		assert(rc == 0);
		check_tuple(tuple, format, exp_tuples_data[i], exp_tuple_size,
			    msg);
		tuple_unref(tuple);
	}
	int rc = merge_source_next(merger, format, &tuple);
	(void) rc;
	assert(rc == 0);
	is(tuple, NULL, format == NULL ?
	   "merger is empty (any format)" :
	   "merger is empty (user's format)");

	merge_source_unref(merger);
	merge_source_unref(sources[0]);
	merge_source_unref(sources[1]);

	footer();
	return check_plan();
}

int
test_basic()
{
	plan(4);
	header();

	struct key_def *key_def = key_def_new(&key_part_integer, 1, false);
	struct tuple_format *format = box_tuple_format_new(&key_def, 1);
	assert(format != NULL);

	test_array_source(NULL);
	test_array_source(format);
	test_merger(NULL);
	test_merger(format);

	key_def_delete(key_def);
	tuple_format_unref(format);

	footer();
	return check_plan();
}

int
main()
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);

	int rc = test_basic();

	tuple_free();
	fiber_free();
	memory_free();

	return rc;
}
