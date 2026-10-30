#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#include "vclock/vclock.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *buf = xcalloc(size + 1, sizeof(char));
	assert(buf);
	memcpy(buf, data, size);
	buf[size] = '\0';

	struct vclock vclock;
	vclock_create(&vclock);
	vclock_from_string(&vclock, buf);
	free(buf);

	return 0;
}
