#include "box/xrow.h"
#include "box/iproto_constants.h"
#include "fiber.h"
#include "memory.h"

void
cord_on_yield(void) {}

__attribute__((constructor))
static void
setup(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
}

__attribute__((destructor))
static void
teardown(void)
{
	fiber_free();
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

	struct raft_request request = {0};
	struct vclock vclock = {0};
	xrow_decode_raft(&row, &request, &vclock);

	return 0;
}
