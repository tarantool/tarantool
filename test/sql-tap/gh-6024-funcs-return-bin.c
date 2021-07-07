#include "msgpuck.h"
#include "module.h"
#include "uuid/mp_uuid.h"
#include "mp_decimal.h"

enum {
	BUF_SIZE = 512,
};

int
ret_bin(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)args;
	(void)args_end;
	const char bin[] = "some varbinary string";
	char res[BUF_SIZE];
	memset(res, '\0', BUF_SIZE);
	char *end = mp_encode_bin(res, bin, sizeof(bin));
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
	memset(res, '\0', BUF_SIZE);
	char *end = mp_encode_uuid(res, &uuid);
	box_return_mp(ctx, res, end);
	return 0;
}

int
ret_decimal(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)args;
	(void)args_end;
	decimal_t dec;
	decimal_from_string(&dec, "9999999999999999999.9999999999999999999");
	char res[BUF_SIZE];
	memset(res, '\0', BUF_SIZE);
	char *end = mp_encode_decimal(res, &dec);
	box_return_mp(ctx, res, end);
	return 0;
}
