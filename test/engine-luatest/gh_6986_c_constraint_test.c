#include "msgpuck.h"
#include "module.h"

enum {
	BUF_SIZE = 512,
};

int
get_check(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count == 0) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "invalid argument count");
	}
	bool check = false;
	assert(mp_typeof(*args) == MP_ARRAY);
	if (mp_decode_array(&args) > 0 && mp_typeof(*args) == MP_UINT)
		check = (mp_decode_uint(&args) & 1) == 1;
	char res[BUF_SIZE];
	memset(res, 0, BUF_SIZE);
	char *end = mp_encode_bool(res, check);
	box_return_mp(ctx, res, end);
	return 0;
}
