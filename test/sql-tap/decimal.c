#include "msgpuck.h"
#include "module.h"
#include "mp_decimal.h"
#include "mp_extension_types.h"

enum {
	BUF_SIZE = 512,
};

int
is_dec(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)args_end;
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count != 1) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "invalid argument count");
	}
	bool is_dec;
	if (mp_typeof(*args) == MP_EXT) {
		const char *str = args;
		int8_t type;
		mp_decode_extl(&str, &type);
		is_dec = type == MP_DECIMAL;
	} else {
		is_dec = false;
	}

	char res[BUF_SIZE];
	memset(res, '\0', BUF_SIZE);
	char *end = mp_encode_bool(res, is_dec);
	box_return_mp(ctx, res, end);
	return 0;
}

int
ret_dec(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)args;
	(void)args_end;
	decimal_t dec;
	decimal_from_string(&dec, "111");
	char res[BUF_SIZE];
	memset(res, '\0', BUF_SIZE);
	char *end = mp_encode_decimal(res, &dec);
	box_return_mp(ctx, res, end);
	return 0;
}
