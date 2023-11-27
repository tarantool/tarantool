#include "module.h"
#include "msgpuck.h"

int
c_func_echo(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	const char *data = args;
	uint32_t count = mp_decode_array(&data);
	for (uint32_t i = 0; i < count; i++) {
		const char *data_end = data;
		mp_next(&data_end);
		box_return_mp(ctx, data, data_end);
		data = data_end;
	}
	return 0;
}

int
c_func_error(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)ctx;
	(void)args;
	(void)args_end;
	return box_error_raise(ER_PROC_LUA, "test");
}
