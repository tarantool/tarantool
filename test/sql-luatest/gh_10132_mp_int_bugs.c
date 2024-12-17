#include "module.h"
#include "msgpuck.h"

int
get_mp_int_one(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)args;
	(void)args_end;
	char data[] = "\xd0\x01";
	return box_return_mp(ctx, data, data + (sizeof(data) - 1));
}
