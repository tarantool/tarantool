#include "msgpuck.h"
#include "module.h"

enum {
	BUF_SIZE = 512,
};

int
ret_str(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count != 1) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "invalid argument count");
	}
	if (mp_typeof(*args) != MP_STR) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "argument should be string");
	}
	const char* str;
	uint32_t str_n;
	str = mp_decode_str(&args, &str_n);

	uint32_t size = mp_sizeof_array(1) + mp_sizeof_str(str_n);
	if (size > BUF_SIZE) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "string is too long");
	}

	char tuple_buf[BUF_SIZE];
	char *d = tuple_buf;
	d = mp_encode_array(d, 1);
	d = mp_encode_str(d, str, str_n);
	assert(d <= tuple_buf + size);

	box_tuple_format_t *fmt = box_tuple_format_default();
	box_tuple_t *tuple = box_tuple_new(fmt, tuple_buf, d);
	if (tuple == NULL)
		return -1;
	return box_return_tuple(ctx, tuple);
}
