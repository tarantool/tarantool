#include "column_mask.h"
#include "xrow_update.h"
#include "unit.h"
#include "msgpuck.h"
#include "trivia/util.h"
#include "fiber.h"
#include "memory.h"
#include "tuple.h"

#define MAX_OPS 20
#define MAX_FIELDS 100
#define LONG_TUPLE {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
		   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
		   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
		   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
		   1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
#define LONG_TUPLE_LEN 70

/** Template for a tuple creation. */
struct tuple_op_template {
	/** Op: '=', '+', ... */
	char op;
	/** Field number. */
	int fieldno;
	/*
	 * Parameter of the operation. Only unsigned integers are
	 * allowed.
	 */
	int arg;
};

/** Template for update operations array. */
struct tuple_update_template {
	/** Update operation templates. */
	const struct tuple_op_template ops[MAX_OPS];
	/** Actual length of the @ops. */
	int count;
};

/** Template for a tuple creation. */
struct tuple_template {
	/** Tuple fields. Only unsigned integers are allowed. */
	const int fields[MAX_FIELDS];
	/** Actual length of the @fields. */
	int count;
};

/*
 * Create a new raw tuple from a template.
 * @param tuple Tuple template.
 * @param[out] end End of the result raw tuple.
 *
 * @retval Begining of the new raw tuple.
 */
static char *
tuple_new_raw(const struct tuple_template *tuple, char **end)
{
	size_t size = mp_sizeof_array(tuple->count);
	for (int i = 0; i < tuple->count; ++i)
		size += mp_sizeof_uint(tuple->fields[i]);
	char *ret = (char *)malloc(size);
	fail_if(ret == NULL);
	char *pos = mp_encode_array(ret, tuple->count);
	for (int i = 0; i < tuple->count; ++i)
		pos = mp_encode_uint(pos, tuple->fields[i]);
	*end = pos;
	return ret;
}

/**
 * Create a new update operations array from a template.
 * @param update Update template.
 * @param[out] end End of the result array.
 *
 * @retval Beginning of the update operations array.
 */
static char *
tuple_new_update(const struct tuple_update_template *update, char **end)
{
	const struct tuple_op_template *ops = update->ops;
	int count = update->count;

	size_t size = mp_sizeof_array(count) +
		      (mp_sizeof_str(1) + mp_sizeof_array(3)) * count;
	for (int i = 0; i < count; ++i) {
		if (ops[i].fieldno >= 0)
			size += mp_sizeof_uint(ops[i].fieldno);
		else
			size += mp_sizeof_int(ops[i].fieldno);
		size += mp_sizeof_uint(ops[i].arg);
	}
	char *ret = (char *)malloc(size);
	fail_if(ret == NULL);
	char *pos = mp_encode_array(ret, count);
	for (int i = 0; i < count; ++i) {
		pos = mp_encode_array(pos, 3);
		pos = mp_encode_str(pos, &ops[i].op, 1);
		if (ops[i].fieldno >= 0)
			pos = mp_encode_uint(pos, ops[i].fieldno);
		else
			pos = mp_encode_int(pos, ops[i].fieldno);
		pos = mp_encode_uint(pos, ops[i].arg);
	}
	*end = pos;
	return ret;
}

/**
 * Execute an update operation from the template and compare it
 * with the expected tuple and expected column_mask.
 *
 * @param orignal Tuple to update.
 * @param update Update operations
 * @param expected Expected update result tuple.
 * @param expected_mask Expected update result column_mask.
 */
static void
check_update_result(const struct tuple_template *original,
		    const struct tuple_update_template *update,
		    const struct tuple_template *expected,
		    uint64_t expected_mask)
{
	char *old_end, *new_end, *ops_end;
	char *old = tuple_new_raw(original, &old_end);
	char *new = tuple_new_raw(expected, &new_end);
	char *ops = tuple_new_update(update, &ops_end);

	uint32_t actual_len;
	uint64_t column_mask;
	struct region *region = &fiber()->gc;
	const char *actual =
		xrow_update_execute(ops, ops_end, old, old_end,
				    box_tuple_format_default(),
				    &actual_len, 1, &column_mask);
	fail_if(actual == NULL);
	is((int32_t)actual_len, new_end - new, "check result length");
	is(memcmp(actual, new, actual_len), 0, "tuple update is correct");
	is(column_mask, expected_mask, "column_mask is correct");
	fiber_gc();

	free(old);
	free(new);
	free(ops);
}

static inline void
basic_test()
{
	const struct tuple_template statements[] = {
		{ {1, 2, 3}, 3 },

		{ {4, 5, 6}, 3 },
		{ {1, 2, 3}, 3 },

		{ {1, 2, 3}, 3 },
		{ {1, 2, 3}, 3 },
		{ {1, 2, 3}, 3 },

		{ {1, 2}, 2 },
		{ {1, 2, 3, 4}, 4 },
		{ LONG_TUPLE, LONG_TUPLE_LEN },
	};

	const struct tuple_update_template update_ops[] = {
		/* simple update, one field. */
		{ {{'=', 3, 30}}, 1 },

		/* field range update. */
		{ {{'#', 3, 1}}, 1 },
		{ {{'!', 2, 100}}, 1 },

		/* negative field numbers. */
		{ {{'#', -1 , 1}}, 1 },
		{ {{'=', -1, 100}}, 1 },
		{ {{'!', -1, 100}}, 1 },

		/*
		 * change field_count and then try to optimize the
		 * negative fieldno update.
		 */
		{ {{'!', 3, 3}, {'=', -3, 10}}, 2 },
		{ {{'#', -1, 1}, {'=', 2, 20}}, 2 },

		/* Change fieldnumbers >= 64. */
		{ {{'=', 64, 1}, {'!', 65, 1}, {'#', -1, 1}, {'=', 32, 1}}, 4 },
	};

	const struct tuple_template results[] = {
		{ {1, 2, 30}, 3 },

		{ {4, 5}, 2 },
		{ {1, 100, 2, 3}, 4 },

		{ {1, 2}, 2 },
		{ {1, 2, 100}, 3 },
		{ {1, 2, 3, 100}, 4 },

		{ {10, 2, 3}, 3 },
		{ {1, 20, 3}, 3 },

		{ LONG_TUPLE, LONG_TUPLE_LEN },
	};

	const uint64_t column_masks[] = {
		1 << 2,

		COLUMN_MASK_FULL << 2,
		COLUMN_MASK_FULL << 1,

		COLUMN_MASK_FULL << 2,
		1 << 2,
		COLUMN_MASK_FULL << 3,

		(COLUMN_MASK_FULL << 2) | 1,
		(COLUMN_MASK_FULL << 3) | (1 << 1),

		((uint64_t) 1) << 63 | ((uint64_t) 1) << 31,
	};

	assert(lengthof(statements) == lengthof(update_ops));
	assert(lengthof(statements) == lengthof(results));
	assert(lengthof(statements) == lengthof(column_masks));
	for (size_t i = 0; i < lengthof(statements); ++i)
		check_update_result(&statements[i], &update_ops[i], &results[i],
				    column_masks[i]);
}

static void
test_paths(void)
{
	header();
	plan(2);

	char buffer1[1024];
	char *pos1 = mp_encode_array(buffer1, 7);

	pos1 = mp_encode_uint(pos1, 1);
	pos1 = mp_encode_uint(pos1, 2);
	pos1 = mp_encode_array(pos1, 2);
		pos1 = mp_encode_uint(pos1, 3);
		pos1 = mp_encode_uint(pos1, 4);
	pos1 = mp_encode_uint(pos1, 5);
	pos1 = mp_encode_array(pos1, 2);
		pos1 = mp_encode_uint(pos1, 6);
		pos1 = mp_encode_uint(pos1, 7);
	pos1 = mp_encode_uint(pos1, 8);
	pos1 = mp_encode_uint(pos1, 9);


	char buffer2[1024];
	char *pos2 = mp_encode_array(buffer2, 2);

	pos2 = mp_encode_array(pos2, 3);
		pos2 = mp_encode_str(pos2, "!", 1);
		pos2 = mp_encode_str(pos2, "[3][1]", 6);
		pos2 = mp_encode_double(pos2, 2.5);

	pos2 = mp_encode_array(pos2, 3);
		pos2 = mp_encode_str(pos2, "#", 1);
		pos2 = mp_encode_str(pos2, "[5][1]", 6);
		pos2 = mp_encode_uint(pos2, 1);

	struct region *gc = &fiber()->gc;
	size_t svp = region_used(gc);
	uint32_t result_size;
	uint64_t column_mask;
	const char *result =
		xrow_update_execute(buffer2, pos2, buffer1, pos1,
				    box_tuple_format_default(),
				    &result_size, 1, &column_mask);
	isnt(result, NULL, "JSON update works");

	/*
	 * Updates on their first level change fields [3] and [5],
	 * or 2 and 4 if 0-based. If that was the single level,
	 * the operations '!' and '#' would change the all the
	 * fields from 2. But each of these operations are not for
	 * the root and therefore does not affect anything except
	 * [3] and [5] on the first level.
	 */
	uint64_t expected_mask = 0;
	column_mask_set_fieldno(&expected_mask, 2);
	column_mask_set_fieldno(&expected_mask, 4);
	is(column_mask, expected_mask, "column mask match");

	region_truncate(gc, svp);

	check_plan();
	footer();
}

static uint32_t
simple_hash(const char* str, uint32_t len)
{
	return str[0] + len;
}

int
main()
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(simple_hash);
	header();
	plan(28);

	basic_test();
	test_paths();

	footer();
	check_plan();
	tuple_free();
	fiber_free();
	memory_free();
}
