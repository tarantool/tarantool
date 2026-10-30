#include "module.h"

int
echo_1(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	const char *mp1 = "\x01";
	return box_return_mp(ctx, mp1, mp1 + 1);
}

int
echo_2(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	const char *mp1 = "\x02";
	return box_return_mp(ctx, mp1, mp1 + 1);
}

int
echo_3(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	const char *mp1 = "\x03";
	return box_return_mp(ctx, mp1, mp1 + 1);
}
