#include <stdbool.h>
#include "module.h"

#include <stdio.h>

#include "msgpuck/msgpuck.h"

int
function1(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	say_info("-- function1 -  called --");
	printf("ok - function1\n");
	return 0;
}

int
args(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count < 1) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C, "%s",
			"invalid argument count");
	}

	if (mp_typeof(*args) != MP_UINT) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C, "%s",
			"first tuple field must be uint");
	}

	uint32_t num = mp_decode_uint(&args);

	char tuple_buf[512];
	char *d = tuple_buf;
	d = mp_encode_array(d, 2);
	d = mp_encode_uint(d, num);
	d = mp_encode_str(d, "hello", strlen("hello"));
	assert(d <= tuple_buf + sizeof(tuple_buf));

	box_tuple_format_t *fmt = box_tuple_format_default();
	box_tuple_t *tuple = box_tuple_new(fmt, tuple_buf, d);
	if (tuple == NULL)
		return -1;
	return box_return_tuple(ctx, tuple);
}

/*
 * For each UINT key in arguments create or increment counter in
 * box.space.test space.
 */
int
multi_inc(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	static const char *SPACE_NAME = "test";
	static const char *INDEX_NAME = "primary";

	uint32_t space_id = box_space_id_by_name(SPACE_NAME, strlen(SPACE_NAME));
	uint32_t index_id = box_index_id_by_name(space_id, INDEX_NAME,
		strlen(INDEX_NAME));
	if (space_id == BOX_ID_NIL || index_id == BOX_ID_NIL) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
			"Can't find index %s in space %s",
			INDEX_NAME, SPACE_NAME);
	}
	say_debug("space_id = %u, index_id = %u", space_id, index_id);

	uint32_t arg_count = mp_decode_array(&args);
	assert(!box_txn());
	box_txn_begin();
	assert(box_txn());
	for (uint32_t i = 0; i < arg_count; i++) {
		/* Decode next argument */
		if (mp_typeof(*args) != MP_UINT)
			return box_error_set(__FILE__, __LINE__,
					       ER_PROC_C, "Expected uint keys");
		uint32_t key = mp_decode_uint(&args);
		(void) key;

		/* Prepare MsgPack key for search */
		char key_buf[16];
		char *key_end = key_buf;
		key_end = mp_encode_array(key_end, 1);
		key_end = mp_encode_uint(key_end, key);
		assert(key_end < key_buf + sizeof(key_buf));

		/* Get current value from space */
		uint64_t counter = 0;
		box_tuple_t *tuple;
		if (box_index_get(space_id, index_id, key_buf, key_end,
				  &tuple) != 0) {
			return -1; /* error */
		} else if (tuple != NULL) {
			const char *field = box_tuple_field(tuple, 1);
			if (field == NULL || mp_typeof(*field) != MP_UINT)
				return box_error_set(__FILE__, __LINE__,
						       ER_PROC_LUA,
						       "Invalid tuple");
			counter = mp_decode_uint(&field) + 1;
		}

		/* Replace value */
		char tuple_buf[16];
		char *tuple_end = tuple_buf;
		tuple_end = mp_encode_array(tuple_end, 2);
		tuple_end = mp_encode_uint(tuple_end, key); /* key */
		tuple_end = mp_encode_uint(tuple_end, counter); /* counter */
		assert(tuple_end <= tuple_buf + sizeof(tuple_buf));

		if (box_replace(space_id, tuple_buf, tuple_end, NULL) != 0)
			return -1;
	}
	box_txn_commit();
	assert(!box_txn());
	return 0;
}

int
errors(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	box_error_set(__FILE__, __LINE__, ER_PROC_C, "%s", "Proc error");

	const box_error_t *error = box_error_last();
	assert(strcmp(box_error_type(error), "ClientError") == 0);
	assert(box_error_code(error) == ER_PROC_C);
	assert(strcmp(box_error_message(error), "Proc error") == 0);
	(void) error;

	box_error_clear();
	assert(box_error_last() == NULL);

	return -1; /* raises "Unknown procedure error" */
}
