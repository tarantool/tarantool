#include "module.h"

/*
 * Before the reload functions are just declared
 * and simply exit with zero.
 *
 * After the module reload we should provide real
 * functionality.
 */

int
cfunc_nop(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	return 0;
}

int
cfunc_echo(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	return 0;
}

int
cfunc_sum(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	return 0;
}
