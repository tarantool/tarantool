#include "module.h"

#include <stdio.h>
#include <msgpuck.h>

int
foo(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	static const char *SPACE_TEST_NAME = "test";
	static const char *INDEX_NAME = "primary";
	uint32_t space_test_id = box_space_id_by_name(SPACE_TEST_NAME,
			strlen(SPACE_TEST_NAME));
	uint32_t index_id = box_index_id_by_name(space_test_id, INDEX_NAME,
		strlen(INDEX_NAME));
	if (space_test_id == BOX_ID_NIL || index_id == BOX_ID_NIL) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
			"Can't find index %s in space %s",
			INDEX_NAME, SPACE_TEST_NAME);
	}
	mp_decode_array(&args);
	uint32_t num = mp_decode_uint(&args);

	char buf[16];
	char *end = buf;
	end = mp_encode_array(end, 1);
	end = mp_encode_uint(end, num);
	if (box_insert(space_test_id, buf, end, NULL) < 0) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
			"Can't insert in space %s", SPACE_TEST_NAME);
	}
	end = buf;
	end = mp_encode_array(end, 1);
	end = mp_encode_uint(end, 0);
	while (box_index_count(space_test_id, index_id, ITER_EQ, buf, end) <= 0) {
		fiber_sleep(0.001);
	}
	end = buf;
	end = mp_encode_array(end, 1);
	end = mp_encode_int(end, -((int)num));
	if (box_insert(space_test_id, buf, end, NULL) < 0) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
			"Can't insert in space %s", SPACE_TEST_NAME);
	}
	return 0;
}

int
test_reload(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	static const char *SPACE_NAME = "test_reload";

	uint32_t space_id = box_space_id_by_name(SPACE_NAME, strlen(SPACE_NAME));
	if (space_id == BOX_ID_NIL) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
			"Can't find space %s", SPACE_NAME);
	}

	assert(!box_txn());
	box_txn_begin();
	assert(box_txn());

	/* Replace value */
	char tuple_buf[16];
	char *tuple_end = tuple_buf;
	tuple_end = mp_encode_array(tuple_end, 2);
	tuple_end = mp_encode_uint(tuple_end, 1);
	tuple_end = mp_encode_uint(tuple_end, 2); /* counter */
	assert(tuple_end <= tuple_buf + sizeof(tuple_buf));

	if (box_replace(space_id, tuple_buf, tuple_end, NULL) != 0)
		return -1;

	box_txn_commit();
	assert(!box_txn());

	fiber_sleep(0.001);

	tuple_end = tuple_buf;
	tuple_end = mp_encode_array(tuple_end, 1);
	tuple_end = mp_encode_uint(tuple_end, 1);
	struct tuple *tuple = box_tuple_new(box_tuple_format_default(), tuple_buf, tuple_end);
	return box_return_tuple(ctx, tuple);
}
