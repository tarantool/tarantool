#include "box/iproto_constants.h"
#include "box/xrow.h"
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
	const char *p = (const char *)data;
	const char *pe = (const char *)data + size;
	if (mp_check(&p, pe) != 0)
		return -1;

	struct xrow_header header;
	xrow_header_decode(&header, &p, pe, false);

	return 0;
}
