#include "box/xrow.h"
#include "box/iproto_constants.h"
#include "fiber.h"
#include "memory.h"
#include "msgpuck.h"

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
	if (mp_check_exact(&d, end) != 0)
		return -1;

	struct watch_request req = {0};
	req.data = (const char *)data;
	req.data_end = (const char *)data + size;

	struct ballot ballot = {0};
	bool is_empty = false;
	if (xrow_decode_ballot_event(&req, &ballot, &is_empty) == -1)
		return -1;

	return 0;
}
