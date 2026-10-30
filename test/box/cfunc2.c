#include <stdlib.h>

#include "msgpuck.h"
#include "module.h"

/*
 * Just make sure we've been called.
 */
int
cfunc_nop(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	return 0;
}

/*
 * Fetch first N even numbers (just to make sure the order of
 * arguments is not screwed).
 */
int
cfunc_echo(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	const char *pos = args;
	if (mp_check(&pos, args_end) != 0)
		abort();
	if (mp_typeof(*args) != MP_ARRAY)
		abort();
	uint32_t arg_count = mp_decode_array(&args);
	for (uint32_t i = 0; i < arg_count; ++i) {
		pos = args;
		mp_next(&pos);
		if (box_return_mp(ctx, args, pos) != 0)
			return -1;
		args = pos;
	}
	return 0;
}

/*
 * Sum two integers.
 */
int
cfunc_sum(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count != 2) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C, "%s",
				     "invalid argument count");
	}
	uint64_t a = mp_decode_uint(&args);
	uint64_t b = mp_decode_uint(&args);

	char res[16];
	char *end = mp_encode_uint(res, a + b);
	box_return_mp(ctx, res, end);
	return 0;
}
