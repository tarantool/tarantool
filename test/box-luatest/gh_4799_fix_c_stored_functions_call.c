#include "module.h"
#include "msgpuck.h"

#define BUF_SIZE 8

/** Simple function returns true and -1 as mutltiresults. */
int
multires(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	char buf[BUF_SIZE];
	memset(buf, '\0', BUF_SIZE);
	char *pos = mp_encode_bool(buf, true);
	box_return_mp(ctx, buf, pos);
	pos = mp_encode_int(buf, -1);
	box_return_mp(ctx, buf, pos);
	return 0;
}
