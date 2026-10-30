#include "msgpuck.h"
#include "module.h"
#include "mp_extension_types.h"
#include "lua/tnt_msgpuck.h"
#include "mp_datetime.h"

enum {
	BUF_SIZE = 512,
};

int
is_datetime(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)args_end;
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count != 1) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "invalid argument count");
	}
	bool is_dt;
	if (mp_typeof(*args) == MP_EXT) {
		const char *str = args;
		int8_t type;
		mp_decode_extl(&str, &type);
		is_dt = type == MP_DATETIME;
	} else {
		is_dt = false;
	}

	char res[BUF_SIZE];
	memset(res, '\0', BUF_SIZE);
	char *end = mp_encode_bool(res, is_dt);
	box_return_mp(ctx, res, end);
	return 0;
}

int
ret_datetime(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count != 1) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "invalid argument count");
	}
	if (mp_typeof(*args) != MP_EXT) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "only datetime is accepted");
	}
	const char *str = args;
	int8_t type;
	mp_decode_extl(&str, &type);
	if (type != MP_DATETIME) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "only datetime is accepted");
	}

	char res[BUF_SIZE];
	int size = args_end - args;
	memcpy(res, args, size);
	box_return_mp(ctx, res, res + size);
	return 0;
}
