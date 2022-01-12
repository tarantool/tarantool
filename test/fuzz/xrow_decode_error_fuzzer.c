#include "box/xrow.h"
#include "box/iproto_constants.h"
#include "memory.h"

void
cord_on_yield(void) {}

__attribute__((constructor))
static void
setup(void)
{
	memory_init();
}

__attribute__((destructor))
static void
teardown(void)
{
	memory_free();
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const char *d = (const char *)data;
	const char *end = (const char *)data + size;
	if (mp_check(&d, end) != 0)
		return -1;

	struct iovec body = {0};
	body.iov_base = (void *)data;
	body.iov_len = size;

	struct xrow_header row = {0};
	row.body[0] = body;
	row.bodycnt = 1;

	xrow_decode_error(&row);

	diag_destroy(diag_get());
	assert(diag_is_empty(diag_get()) == true);

	return 0;
}
