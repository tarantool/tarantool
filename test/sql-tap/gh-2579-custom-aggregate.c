#include "msgpuck.h"
#include "module.h"

enum {
	BUF_SIZE = 512,
};

int
f3(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)args_end;
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count != 2) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "invalid argument count");
	}
	int num = mp_decode_uint(&args);
	int sum = 0;
	if (mp_typeof(*args) != MP_UINT)
		mp_decode_nil(&args);
	else
		sum = mp_decode_uint(&args);
	sum += num * num;
	char res[BUF_SIZE];
	char *end = mp_encode_uint(res, sum);
	box_return_mp(ctx, res, end);
	return 0;
}
