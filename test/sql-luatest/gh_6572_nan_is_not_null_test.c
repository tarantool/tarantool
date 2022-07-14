#include "msgpuck.h"
#include "module.h"
#include "math.h"

enum {
	BUF_SIZE = 512,
};

int
get_nan(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count > 0) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "invalid argument count");
	}
	char res[BUF_SIZE];
	memset(res, 0, BUF_SIZE);
	double d;
	*(uint64_t *)&d = 0xfff8000000000000;
	assert(isnan(d));
	char *end = mp_encode_double(res, d);
	box_return_mp(ctx, res, end);
	return 0;
}
