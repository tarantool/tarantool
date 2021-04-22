#include "msgpuck.h"
#include "module.h"
#include "uuid/mp_uuid.h"
#include "mp_extension_types.h"

enum {
	BUF_SIZE = 512,
};

int
is_uuid(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)args_end;
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count != 1) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
				     "invalid argument count");
	}
	bool is_uuid;
	if (mp_typeof(*args) == MP_EXT) {
		const char *str = args;
		int8_t type;
		mp_decode_extl(&str, &type);
		is_uuid = type == MP_UUID;
	} else {
		is_uuid = false;
	}

	char res[BUF_SIZE];
	char *end = mp_encode_bool(res, is_uuid);
	box_return_mp(ctx, res, end);
	return 0;
}

int
ret_uuid(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)args;
	(void)args_end;
	struct tt_uuid uuid;
	memset(&uuid, 0x11, sizeof(uuid));
	char res[BUF_SIZE];
	char *end = mp_encode_uuid(res, &uuid);
	box_return_mp(ctx, res, end);
	return 0;
}
