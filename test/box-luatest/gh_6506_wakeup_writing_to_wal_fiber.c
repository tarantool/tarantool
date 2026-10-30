#include "module.h"

static struct fiber *saved = NULL;

int
save_fiber(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	saved = fiber_self();
	return 0;
}

int
wakeup_saved(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	fiber_wakeup(saved);
	return 0;
}
