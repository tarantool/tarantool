#include "module.h"

#include <stdio.h>
#include <msgpuck.h>

int
echo_1(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	char tuple_buf[16];
	char *tuple_end = tuple_buf;
	tuple_end = mp_encode_array(tuple_end, 1);
	tuple_end = mp_encode_uint(tuple_end, 1);
	struct tuple *tuple = box_tuple_new(box_tuple_format_default(),
					    tuple_buf, tuple_end);
	return box_return_tuple(ctx, tuple);
}

int
echo_2(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	char tuple_buf[16];
	char *tuple_end = tuple_buf;
	tuple_end = mp_encode_array(tuple_end, 1);
	tuple_end = mp_encode_uint(tuple_end, 2);
	struct tuple *tuple = box_tuple_new(box_tuple_format_default(),
					    tuple_buf, tuple_end);
	return box_return_tuple(ctx, tuple);
}

int
echo_3(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	char tuple_buf[16];
	char *tuple_end = tuple_buf;
	tuple_end = mp_encode_array(tuple_end, 1);
	tuple_end = mp_encode_uint(tuple_end, 3);
	struct tuple *tuple = box_tuple_new(box_tuple_format_default(),
					    tuple_buf, tuple_end);
	return box_return_tuple(ctx, tuple);
}
